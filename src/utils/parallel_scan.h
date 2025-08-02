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

// High-performance scanning using binary index and stat-only strategy  
// Phase 1: Lightning-fast stat-only check for all known files using binary index
// Phase 2: Quick new file detection (only when needed)
// Phase 3: Save updated binary index
int scan_directory_binary(const char *root_path, index_t *new_index, 
                          const index_t *prev_index, const char *fractyl_dir,
                          const char *branch);

// Pure stat-only scanning - ultimate Git-style performance
// Only stats files known to binary index, never traverses directories
// Fastest possible scanning method, trades new file detection for speed
int scan_directory_stat_only(const char *root_path, index_t *new_index, 
                             const index_t *prev_index, const char *fractyl_dir,
                             const char *branch);

#endif // PARALLEL_SCAN_H