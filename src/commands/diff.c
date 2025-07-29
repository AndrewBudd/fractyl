#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../core/hash.h"
#include "../vendor/xdiff/fractyl-diff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>


static void print_hash(const unsigned char *hash) {
    for (int i = 0; i < 32; i++) {
        printf("%02x", hash[i]);
    }
}

// Find the most recent snapshot
static char* find_latest_snapshot(const char *fractyl_dir) {
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        return NULL;
    }
    
    char *latest_id = NULL;
    time_t latest_timestamp = 0;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", snapshots_dir, entry->d_name);
        
        snapshot_t snapshot;
        if (json_load_snapshot(&snapshot, snapshot_path) == FRACTYL_OK) {
            if (snapshot.timestamp > latest_timestamp) {
                latest_timestamp = snapshot.timestamp;
                free(latest_id);
                latest_id = strdup(snapshot.id);
            }
            json_free_snapshot(&snapshot);
        }
    }
    
    closedir(d);
    return latest_id;
}

// Resolve a hash prefix to a full snapshot ID
// Returns 0 on success, negative on error
// result_id must be at least 65 chars (64 + null terminator)
static int resolve_snapshot_prefix(const char *prefix, const char *fractyl_dir, char *result_id) {
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        return FRACTYL_ERROR_IO;
    }
    
    size_t prefix_len = strlen(prefix);
    if (prefix_len < 4) {
        closedir(d);
        return FRACTYL_ERROR_GENERIC; // Prefix too short
    }
    
    char *matches[64]; // Store up to 64 matches
    int match_count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && match_count < 64) {
        // Skip . and .. and non-.json files
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        // Remove .json extension for comparison
        char snapshot_id[256];
        size_t name_len = strlen(entry->d_name);
        if (name_len > sizeof(snapshot_id) - 1) {
            continue;
        }
        strcpy(snapshot_id, entry->d_name);
        char *dot = strrchr(snapshot_id, '.');
        if (dot) *dot = '\0';
        
        // Check if this snapshot ID starts with the prefix
        if (strncmp(snapshot_id, prefix, prefix_len) == 0) {
            matches[match_count] = strdup(snapshot_id);
            match_count++;
        }
    }
    
    closedir(d);
    
    if (match_count == 0) {
        return FRACTYL_ERROR_SNAPSHOT_NOT_FOUND;
    } else if (match_count == 1) {
        // Unique match found
        strcpy(result_id, matches[0]);
        free(matches[0]);
        return FRACTYL_OK;
    } else {
        // Ambiguous prefix
        printf("Error: Prefix '%s' is ambiguous, matches %d snapshots:\n", prefix, match_count);
        for (int i = 0; i < match_count; i++) {
            printf("  %s\n", matches[i]);
            free(matches[i]);
        }
        printf("Use a longer prefix to disambiguate\n");
        return FRACTYL_ERROR_GENERIC;
    }
}

