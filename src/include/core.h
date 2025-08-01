// core.h - Core public types
#ifndef CORE_H
#define CORE_H

#include <sys/types.h>
#include <time.h>

typedef struct {
    char *path;
    unsigned char hash[32];
    mode_t mode;
    off_t size;
    time_t mtime;
} index_entry_t;

typedef struct {
    index_entry_t *entries;
    size_t count;
    size_t capacity;
} index_t;

typedef struct {
    char id[64];
    char *parent;
    char *description;
    time_t timestamp;
    unsigned char index_hash[32];
    unsigned char tree_hash[32];
    char **git_status;
    size_t git_status_count;
    // Git-related fields
    char *git_branch;
    char *git_commit;
    int git_dirty;  // 1 if there are uncommitted changes, 0 otherwise
} snapshot_t;

typedef struct {
    char *repo_root;
    char *fractyl_dir;
    int hash_algorithm;
    size_t max_file_size;
} config_t;

#endif // CORE_H
