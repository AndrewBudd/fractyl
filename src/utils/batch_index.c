#include "batch_index.h"
#include "../core/index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// dir_entry_t is defined in batch_index.h

// Build a directory map from an index for efficient batch operations
dir_map_t* build_directory_map(const index_t *index) {
    if (!index || index->count == 0) return NULL;
    
    dir_map_t *map = calloc(1, sizeof(dir_map_t));
    if (!map) return NULL;
    
    // Pre-allocate space for directories
    map->capacity = 1024;
    map->dirs = calloc(map->capacity, sizeof(dir_entry_t));
    if (!map->dirs) {
        free(map);
        return NULL;
    }
    
    // Build map of files by directory
    for (size_t i = 0; i < index->count; i++) {
        const char *file_path = index->entries[i].path;
        
        // Extract directory path
        char dir_path[2048];
        const char *last_slash = strrchr(file_path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - file_path;
            strncpy(dir_path, file_path, dir_len);
            dir_path[dir_len] = '\0';
        } else {
            strcpy(dir_path, ""); // Root directory
        }
        
        // Find or create directory entry
        dir_entry_t *dir = NULL;
        for (size_t j = 0; j < map->count; j++) {
            if (strcmp(map->dirs[j].path, dir_path) == 0) {
                dir = &map->dirs[j];
                break;
            }
        }
        
        if (!dir) {
            // Add new directory
            if (map->count >= map->capacity) {
                size_t new_capacity = map->capacity * 2;
                dir_entry_t *new_dirs = realloc(map->dirs, new_capacity * sizeof(dir_entry_t));
                if (!new_dirs) continue;
                map->dirs = new_dirs;
                map->capacity = new_capacity;
                memset(&map->dirs[map->count], 0, (new_capacity - map->count) * sizeof(dir_entry_t));
            }
            
            dir = &map->dirs[map->count++];
            dir->path = strdup(dir_path);
            dir->capacity = 16;
            dir->file_indices = calloc(dir->capacity, sizeof(size_t));
        }
        
        // Add file index to directory
        if (dir->file_count >= dir->capacity) {
            size_t new_capacity = dir->capacity * 2;
            size_t *new_indices = realloc(dir->file_indices, new_capacity * sizeof(size_t));
            if (!new_indices) continue;
            dir->file_indices = new_indices;
            dir->capacity = new_capacity;
        }
        
        dir->file_indices[dir->file_count++] = i;
    }
    
    return map;
}

// Free directory map
void free_directory_map(dir_map_t *map) {
    if (!map) return;
    
    for (size_t i = 0; i < map->count; i++) {
        free(map->dirs[i].path);
        free(map->dirs[i].file_indices);
    }
    free(map->dirs);
    free(map);
}

// Batch copy all files from a directory (no stat calls, no string ops per file)
int batch_copy_directory_files(index_t *new_index, const index_t *prev_index,
                             const dir_entry_t *dir) {
    if (!new_index || !prev_index || !dir) return -1;
    
    // Pre-allocate space in new index if needed
    size_t required_capacity = new_index->count + dir->file_count;
    if (required_capacity > new_index->capacity) {
        size_t new_capacity = required_capacity * 2;
        index_entry_t *new_entries = realloc(new_index->entries, 
                                            new_capacity * sizeof(index_entry_t));
        if (!new_entries) return -1;
        new_index->entries = new_entries;
        new_index->capacity = new_capacity;
    }
    
    // Batch copy all files from this directory
    for (size_t i = 0; i < dir->file_count; i++) {
        const index_entry_t *src = &prev_index->entries[dir->file_indices[i]];
        index_entry_t *dst = &new_index->entries[new_index->count++];
        
        // Direct memory copy of entry
        *dst = *src;
        
        // Duplicate the path string
        dst->path = strdup(src->path);
        if (!dst->path) {
            new_index->count--;
            return -1;
        }
    }
    
    return 0;
}

// Get directory entry from map
const dir_entry_t* get_directory_entry(const dir_map_t *map, const char *dir_path) {
    if (!map || !dir_path) return NULL;
    
    for (size_t i = 0; i < map->count; i++) {
        if (strcmp(map->dirs[i].path, dir_path) == 0) {
            return &map->dirs[i];
        }
    }
    
    return NULL;
}