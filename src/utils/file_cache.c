#include "file_cache.h"
#include "json.h"
#include "paths.h"
#include "fs.h"
#include "../include/fractyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_CJSON
#include <cjson/cJSON.h>
#endif

// Initialize empty file cache
int file_cache_init(file_cache_t *cache, const char *branch) {
    if (!cache) return FRACTYL_ERROR_INVALID_ARGS;
    
    memset(cache, 0, sizeof(file_cache_t));
    cache->capacity = 4096; // Start with room for 4096 files
    cache->entries = calloc(cache->capacity, sizeof(file_cache_entry_t));
    if (!cache->entries) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    cache->branch = branch ? strdup(branch) : strdup("master");
    if (!cache->branch) {
        free(cache->entries);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    cache->cache_timestamp = time(NULL);
    return FRACTYL_OK;
}

// Free file cache memory
void file_cache_free(file_cache_t *cache) {
    if (!cache) return;
    
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].path);
    }
    free(cache->entries);
    free(cache->branch);
    memset(cache, 0, sizeof(file_cache_t));
}

// Get cache file path for a branch
static char *file_cache_get_path(const char *fractyl_dir, const char *branch) {
    if (!fractyl_dir || !branch) return NULL;
    
    char *cache_dir = malloc(strlen(fractyl_dir) + 32);
    if (!cache_dir) return NULL;
    
    snprintf(cache_dir, strlen(fractyl_dir) + 32, "%s/cache", fractyl_dir);
    
    // Ensure cache directory exists
    if (paths_ensure_directory(cache_dir) != FRACTYL_OK) {
        free(cache_dir);
        return NULL;
    }
    
    char *cache_path = malloc(strlen(fractyl_dir) + strlen(branch) + 64);
    if (!cache_path) {
        free(cache_dir);
        return NULL;
    }
    
    snprintf(cache_path, strlen(fractyl_dir) + strlen(branch) + 64, 
             "%s/file_metadata_%s.json", cache_dir, branch);
    
    free(cache_dir);
    return cache_path;
}

