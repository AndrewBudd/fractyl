#include "../include/commands.h"
#include "../include/core.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../core/hash.h"
#include "../utils/json.h"
#include "../utils/fs.h"
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

static int scan_directory_recursive(const char *dir_path, const char *relative_path, 
                                   index_t *new_index, const char *fractyl_dir) {
    DIR *d = opendir(dir_path);
    if (!d) return FRACTYL_ERROR_IO;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip .fractyl and .git directories
        if (strcmp(entry->d_name, ".fractyl") == 0 || strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char full_path[2048];
        char rel_path[2048];
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (strlen(relative_path) == 0) {
            strcpy(rel_path, entry->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "%s/%s", relative_path, entry->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recursively scan directory
            int result = scan_directory_recursive(full_path, rel_path, new_index, fractyl_dir);
            if (result != FRACTYL_OK) {
                closedir(d);
                return result;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Skip files larger than 1GB
            if (st.st_size > 1024 * 1024 * 1024) {
                printf("Skipping large file: %s (%ld bytes)\n", rel_path, st.st_size);
                continue;
            }

            // Create index entry
            index_entry_t entry_data;
            memset(&entry_data, 0, sizeof(entry_data));
            
            entry_data.path = strdup(rel_path);
            if (!entry_data.path) {
                closedir(d);
                return FRACTYL_ERROR_OUT_OF_MEMORY;
            }
            
            entry_data.mode = st.st_mode;
            entry_data.size = st.st_size;
            entry_data.mtime = st.st_mtime;
            
            // Hash the file and store in object storage
            int result = object_store_file(full_path, fractyl_dir, entry_data.hash);
            if (result != FRACTYL_OK) {
                printf("Warning: Failed to store file %s: %d\n", rel_path, result);
                free(entry_data.path);
                continue;
            }
            
            // Add to index
            result = index_add_entry(new_index, &entry_data);
            free(entry_data.path);
            
            if (result != FRACTYL_OK) {
                closedir(d);
                return result;
            }
        }
    }

    closedir(d);
    return FRACTYL_OK;
}

// Find the most recent snapshot to use as parent
static char* find_latest_snapshot(const char *fractyl_dir) {
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
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
    return latest_id; // Caller must free
}

// Get current snapshot ID from CURRENT file
static char* get_current_snapshot_id(const char *fractyl_dir) {
    char current_path[2048];
    snprintf(current_path, sizeof(current_path), "%s/CURRENT", fractyl_dir);
    
    FILE *f = fopen(current_path, "r");
    if (!f) {
        return NULL;
    }
    
    char current_id[64];
    if (!fgets(current_id, sizeof(current_id), f)) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    
    // Strip newline
    size_t len = strlen(current_id);
    if (len > 0 && current_id[len-1] == '\n') {
        current_id[len-1] = '\0';
    }
    
    return strdup(current_id);
}

// Generate short hash from snapshot ID (first 6 characters)
static void generate_short_hash(const char *snapshot_id, char *short_hash, size_t max_len) {
    size_t id_len = strlen(snapshot_id);
    size_t copy_len = (id_len < 6) ? id_len : 6;
    if (copy_len >= max_len) {
        copy_len = max_len - 1;
    }
    strncpy(short_hash, snapshot_id, copy_len);
    short_hash[copy_len] = '\0';
}

// Check if we're creating a divergent branch
static int is_divergent_branch(const char *fractyl_dir, const char *current_id, const char *latest_id) {
    if (!current_id || !latest_id) {
        return 0; // Not divergent if we don't have both IDs
    }
    
    if (strcmp(current_id, latest_id) == 0) {
        return 0; // Current is latest, not divergent
    }
    
    // Load latest snapshot to check its parent
    char latest_path[2048];
    snprintf(latest_path, sizeof(latest_path), "%s/snapshots/%s.json", fractyl_dir, latest_id);
    
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
static char* generate_auto_description(const char *fractyl_dir) {
    char *latest_id = find_latest_snapshot(fractyl_dir);
    char *current_id = get_current_snapshot_id(fractyl_dir);
    
    if (!latest_id) {
        // First snapshot
        free(current_id);
        return strdup("working");
    }
    
    // Check if this is a divergent branch
    int divergent = is_divergent_branch(fractyl_dir, current_id, latest_id);
    
    // Load the snapshot we're building from (current, not latest)
    char *parent_id = current_id ? current_id : latest_id;
    char parent_path[2048];
    snprintf(parent_path, sizeof(parent_path), "%s/snapshots/%s.json", fractyl_dir, parent_id);
    
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
    
    // Generate auto description if no message provided
    if (!message) {
        auto_message = generate_auto_description(fractyl_dir);
        message = auto_message;
        printf("Auto-generating description: \"%s\"\n", message);
    }
    
    // Load current index
    char index_path[2048];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    
    index_t current_index;
    int result = index_load(&current_index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to load index: %d\n", result);
        if (auto_message) free(auto_message);
        free(repo_root);
        return 1;
    }
    
    // Scan current directory and build new index
    index_t new_index;
    index_init(&new_index);
    
    printf("Scanning directory...\n");
    
    result = scan_directory_recursive(repo_root, "", &new_index, fractyl_dir);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to scan directory: %d\n", result);
        if (auto_message) free(auto_message);
        free(repo_root);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    printf("Found %zu files\n", new_index.count);
    
    // Check if there are changes (simple comparison for now)
    if (current_index.count == new_index.count) {
        // TODO: More sophisticated change detection
        printf("No changes detected since last snapshot\n");
        if (auto_message) free(auto_message);
        free(repo_root);
        index_free(&current_index);
        index_free(&new_index);
        return 0;
    }
    
    // Save new index
    result = index_save(&new_index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save index: %d\n", result);
        if (auto_message) free(auto_message);
        free(repo_root);
        index_free(&current_index);
        index_free(&new_index);
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
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    strncpy(snapshot.id, snapshot_id, sizeof(snapshot.id) - 1);
    snapshot.description = strdup(message);
    snapshot.timestamp = time(NULL);
    
    // Find parent snapshot (most recent one)
    char *parent_id = find_latest_snapshot(fractyl_dir);
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
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
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
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
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
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    // Save snapshot metadata
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", 
             fractyl_dir, snapshot_id);
    
    result = json_save_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save snapshot: %d\n", result);
        free(snapshot_id);
        if (auto_message) free(auto_message);
        free(repo_root);
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    // Update CURRENT file
    char current_path[2048];
    snprintf(current_path, sizeof(current_path), "%s/CURRENT", fractyl_dir);
    FILE *current_file = fopen(current_path, "w");
    if (current_file) {
        fprintf(current_file, "%s\n", snapshot_id);
        fclose(current_file);
    }
    
    printf("Created snapshot %s: \"%s\"\n", snapshot_id, message);
    printf("Stored %zu files in object storage\n", new_index.count);
    
    // Cleanup
    free(snapshot_id);
    if (auto_message) {
        free(auto_message);
    }
    free(repo_root);
    json_free_snapshot(&snapshot);
    index_free(&current_index);
    index_free(&new_index);
    
    return 0;
}