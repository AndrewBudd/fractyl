#include "../include/commands.h"
#include "../include/core.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../core/hash.h"
#include "../utils/json.h"
#include "../utils/fs.h"
#include "../utils/git.h"
#include "../utils/paths.h"
#include "../utils/gitignore.h"
#include "../utils/lock.h"
#include "../utils/parallel_scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#ifdef HAVE_UUID
#include <uuid/uuid.h>
#else
// Fallback UUID generation
#include <fcntl.h>
#endif


// Find the most recent snapshot to use as parent
static char* find_latest_snapshot(const char *fractyl_dir, const char *branch) {
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        return NULL;
    }
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        free(snapshots_dir);
        return NULL; // No snapshots directory or can't open
    }
    
    char *latest_id = NULL;
    time_t latest_timestamp = 0;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        // Skip . and .. and non-.json files
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        // Load snapshot to get timestamp
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", snapshots_dir, entry->d_name);
        
        snapshot_t snapshot;
        if (json_load_snapshot(&snapshot, snapshot_path) == FRACTYL_OK) {
            if (snapshot.timestamp > latest_timestamp) {
                latest_timestamp = snapshot.timestamp;
                free(latest_id); // Free previous candidate
                latest_id = strdup(snapshot.id);
            }
            json_free_snapshot(&snapshot);
        }
    }
    
    closedir(d);
    free(snapshots_dir);
    return latest_id; // Caller must free
}

// Get current snapshot ID from CURRENT file
static char* get_current_snapshot_id(const char *fractyl_dir, const char *branch) {
    char *current_path = paths_get_current_file(fractyl_dir, branch);
    if (!current_path) {
        return NULL;
    }
    
    FILE *f = fopen(current_path, "r");
    if (!f) {
        free(current_path);
        return NULL;
    }
    
    char current_id[64];
    if (!fgets(current_id, sizeof(current_id), f)) {
        fclose(f);
        free(current_path);
        return NULL;
    }
    fclose(f);
    free(current_path);
    
    // Strip newline
    size_t len = strlen(current_id);
    if (len > 0 && current_id[len-1] == '\n') {
        current_id[len-1] = '\0';
    }
    
    return strdup(current_id);
}

// Generate short hash from snapshot ID (first 6 characters)
static void generate_short_hash(const char *snapshot_id, char *short_hash, size_t max_len) {
    if (max_len == 0) return;
    
    size_t copy_len = 6; // Always try to copy 6 characters
    if (copy_len >= max_len) {
        copy_len = max_len - 1;
    }
    
    // Use snprintf to safely copy and null-terminate
    snprintf(short_hash, max_len, "%.*s", (int)copy_len, snapshot_id);
}

// Check if we're creating a divergent branch
static int is_divergent_branch(const char *fractyl_dir, const char *branch, const char *current_id, const char *latest_id) {
    if (!current_id || !latest_id) {
        return 0; // Not divergent if we don't have both IDs
    }
    
    if (strcmp(current_id, latest_id) == 0) {
        return 0; // Current is latest, not divergent
    }
    
    // Load latest snapshot to check its parent
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        return 0;
    }
    
    char latest_path[2048];
    snprintf(latest_path, sizeof(latest_path), "%s/%s.json", snapshots_dir, latest_id);
    free(snapshots_dir);
    
    snapshot_t latest_snapshot;
    if (json_load_snapshot(&latest_snapshot, latest_path) != FRACTYL_OK) {
        return 0; // Can't determine, assume not divergent
    }
    
    int is_divergent = 1;
    if (latest_snapshot.parent && strcmp(latest_snapshot.parent, current_id) == 0) {
        is_divergent = 0; // Current is direct parent of latest, not divergent
    }
    
    json_free_snapshot(&latest_snapshot);
    return is_divergent;
}

