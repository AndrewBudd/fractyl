#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../core/index.h"
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
        
        printf("\nTo see which files changed, you can:\n");
        printf("  1. Use 'fractyl restore %s' to restore first snapshot\n", snapshot_a);
        printf("  2. Compare with working directory\n");
        printf("  3. Use 'fractyl restore %s' to restore second snapshot\n", snapshot_b);
        printf("\nNote: Full file-by-file diff coming in future release\n");
    }
    
    json_free_snapshot(&snap_a);
    json_free_snapshot(&snap_b);
    
    return 0;
}