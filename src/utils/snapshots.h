#ifndef SNAPSHOTS_H
#define SNAPSHOTS_H

#include "../include/fractyl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resolve any snapshot identifier (full ID, prefix, or relative like -1, -2) to full ID
// Returns FRACTYL_OK on success, error code on failure
// result_id must be at least 65 chars (64 + null terminator)
int resolve_snapshot_id(const char *input, const char *fractyl_dir, const char *branch, char *result_id);

// Get chronologically ordered snapshots for a branch (newest first)
// Caller must free returned array and each string in it
char** get_chronological_snapshots(const char *fractyl_dir, const char *branch, size_t *count);

#ifdef __cplusplus
}
#endif

#endif // SNAPSHOTS_H