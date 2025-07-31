#ifndef DIRECTORY_CACHE_H
#define DIRECTORY_CACHE_H

#include <time.h>
#include <stddef.h>

// Directory cache entry - tracks mtime and file count for a directory
typedef struct {
    char *path;              // Relative path from repo root
    time_t mtime;           // Directory modification time
    int file_count;         // Number of files in directory (for validation)
} dir_cache_entry_t;

// Directory cache - holds all cached directory information
typedef struct {
    dir_cache_entry_t *entries;
    size_t count;
    size_t capacity;
    time_t cache_timestamp;  // When cache was last updated
    char *branch;           // Git branch this cache belongs to
} directory_cache_t;

// Directory scan result - indicates what needs to be done
typedef enum {
    DIR_UNCHANGED,          // Directory unchanged, skip completely
    DIR_CHANGED,           // Directory changed, needs full traversal
    DIR_NEW,               // New directory, needs full traversal
    DIR_DELETED            // Directory deleted, remove from cache
} dir_scan_result_t;

// API Functions

// Initialize empty directory cache
int dir_cache_init(directory_cache_t *cache, const char *branch);

// Free directory cache memory
void dir_cache_free(directory_cache_t *cache);

// Load cache from disk (.fractyl/cache/directory_mtimes_<branch>.json)
int dir_cache_load(directory_cache_t *cache, const char *fractyl_dir, const char *branch);

// Save cache to disk
int dir_cache_save(const directory_cache_t *cache, const char *fractyl_dir);

// Add or update directory entry in cache
int dir_cache_update_entry(directory_cache_t *cache, const char *path, time_t mtime, int file_count);

// Find directory entry in cache
const dir_cache_entry_t *dir_cache_find_entry(const directory_cache_t *cache, const char *path);

// Check if directory needs scanning based on cache
dir_scan_result_t dir_cache_check_directory(const directory_cache_t *cache, 
                                           const char *path, time_t current_mtime, 
                                           int current_file_count);

// Remove directory entry from cache (for deleted directories)
int dir_cache_remove_entry(directory_cache_t *cache, const char *path);

// Clear entire cache (for cache invalidation)
void dir_cache_clear(directory_cache_t *cache);

// Check if cache is stale and needs rebuilding
int dir_cache_is_stale(const directory_cache_t *cache, time_t max_age_seconds);

// Get cache file path for a branch
char *dir_cache_get_path(const char *fractyl_dir, const char *branch);

// Validate cache integrity
int dir_cache_validate(const directory_cache_t *cache);

#endif // DIRECTORY_CACHE_H