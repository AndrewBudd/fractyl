#include "objects.h"
#include "hash.h"
#include "../utils/fs.h"
#include "../include/fractyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static char* hash_to_object_path(const unsigned char *hash, const char *fractyl_dir) {
    if (!hash || !fractyl_dir) return NULL;
    
    char hash_hex[FRACTYL_HASH_HEX_SIZE];
    hash_to_string(hash, hash_hex);
    
    // Create path: .fractyl/objects/XX/XXXX...
    // Need space for: fractyl_dir + "/objects/" + "XX/" + hash_hex + null terminator
    size_t path_len = strlen(fractyl_dir) + 10 + 3 + strlen(hash_hex) + 1;
    char *path = malloc(path_len);
    if (!path) return NULL;
    
    sprintf(path, "%s/objects/%.2s/%s", fractyl_dir, hash_hex, hash_hex + 2);
    return path;
}

static int ensure_object_dir(const unsigned char *hash, const char *fractyl_dir) {
    if (!hash || !fractyl_dir) return FRACTYL_ERROR_GENERIC;
    
    char hash_hex[FRACTYL_HASH_HEX_SIZE];
    hash_to_string(hash, hash_hex);
    
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s/objects/%.2s", fractyl_dir, hash_hex);
    
    // Create directory recursively
    struct stat st;
    if (stat(dir_path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? FRACTYL_OK : FRACTYL_ERROR_IO;
    }
    
    // Create objects directory first
    char objects_dir[512];
    snprintf(objects_dir, sizeof(objects_dir), "%s/objects", fractyl_dir);
    if (stat(objects_dir, &st) != 0) {
        if (mkdir(objects_dir, 0755) != 0) {
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Create subdirectory
    if (mkdir(dir_path, 0755) != 0) {
        return FRACTYL_ERROR_IO;
    }
    
    return FRACTYL_OK;
}

int object_store_file(const char *file_path, const char *fractyl_dir, unsigned char *hash_out) {
    if (!file_path || !fractyl_dir || !hash_out) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // First compute the hash
    int result = hash_file(file_path, hash_out);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Check if object already exists
    if (object_exists(hash_out, fractyl_dir)) {
        return FRACTYL_OK; // Already stored
    }
    
    // Ensure object directory exists
    result = ensure_object_dir(hash_out, fractyl_dir);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Get destination path
    char *dest_path = hash_to_object_path(hash_out, fractyl_dir);
    if (!dest_path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    // Copy file to object store
    FILE *src = fopen(file_path, "rb");
    if (!src) {
        free(dest_path);
        return FRACTYL_ERROR_IO;
    }
    
    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(src);
        free(dest_path);
        return FRACTYL_ERROR_IO;
    }
    
    char buffer[8192];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dest) != bytes_read) {
            fclose(src);
            fclose(dest);
            unlink(dest_path); // Clean up partial file
            free(dest_path);
            return FRACTYL_ERROR_IO;
        }
    }
    
    fclose(src);
    fclose(dest);
    free(dest_path);
    
    return FRACTYL_OK;
}

int object_store_data(const void *data, size_t size, const char *fractyl_dir, unsigned char *hash_out) {
    if (!data || !fractyl_dir || !hash_out) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // Compute hash
    int result = hash_data(data, size, hash_out);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Check if object already exists
    if (object_exists(hash_out, fractyl_dir)) {
        return FRACTYL_OK; // Already stored
    }
    
    // Ensure object directory exists
    result = ensure_object_dir(hash_out, fractyl_dir);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Get destination path
    char *dest_path = hash_to_object_path(hash_out, fractyl_dir);
    if (!dest_path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    // Write data to object store
    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        free(dest_path);
        return FRACTYL_ERROR_IO;
    }
    
    if (fwrite(data, 1, size, dest) != size) {
        fclose(dest);
        unlink(dest_path); // Clean up partial file
        free(dest_path);
        return FRACTYL_ERROR_IO;
    }
    
    fclose(dest);
    free(dest_path);
    
    return FRACTYL_OK;
}

int object_load(const unsigned char *hash, const char *fractyl_dir, void **data_out, size_t *size_out) {
    if (!hash || !fractyl_dir || !data_out || !size_out) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    char *obj_path = hash_to_object_path(hash, fractyl_dir);
    if (!obj_path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    FILE *fp = fopen(obj_path, "rb");
    if (!fp) {
        free(obj_path);
        return FRACTYL_ERROR_IO;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(fp);
        free(obj_path);
        return FRACTYL_ERROR_IO;
    }
    
    // Allocate buffer
    void *data = malloc(file_size);
    if (!data) {
        fclose(fp);
        free(obj_path);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    // Read data
    size_t bytes_read = fread(data, 1, file_size, fp);
    fclose(fp);
    free(obj_path);
    
    if (bytes_read != (size_t)file_size) {
        free(data);
        return FRACTYL_ERROR_IO;
    }
    
    *data_out = data;
    *size_out = file_size;
    
    return FRACTYL_OK;
}

int object_exists(const unsigned char *hash, const char *fractyl_dir) {
    if (!hash || !fractyl_dir) return 0;
    
    char *obj_path = hash_to_object_path(hash, fractyl_dir);
    if (!obj_path) return 0;
    
    struct stat st;
    int exists = (stat(obj_path, &st) == 0 && S_ISREG(st.st_mode));
    
    free(obj_path);
    return exists;
}

char* object_path(const unsigned char *hash, const char *fractyl_dir) {
    return hash_to_object_path(hash, fractyl_dir);
}

int object_restore_file(const unsigned char *hash, const char *fractyl_dir, const char *dest_path) {
    if (!hash || !fractyl_dir || !dest_path) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    char *obj_path = hash_to_object_path(hash, fractyl_dir);
    if (!obj_path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    FILE *src = fopen(obj_path, "rb");
    if (!src) {
        free(obj_path);
        return FRACTYL_ERROR_IO;
    }
    
    FILE *dest = fopen(dest_path, "wb");
    if (!dest) {
        fclose(src);
        free(obj_path);
        return FRACTYL_ERROR_IO;
    }
    
    char buffer[8192];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dest) != bytes_read) {
            fclose(src);
            fclose(dest);
            unlink(dest_path); // Clean up partial file
            free(obj_path);
            return FRACTYL_ERROR_IO;
        }
    }
    
    fclose(src);
    fclose(dest);
    free(obj_path);
    
    return FRACTYL_OK;
}

int object_storage_init(const char *fractyl_dir) {
    if (!fractyl_dir) return FRACTYL_ERROR_GENERIC;
    
    char objects_dir[512];
    snprintf(objects_dir, sizeof(objects_dir), "%s/objects", fractyl_dir);
    
    struct stat st;
    if (stat(objects_dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? FRACTYL_OK : FRACTYL_ERROR_IO;
    }
    
    // Create fractyl directory first
    if (stat(fractyl_dir, &st) != 0) {
        if (mkdir(fractyl_dir, 0755) != 0) {
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Create objects directory
    if (mkdir(objects_dir, 0755) != 0) {
        return FRACTYL_ERROR_IO;
    }
    
    return FRACTYL_OK;
}

int object_gc(const char *fractyl_dir, const char **keep_hashes, size_t keep_count) {
    // TODO: Implement garbage collection
    // For now, just return success
    (void)fractyl_dir;
    (void)keep_hashes;
    (void)keep_count;
    return FRACTYL_OK;
}