// Load cache from disk
int file_cache_load(file_cache_t *cache, const char *fractyl_dir, const char *branch) {
    if (!cache || !fractyl_dir || !branch) return FRACTYL_ERROR_INVALID_ARGS;
    
#ifndef HAVE_CJSON
    // No cJSON support, initialize empty cache
    return file_cache_init(cache, branch);
#else
    
    char *cache_path = file_cache_get_path(fractyl_dir, branch);
    if (!cache_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    FILE *f = fopen(cache_path, "r");
    if (!f) {
        // Cache file doesn't exist, initialize empty cache
        free(cache_path);
        return file_cache_init(cache, branch);
    }
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json_data = malloc(file_size + 1);
    if (!json_data) {
        fclose(f);
        free(cache_path);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    size_t bytes_read = fread(json_data, 1, file_size, f);
    json_data[bytes_read] = '\0';
    fclose(f);
    free(cache_path);
    
    // Parse JSON
    cJSON *root = cJSON_Parse(json_data);
    free(json_data);
    
    if (!root) {
        // Invalid JSON, start fresh
        return file_cache_init(cache, branch);
    }
    
    // Initialize cache
    int result = file_cache_init(cache, branch);
    if (result != FRACTYL_OK) {
        cJSON_Delete(root);
        return result;
    }
    
    // Read cache timestamp
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsNumber(timestamp)) {
        cache->cache_timestamp = (time_t)timestamp->valueint;
    }
    
    // Read files
    cJSON *files = cJSON_GetObjectItem(root, "files");
    if (files && cJSON_IsObject(files)) {
        cJSON *file_entry = NULL;
        cJSON_ArrayForEach(file_entry, files) {
            const char *path = file_entry->string;
            if (!path) continue;
            
            cJSON *mtime_json = cJSON_GetObjectItem(file_entry, "mtime");
            cJSON *ctime_json = cJSON_GetObjectItem(file_entry, "ctime");
            cJSON *size_json = cJSON_GetObjectItem(file_entry, "size");
            cJSON *ino_json = cJSON_GetObjectItem(file_entry, "ino");
            cJSON *dev_json = cJSON_GetObjectItem(file_entry, "dev");
            cJSON *mode_json = cJSON_GetObjectItem(file_entry, "mode");
            cJSON *uid_json = cJSON_GetObjectItem(file_entry, "uid");
            cJSON *gid_json = cJSON_GetObjectItem(file_entry, "gid");
            
            if (mtime_json && cJSON_IsNumber(mtime_json) &&
                size_json && cJSON_IsNumber(size_json) &&
                ino_json && cJSON_IsNumber(ino_json)) {
                
                // Add new entry - check if we need to resize
                if (cache->count >= cache->capacity) {
                    size_t new_capacity = cache->capacity * 2;
                    file_cache_entry_t *new_entries = realloc(cache->entries, 
                                                             new_capacity * sizeof(file_cache_entry_t));
                    if (!new_entries) continue; // Skip this entry
                    cache->entries = new_entries;
                    cache->capacity = new_capacity;
                    
                    // Zero out new memory
                    memset(&cache->entries[cache->count], 0, 
                           (new_capacity - cache->count) * sizeof(file_cache_entry_t));
                }
                
                file_cache_entry_t *entry = &cache->entries[cache->count];
                entry->path = strdup(path);
                if (!entry->path) continue; // Skip this entry
                
                entry->mtime = (time_t)mtime_json->valueint;
                entry->ctime = ctime_json && cJSON_IsNumber(ctime_json) ? 
                              (time_t)ctime_json->valueint : entry->mtime;
                entry->size = (off_t)size_json->valueint;
                entry->ino = (ino_t)ino_json->valueint;
                entry->dev = dev_json && cJSON_IsNumber(dev_json) ? 
                            (dev_t)dev_json->valueint : 0;
                entry->mode = mode_json && cJSON_IsNumber(mode_json) ? 
                             (mode_t)mode_json->valueint : S_IFREG;
                entry->uid = uid_json && cJSON_IsNumber(uid_json) ? 
                            (uid_t)uid_json->valueint : 0;
                entry->gid = gid_json && cJSON_IsNumber(gid_json) ? 
                            (gid_t)gid_json->valueint : 0;
                
                cache->count++;
            }
        }
    }
    
    cJSON_Delete(root);
    return FRACTYL_OK;
#endif
}

// Save cache to disk
int file_cache_save(const file_cache_t *cache, const char *fractyl_dir) {
    if (!cache || !fractyl_dir) return FRACTYL_ERROR_INVALID_ARGS;
    
#ifndef HAVE_CJSON
    // No cJSON support, skip saving
    return FRACTYL_OK;
#else
    
    char *cache_path = file_cache_get_path(fractyl_dir, cache->branch);
    if (!cache_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    // Create JSON structure
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(cache_path);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    cJSON_AddNumberToObject(root, "timestamp", (double)cache->cache_timestamp);
    cJSON_AddStringToObject(root, "branch", cache->branch);
    cJSON_AddNumberToObject(root, "file_count", (double)cache->count);
    
    cJSON *files = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "files", files);
    
    // Add all file entries
    for (size_t i = 0; i < cache->count; i++) {
        const file_cache_entry_t *entry = &cache->entries[i];
        
        cJSON *file_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(file_obj, "mtime", (double)entry->mtime);
        cJSON_AddNumberToObject(file_obj, "ctime", (double)entry->ctime);
        cJSON_AddNumberToObject(file_obj, "size", (double)entry->size);
        cJSON_AddNumberToObject(file_obj, "ino", (double)entry->ino);
        cJSON_AddNumberToObject(file_obj, "dev", (double)entry->dev);
        cJSON_AddNumberToObject(file_obj, "mode", (double)entry->mode);
        cJSON_AddNumberToObject(file_obj, "uid", (double)entry->uid);
        cJSON_AddNumberToObject(file_obj, "gid", (double)entry->gid);
        
        cJSON_AddItemToObject(files, entry->path, file_obj);
    }
    
    // Write JSON to file
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        free(cache_path);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    FILE *f = fopen(cache_path, "w");
    if (!f) {
        free(json_string);
        free(cache_path);
        return FRACTYL_ERROR_IO;
    }
    
    size_t len = strlen(json_string);
    size_t written = fwrite(json_string, 1, len, f);
    fclose(f);
    
    free(json_string);
    free(cache_path);
    
    return (written == len) ? FRACTYL_OK : FRACTYL_ERROR_IO;
#endif
}

// Helper function to populate file_cache_entry_t from stat
void file_cache_entry_from_stat(file_cache_entry_t *entry, const char *path, const struct stat *file_stat) {
    if (!entry || !path || !file_stat) return;
    
    entry->path = strdup(path);
    entry->mtime = file_stat->st_mtime;
    entry->ctime = file_stat->st_ctime;
    entry->size = file_stat->st_size;
    entry->ino = file_stat->st_ino;
    entry->dev = file_stat->st_dev;
    entry->mode = file_stat->st_mode;
    entry->uid = file_stat->st_uid;
    entry->gid = file_stat->st_gid;
    entry->hash[0] = '\0'; // No hash initially
}

// Add or update file entry in cache
int file_cache_update_entry(file_cache_t *cache, const char *path, const struct stat *file_stat) {
    if (!cache || !path || !file_stat) return FRACTYL_ERROR_INVALID_ARGS;
    
    // Look for existing entry
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) {
            // Update existing entry
            cache->entries[i].mtime = file_stat->st_mtime;
            cache->entries[i].ctime = file_stat->st_ctime;
            cache->entries[i].size = file_stat->st_size;
            cache->entries[i].ino = file_stat->st_ino;
            cache->entries[i].dev = file_stat->st_dev;
            cache->entries[i].mode = file_stat->st_mode;
            cache->entries[i].uid = file_stat->st_uid;
            cache->entries[i].gid = file_stat->st_gid;
            return FRACTYL_OK;
        }
    }
    
    // Add new entry - check if we need to resize
    if (cache->count >= cache->capacity) {
        size_t new_capacity = cache->capacity * 2;
        file_cache_entry_t *new_entries = realloc(cache->entries, 
                                                 new_capacity * sizeof(file_cache_entry_t));
        if (!new_entries) {
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        cache->entries = new_entries;
        cache->capacity = new_capacity;
        
        // Zero out new memory
        memset(&cache->entries[cache->count], 0, 
               (new_capacity - cache->count) * sizeof(file_cache_entry_t));
    }
    
    // Add new entry
    file_cache_entry_t *entry = &cache->entries[cache->count];
    file_cache_entry_from_stat(entry, path, file_stat);
    if (!entry->path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    cache->count++;
    
    return FRACTYL_OK;
}

// Find file entry in cache
const file_cache_entry_t *file_cache_find_entry(const file_cache_t *cache, const char *path) {
    if (!cache || !path) return NULL;
    
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) {
            return &cache->entries[i];
        }
    }
    
    return NULL;
}

