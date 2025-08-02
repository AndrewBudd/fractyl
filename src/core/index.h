#ifndef INDEX_H
#define INDEX_H

#include "../include/core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Index management API ---

// Initialize empty index
int index_init(index_t *index);
// Load index from disk
int index_load(index_t *index, const char *path);
// Save index to disk
int index_save(const index_t *index, const char *path);
// Add/update index entry
int index_add_entry(index_t *index, const index_entry_t *entry);
// Fast direct append - assumes caller has verified no duplicates exist
int index_add_entry_direct(index_t *index, const index_entry_t *entry);
// Remove index entry
int index_remove_entry(index_t *index, const char *path);
// Find entry by path
const index_entry_t* index_find_entry(const index_t *index, const char *path);
// Check if working dir differs from index
int index_has_changes(const index_t *index, const char *workdir, int *has_changes);
// Free index struct
void index_free(index_t *index);
// Print index for debugging
void index_print(const index_t *index);

#ifdef __cplusplus
}
#endif

#endif // INDEX_H
