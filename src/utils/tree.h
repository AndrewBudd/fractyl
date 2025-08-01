#ifndef TREE_H
#define TREE_H

#include "../core/index.h"
#include "directory_cache.h"

// Build git-style tree objects from the index. Updates the directory cache
// with a tree hash for every directory and returns the hash of the root tree.
int build_trees_from_index(const index_t *index, const char *fractyl_dir,
                           directory_cache_t *cache, unsigned char *root_hash);

#endif // TREE_H

