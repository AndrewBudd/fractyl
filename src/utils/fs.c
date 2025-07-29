// fs.c - Implementation of filesystem helpers
#include "fs.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#ifdef _WIN32
    #include <direct.h>
#else
    #include <sys/stat.h>
#endif

bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool mkdir_p(const char *path) {
    if (!path) return false;
    
    // TODO: recursively make directories
    // For now, try to create the directory (works if all parent dirs exist)
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);  // Already exists and is directory
    }
    
    // Simple mkdir - will fail if parent doesn't exist
    #ifdef _WIN32
        return _mkdir(path) == 0;
    #else
        return mkdir(path, 0755) == 0;
    #endif
}

int enumerate_files(const char *root, char ***out_paths, size_t *out_count) {
    // TODO: recursive file listing later
    (void)root; (void)out_paths; (void)out_count;
    return 0;
}
