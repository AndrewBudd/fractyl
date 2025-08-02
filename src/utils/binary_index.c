#include "binary_index.h"
#include "paths.h"
#include "../include/fractyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

// Hash table size (power of 2 for fast modulo)
#define HASH_TABLE_SIZE 8192
#define HASH_TABLE_MASK (HASH_TABLE_SIZE - 1)

// Simple hash function for paths
uint32_t binary_index_hash_path(const char *path) {
    uint32_t hash = 5381;
    int c;
    while ((c = *path++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Simple CRC32 implementation (truncated table for demo)
static uint32_t simple_crc32(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t crc = 0;
    
    while (len--) {
        crc = (crc >> 8) ^ (crc ^ *p++);
    }
    
    return crc;
}

uint32_t binary_index_crc32(const void *data, size_t len) {
    return simple_crc32(data, len);
}

// Initialize binary index
int binary_index_init(binary_index_t *index, const char *branch) {
    if (!index || !branch) return FRACTYL_ERROR_INVALID_ARGS;
    
    memset(index, 0, sizeof(binary_index_t));
    
    // Initialize header
    index->header.signature = BINARY_INDEX_SIGNATURE;
    index->header.version = BINARY_INDEX_VERSION;
    index->header.entry_count = 0;
    index->header.timestamp = time(NULL);
    strncpy(index->header.branch, branch, sizeof(index->header.branch) - 1);
    
    // Initialize hash table
    index->hash_table_size = HASH_TABLE_SIZE;
    index->hash_table = calloc(HASH_TABLE_SIZE, sizeof(struct index_hash_entry *));
    if (!index->hash_table) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    return FRACTYL_OK;
}

// Free binary index
void binary_index_free(binary_index_t *index) {
    if (!index) return;
    
    // Unmap memory if mapped
    if (index->mmap_addr && index->mmap_addr != MAP_FAILED) {
        munmap(index->mmap_addr, index->mmap_size);
        close(index->mmap_fd);
    }
    
    // Free entries and paths (if not memory-mapped)
    if (!index->mmap_addr) {
        free(index->entries);
        if (index->paths) {
            for (uint32_t i = 0; i < index->header.entry_count; i++) {
                free(index->paths[i]);
            }
            free(index->paths);
        }
    }
    
    // Free hash table
    if (index->hash_table) {
        for (uint32_t i = 0; i < index->hash_table_size; i++) {
            struct index_hash_entry *entry = index->hash_table[i];
            while (entry) {
                struct index_hash_entry *next = entry->next;
                free(entry);
                entry = next;
            }
        }
        free(index->hash_table);
    }
    
    memset(index, 0, sizeof(binary_index_t));
}

// Get index file path
static char *get_index_path(const char *fractyl_dir, const char *branch) {
    if (!fractyl_dir || !branch) return NULL;
    
    char *cache_dir = malloc(strlen(fractyl_dir) + 32);
    if (!cache_dir) return NULL;
    
    snprintf(cache_dir, strlen(fractyl_dir) + 32, "%s/cache", fractyl_dir);
    
    // Ensure cache directory exists
    if (paths_ensure_directory(cache_dir) != FRACTYL_OK) {
        free(cache_dir);
        return NULL;
    }
    
    char *index_path = malloc(strlen(fractyl_dir) + strlen(branch) + 64);
    if (!index_path) {
        free(cache_dir);
        return NULL;
    }
    
    snprintf(index_path, strlen(fractyl_dir) + strlen(branch) + 64, 
             "%s/index_%s.bin", cache_dir, branch);
    
    free(cache_dir);
    return index_path;
}

// Build hash table from loaded entries
static int build_hash_table(binary_index_t *index) {
    // Clear existing hash table
    for (uint32_t i = 0; i < index->hash_table_size; i++) {
        struct index_hash_entry *entry = index->hash_table[i];
        while (entry) {
            struct index_hash_entry *next = entry->next;
            free(entry);
            entry = next;
        }
        index->hash_table[i] = NULL;
    }
    
    // Build new hash table
    for (uint32_t i = 0; i < index->header.entry_count; i++) {
        if (!index->paths[i] || strlen(index->paths[i]) == 0) continue;
        
        uint32_t hash = binary_index_hash_path(index->paths[i]);
        uint32_t bucket = hash & HASH_TABLE_MASK;
        
        struct index_hash_entry *entry = malloc(sizeof(struct index_hash_entry));
        if (!entry) return FRACTYL_ERROR_OUT_OF_MEMORY;
        
        entry->hash = hash;
        entry->index = i;
        entry->next = index->hash_table[bucket];
        index->hash_table[bucket] = entry;
    }
    
    return FRACTYL_OK;
}

// Load binary index from file with memory mapping
int binary_index_load(binary_index_t *index, const char *fractyl_dir, const char *branch) {
    if (!index || !fractyl_dir || !branch) return FRACTYL_ERROR_INVALID_ARGS;
    
    char *index_path = get_index_path(fractyl_dir, branch);
    if (!index_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    // Try to open index file
    int fd = open(index_path, O_RDONLY);
    if (fd < 0) {
        // No index file - initialize empty index
        free(index_path);
        return binary_index_init(index, branch);
    }
    
    // Get file size
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        free(index_path);
        return FRACTYL_ERROR_IO;
    }
    
    // Validate minimum size
    if ((size_t)st.st_size < sizeof(binary_index_header_t)) {
        close(fd);
        free(index_path);
        return binary_index_init(index, branch);
    }
    
    // Initialize index structure
    int result = binary_index_init(index, branch);
    if (result != FRACTYL_OK) {
        close(fd);
        free(index_path);
        return result;
    }
    
    // Read header
    if (read(fd, &index->header, sizeof(binary_index_header_t)) != sizeof(binary_index_header_t)) {
        close(fd);
        free(index_path);
        return binary_index_init(index, branch);
    }
    
    // Validate header
    if (index->header.signature != BINARY_INDEX_SIGNATURE ||
        index->header.version != BINARY_INDEX_VERSION) {
        close(fd);
        free(index_path);
        return binary_index_init(index, branch);
    }
    
    // Read entries if any
    if (index->header.entry_count > 0) {
        // Allocate entries array
        index->entries = malloc(index->header.entry_count * sizeof(binary_index_entry_t));
        if (!index->entries) {
            close(fd);
            free(index_path);
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        
        // Read entries
        size_t entries_size = index->header.entry_count * sizeof(binary_index_entry_t);
        if (read(fd, index->entries, entries_size) != (ssize_t)entries_size) {
            close(fd);
            free(index_path);
            return binary_index_init(index, branch);
        }
        
        // Allocate paths array
        index->paths = malloc(index->header.entry_count * sizeof(char *));
        if (!index->paths) {
            close(fd);
            free(index_path);
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        
        // Read variable-length paths
        for (uint32_t i = 0; i < index->header.entry_count; i++) {
            uint16_t path_len = index->entries[i].path_length;
            
            if (path_len > 0 && path_len < MAX_PATH_LENGTH) {
                index->paths[i] = malloc(path_len + 1);
                if (index->paths[i]) {
                    if (read(fd, index->paths[i], path_len) == path_len) {
                        index->paths[i][path_len] = '\0';
                    } else {
                        free(index->paths[i]);
                        index->paths[i] = NULL;
                    }
                }
            } else {
                index->paths[i] = NULL;
            }
        }
        
        // Build hash table for fast lookups
        build_hash_table(index);
    }
    
    close(fd);
    
    free(index_path);
    return FRACTYL_OK;
}

// Save binary index to file
int binary_index_save(const binary_index_t *index, const char *fractyl_dir) {
    if (!index || !fractyl_dir) return FRACTYL_ERROR_INVALID_ARGS;
    
    char *index_path = get_index_path(fractyl_dir, index->header.branch);
    if (!index_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    FILE *f = fopen(index_path, "wb");
    if (!f) {
        free(index_path);
        return FRACTYL_ERROR_IO;
    }
    
    // Write header
    binary_index_header_t header = index->header;
    header.checksum = 0; // Calculate later
    
    if (fwrite(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        free(index_path);
        return FRACTYL_ERROR_IO;
    }
    
    // Write entries
    if (index->header.entry_count > 0) {
        if (fwrite(index->entries, sizeof(binary_index_entry_t), 
                  index->header.entry_count, f) != index->header.entry_count) {
            fclose(f);
            free(index_path);
            return FRACTYL_ERROR_IO;
        }
        
        // Write variable-length paths
        for (uint32_t i = 0; i < index->header.entry_count; i++) {
            if (index->paths[i] && index->entries[i].path_length > 0) {
                if (fwrite(index->paths[i], index->entries[i].path_length, 1, f) != 1) {
                    fclose(f);
                    free(index_path);
                    return FRACTYL_ERROR_IO;
                }
            }
        }
    }
    
    fclose(f);
    free(index_path);
    return FRACTYL_OK;
}

// Find entry by path using hash table (O(1) lookup)
const binary_index_entry_t *binary_index_find_entry(const binary_index_t *index, 
                                                   const char *path, 
                                                   const char **entry_path) {
    if (!index || !path) return NULL;
    
    uint32_t hash = binary_index_hash_path(path);
    uint32_t bucket = hash & HASH_TABLE_MASK;
    
    struct index_hash_entry *entry = index->hash_table[bucket];
    while (entry) {
        if (entry->hash == hash && entry->index < index->header.entry_count) {
            if (index->paths[entry->index] && 
                strcmp(index->paths[entry->index], path) == 0) {
                if (entry_path) *entry_path = index->paths[entry->index];
                return &index->entries[entry->index];
            }
        }
        entry = entry->next;
    }
    
    return NULL;
}

// Add/update entry in index
int binary_index_update_entry(binary_index_t *index, const char *path, 
                              const struct stat *file_stat, const unsigned char *hash) {
    if (!index || !path || !file_stat || !hash) return FRACTYL_ERROR_INVALID_ARGS;
    
    // Check if entry already exists
    uint32_t hash_val = binary_index_hash_path(path);
    uint32_t bucket = hash_val & HASH_TABLE_MASK;
    
    struct index_hash_entry *hash_entry = index->hash_table[bucket];
    while (hash_entry) {
        if (hash_entry->hash == hash_val && hash_entry->index < index->header.entry_count) {
            if (index->paths[hash_entry->index] && 
                strcmp(index->paths[hash_entry->index], path) == 0) {
                // Update existing entry
                binary_index_entry_t *entry = &index->entries[hash_entry->index];
                entry->mtime_sec = file_stat->st_mtime;
                entry->mtime_nsec = 0; // TODO: Use st_mtim.tv_nsec if available
                entry->ctime_sec = file_stat->st_ctime;
                entry->ctime_nsec = 0;
                entry->size = file_stat->st_size;
                entry->inode = file_stat->st_ino;
                entry->dev = file_stat->st_dev;
                entry->mode = file_stat->st_mode;
                entry->uid = file_stat->st_uid;
                entry->gid = file_stat->st_gid;
                memcpy(entry->hash, hash, 20);
                return FRACTYL_OK;
            }
        }
        hash_entry = hash_entry->next;
    }
    
    // Add new entry - need to resize arrays
    uint32_t new_count = index->header.entry_count + 1;
    
    binary_index_entry_t *new_entries = realloc(index->entries, 
                                                new_count * sizeof(binary_index_entry_t));
    if (!new_entries) return FRACTYL_ERROR_OUT_OF_MEMORY;
    index->entries = new_entries;
    
    char **new_paths = realloc(index->paths, new_count * sizeof(char *));
    if (!new_paths) return FRACTYL_ERROR_OUT_OF_MEMORY;
    index->paths = new_paths;
    
    // Add new entry
    uint32_t entry_idx = index->header.entry_count;
    binary_index_entry_t *entry = &index->entries[entry_idx];
    
    entry->mtime_sec = file_stat->st_mtime;
    entry->mtime_nsec = 0;
    entry->ctime_sec = file_stat->st_ctime;
    entry->ctime_nsec = 0;
    entry->size = file_stat->st_size;
    entry->inode = file_stat->st_ino;
    entry->dev = file_stat->st_dev;
    entry->mode = file_stat->st_mode;
    entry->uid = file_stat->st_uid;
    entry->gid = file_stat->st_gid;
    memcpy(entry->hash, hash, 20);
    entry->path_length = strlen(path);
    entry->flags = 0;
    
    index->paths[entry_idx] = strdup(path);
    if (!index->paths[entry_idx]) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    // Add to hash table
    struct index_hash_entry *new_hash_entry = malloc(sizeof(struct index_hash_entry));
    if (!new_hash_entry) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    new_hash_entry->hash = hash_val;
    new_hash_entry->index = entry_idx;
    new_hash_entry->next = index->hash_table[bucket];
    index->hash_table[bucket] = new_hash_entry;
    
    index->header.entry_count = new_count;
    return FRACTYL_OK;
}

// Remove entry from index
int binary_index_remove_entry(binary_index_t *index, const char *path) {
    if (!index || !path) return FRACTYL_ERROR_INVALID_ARGS;
    
    uint32_t hash_val = binary_index_hash_path(path);
    uint32_t bucket = hash_val & HASH_TABLE_MASK;
    
    // Remove from hash table
    struct index_hash_entry **hash_entry_ptr = &index->hash_table[bucket];
    while (*hash_entry_ptr) {
        struct index_hash_entry *hash_entry = *hash_entry_ptr;
        if (hash_entry->hash == hash_val && hash_entry->index < index->header.entry_count) {
            if (index->paths[hash_entry->index] && 
                strcmp(index->paths[hash_entry->index], path) == 0) {
                
                uint32_t remove_idx = hash_entry->index;
                
                // Remove from hash table
                *hash_entry_ptr = hash_entry->next;
                free(hash_entry);
                
                // Free path
                free(index->paths[remove_idx]);
                
                // Move last entry to removed position
                uint32_t last_idx = index->header.entry_count - 1;
                if (remove_idx != last_idx) {
                    index->entries[remove_idx] = index->entries[last_idx];
                    index->paths[remove_idx] = index->paths[last_idx];
                    
                    // Update hash table for moved entry
                    // TODO: This is complex - for now just rebuild hash table
                    build_hash_table(index);
                }
                
                index->header.entry_count--;
                return FRACTYL_OK;
            }
        }
        hash_entry_ptr = &hash_entry->next;
    }
    
    return FRACTYL_ERROR_NOT_FOUND;
}

// Check file status against index
binary_file_status_t binary_index_check_file(const binary_index_t *index, 
                                            const char *path, 
                                            const struct stat *current_stat) {
    if (!index || !path || !current_stat) return BINARY_FILE_NEW;
    
    const binary_index_entry_t *entry = binary_index_find_entry(index, path, NULL);
    if (!entry) {
        return BINARY_FILE_NEW;
    }
    
    // Compare metadata (same as git's strategy)
    if (entry->mtime_sec != (uint32_t)current_stat->st_mtime ||
        entry->size != (uint64_t)current_stat->st_size ||
        entry->inode != (uint64_t)current_stat->st_ino ||
        entry->mode != current_stat->st_mode) {
        return BINARY_FILE_CHANGED;
    }
    
    return BINARY_FILE_UNCHANGED;
}

// Iterator functions
void binary_index_iterator_init(binary_index_iterator_t *iter, const binary_index_t *index) {
    if (!iter || !index) return;
    iter->index = index;
    iter->current = 0;
}

int binary_index_iterator_next(binary_index_iterator_t *iter, 
                              const char **path, 
                              const binary_index_entry_t **entry) {
    if (!iter || !iter->index || iter->current >= iter->index->header.entry_count) {
        return 0;
    }
    
    if (path) *path = iter->index->paths[iter->current];
    if (entry) *entry = &iter->index->entries[iter->current];
    
    iter->current++;
    return 1;
}