// Resolve relative notation like -1, -2 to snapshot IDs
// Returns 0 on success, negative on error
static int resolve_relative_snapshot(const char *relative_spec, const char *fractyl_dir, char *result_id) {
    if (relative_spec[0] != '-' || strlen(relative_spec) < 2) {
        return FRACTYL_ERROR_GENERIC; // Not a relative spec
    }
    
    // Parse the number after the dash
    int steps_back = atoi(relative_spec + 1);
    if (steps_back <= 0) {
        return FRACTYL_ERROR_GENERIC; // Invalid number
    }
    
    // Find the current HEAD (most recent snapshot)
    char *current_id = find_latest_snapshot(fractyl_dir);
    if (!current_id) {
        return FRACTYL_ERROR_SNAPSHOT_NOT_FOUND; // No snapshots
    }
    
    // Walk back through parent chain
    char *walking_id = current_id;
    for (int i = 0; i < steps_back && walking_id; i++) {
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", fractyl_dir, walking_id);
        
        snapshot_t snapshot;
        if (json_load_snapshot(&snapshot, snapshot_path) != FRACTYL_OK) {
            free(current_id);
            if (walking_id != current_id) free(walking_id);
            return FRACTYL_ERROR_IO;
        }
        
        char *next_id = NULL;
        if (snapshot.parent) {
            next_id = strdup(snapshot.parent);
        }
        
        json_free_snapshot(&snapshot);
        
        if (walking_id != current_id) {
            free(walking_id);
        }
        walking_id = next_id;
    }
    
    free(current_id);
    
    if (!walking_id) {
        return FRACTYL_ERROR_SNAPSHOT_NOT_FOUND; // Walked past the beginning
    }
    
    strcpy(result_id, walking_id);
    free(walking_id);
    return FRACTYL_OK;
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

// Compare file contents between two snapshots with line-by-line diff
static int compare_file_contents(const index_entry_t *entry_a, const index_entry_t *entry_b, 
                                const char *path, const char *fractyl_dir) {
    void *data_a = NULL, *data_b = NULL;
    size_t size_a = 0, size_b = 0;
    
    // Load file content from first snapshot
    if (entry_a) {
        if (object_load(entry_a->hash, fractyl_dir, &data_a, &size_a) != FRACTYL_OK) {
            printf("Warning: Could not load content for %s from first snapshot\n", path);
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Load file content from second snapshot
    if (entry_b) {
        if (object_load(entry_b->hash, fractyl_dir, &data_b, &size_b) != FRACTYL_OK) {
            printf("Warning: Could not load content for %s from second snapshot\n", path);
            free(data_a);
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Perform the diff using xdiff
    int result = fractyl_diff_unified(path, data_a, size_a, path, data_b, size_b, 3);
    
    // Cleanup
    free(data_a);
    free(data_b);
    
    return result == 0 ? FRACTYL_OK : FRACTYL_ERROR_GENERIC;
}

// Compare file contents between two snapshots  
static int compare_snapshot_contents(const snapshot_t *snap_a, const snapshot_t *snap_b, const char *fractyl_dir) {
    printf("\nFile-by-file comparison:\n");
    
    // Compare the index hashes to determine if there are differences
    if (memcmp(snap_a->index_hash, snap_b->index_hash, 32) == 0) {
        printf("No differences detected between snapshots\n");
        return FRACTYL_OK;
    }
    
    // Load indices from both snapshots
    index_t index_a, index_b;
    if (load_snapshot_index(snap_a->index_hash, fractyl_dir, &index_a) != FRACTYL_OK) {
        printf("Could not load index from first snapshot\n");
        return FRACTYL_ERROR_IO;
    }
    
    if (load_snapshot_index(snap_b->index_hash, fractyl_dir, &index_b) != FRACTYL_OK) {
        printf("Could not load index from second snapshot\n");
        index_free(&index_a);
        return FRACTYL_ERROR_IO;
    }
    
    // Create a unified list of all files from both indices
    typedef struct {
        char *path;
        const index_entry_t *entry_a;
        const index_entry_t *entry_b;
    } file_comparison_t;
    
    file_comparison_t *comparisons = NULL;
    size_t comparison_count = 0;
    size_t comparison_capacity = 0;
    
    // Add all files from index_a
    for (size_t i = 0; i < index_a.count; i++) {
        if (comparison_count >= comparison_capacity) {
            comparison_capacity = comparison_capacity ? comparison_capacity * 2 : 16;
            comparisons = realloc(comparisons, comparison_capacity * sizeof(file_comparison_t));
        }
        comparisons[comparison_count].path = strdup(index_a.entries[i].path);
        comparisons[comparison_count].entry_a = &index_a.entries[i];
        comparisons[comparison_count].entry_b = index_find_entry(&index_b, index_a.entries[i].path);
        comparison_count++;
    }
    
    // Add files that are only in index_b
    for (size_t i = 0; i < index_b.count; i++) {
        const index_entry_t *found_in_a = index_find_entry(&index_a, index_b.entries[i].path);
        if (!found_in_a) {
            if (comparison_count >= comparison_capacity) {
                comparison_capacity = comparison_capacity ? comparison_capacity * 2 : 16;
                comparisons = realloc(comparisons, comparison_capacity * sizeof(file_comparison_t));
            }
            comparisons[comparison_count].path = strdup(index_b.entries[i].path);
            comparisons[comparison_count].entry_a = NULL;
            comparisons[comparison_count].entry_b = &index_b.entries[i];
            comparison_count++;
        }
    }
    
    // Compare each file
    for (size_t i = 0; i < comparison_count; i++) {
        const file_comparison_t *comp = &comparisons[i];
        
        // Skip if files are identical (same hash)
        if (comp->entry_a && comp->entry_b && 
            memcmp(comp->entry_a->hash, comp->entry_b->hash, 32) == 0) {
            continue;
        }
        
        // Perform the actual diff
        compare_file_contents(comp->entry_a, comp->entry_b, comp->path, fractyl_dir);
    }
    
    // Cleanup
    for (size_t i = 0; i < comparison_count; i++) {
        free(comparisons[i].path);
    }
    free(comparisons);
    index_free(&index_a);
    index_free(&index_b);
    
    return FRACTYL_OK;
}

int cmd_diff(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: fractyl diff <snapshot-a> <snapshot-b>\n");
        printf("Compare files between two snapshots\n");
        printf("\nSnapshot identifiers can be:\n");
        printf("  abc123                          # Hash prefix (minimum 4 chars)\n");
        printf("  abc123...                       # Full hash\n");
        printf("  -1                              # Previous snapshot\n");
        printf("  -2                              # Two snapshots back\n");
        printf("\nExamples:\n");
        printf("  fractyl diff -2 -1              # Compare last two snapshots\n");
        printf("  fractyl diff abc123 -1          # Compare prefix with latest\n");
        printf("\nUse 'fractyl list' to see available snapshots\n");
        return 1;
    }
    
    const char *snapshot_a_input = argv[2];
    const char *snapshot_b_input = argv[3];
    
    // Find .fractyl directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("Error: Cannot get current directory\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", cwd);
    
    struct stat st;
    if (stat(fractyl_dir, &st) != 0) {
        printf("Error: Not a fractyl repository (no .fractyl directory found)\n");
        return 1;
    }
    
    // Resolve snapshot identifiers (prefixes or relative notation) to full IDs
    char snapshot_a[65], snapshot_b[65];
    
    // Try relative notation first, then prefix resolution
    int result = resolve_relative_snapshot(snapshot_a_input, fractyl_dir, snapshot_a);
    if (result != FRACTYL_OK) {
        result = resolve_snapshot_prefix(snapshot_a_input, fractyl_dir, snapshot_a);
        if (result != FRACTYL_OK) {
            if (result == FRACTYL_ERROR_SNAPSHOT_NOT_FOUND) {
                printf("Error: No snapshot found matching '%s'\n", snapshot_a_input);
                printf("Use 'fractyl list' to see available snapshots\n");
            } else if (result == FRACTYL_ERROR_GENERIC && strlen(snapshot_a_input) < 4) {
                printf("Error: Snapshot identifier '%s' is too short (minimum 4 characters for prefixes)\n", snapshot_a_input);
            }
            return 1;
        }
    }
    
    result = resolve_relative_snapshot(snapshot_b_input, fractyl_dir, snapshot_b);
    if (result != FRACTYL_OK) {
        result = resolve_snapshot_prefix(snapshot_b_input, fractyl_dir, snapshot_b);
        if (result != FRACTYL_OK) {
            if (result == FRACTYL_ERROR_SNAPSHOT_NOT_FOUND) {
                printf("Error: No snapshot found matching '%s'\n", snapshot_b_input);
                printf("Use 'fractyl list' to see available snapshots\n");
            } else if (result == FRACTYL_ERROR_GENERIC && strlen(snapshot_b_input) < 4) {
                printf("Error: Snapshot identifier '%s' is too short (minimum 4 characters for prefixes)\n", snapshot_b_input);
            }
            return 1;
        }
    }
    
    // Check if comparing snapshot with itself
    if (strcmp(snapshot_a, snapshot_b) == 0) {
        printf("Warning: Comparing snapshot with itself\n");
        printf("Snapshot '%s' is identical to itself\n", snapshot_a);
        return 0;
    }
    
    // Build paths to snapshot files
    char snapshot_a_path[2048], snapshot_b_path[2048];
    snprintf(snapshot_a_path, sizeof(snapshot_a_path), "%s/snapshots/%s.json", fractyl_dir, snapshot_a);
    snprintf(snapshot_b_path, sizeof(snapshot_b_path), "%s/snapshots/%s.json", fractyl_dir, snapshot_b);
    
    // Load snapshot metadata for display
    snapshot_t snap_a, snap_b;
    if (json_load_snapshot(&snap_a, snapshot_a_path) != FRACTYL_OK) {
        printf("Error: Cannot load snapshot '%s'\n", snapshot_a);
        return 1;
    }
    
    if (json_load_snapshot(&snap_b, snapshot_b_path) != FRACTYL_OK) {
        printf("Error: Cannot load snapshot '%s'\n", snapshot_b);
        json_free_snapshot(&snap_a);
        return 1;
    }
    
    // Note: In a complete implementation, we would load the actual index
    // from the snapshot's index_hash. For now, we'll use a simplified approach.
    printf("diff %s..%s\n", snapshot_a, snapshot_b);
    printf("--- %s (%s)\n", snapshot_a, snap_a.description ? snap_a.description : "");
    printf("+++ %s (%s)\n", snapshot_b, snap_b.description ? snap_b.description : "");
    printf("\n");
    
    // Compare snapshots
    if (memcmp(snap_a.index_hash, snap_b.index_hash, 32) == 0) {
        printf("✓ Snapshots are identical\n");
        printf("  Both snapshots have the same file content and structure.\n");
    } else {
        printf("✗ Snapshots differ\n");
        printf("\nIndex hashes:\n");
        printf("  %s: ", snapshot_a);
        print_hash(snap_a.index_hash);
        printf("\n");
        printf("  %s: ", snapshot_b);
        print_hash(snap_b.index_hash);
        printf("\n");
        
        // Show timestamps for context
        printf("\nTimeline:\n");
        if (snap_a.timestamp < snap_b.timestamp) {
            printf("  %s (older) → %s (newer)\n", snapshot_a, snapshot_b);
        } else if (snap_a.timestamp > snap_b.timestamp) {
            printf("  %s (newer) ← %s (older)\n", snapshot_a, snapshot_b);
        } else {
            printf("  Both snapshots created at same time\n");
        }
        
        // Load indices from snapshots to perform file-by-file comparison
        if (compare_snapshot_contents(&snap_a, &snap_b, fractyl_dir) != FRACTYL_OK) {
            printf("\nWarning: Could not perform detailed file comparison\n");
            printf("To see which files changed, you can:\n");
            printf("  1. Use 'fractyl restore %s' to restore first snapshot\n", snapshot_a);
            printf("  2. Compare with working directory\n");
            printf("  3. Use 'fractyl restore %s' to restore second snapshot\n", snapshot_b);
        }
    }
    
    json_free_snapshot(&snap_a);
    json_free_snapshot(&snap_b);
    
    return 0;
}