// Check if file has changed based on cache
file_change_result_t file_cache_check_file(const file_cache_t *cache, 
                                          const char *path, const struct stat *current_stat) {
    if (!cache || !path || !current_stat) return FILE_NEW;
    
    const file_cache_entry_t *entry = file_cache_find_entry(cache, path);
    if (!entry) {
        return FILE_NEW;
    }
    
    // Compare key metadata (like git does)
    if (entry->mtime != current_stat->st_mtime ||
        entry->size != current_stat->st_size ||
        entry->ino != current_stat->st_ino ||
        entry->dev != current_stat->st_dev ||
        entry->mode != current_stat->st_mode ||
        entry->ctime != current_stat->st_ctime) {
        return FILE_CHANGED;
    }
    
    return FILE_UNCHANGED;
}

// Remove file entry from cache
int file_cache_remove_entry(file_cache_t *cache, const char *path) {
    if (!cache || !path) return FRACTYL_ERROR_INVALID_ARGS;
    
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) {
            // Free the path string
            free(cache->entries[i].path);
            
            // Move remaining entries down
            for (size_t j = i; j < cache->count - 1; j++) {
                cache->entries[j] = cache->entries[j + 1];
            }
            cache->count--;
            
            // Clear the last entry
            memset(&cache->entries[cache->count], 0, sizeof(file_cache_entry_t));
            return FRACTYL_OK;
        }
    }
    
    return FRACTYL_ERROR_NOT_FOUND;
}

// Clear entire cache
void file_cache_clear(file_cache_t *cache) {
    if (!cache) return;
    
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].path);
    }
    cache->count = 0;
    cache->cache_timestamp = time(NULL);
}

// Check if cache is stale and needs rebuilding
int file_cache_is_stale(const file_cache_t *cache, time_t max_age_seconds) {
    if (!cache) return 1;
    
    time_t now = time(NULL);
    return (now - cache->cache_timestamp) > max_age_seconds;
}

// Validate cache integrity
int file_cache_validate(const file_cache_t *cache) {
    if (!cache) return FRACTYL_ERROR_INVALID_ARGS;
    if (!cache->entries && cache->count > 0) return FRACTYL_ERROR_INVALID_STATE;
    if (!cache->branch) return FRACTYL_ERROR_INVALID_STATE;
    if (cache->count > cache->capacity) return FRACTYL_ERROR_INVALID_STATE;
    
    // Check for duplicate paths
    for (size_t i = 0; i < cache->count; i++) {
        if (!cache->entries[i].path) return FRACTYL_ERROR_INVALID_STATE;
        
        for (size_t j = i + 1; j < cache->count; j++) {
            if (strcmp(cache->entries[i].path, cache->entries[j].path) == 0) {
                return FRACTYL_ERROR_INVALID_STATE;
            }
        }
    }
    
    return FRACTYL_OK;
}