#include "../include/commands.h"
#include "../include/core.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../utils/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

// Find the root of a fractyl repository by searching up the directory tree
char* fractyl_find_repo_root(const char *start_path) {
    char current_path[PATH_MAX];
    char fractyl_dir[PATH_MAX];
    
    // Start with the provided path, or current directory if NULL
    if (start_path) {
        if (realpath(start_path, current_path) == NULL) {
            return NULL;
        }
    } else {
        if (!getcwd(current_path, sizeof(current_path))) {
            return NULL;
        }
    }
    
    // Search up the directory tree
    char *path_copy = strdup(current_path);
    if (!path_copy) {
        return NULL;
    }
    
    while (strlen(path_copy) > 1) { // Stop at root directory "/"
        snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", path_copy);
        
        struct stat st;
        if (stat(fractyl_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            // Found .fractyl directory, return this path as repo root
            return path_copy; // Caller must free
        }
        
        // Move to parent directory
        char *last_slash = strrchr(path_copy, '/');
        if (last_slash == path_copy) {
            // We're at root directory
            break;
        } else if (last_slash) {
            *last_slash = '\0';
        } else {
            // No more parent directories
            break;
        }
    }
    
    // Check root directory as well
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", path_copy);
    struct stat st;
    if (stat(fractyl_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        return path_copy;
    }
    
    free(path_copy);
    return NULL; // No repository found
}

// Initialize a new fractyl repository in the given path
int fractyl_init_repo(const char *path) {
    char fractyl_dir[PATH_MAX];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", path);
    
    // Check if already initialized
    struct stat st;
    if (stat(fractyl_dir, &st) == 0) {
        return 0; // Already exists
    }
    
    // Initialize object storage
    int result = object_storage_init(fractyl_dir);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Create snapshots directory
    char snapshots_dir[PATH_MAX];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    if (mkdir(snapshots_dir, 0755) != 0) {
        return FRACTYL_ERROR_IO;
    }
    
    // Initialize empty index
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    
    index_t empty_index;
    index_init(&empty_index);
    
    result = index_save(&empty_index, index_path);
    index_free(&empty_index);
    
    return result;
}