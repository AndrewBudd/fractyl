#ifndef BINARY_INDEX_H
#define BINARY_INDEX_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

// Binary index format (similar to git's index)
// Fixed-size entries for fast access and memory mapping

#define BINARY_INDEX_SIGNATURE 0x46524143  // "FRAC" 
#define BINARY_INDEX_VERSION 1
#define MAX_PATH_LENGTH 1024

// Binary index header (32 bytes)
typedef struct {
    uint32_t signature;       // "FRAC" magic number
    uint32_t version;         // Index format version
    uint32_t entry_count;     // Number of entries
    uint32_t checksum;        // CRC32 of entries
    char branch[16];          // Git branch name (null-terminated)
    uint64_t timestamp;       // Index creation time
} __attribute__((packed)) binary_index_header_t;

// Binary index entry (64 bytes fixed size)
typedef struct {
    uint32_t mtime_sec;       // Modification time (seconds)
    uint32_t mtime_nsec;      // Modification time (nanoseconds)
    uint32_t ctime_sec;       // Change time (seconds) 
    uint32_t ctime_nsec;      // Change time (nanoseconds)
    uint64_t size;            // File size
    uint64_t inode;           // Inode number
    uint32_t dev;             // Device number
    uint32_t mode;            // File mode/permissions
    uint32_t uid;             // User ID
    uint32_t gid;             // Group ID
    unsigned char hash[20];   // SHA-1 hash (like git)
    uint16_t path_length;     // Length of path string
    uint16_t flags;           // Status flags
} __attribute__((packed)) binary_index_entry_t;

// In-memory index with hash table for O(1) lookups
typedef struct {
    binary_index_header_t header;
    binary_index_entry_t *entries;
    char **paths;             // Variable-length paths
    
    // Hash table for fast lookups
    struct index_hash_entry {
        uint32_t hash;        // Hash of path
        uint32_t index;       // Index into entries array
        struct index_hash_entry *next;
    } **hash_table;
    uint32_t hash_table_size;
    
    // Memory mapping info
    void *mmap_addr;
    size_t mmap_size;
    int mmap_fd;
} binary_index_t;

// File change detection results
typedef enum {
    BINARY_FILE_UNCHANGED = 0,
    BINARY_FILE_CHANGED = 1,
    BINARY_FILE_NEW = 2,
    BINARY_FILE_DELETED = 3
} binary_file_status_t;

// Initialize binary index
int binary_index_init(binary_index_t *index, const char *branch);

// Free binary index
void binary_index_free(binary_index_t *index);

// Load binary index from file (with memory mapping)
int binary_index_load(binary_index_t *index, const char *fractyl_dir, const char *branch);

// Save binary index to file
int binary_index_save(const binary_index_t *index, const char *fractyl_dir);

// Add/update entry in index
int binary_index_update_entry(binary_index_t *index, const char *path, const struct stat *file_stat, const unsigned char *hash);

// Find entry by path (O(1) hash table lookup)
const binary_index_entry_t *binary_index_find_entry(const binary_index_t *index, const char *path, const char **entry_path);

// Check file status against index
binary_file_status_t binary_index_check_file(const binary_index_t *index, const char *path, const struct stat *current_stat);

// Remove entry from index
int binary_index_remove_entry(binary_index_t *index, const char *path);

// Get all entries for iteration
typedef struct {
    const binary_index_t *index;
    uint32_t current;
} binary_index_iterator_t;

void binary_index_iterator_init(binary_index_iterator_t *iter, const binary_index_t *index);
int binary_index_iterator_next(binary_index_iterator_t *iter, const char **path, const binary_index_entry_t **entry);

// Utility functions
uint32_t binary_index_hash_path(const char *path);
uint32_t binary_index_crc32(const void *data, size_t len);

#endif /* BINARY_INDEX_H */