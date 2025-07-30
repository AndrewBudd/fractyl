#include "git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Check if we're in a git repository by looking for .git directory
int git_is_repository(const char *path) {
    char git_dir[2048];
    char current_path[2048];
    
    // Start from the given path or current directory
    if (path) {
        strncpy(current_path, path, sizeof(current_path) - 1);
        current_path[sizeof(current_path) - 1] = '\0';
    } else {
        if (!getcwd(current_path, sizeof(current_path))) {
            return 0;
        }
    }
    
    // Walk up the directory tree looking for .git
    while (1) {
        snprintf(git_dir, sizeof(git_dir), "%s/.git", current_path);
        
        struct stat st;
        if (stat(git_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 1; // Found .git directory
        }
        
        // Check if we've reached the root
        if (strcmp(current_path, "/") == 0) {
            break;
        }
        
        // Move up one directory
        char *last_slash = strrchr(current_path, '/');
        if (!last_slash || last_slash == current_path) {
            strcpy(current_path, "/");
        } else {
            *last_slash = '\0';
        }
    }
    
    return 0; // Not in a git repository
}

// Get the current git branch name
// Returns allocated string that caller must free, or NULL on error
char* git_get_current_branch(const char *repo_path) {
    char command[2048];
    char branch[256];
    FILE *fp;
    
    // Use git symbolic-ref to get current branch
    if (repo_path) {
        snprintf(command, sizeof(command), "cd \"%s\" && git symbolic-ref --short HEAD 2>/dev/null", repo_path);
    } else {
        snprintf(command, sizeof(command), "git symbolic-ref --short HEAD 2>/dev/null");
    }
    
    fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    if (fgets(branch, sizeof(branch), fp) == NULL) {
        pclose(fp);
        // Not on a branch, might be detached HEAD
        // Try to get commit hash instead
        if (repo_path) {
            snprintf(command, sizeof(command), "cd \"%s\" && git rev-parse --short HEAD 2>/dev/null", repo_path);
        } else {
            snprintf(command, sizeof(command), "git rev-parse --short HEAD 2>/dev/null");
        }
        
        fp = popen(command, "r");
        if (!fp) {
            return NULL;
        }
        
        if (fgets(branch, sizeof(branch), fp) == NULL) {
            pclose(fp);
            return NULL;
        }
        
        // Prepend "detached-" to indicate detached HEAD
        char detached_name[256];
        snprintf(detached_name, sizeof(detached_name), "detached-%s", branch);
        strcpy(branch, detached_name);
    }
    
    pclose(fp);
    
    // Remove trailing newline
    size_t len = strlen(branch);
    if (len > 0 && branch[len-1] == '\n') {
        branch[len-1] = '\0';
    }
    
    // Sanitize branch name for filesystem use
    for (size_t i = 0; i < strlen(branch); i++) {
        if (branch[i] == '/' || branch[i] == '\\' || branch[i] == ':' || 
            branch[i] == '*' || branch[i] == '?' || branch[i] == '"' || 
            branch[i] == '<' || branch[i] == '>' || branch[i] == '|') {
            branch[i] = '-';
        }
    }
    
    return strdup(branch);
}

// Get the current git commit hash
// Returns allocated string that caller must free, or NULL on error
char* git_get_current_commit(const char *repo_path) {
    char command[2048];
    char commit[256];
    FILE *fp;
    
    if (repo_path) {
        snprintf(command, sizeof(command), "cd \"%s\" && git rev-parse HEAD 2>/dev/null", repo_path);
    } else {
        snprintf(command, sizeof(command), "git rev-parse HEAD 2>/dev/null");
    }
    
    fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    if (fgets(commit, sizeof(commit), fp) == NULL) {
        pclose(fp);
        return NULL;
    }
    
    pclose(fp);
    
    // Remove trailing newline
    size_t len = strlen(commit);
    if (len > 0 && commit[len-1] == '\n') {
        commit[len-1] = '\0';
    }
    
    return strdup(commit);
}

// Check if there are uncommitted changes
int git_has_uncommitted_changes(const char *repo_path) {
    char command[2048];
    FILE *fp;
    
    if (repo_path) {
        snprintf(command, sizeof(command), "cd \"%s\" && git status --porcelain 2>/dev/null", repo_path);
    } else {
        snprintf(command, sizeof(command), "git status --porcelain 2>/dev/null");
    }
    
    fp = popen(command, "r");
    if (!fp) {
        return 0; // Assume no changes if we can't check
    }
    
    char buffer[256];
    int has_changes = 0;
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        has_changes = 1; // Any output means there are changes
    }
    
    pclose(fp);
    return has_changes;
}

// Get git repository root directory
// Returns allocated string that caller must free, or NULL on error
char* git_get_repository_root(const char *path) {
    char command[2048];
    char root[2048];
    FILE *fp;
    
    if (path) {
        snprintf(command, sizeof(command), "cd \"%s\" && git rev-parse --show-toplevel 2>/dev/null", path);
    } else {
        snprintf(command, sizeof(command), "git rev-parse --show-toplevel 2>/dev/null");
    }
    
    fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    if (fgets(root, sizeof(root), fp) == NULL) {
        pclose(fp);
        return NULL;
    }
    
    pclose(fp);
    
    // Remove trailing newline
    size_t len = strlen(root);
    if (len > 0 && root[len-1] == '\n') {
        root[len-1] = '\0';
    }
    
    return strdup(root);
}