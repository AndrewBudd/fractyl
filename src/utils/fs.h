// fs.h - Filesystem helper functions
#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdbool.h>

bool file_exists(const char *path);
bool is_directory(const char *path);
bool mkdir_p(const char *path);
int enumerate_files(const char *root, char ***out_paths, size_t *out_count);
// TODO: add more function declarations as necessary

#endif // FS_H
