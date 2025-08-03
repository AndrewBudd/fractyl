#ifndef FRACTYL_GIT_H
#define FRACTYL_GIT_H

// Check if we're in a git repository
int git_is_repository(const char *path);

// Get the current git branch name
// Returns allocated string that caller must free, or NULL on error
char* git_get_current_branch(const char *repo_path);

// Get the current git commit hash  
// Returns allocated string that caller must free, or NULL on error
char* git_get_current_commit(const char *repo_path);

// Check if there are uncommitted changes
int git_has_uncommitted_changes(const char *repo_path);

// Get git repository root directory
// Returns allocated string that caller must free, or NULL on error  
char* git_get_repository_root(const char *path);

// Check if a directory is a git repository root (potential submodule boundary)
// This checks for .git directory directly in the given path, not walking up the tree
int git_is_repository_root(const char *path);

#endif // FRACTYL_GIT_H