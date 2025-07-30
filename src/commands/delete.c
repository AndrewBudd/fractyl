#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../utils/paths.h"
#include "../utils/git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

// Get chronologically ordered snapshots (newest first)
static char** get_chronological_snapshots_for_branch(const char *fractyl_dir, const char *branch, size_t *count) {
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        *count = 0;
        return NULL;
    }
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        *count = 0;
        free(snapshots_dir);
        return NULL;
    }
    
    // First pass: collect all snapshots with timestamps
    typedef struct {
        char *id;
        time_t timestamp;
    } snapshot_info_t;
    
    snapshot_info_t *snapshots = NULL;
    size_t capacity = 0;
    *count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", snapshots_dir, entry->d_name);
        
        snapshot_t snapshot;
        if (json_load_snapshot(&snapshot, snapshot_path) == FRACTYL_OK) {
            if (*count >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                snapshots = realloc(snapshots, capacity * sizeof(snapshot_info_t));
            }
            
            snapshots[*count].id = strdup(snapshot.id);
            snapshots[*count].timestamp = snapshot.timestamp;
            (*count)++;
            
            json_free_snapshot(&snapshot);
        }
    }
    closedir(d);
    free(snapshots_dir);
    
    if (*count == 0) {
        free(snapshots);
        return NULL;
    }
    
    // Sort by timestamp (newest first)
    for (size_t i = 0; i < *count - 1; i++) {
        for (size_t j = i + 1; j < *count; j++) {
            if (snapshots[i].timestamp < snapshots[j].timestamp) {
                snapshot_info_t temp = snapshots[i];
                snapshots[i] = snapshots[j];
                snapshots[j] = temp;
            }
        }
    }
    
    // Extract just the IDs
    char **result = malloc(*count * sizeof(char*));
    for (size_t i = 0; i < *count; i++) {
        result[i] = snapshots[i].id;
    }
    free(snapshots);
    
    return result;
}

// Resolve a hash prefix to a full snapshot ID
// Returns 0 on success, negative on error
// result_id must be at least 65 chars (64 + null terminator)
static int resolve_snapshot_prefix(const char *prefix, const char *fractyl_dir, const char *branch, char *result_id) {
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        return FRACTYL_ERROR_IO;
    }
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        free(snapshots_dir);
        return FRACTYL_ERROR_IO;
    }
    
    size_t prefix_len = strlen(prefix);
    if (prefix_len < 4) {
        closedir(d);
        free(snapshots_dir);
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
    free(snapshots_dir);
    
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
static int resolve_relative_snapshot(const char *relative_spec, const char *fractyl_dir, const char *branch, char *result_id) {
    if (relative_spec[0] != '-' || strlen(relative_spec) < 2) {
        return FRACTYL_ERROR_GENERIC; // Not a relative spec
    }
    
    // Parse the number after the dash
    int steps_back = atoi(relative_spec + 1);
    if (steps_back <= 0) {
        return FRACTYL_ERROR_GENERIC; // Invalid number
    }
    
    // Get chronologically ordered snapshots
    size_t count;
    char **snapshots = get_chronological_snapshots_for_branch(fractyl_dir, branch, &count);
    if (!snapshots || count == 0) {
        return FRACTYL_ERROR_SNAPSHOT_NOT_FOUND;
    }
    
    // -1 means latest (index 0), -2 means second latest (index 1), etc.
    size_t index = (size_t)(steps_back - 1);
    if (index >= count) {
        // Free the snapshots array
        for (size_t i = 0; i < count; i++) {
            free(snapshots[i]);
        }
        free(snapshots);
        return FRACTYL_ERROR_SNAPSHOT_NOT_FOUND; // Not enough snapshots
    }
    
    strcpy(result_id, snapshots[index]);
    
    // Free the snapshots array
    for (size_t i = 0; i < count; i++) {
        free(snapshots[i]);
    }
    free(snapshots);
    
    return FRACTYL_OK;
}

int cmd_delete(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: frac delete <snapshot-id>\n");
        printf("Delete a snapshot\n");
        printf("\nSnapshot identifiers can be:\n");
        printf("  abc123                          # Hash prefix (minimum 4 chars)\n");
        printf("  abc123...                       # Full hash\n");
        printf("  -1                              # Previous snapshot\n");
        printf("  -2                              # Two snapshots back\n");
        printf("\nExamples:\n");
        printf("  frac delete -1                  # Delete latest snapshot\n");
        printf("  frac delete abc123              # Delete by prefix\n");
        printf("\nUse 'frac list' to see available snapshots\n");
        return 1;
    }
    
    const char *snapshot_input = argv[2];
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Get current git branch
    char *git_branch = paths_get_current_branch(repo_root);
    
    // Resolve snapshot identifier (prefix or relative notation) to full ID
    char snapshot_id[65];
    
    // Try relative notation first, then prefix resolution
    int result = resolve_relative_snapshot(snapshot_input, fractyl_dir, git_branch, snapshot_id);
    if (result != FRACTYL_OK) {
        result = resolve_snapshot_prefix(snapshot_input, fractyl_dir, git_branch, snapshot_id);
        if (result != FRACTYL_OK) {
            if (result == FRACTYL_ERROR_SNAPSHOT_NOT_FOUND) {
                printf("Error: No snapshot found matching '%s'\n", snapshot_input);
                printf("Use 'frac list' to see available snapshots\n");
            } else if (result == FRACTYL_ERROR_GENERIC && strlen(snapshot_input) < 4) {
                printf("Error: Snapshot identifier '%s' is too short (minimum 4 characters for prefixes)\n", snapshot_input);
            }
            free(repo_root);
            free(git_branch);
            return 1;
        }
    }
    
    // Build path to snapshot file using branch-aware directory
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, git_branch);
    if (!snapshots_dir) {
        printf("Error: Failed to get snapshots directory\n");
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s.json", snapshots_dir, snapshot_id);
    free(snapshots_dir);
    
    // Check if snapshot exists
    struct stat st;
    if (stat(snapshot_path, &st) != 0) {
        printf("Error: Snapshot '%s' not found\n", snapshot_id);
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    // Load snapshot to show info
    snapshot_t snapshot;
    result = json_load_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Invalid snapshot file\n");
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    printf("Deleting snapshot %s: \"%s\"\n", snapshot_id,
           snapshot.description ? snapshot.description : "");
    
    // Delete snapshot file
    if (unlink(snapshot_path) != 0) {
        printf("Error: Failed to delete snapshot file\n");
        json_free_snapshot(&snapshot);
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    printf("Snapshot %s deleted successfully\n", snapshot_id);
    printf("Note: Object files are not garbage collected yet\n");
    
    json_free_snapshot(&snapshot);
    free(repo_root);
    free(git_branch);
    return 0;
}