#ifndef GIT_COMPAT_H
#define GIT_COMPAT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

// Simple wrappers for Git's memory functions
static inline void *xmalloc(size_t size) {
    void *ret = malloc(size);
    if (!ret && size) {
        fprintf(stderr, "Out of memory, malloc failed\n");
        exit(1);
    }
    return ret;
}

static inline void *xcalloc(size_t nmemb, size_t size) {
    void *ret = calloc(nmemb, size);
    if (!ret && nmemb && size) {
        fprintf(stderr, "Out of memory, calloc failed\n");
        exit(1);
    }
    return ret;
}

static inline void *xrealloc(void *ptr, size_t size) {
    void *ret = realloc(ptr, size);
    if (!ret && size) {
        fprintf(stderr, "Out of memory, realloc failed\n");
        exit(1);
    }
    return ret;
}

// Git utility functions
static inline int signed_add_overflows(long a, long b) {
    return (b > 0 && a > LONG_MAX - b) || (b < 0 && a < LONG_MIN - b);
}

// Git's BUG() macro
#define BUG(fmt, ...) do { \
    fprintf(stderr, "BUG: " fmt "\n", ##__VA_ARGS__); \
    exit(1); \
} while (0)

// UNUSED macro
#define UNUSED __attribute__((unused))

// regexec_buf - simplified version
static inline int regexec_buf(const regex_t *preg, const char *buf, size_t size, 
                             size_t nmatch, regmatch_t pmatch[], int eflags) {
    // For simplicity, create a null-terminated copy
    char *str = malloc(size + 1);
    if (!str) return REG_ESPACE;
    memcpy(str, buf, size);
    str[size] = '\0';
    int ret = regexec(preg, str, nmatch, pmatch, eflags);
    free(str);
    return ret;
}

static inline unsigned long cast_size_t_to_ulong(size_t a) {
    if (a > ULONG_MAX) {
        fprintf(stderr, "size_t overflow converting to unsigned long\n");
        exit(1);
    }
    return (unsigned long)a;
}

static inline size_t st_add(size_t a, size_t b) {
    if (SIZE_MAX - a < b) {
        fprintf(stderr, "size_t overflow in addition\n");
        exit(1);
    }
    return a + b;
}

static inline size_t st_mult(size_t a, size_t b) {
    if (a && SIZE_MAX / a < b) {
        fprintf(stderr, "size_t overflow in multiplication\n");
        exit(1);
    }
    return a * b;
}

#endif // GIT_COMPAT_H