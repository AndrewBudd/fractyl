#ifndef OBJECTS_H
#define OBJECTS_H

#include "../include/fractyl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Store file content by hash in .fractyl/objects/
int object_store_file(const char *file_path, const char *fractyl_dir, unsigned char *hash_out);

// Store data buffer by hash in .fractyl/objects/
int object_store_data(const void *data, size_t size, const char *fractyl_dir, unsigned char *hash_out);

// Load object content by hash (caller must free returned buffer)
int object_load(const unsigned char *hash, const char *fractyl_dir, void **data_out, size_t *size_out);

// Check if object exists by hash
int object_exists(const unsigned char *hash, const char *fractyl_dir);

// Get full path for hash (caller must free)
char* object_path(const unsigned char *hash, const char *fractyl_dir);

// Restore file from object storage
int object_restore_file(const unsigned char *hash, const char *fractyl_dir, const char *dest_path);

// Initialize object storage directory structure
int object_storage_init(const char *fractyl_dir);

// Garbage collect unreferenced objects (future enhancement)
int object_gc(const char *fractyl_dir, const char **keep_hashes, size_t keep_count);

#ifdef __cplusplus
}
#endif

#endif // OBJECTS_H