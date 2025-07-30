#ifndef FRACTYL_GITIGNORE_H
#define FRACTYL_GITIGNORE_H

// Check if a path should be ignored according to gitignore rules
int gitignore_should_ignore(const char *repo_root, const char *relative_path, int is_directory);

// Check if a path should be ignored (convenience function that determines if it's a directory)
int gitignore_should_ignore_path(const char *repo_root, const char *full_path, const char *relative_path);

#endif // FRACTYL_GITIGNORE_H