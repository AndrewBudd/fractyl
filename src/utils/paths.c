#include "paths.h"
#include "git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// Get the branch-aware snapshots directory path
// Returns allocated string that caller must free
char* paths_get_snapshots_dir(const char *fractyl_dir, const char *branch) {
    if (!fractyl_dir) return NULL;
    
    char *path = malloc(4096);
    if (!path) return NULL;
    
    if (branch && strlen(branch) > 0) {
        // Branch-specific path
        snprintf(path, 4096, "%s/refs/heads/%s/snapshots", fractyl_dir, branch);
    } else {
        // Legacy path for non-git repos
        snprintf(path, 4096, "%s/snapshots", fractyl_dir);
    }
    
    return path;
}

// Get the branch-aware CURRENT file path
// Returns allocated string that caller must free
char* paths_get_current_file(const char *fractyl_dir, const char *branch) {
    if (!fractyl_dir) return NULL;
    
    char *path = malloc(4096);
    if (!path) return NULL;
    
    if (branch && strlen(branch) > 0) {
        // Branch-specific path
        snprintf(path, 4096, "%s/refs/heads/%s/CURRENT", fractyl_dir, branch);
    } else {
        // Legacy path for non-git repos
        snprintf(path, 4096, "%s/CURRENT", fractyl_dir);
    }
    
    return path;
}

// Ensure directory exists, creating parent directories as needed
int paths_ensure_directory(const char *path) {
    if (!path) return -1;
    
    char *path_copy = strdup(path);
    if (!path_copy) return -1;
    
    char *p = path_copy;
    
    // Skip leading slash
    if (*p == '/') p++;
    
    while (*p) {
        // Find next slash
        while (*p && *p != '/') p++;
        
        if (*p == '/') {
            // Temporarily terminate string
            *p = '\0';
            
            // Create directory if it doesn't exist
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                free(path_copy);
                return -1;
            }
            
            // Restore slash
            *p = '/';
            p++;
        }
    }
    
    // Create final directory
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }
    
    free(path_copy);
    return 0;
}

// Get current branch for path operations
// Returns allocated string that caller must free, or NULL if not in git repo
char* paths_get_current_branch(const char *repo_root) {
    if (git_is_repository(repo_root)) {
        return git_get_current_branch(repo_root);
    }
    return NULL;
}

// Migrate legacy snapshots to branch-specific directory
int paths_migrate_legacy_snapshots(const char *fractyl_dir, const char *branch) {
    if (!fractyl_dir || !branch) return -1;
    
    // Check if legacy snapshots directory exists
    char legacy_snapshots[4096];
    snprintf(legacy_snapshots, sizeof(legacy_snapshots), "%s/snapshots", fractyl_dir);
    
    struct stat st;
    if (stat(legacy_snapshots, &st) != 0 || !S_ISDIR(st.st_mode)) {
        // No legacy snapshots to migrate
        return 0;
    }
    
    // Get new branch-specific directory
    char *new_snapshots = paths_get_snapshots_dir(fractyl_dir, branch);
    if (!new_snapshots) return -1;
    
    // Ensure new directory structure exists
    char parent_dir[4096];
    snprintf(parent_dir, sizeof(parent_dir), "%s/refs/heads/%s", fractyl_dir, branch);
    if (paths_ensure_directory(parent_dir) != 0) {
        free(new_snapshots);
        return -1;
    }
    
    // Move snapshots directory
    if (rename(legacy_snapshots, new_snapshots) != 0) {
        free(new_snapshots);
        return -1;
    }
    
    free(new_snapshots);
    
    // Also migrate CURRENT file
    char legacy_current[4096];
    char new_current[4096];
    snprintf(legacy_current, sizeof(legacy_current), "%s/CURRENT", fractyl_dir);
    snprintf(new_current, sizeof(new_current), "%s/refs/heads/%s/CURRENT", fractyl_dir, branch);
    
    if (stat(legacy_current, &st) == 0) {
        rename(legacy_current, new_current); // Ignore errors
    }
    
    return 0;
}