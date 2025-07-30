#ifndef FRACTYL_PATHS_H
#define FRACTYL_PATHS_H

// Get the branch-aware snapshots directory path
// Returns allocated string that caller must free
char* paths_get_snapshots_dir(const char *fractyl_dir, const char *branch);

// Get the branch-aware CURRENT file path
// Returns allocated string that caller must free
char* paths_get_current_file(const char *fractyl_dir, const char *branch);

// Ensure directory exists, creating parent directories as needed
int paths_ensure_directory(const char *path);

// Get current branch for path operations
// Returns allocated string that caller must free, or NULL if not in git repo
char* paths_get_current_branch(const char *repo_root);

// Migrate legacy snapshots to branch-specific directory
int paths_migrate_legacy_snapshots(const char *fractyl_dir, const char *branch);

#endif // FRACTYL_PATHS_H