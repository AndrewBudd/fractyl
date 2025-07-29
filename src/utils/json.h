#ifndef JSON_H
#define JSON_H

#include "../include/core.h"
#include "../include/fractyl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Serialize snapshot to JSON string (caller must free)
char* json_serialize_snapshot(const snapshot_t *snapshot);

// Deserialize snapshot from JSON string
int json_deserialize_snapshot(const char *json_str, snapshot_t *snapshot);

// Save snapshot to JSON file
int json_save_snapshot(const snapshot_t *snapshot, const char *file_path);

// Load snapshot from JSON file
int json_load_snapshot(snapshot_t *snapshot, const char *file_path);

// Free snapshot structure (including allocated strings)
void json_free_snapshot(snapshot_t *snapshot);

// Serialize simple graph structure for snapshot relationships
char* json_serialize_graph(const char **snapshot_ids, size_t count);

// Deserialize graph structure
int json_deserialize_graph(const char *json_str, char ***snapshot_ids, size_t *count);

#ifdef __cplusplus
}
#endif

#endif // JSON_H