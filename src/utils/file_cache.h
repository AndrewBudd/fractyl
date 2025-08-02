#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

// File metadata entry for caching (similar to git's approach)
typedef struct {
    char *path;              // Relative path from repo root
    time_t mtime;           // File modification time
    time_t ctime;           // File change time (metadata changes)
    off_t size;             // File size in bytes
    ino_t ino;              // Inode number
    dev_t dev;              // Device number
    mode_t mode;            // File permissions/type
    uid_t uid;              // File owner
    gid_t gid;              // File group
    char hash[65];          // SHA-256 hash (optional, for content validation)
} file_cache_entry_t;

// File metadata cache
typedef struct {
    file_cache_entry_t *entries;
    size_t count;
    size_t capacity;
    char *branch;           // Git branch name
    time_t cache_timestamp; // When cache was created/updated
} file_cache_t;

// File change detection results
typedef enum {
    FILE_UNCHANGED = 0,     // All metadata matches - file unchanged
    FILE_CHANGED = 1,       // Metadata differs - file changed
    FILE_NEW = 2           // File not in cache - new file
} file_change_result_t;

// Initialize empty file cache
int file_cache_init(file_cache_t *cache, const char *branch);

// Free file cache memory
void file_cache_free(file_cache_t *cache);

// Load cache from disk
int file_cache_load(file_cache_t *cache, const char *fractyl_dir, const char *branch);

// Save cache to disk
int file_cache_save(const file_cache_t *cache, const char *fractyl_dir);

// Add or update file entry in cache
int file_cache_update_entry(file_cache_t *cache, const char *path, const struct stat *file_stat);

// Find file entry in cache
const file_cache_entry_t *file_cache_find_entry(const file_cache_t *cache, const char *path);

// Check if file has changed based on cache
file_change_result_t file_cache_check_file(const file_cache_t *cache, 
                                          const char *path, const struct stat *current_stat);

// Remove file entry from cache
int file_cache_remove_entry(file_cache_t *cache, const char *path);

// Clear entire cache
void file_cache_clear(file_cache_t *cache);

// Check if cache is stale and needs rebuilding
int file_cache_is_stale(const file_cache_t *cache, time_t max_age_seconds);

// Validate cache integrity
int file_cache_validate(const file_cache_t *cache);

// Helper function to populate file_cache_entry_t from stat
void file_cache_entry_from_stat(file_cache_entry_t *entry, const char *path, const struct stat *file_stat);

#endif /* FILE_CACHE_H */