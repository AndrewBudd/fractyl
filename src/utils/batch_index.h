#ifndef BATCH_INDEX_H
#define BATCH_INDEX_H

#include "../include/core.h"
#include <stddef.h>

// Directory entry with list of file indices
typedef struct {
    char *path;
    size_t *file_indices;
    size_t file_count;
    size_t capacity;
} dir_entry_t;

// Forward declarations
typedef struct dir_map dir_map_t;

// Directory map for batch operations
struct dir_map {
    dir_entry_t *dirs;
    size_t count;
    size_t capacity;
};

// Build directory map from index for efficient batch operations
dir_map_t* build_directory_map(const index_t *index);

// Free directory map
void free_directory_map(dir_map_t *map);

// Batch copy all files from a directory (no stat calls, no string ops per file)
int batch_copy_directory_files(index_t *new_index, const index_t *prev_index,
                             const dir_entry_t *dir);

// Get directory entry from map
const dir_entry_t* get_directory_entry(const dir_map_t *map, const char *dir_path);

#endif // BATCH_INDEX_H