#ifndef FRACTYL_DIFF_H
#define FRACTYL_DIFF_H

#include "xdiff.h"

#ifdef __cplusplus
extern "C" {
#endif

// Simple interface for fractyl to perform unified diffs
int fractyl_diff_unified(const char *path_a, const char *data_a, size_t size_a,
                        const char *path_b, const char *data_b, size_t size_b,
                        int context_lines);

#ifdef __cplusplus
}
#endif

#endif // FRACTYL_DIFF_H