// Generate auto-description based on parent description +n format, with divergence detection
static char* generate_auto_description(const char *fractyl_dir, const char *branch) {
    char *latest_id = find_latest_snapshot(fractyl_dir, branch);
    char *current_id = get_current_snapshot_id(fractyl_dir, branch);
    
    if (!latest_id) {
        // First snapshot
        free(current_id);
        return strdup("working");
    }
    
    // Check if this is a divergent branch
    int divergent = is_divergent_branch(fractyl_dir, branch, current_id, latest_id);
    
    // Load the snapshot we're building from (current, not latest)
    char *parent_id = current_id ? current_id : latest_id;
    
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        free(latest_id);
        free(current_id);
        return strdup("working +1");
    }
    
    char parent_path[2048];
    snprintf(parent_path, sizeof(parent_path), "%s/%s.json", snapshots_dir, parent_id);
    free(snapshots_dir);
    
    snapshot_t parent_snapshot;
    if (json_load_snapshot(&parent_snapshot, parent_path) != FRACTYL_OK) {
        free(latest_id);
        free(current_id);
        return strdup("working +1");
    }
    
    const char *parent_desc = parent_snapshot.description ? parent_snapshot.description : "working";
    char *result = NULL;
    
    if (divergent && current_id) {
        // This is a divergent branch - use short hash suffix
        char short_hash[8];
        generate_short_hash(current_id, short_hash, sizeof(short_hash));
        
        // Extract base description (remove any existing +n suffix)
        char base_desc[256];
        char *plus_pos = strrchr(parent_desc, '+');
        if (plus_pos && plus_pos != parent_desc) {
            size_t base_len = plus_pos - parent_desc;
            if (base_len >= sizeof(base_desc)) {
                base_len = sizeof(base_desc) - 1;
            }
            strncpy(base_desc, parent_desc, base_len);
            base_desc[base_len] = '\0';
            // Trim trailing whitespace
            while (base_len > 0 && base_desc[base_len - 1] == ' ') {
                base_desc[--base_len] = '\0';
            }
        } else {
            strncpy(base_desc, parent_desc, sizeof(base_desc) - 1);
            base_desc[sizeof(base_desc) - 1] = '\0';
        }
        
        // Create divergent branch name: "base-hash"
        result = malloc(strlen(base_desc) + strlen(short_hash) + 2);
        snprintf(result, strlen(base_desc) + strlen(short_hash) + 2, "%s-%s", base_desc, short_hash);
    } else {
        // Normal increment behavior
        char *plus_pos = strrchr(parent_desc, '+');
        if (plus_pos && plus_pos != parent_desc) {
            // Extract base description and increment number
            char base_desc[256];
            size_t base_len = plus_pos - parent_desc;
            if (base_len >= sizeof(base_desc)) {
                base_len = sizeof(base_desc) - 1;
            }
            strncpy(base_desc, parent_desc, base_len);
            base_desc[base_len] = '\0';
            
            // Trim trailing whitespace
            while (base_len > 0 && base_desc[base_len - 1] == ' ') {
                base_desc[--base_len] = '\0';
            }
            
            int current_num = atoi(plus_pos + 1);
            if (current_num > 0) {
                result = malloc(strlen(base_desc) + 20);
                snprintf(result, strlen(base_desc) + 20, "%s +%d", base_desc, current_num + 1);
            }
        }
        
        if (!result) {
            // Parent doesn't follow +n pattern, so start with +1
            result = malloc(strlen(parent_desc) + 5);
            snprintf(result, strlen(parent_desc) + 5, "%s +1", parent_desc);
        }
    }
    
    free(latest_id);
    free(current_id);
    json_free_snapshot(&parent_snapshot);
    return result;
}

static char* generate_snapshot_id(void) {
#ifdef HAVE_UUID
    uuid_t uuid;
    uuid_generate(uuid);
    
    char *uuid_str = malloc(37); // 36 chars + null terminator
    if (!uuid_str) return NULL;
    
    uuid_unparse(uuid, uuid_str);
    return uuid_str;
#else
    // Fallback: use timestamp + random number
    char *id_str = malloc(64);
    if (!id_str) return NULL;
    
    snprintf(id_str, 64, "%ld-%d", time(NULL), rand() % 10000);
    return id_str;
#endif
}

// Load index from snapshot's stored index hash
static int load_snapshot_index(const unsigned char *index_hash, const char *fractyl_dir, index_t *index) {
    void *index_data;
    size_t index_size;
    
    // Load the index data from object storage
    int result = object_load(index_hash, fractyl_dir, &index_data, &index_size);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Write to a temporary file to use index_load
    char temp_path[] = "/tmp/fractyl_index_XXXXXX";
    int temp_fd = mkstemp(temp_path);
    if (temp_fd == -1) {
        free(index_data);
        return FRACTYL_ERROR_IO;
    }
    
    if (write(temp_fd, index_data, index_size) != (ssize_t)index_size) {
        close(temp_fd);
        unlink(temp_path);
        free(index_data);
        return FRACTYL_ERROR_IO;
    }
    close(temp_fd);
    free(index_data);
    
    // Load the index from temporary file
    result = index_load(index, temp_path);
    unlink(temp_path);
    
    return result;
}

