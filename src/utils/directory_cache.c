#include "directory_cache.h"
#include "json.h"
#include "paths.h"
#include "fs.h"
#include "../include/fractyl.h"
#include "../core/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_CJSON
#include <cjson/cJSON.h>
#endif

// Initialize empty directory cache
int dir_cache_init(directory_cache_t *cache, const char *branch) {
    if (!cache) return FRACTYL_ERROR_INVALID_ARGS;
    
    memset(cache, 0, sizeof(directory_cache_t));
    cache->capacity = 1024; // Start with room for 1024 directories
    cache->entries = calloc(cache->capacity, sizeof(dir_cache_entry_t));
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

// Free directory cache memory
void dir_cache_free(directory_cache_t *cache) {
    if (!cache) return;
    
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].path);
    }
    free(cache->entries);
    free(cache->branch);
    memset(cache, 0, sizeof(directory_cache_t));
}

// Get cache file path for a branch
char *dir_cache_get_path(const char *fractyl_dir, const char *branch) {
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
             "%s/directory_mtimes_%s.json", cache_dir, branch);
    
    free(cache_dir);
    return cache_path;
}

// Load cache from disk
int dir_cache_load(directory_cache_t *cache, const char *fractyl_dir, const char *branch) {
    if (!cache || !fractyl_dir || !branch) return FRACTYL_ERROR_INVALID_ARGS;
    
#ifndef HAVE_CJSON
    // No cJSON support, initialize empty cache
    return dir_cache_init(cache, branch);
#else
    
    char *cache_path = dir_cache_get_path(fractyl_dir, branch);
    if (!cache_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    FILE *f = fopen(cache_path, "r");
    if (!f) {
        // Cache file doesn't exist, initialize empty cache
        free(cache_path);
        return dir_cache_init(cache, branch);
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
        return dir_cache_init(cache, branch);
    }
    
    // Initialize cache
    int result = dir_cache_init(cache, branch);
    if (result != FRACTYL_OK) {
        cJSON_Delete(root);
        return result;
    }
    
    // Read cache timestamp
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsNumber(timestamp)) {
        cache->cache_timestamp = (time_t)timestamp->valueint;
    }
    
    // Read directories
    cJSON *directories = cJSON_GetObjectItem(root, "directories");
    if (directories && cJSON_IsObject(directories)) {
        cJSON *dir_entry = NULL;
        cJSON_ArrayForEach(dir_entry, directories) {
            const char *path = dir_entry->string;
            if (!path) continue;
            
            cJSON *mtime_json = cJSON_GetObjectItem(dir_entry, "mtime");
            cJSON *file_count_json = cJSON_GetObjectItem(dir_entry, "file_count");
            cJSON *hash_json = cJSON_GetObjectItem(dir_entry, "hash");
            
            if (mtime_json && cJSON_IsNumber(mtime_json) &&
                file_count_json && cJSON_IsNumber(file_count_json) &&
                hash_json && cJSON_IsString(hash_json)) {
                
                time_t mtime = (time_t)mtime_json->valueint;
                int file_count = file_count_json->valueint;
                
                unsigned char hash[32] = {0};
                string_to_hash(hash_json->valuestring, hash);
                dir_cache_update_entry(cache, path, mtime, file_count, hash);
            }
        }
    }
    
    cJSON_Delete(root);
    return FRACTYL_OK;
#endif
}

