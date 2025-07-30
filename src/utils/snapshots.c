#include "snapshots.h"
#include "paths.h"
#include "json.h"
#include "../include/core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

// Get chronologically ordered snapshots for a branch (newest first)
char** get_chronological_snapshots(const char *fractyl_dir, const char *branch, size_t *count) {
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
static int resolve_snapshot_prefix(const char *prefix, const char *fractyl_dir, const char *branch, char *result_id) {
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!snapshots_dir) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        free(snapshots_dir);
        return FRACTYL_ERROR_GENERIC;
    }
    
    char *matches[256]; // Max matches to track
    int match_count = 0;
    size_t prefix_len = strlen(prefix);
    
    // Minimum prefix length check
    if (prefix_len < 4) {
        closedir(d);
        free(snapshots_dir);
        return FRACTYL_ERROR_GENERIC;
    }
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && match_count < 256) {
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        // Extract snapshot ID from filename (remove .json)
        char *dot = strstr(entry->d_name, ".json");
        if (!dot) continue;
        
        size_t id_len = dot - entry->d_name;
        if (id_len >= 64) continue; // ID too long
        
        char snapshot_id[65];
        strncpy(snapshot_id, entry->d_name, id_len);
        snapshot_id[id_len] = '\0';
        
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
        strcpy(result_id, matches[0]);
        free(matches[0]);
        return FRACTYL_OK;
    } else {
        // Multiple matches - clean up and return error
        for (int i = 0; i < match_count; i++) {
            free(matches[i]);
        }
        return FRACTYL_ERROR_GENERIC; // Ambiguous prefix
    }
}

// Resolve relative notation like -1, -2 to snapshot IDs
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
    char **snapshots = get_chronological_snapshots(fractyl_dir, branch, &count);
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

// Check if input is a full snapshot ID (64 hex chars, possibly with hyphens)
static int is_full_snapshot_id(const char *input) {
    size_t len = strlen(input);
    if (len != 64 && len != 36) { // 64 chars or UUID format with hyphens
        return 0;
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || 
              (c >= 'A' && c <= 'F') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

// Resolve any snapshot identifier to full ID
int resolve_snapshot_id(const char *input, const char *fractyl_dir, const char *branch, char *result_id) {
    if (!input || !fractyl_dir || !result_id) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // Try relative notation first (-1, -2, etc.)
    if (input[0] == '-') {
        return resolve_relative_snapshot(input, fractyl_dir, branch, result_id);
    }
    
    // If it looks like a full ID, use it directly
    if (is_full_snapshot_id(input)) {
        strcpy(result_id, input);
        return FRACTYL_OK;
    }
    
    // Otherwise, try prefix resolution
    return resolve_snapshot_prefix(input, fractyl_dir, branch, result_id);
}