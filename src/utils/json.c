#define _GNU_SOURCE  // For strptime
#include "json.h"
#include "../core/hash.h"
#ifdef HAVE_CJSON
#include <cJSON.h>
#else
#error "cJSON library is required for JSON functionality"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char* json_serialize_snapshot(const snapshot_t *snapshot) {
    if (!snapshot) return NULL;

    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;

    // Add snapshot ID
    cJSON_AddStringToObject(json, "id", snapshot->id);

    // Add parent (nullable)
    if (snapshot->parent) {
        cJSON_AddStringToObject(json, "parent", snapshot->parent);
    } else {
        cJSON_AddNullToObject(json, "parent");
    }

    // Add description
    if (snapshot->description) {
        cJSON_AddStringToObject(json, "description", snapshot->description);
    } else {
        cJSON_AddStringToObject(json, "description", "");
    }

    // Add timestamp as ISO 8601 string
    char timestamp_str[64];
    struct tm *tm_info = gmtime(&snapshot->timestamp);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    cJSON_AddStringToObject(json, "timestamp", timestamp_str);

    // Add index hash as hex string
    char hash_hex[FRACTYL_HASH_HEX_SIZE];
    hash_to_string(snapshot->index_hash, hash_hex);
    cJSON_AddStringToObject(json, "index_hash", hash_hex);

    // Add git status array
    if (snapshot->git_status && snapshot->git_status_count > 0) {
        cJSON *git_status_array = cJSON_CreateArray();
        for (size_t i = 0; i < snapshot->git_status_count; i++) {
            if (snapshot->git_status[i]) {
                cJSON_AddItemToArray(git_status_array, cJSON_CreateString(snapshot->git_status[i]));
            }
        }
        cJSON_AddItemToObject(json, "git_status", git_status_array);
    } else {
        cJSON_AddItemToObject(json, "git_status", cJSON_CreateArray());
    }

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    return json_string; // Caller must free with free()
}