// Save cache to disk
int dir_cache_save(const directory_cache_t *cache, const char *fractyl_dir) {
    if (!cache || !fractyl_dir) return FRACTYL_ERROR_INVALID_ARGS;
    
#ifndef HAVE_CJSON
    // No cJSON support, skip saving
    return FRACTYL_OK;
#else
    
    char *cache_path = dir_cache_get_path(fractyl_dir, cache->branch);
    if (!cache_path) return FRACTYL_ERROR_OUT_OF_MEMORY;
    
    // Create JSON structure
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(cache_path);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    cJSON_AddNumberToObject(root, "timestamp", (double)cache->cache_timestamp);
    cJSON_AddStringToObject(root, "branch", cache->branch);
    
    cJSON *directories = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "directories", directories);
    
    // Add all directory entries
    for (size_t i = 0; i < cache->count; i++) {
        const dir_cache_entry_t *entry = &cache->entries[i];
        
        cJSON *dir_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(dir_obj, "mtime", (double)entry->mtime);
        cJSON_AddNumberToObject(dir_obj, "file_count", entry->file_count);
        char hash_hex[65];
        hash_to_string(entry->hash, hash_hex);
        cJSON_AddStringToObject(dir_obj, "hash", hash_hex);

        cJSON_AddItemToObject(directories, entry->path, dir_obj);
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

// Add or update directory entry in cache
int dir_cache_update_entry(directory_cache_t *cache, const char *path, time_t mtime, int file_count, const unsigned char *hash) {
    if (!cache || !path) return FRACTYL_ERROR_INVALID_ARGS;
    
    // Look for existing entry
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) {
            // Update existing entry
            cache->entries[i].mtime = mtime;
            cache->entries[i].file_count = file_count;
            if (hash) memcpy(cache->entries[i].hash, hash, 32);
            return FRACTYL_OK;
        }
    }
    
    // Add new entry - check if we need to resize
    if (cache->count >= cache->capacity) {
        size_t new_capacity = cache->capacity * 2;
        dir_cache_entry_t *new_entries = realloc(cache->entries, 
                                                 new_capacity * sizeof(dir_cache_entry_t));
        if (!new_entries) {
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        cache->entries = new_entries;
        cache->capacity = new_capacity;
        
        // Zero out new memory
        memset(&cache->entries[cache->count], 0, 
               (new_capacity - cache->count) * sizeof(dir_cache_entry_t));
    }
    
    // Add new entry
    dir_cache_entry_t *entry = &cache->entries[cache->count];
    entry->path = strdup(path);
    if (!entry->path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    entry->mtime = mtime;
    entry->file_count = file_count;
    if (hash) memcpy(entry->hash, hash, 32);
    cache->count++;
    
    return FRACTYL_OK;
}

// Find directory entry in cache
const dir_cache_entry_t *dir_cache_find_entry(const directory_cache_t *cache, const char *path) {
    if (!cache || !path) return NULL;
    
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].path, path) == 0) {
            return &cache->entries[i];
        }
    }
    
    return NULL;
}

// Check if directory needs scanning based on cache
dir_scan_result_t dir_cache_check_directory(const directory_cache_t *cache, 
                                           const char *path, time_t current_mtime, 
                                           int current_file_count) {
    if (!cache || !path) return DIR_NEW;
    
    const dir_cache_entry_t *entry = dir_cache_find_entry(cache, path);
    if (!entry) {
        return DIR_NEW;
    }
    
    // Check if mtime changed
    if (entry->mtime != current_mtime) {
        return DIR_CHANGED;
    }
    
    // Check if file count changed (indicates files added/removed)
    if (current_file_count >= 0 && entry->file_count != current_file_count) {
        return DIR_CHANGED;
    }
    
    return DIR_UNCHANGED;
}

// Remove directory entry from cache
int dir_cache_remove_entry(directory_cache_t *cache, const char *path) {
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
            memset(&cache->entries[cache->count], 0, sizeof(dir_cache_entry_t));
            return FRACTYL_OK;
        }
    }
    
    return FRACTYL_ERROR_NOT_FOUND;
}

// Clear entire cache
void dir_cache_clear(directory_cache_t *cache) {
    if (!cache) return;
    
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->entries[i].path);
    }
    cache->count = 0;
    cache->cache_timestamp = time(NULL);
}

// Check if cache is stale and needs rebuilding
int dir_cache_is_stale(const directory_cache_t *cache, time_t max_age_seconds) {
    if (!cache) return 1;
    
    time_t now = time(NULL);
    return (now - cache->cache_timestamp) > max_age_seconds;
}

// Validate cache integrity
int dir_cache_validate(const directory_cache_t *cache) {
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