int cmd_snapshot(int argc, char **argv) {
    const char *message = NULL;
    char *auto_message = NULL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            i++; // Skip next argument
        }
    }
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Acquire exclusive lock for snapshot operation
    fractyl_lock_t lock;
    if (fractyl_lock_wait_acquire(fractyl_dir, &lock, 30) != 0) {
        printf("Error: Could not acquire lock for snapshot operation\n");
        free(repo_root);
        return 1;
    }
    
    // Get current git branch
    char *git_branch = paths_get_current_branch(repo_root);
    
    // Use default branch name for non-git repositories
    const char *branch_name = git_branch ? git_branch : "main";
    
    // Migrate legacy snapshots if needed (first time on a branch)
    if (git_branch) {
        paths_migrate_legacy_snapshots(fractyl_dir, git_branch);
    }
    
    // Generate auto description if no message provided
    if (!message) {
        auto_message = generate_auto_description(fractyl_dir, git_branch);
        message = auto_message;
        printf("Auto-generating description: \"%s\"\n", message);
    }
    
    // Load previous snapshot's index for comparison
    char *current_snapshot_id = get_current_snapshot_id(fractyl_dir, git_branch);
    index_t prev_index;
    index_t *prev_index_ptr = NULL;
    
    if (current_snapshot_id) {
        // Load the current snapshot to get its index hash
        char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, git_branch);
        if (snapshots_dir) {
            char current_snapshot_path[2048];
            snprintf(current_snapshot_path, sizeof(current_snapshot_path), "%s/%s.json", snapshots_dir, current_snapshot_id);
            free(snapshots_dir);
            
            snapshot_t current_snapshot;
            if (json_load_snapshot(&current_snapshot, current_snapshot_path) == FRACTYL_OK) {
                // Load the index from the snapshot's index hash
                if (load_snapshot_index(current_snapshot.index_hash, fractyl_dir, &prev_index) == FRACTYL_OK) {
                    prev_index_ptr = &prev_index;
                    printf("Comparing against snapshot %s (%s)...\n", current_snapshot_id, 
                           current_snapshot.description ? current_snapshot.description : "no description");
                }
                json_free_snapshot(&current_snapshot);
            }
        }
        free(current_snapshot_id);
    }
    
    // Scan current directory and build new index
    index_t new_index;
    index_init(&new_index);
    
    printf("Scanning directory...\n");
    
    // Use stat-only scanning for maximum performance
    int result = scan_directory_stat_only(repo_root, &new_index, prev_index_ptr, fractyl_dir, branch_name);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to scan directory: %d\n", result);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    printf("Found %zu files\n", new_index.count);
    
    // Check if there are any actual changes
    // Use optimized change detection to avoid O(n²) complexity
    int changes_detected = 0;
    
    if (!prev_index_ptr) {
        // No previous index - all files are new
        changes_detected = new_index.count;
    } else if (new_index.count != prev_index.count) {
        // Different file counts - definitely changes
        changes_detected = 1;
        printf("File count changed: %zu -> %zu\n", prev_index.count, new_index.count);
    } else {
        // Same file count - do optimized comparison
        // Since both indexes should be sorted the same way from scanning,
        // we can do a direct comparison without expensive lookups
        printf("Comparing %zu files for changes...\n", new_index.count);
        
        for (size_t i = 0; i < new_index.count && changes_detected < 10; i++) {
            const index_entry_t *new_entry = &new_index.entries[i];
            const index_entry_t *prev_entry = &prev_index.entries[i];
            
            // Check if path changed (files reordered) or content changed
            if (strcmp(new_entry->path, prev_entry->path) != 0 || 
                memcmp(new_entry->hash, prev_entry->hash, 32) != 0) {
                changes_detected++;
                if (strcmp(new_entry->path, prev_entry->path) != 0) {
                    printf("  Files reordered at index %zu: '%s' vs '%s'\n", 
                           i, new_entry->path, prev_entry->path);
                    // If files are reordered, fall back to careful comparison
                    changes_detected = -1; // Signal to use fallback method
                    break;
                } else {
                    printf("  M %s\n", new_entry->path);
                }
            }
        }
        
        // Fallback method if files are reordered or for verification
        if (changes_detected == -1) {
            printf("Files reordered - using smart sampling...\n");
            changes_detected = 0;
            
            // Smart sampling approach: check every Nth file to detect if there are changes
            // If we find no changes in a good sample, assume no changes exist
            size_t sample_size = (new_index.count < 1000) ? new_index.count : 1000;
            size_t step = new_index.count / sample_size;
            if (step < 1) step = 1;
            
            printf("Sampling %zu files (every %zu files)...\n", sample_size, step);
            
            for (size_t i = 0; i < new_index.count && changes_detected < 10; i += step) {
                const index_entry_t *new_entry = &new_index.entries[i];
                const index_entry_t *prev_entry = index_find_entry(prev_index_ptr, new_entry->path);
                
                if (!prev_entry || memcmp(new_entry->hash, prev_entry->hash, 32) != 0) {
                    changes_detected++;
                    if (!prev_entry) {
                        printf("  A %s\n", new_entry->path);
                    } else {
                        printf("  M %s\n", new_entry->path);
                    }
                }
            }
            
            // If no changes found in sample and file counts match, assume no changes
            if (changes_detected == 0 && new_index.count == prev_index.count) {
                printf("No changes found in sample - assuming no changes\n");
            } else if (changes_detected > 0) {
                printf("Changes detected in sample - would need full comparison\n");
                // For now, just report the sampled changes
            }
        }
    }
    
    if (changes_detected == 0) {
        printf("No changes detected since last snapshot\n");
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 0;
    }
    
    // Save new index
    char index_path[2048];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    result = index_save(&new_index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save index: %d\n", result);
        if (auto_message) free(auto_message);
        free(repo_root);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    // Create snapshot metadata
    snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    
    char *snapshot_id = generate_snapshot_id();
    if (!snapshot_id) {
        printf("Error: Failed to generate snapshot ID\n");
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    strncpy(snapshot.id, snapshot_id, sizeof(snapshot.id) - 1);
    snapshot.description = strdup(message);
    snapshot.timestamp = time(NULL);
    
    // Add git information
    if (git_branch) {
        snapshot.git_branch = strdup(git_branch);
        snapshot.git_commit = git_get_current_commit(repo_root);
        snapshot.git_dirty = git_has_uncommitted_changes(repo_root);
        
        if (snapshot.git_commit) {
            printf("Git branch: %s (commit: %.7s%s)\n", git_branch, snapshot.git_commit,
                   snapshot.git_dirty ? ", uncommitted changes" : "");
        }
    }
    
    // Find parent snapshot (most recent one)
    char *parent_id = find_latest_snapshot(fractyl_dir, git_branch);
    if (parent_id) {
        snapshot.parent = parent_id; // Transfer ownership to snapshot
    }
    
    // Store the index in object storage and get its hash
    // First, serialize the index to get its data
    char temp_index_path[] = "/tmp/fractyl_snapshot_index_XXXXXX";
    int temp_fd = mkstemp(temp_index_path);
    if (temp_fd == -1) {
        printf("Error: Failed to create temporary index file\n");
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        json_free_snapshot(&snapshot);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    close(temp_fd);
    
    // Save index to temporary file
    result = index_save(&new_index, temp_index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save temporary index: %d\n", result);
        unlink(temp_index_path);
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        json_free_snapshot(&snapshot);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    // Store the index file in object storage
    result = object_store_file(temp_index_path, fractyl_dir, snapshot.index_hash);
    unlink(temp_index_path); // Clean up temp file
    
    if (result != FRACTYL_OK) {
        printf("Error: Failed to store index in object storage: %d\n", result);
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        json_free_snapshot(&snapshot);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    // Save snapshot metadata
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, git_branch);
    if (!snapshots_dir) {
        printf("Error: Failed to get snapshots directory\n");
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        json_free_snapshot(&snapshot);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    // Ensure snapshots directory exists
    paths_ensure_directory(snapshots_dir);
    
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s.json", snapshots_dir, snapshot_id);
    
    result = json_save_snapshot(&snapshot, snapshot_path);
    free(snapshots_dir);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save snapshot: %d\n", result);
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        free(git_branch);
        json_free_snapshot(&snapshot);
        if (prev_index_ptr) index_free(&prev_index);
        index_free(&new_index);
        fractyl_lock_release(&lock);
        return 1;
    }
    
    // Update CURRENT file
    char *current_path = paths_get_current_file(fractyl_dir, git_branch);
    if (current_path) {
        // Ensure parent directory exists
        char *parent_dir = strdup(current_path);
        char *last_slash = strrchr(parent_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            paths_ensure_directory(parent_dir);
        }
        free(parent_dir);
        
        FILE *current_file = fopen(current_path, "w");
        if (current_file) {
            fprintf(current_file, "%s\n", snapshot_id);
            fclose(current_file);
        }
        free(current_path);
    }
    
    printf("Created snapshot %s: \"%s\"\n", snapshot_id, message);
    printf("Stored %zu files in object storage\n", new_index.count);
    
    // Cleanup
    free(snapshot_id);
    if (auto_message) {
        free(auto_message);
    }
    free(repo_root);
    free(git_branch);
    json_free_snapshot(&snapshot);
    if (prev_index_ptr) index_free(&prev_index);
    index_free(&new_index);
    fractyl_lock_release(&lock);
    
    return 0;
}