int json_deserialize_snapshot(const char *json_str, snapshot_t *snapshot) {
    if (!json_str || !snapshot) {
        return FRACTYL_ERROR_GENERIC;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        return FRACTYL_ERROR_GENERIC;
    }

    // Initialize snapshot
    memset(snapshot, 0, sizeof(snapshot_t));

    // Parse ID
    cJSON *id = cJSON_GetObjectItem(json, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        strncpy(snapshot->id, id->valuestring, sizeof(snapshot->id) - 1);
    }

    // Parse parent (nullable)
    cJSON *parent = cJSON_GetObjectItem(json, "parent");
    if (cJSON_IsString(parent) && parent->valuestring) {
        snapshot->parent = strdup(parent->valuestring);
    }

    // Parse description
    cJSON *description = cJSON_GetObjectItem(json, "description");
    if (cJSON_IsString(description) && description->valuestring) {
        snapshot->description = strdup(description->valuestring);
    }

    // Parse timestamp
    cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");
    if (cJSON_IsString(timestamp) && timestamp->valuestring) {
        struct tm tm_info = {0};
        if (strptime(timestamp->valuestring, "%Y-%m-%dT%H:%M:%SZ", &tm_info)) {
            snapshot->timestamp = timegm(&tm_info);
        }
    }

    // Parse index hash
    cJSON *index_hash = cJSON_GetObjectItem(json, "index_hash");
    if (cJSON_IsString(index_hash) && index_hash->valuestring) {
        string_to_hash(index_hash->valuestring, snapshot->index_hash);
    }

    // Parse git status array
    cJSON *git_status = cJSON_GetObjectItem(json, "git_status");
    if (cJSON_IsArray(git_status)) {
        int array_size = cJSON_GetArraySize(git_status);
        if (array_size > 0) {
            snapshot->git_status = malloc(sizeof(char*) * array_size);
            if (snapshot->git_status) {
                snapshot->git_status_count = 0;
                for (int i = 0; i < array_size; i++) {
                    cJSON *item = cJSON_GetArrayItem(git_status, i);
                    if (cJSON_IsString(item) && item->valuestring) {
                        snapshot->git_status[snapshot->git_status_count] = strdup(item->valuestring);
                        if (snapshot->git_status[snapshot->git_status_count]) {
                            snapshot->git_status_count++;
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(json);
    return FRACTYL_OK;
}

int json_save_snapshot(const snapshot_t *snapshot, const char *file_path) {
    if (!snapshot || !file_path) {
        return FRACTYL_ERROR_GENERIC;
    }

    char *json_str = json_serialize_snapshot(snapshot);
    if (!json_str) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }

    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        free(json_str);
        return FRACTYL_ERROR_IO;
    }

    size_t len = strlen(json_str);
    if (fwrite(json_str, 1, len, fp) != len) {
        fclose(fp);
        free(json_str);
        return FRACTYL_ERROR_IO;
    }

    fclose(fp);
    free(json_str);
    return FRACTYL_OK;
}

int json_load_snapshot(snapshot_t *snapshot, const char *file_path) {
    if (!snapshot || !file_path) {
        return FRACTYL_ERROR_GENERIC;
    }

    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        return FRACTYL_ERROR_IO;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return FRACTYL_ERROR_IO;
    }

    // Read entire file
    char *json_str = malloc(file_size + 1);
    if (!json_str) {
        fclose(fp);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }

    size_t bytes_read = fread(json_str, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        free(json_str);
        return FRACTYL_ERROR_IO;
    }

    json_str[file_size] = '\0';

    int result = json_deserialize_snapshot(json_str, snapshot);
    free(json_str);
    
    return result;
}

void json_free_snapshot(snapshot_t *snapshot) {
    if (!snapshot) return;

    free(snapshot->parent);
    free(snapshot->description);

    if (snapshot->git_status) {
        for (size_t i = 0; i < snapshot->git_status_count; i++) {
            free(snapshot->git_status[i]);
        }
        free(snapshot->git_status);
    }

    memset(snapshot, 0, sizeof(snapshot_t));
}

char* json_serialize_graph(const char **snapshot_ids, size_t count) {
    if (!snapshot_ids || count == 0) return NULL;

    cJSON *json = cJSON_CreateObject();
    cJSON *snapshots = cJSON_CreateArray();

    for (size_t i = 0; i < count; i++) {
        if (snapshot_ids[i]) {
            cJSON_AddItemToArray(snapshots, cJSON_CreateString(snapshot_ids[i]));
        }
    }

    cJSON_AddItemToObject(json, "snapshots", snapshots);

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    return json_string;
}

int json_deserialize_graph(const char *json_str, char ***snapshot_ids, size_t *count) {
    if (!json_str || !snapshot_ids || !count) {
        return FRACTYL_ERROR_GENERIC;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        return FRACTYL_ERROR_GENERIC;
    }

    cJSON *snapshots = cJSON_GetObjectItem(json, "snapshots");
    if (!cJSON_IsArray(snapshots)) {
        cJSON_Delete(json);
        return FRACTYL_ERROR_GENERIC;
    }

    int array_size = cJSON_GetArraySize(snapshots);
    if (array_size <= 0) {
        *snapshot_ids = NULL;
        *count = 0;
        cJSON_Delete(json);
        return FRACTYL_OK;
    }

    char **ids = malloc(sizeof(char*) * array_size);
    if (!ids) {
        cJSON_Delete(json);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }

    size_t actual_count = 0;
    for (int i = 0; i < array_size; i++) {
        cJSON *item = cJSON_GetArrayItem(snapshots, i);
        if (cJSON_IsString(item) && item->valuestring) {
            ids[actual_count] = strdup(item->valuestring);
            if (ids[actual_count]) {
                actual_count++;
            }
        }
    }

    *snapshot_ids = ids;
    *count = actual_count;

    cJSON_Delete(json);
    return FRACTYL_OK;
}