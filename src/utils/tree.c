#include "tree.h"
#include "../core/hash.h"
#include "../core/objects.h"
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>

typedef struct {
    char *name;
    mode_t mode;
    int is_dir;
    unsigned char hash[32];
} tree_entry_t;

typedef struct {
    char *path;             // directory path relative to root
    int depth;              // depth for sorting
    tree_entry_t *entries;
    size_t count;
    size_t capacity;
    unsigned char hash[32];
} tree_builder_t;

static tree_builder_t *find_dir(tree_builder_t *dirs, size_t count, const char *path) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(dirs[i].path, path) == 0) return &dirs[i];
    }
    return NULL;
}

static tree_builder_t *add_dir(tree_builder_t **dirs, size_t *count, size_t *cap, const char *path) {
    tree_builder_t *existing = find_dir(*dirs, *count, path);
    if (existing) return existing;

    if (*count >= *cap) {
        size_t newcap = *cap ? *cap * 2 : 32;
        tree_builder_t *tmp = realloc(*dirs, newcap * sizeof(tree_builder_t));
        if (!tmp) return NULL;
        *dirs = tmp;
        *cap = newcap;
    }

    tree_builder_t *b = &(*dirs)[(*count)++];
    b->path = strdup(path);
    if (!b->path) return NULL;
    b->depth = 0;
    b->entries = NULL;
    b->count = 0;
    b->capacity = 0;
    memset(b->hash, 0, 32);
    return b;
}

static void add_entry(tree_builder_t *dir, const char *name, mode_t mode, int is_dir, const unsigned char *hash) {
    if (!dir) return;

    for (size_t i = 0; i < dir->count; i++) {
        if (strcmp(dir->entries[i].name, name) == 0 && dir->entries[i].is_dir == is_dir) {
            // entry already exists
            return;
        }
    }

    if (dir->count >= dir->capacity) {
        size_t newcap = dir->capacity ? dir->capacity * 2 : 8;
        tree_entry_t *tmp = realloc(dir->entries, newcap * sizeof(tree_entry_t));
        if (!tmp) return;
        dir->entries = tmp;
        dir->capacity = newcap;
    }

    tree_entry_t *e = &dir->entries[dir->count++];
    e->name = strdup(name);
    e->mode = mode;
    e->is_dir = is_dir;
    if (hash) memcpy(e->hash, hash, 32); else memset(e->hash, 0, 32);
}

static int depth_of(const char *path) {
    int d = 0; for (const char *p = path; *p; p++) if (*p == '/') d++; return d;
}

static int cmp_depth_desc(const void *a, const void *b) {
    const tree_builder_t *da = a, *db = b;
    return db->depth - da->depth;
}

static int cmp_entry_name(const void *a, const void *b) {
    const tree_entry_t *ea = a, *eb = b;
    return strcmp(ea->name, eb->name);
}

