#ifndef PARALLEL_SCAN_H
#define PARALLEL_SCAN_H

#include "../include/core.h"

// Scan directory tree in parallel using multiple threads
// Returns FRACTYL_OK on success
int scan_directory_parallel(const char *root_path, index_t *new_index, 
                           const index_t *prev_index, const char *fractyl_dir);

// Optimized scan using directory cache - two-phase approach
// Phase 1: Fast file-only stat() check for known files
// Phase 2: Selective directory traversal for changed directories only
int scan_directory_cached(const char *root_path, index_t *new_index, 
                         const index_t *prev_index, const char *fractyl_dir,
                         const char *branch);

#endif // PARALLEL_SCAN_H