int build_trees_from_index(const index_t *index, const char *fractyl_dir,
                           directory_cache_t *cache, unsigned char *root_hash) {
    if (!index || !fractyl_dir || !cache || !root_hash) return FRACTYL_ERROR_INVALID_ARGS;

    tree_builder_t *dirs = NULL;
    size_t dir_count = 0, dir_cap = 0;

    tree_builder_t *root = add_dir(&dirs, &dir_count, &dir_cap, "");
    if (!root) return FRACTYL_ERROR_OUT_OF_MEMORY;

    // Collect directories and add file entries
    for (size_t i = 0; i < index->count; i++) {
        const index_entry_t *ent = &index->entries[i];

        char *path_copy = strdup(ent->path);
        if (!path_copy) return FRACTYL_ERROR_OUT_OF_MEMORY;
        char *dir_path = dirname(path_copy);
        if (strcmp(dir_path, ".") == 0) dir_path = "";
        tree_builder_t *dir = add_dir(&dirs, &dir_count, &dir_cap, dir_path);
        if (!dir) { free(path_copy); return FRACTYL_ERROR_OUT_OF_MEMORY; }

        char *base = basename(ent->path);
        add_entry(dir, base, ent->mode, 0, ent->hash);

        // ensure parent dirs exist
        char *p = strdup(ent->path);
        for (char *slash = strchr(p, '/'); slash; slash = strchr(slash + 1, '/')) {
            *slash = '\0';
            add_dir(&dirs, &dir_count, &dir_cap, p);
            *slash = '/';
        }
        free(p);
        free(path_copy);
    }

    // Add directory entries to parents
    for (size_t i = 0; i < dir_count; i++) {
        if (dirs[i].path[0] == '\0') continue; // skip root
        char *parent_path = strdup(dirs[i].path);
        char *base = strrchr(parent_path, '/');
        tree_builder_t *parent;
        char *name;
        if (base) {
            *base = '\0';
            parent = add_dir(&dirs, &dir_count, &dir_cap, parent_path);
            name = base + 1;
        } else {
            parent = root;
            name = parent_path;
        }
        add_entry(parent, name, 040000, 1, NULL);
        free(parent_path);
    }

    // compute depth for sorting
    for (size_t i = 0; i < dir_count; i++) dirs[i].depth = depth_of(dirs[i].path);
    qsort(dirs, dir_count, sizeof(tree_builder_t), cmp_depth_desc);

    // Build trees bottom-up
    for (size_t di = 0; di < dir_count; di++) {
        tree_builder_t *dir = &dirs[di];
        if (dir->count > 1) qsort(dir->entries, dir->count, sizeof(tree_entry_t), cmp_entry_name);

        // Fill in hashes for directory entries
        for (size_t ei = 0; ei < dir->count; ei++) {
            tree_entry_t *e = &dir->entries[ei];
            if (e->is_dir) {
                char child_path[512];
                if (dir->path[0]) snprintf(child_path, sizeof(child_path), "%s/%s", dir->path, e->name);
                else snprintf(child_path, sizeof(child_path), "%s", e->name);
                tree_builder_t *child = find_dir(dirs, dir_count, child_path);
                if (child) memcpy(e->hash, child->hash, 32);
            }
        }

        // Build binary representation
        size_t buf_len = 0, buf_cap = 128;
        unsigned char *buf = malloc(buf_cap);
        if (!buf) return FRACTYL_ERROR_OUT_OF_MEMORY;

        for (size_t ei = 0; ei < dir->count; ei++) {
            tree_entry_t *e = &dir->entries[ei];
            char mode_str[8];
            sprintf(mode_str, "%o", e->is_dir ? 040000 : e->mode);
            size_t need = strlen(mode_str) + 1 + strlen(e->name) + 1 + 32;
            if (buf_len + need > buf_cap) {
                buf_cap = buf_cap * 2 + need;
                buf = realloc(buf, buf_cap);
                if (!buf) return FRACTYL_ERROR_OUT_OF_MEMORY;
            }
            memcpy(buf + buf_len, mode_str, strlen(mode_str));
            buf_len += strlen(mode_str);
            buf[buf_len++] = ' ';
            memcpy(buf + buf_len, e->name, strlen(e->name));
            buf_len += strlen(e->name);
            buf[buf_len++] = '\0';
            memcpy(buf + buf_len, e->hash, 32);
            buf_len += 32;
        }

        int res = object_store_data(buf, buf_len, fractyl_dir, dir->hash);
        free(buf);
        if (res != FRACTYL_OK) return res;

        // Update cache
        const dir_cache_entry_t *existing = dir_cache_find_entry(cache, dir->path);
        time_t mtime = existing ? existing->mtime : time(NULL);
        int file_count = 0;
        for (size_t ei = 0; ei < dir->count; ei++) if (!dir->entries[ei].is_dir) file_count++;
        dir_cache_update_entry(cache, dir->path, mtime, file_count, dir->hash);
    }

    if (dir_count > 0) memcpy(root_hash, dirs[dir_count - 1].hash, 32);

    // cleanup
    for (size_t i = 0; i < dir_count; i++) {
        for (size_t j = 0; j < dirs[i].count; j++) free(dirs[i].entries[j].name);
        free(dirs[i].entries);
        free(dirs[i].path);
    }
    free(dirs);

    return FRACTYL_OK;
}

