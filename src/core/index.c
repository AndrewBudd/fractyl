#include "index.h"
#include "../include/fractyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

// --- Index management implementation ---

int index_load(index_t *index, const char *path) {
    if (!index || !path) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // Initialize index structure
    memset(index, 0, sizeof(index_t));
    
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        // If file doesn't exist, that's ok - start with empty index
        if (errno == ENOENT) {
            return FRACTYL_OK;
        }
        return FRACTYL_ERROR_IO;
    }
    
    // Read binary format:
    // Header: "FIDX" (4 bytes) + version (4 bytes) + entry count (4 bytes)
    char magic[5];
    uint32_t version, count;
    
    if (fread(magic, 1, 4, fp) != 4) {
        fclose(fp);
        return FRACTYL_ERROR_IO;
    }
    magic[4] = '\0';
    
    if (strcmp(magic, "FIDX") != 0) {
        fclose(fp);
        return FRACTYL_ERROR_GENERIC; // Not a valid index file
    }
    
    if (fread(&version, sizeof(uint32_t), 1, fp) != 1 ||
        fread(&count, sizeof(uint32_t), 1, fp) != 1) {
        fclose(fp);
        return FRACTYL_ERROR_IO;
    }
    
    if (version != 1) {
        fclose(fp);
        return FRACTYL_ERROR_GENERIC; // Unsupported version
    }
    
    if (count == 0) {
        fclose(fp);
        return FRACTYL_OK; // Empty index
    }
    
    // Allocate entries
    index->entries = malloc(sizeof(index_entry_t) * count);
    if (!index->entries) {
        fclose(fp);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    index->capacity = count;
    index->count = 0;
    
    // Read each entry
    for (uint32_t i = 0; i < count; i++) {
        uint16_t path_len;
        if (fread(&path_len, sizeof(uint16_t), 1, fp) != 1) {
            index_free(index);
            fclose(fp);
            return FRACTYL_ERROR_IO;
        }
        
        if (path_len == 0 || path_len > 4096) { // Sanity check
            index_free(index);
            fclose(fp);
            return FRACTYL_ERROR_GENERIC;
        }
        
        // Read path
        char *path_buf = malloc(path_len + 1);
        if (!path_buf) {
            index_free(index);
            fclose(fp);
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        
        if (fread(path_buf, 1, path_len, fp) != path_len) {
            free(path_buf);
            index_free(index);
            fclose(fp);
            return FRACTYL_ERROR_IO;
        }
        path_buf[path_len] = '\0';
        
        // Read hash, mode, size, mtime
        index_entry_t *entry = &index->entries[index->count];
        entry->path = path_buf;
        
        if (fread(entry->hash, 1, 32, fp) != 32 ||
            fread(&entry->mode, sizeof(mode_t), 1, fp) != 1 ||
            fread(&entry->size, sizeof(off_t), 1, fp) != 1 ||
            fread(&entry->mtime, sizeof(time_t), 1, fp) != 1) {
            index_free(index);
            fclose(fp);
            return FRACTYL_ERROR_IO;
        }
        
        index->count++;
    }
    
    fclose(fp);
    return FRACTYL_OK;
}

int index_save(const index_t *index, const char *path) {
    if (!index || !path) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return FRACTYL_ERROR_IO;
    }
    
    // Write header
    const char magic[] = "FIDX";
    uint32_t version = 1;
    uint32_t count = (uint32_t)index->count;
    
    if (fwrite(magic, 1, 4, fp) != 4 ||
        fwrite(&version, sizeof(uint32_t), 1, fp) != 1 ||
        fwrite(&count, sizeof(uint32_t), 1, fp) != 1) {
        fclose(fp);
        return FRACTYL_ERROR_IO;
    }
    
    // Write entries
    for (size_t i = 0; i < index->count; i++) {
        const index_entry_t *entry = &index->entries[i];
        if (!entry->path) continue;
        
        uint16_t path_len = (uint16_t)strlen(entry->path);
        
        if (fwrite(&path_len, sizeof(uint16_t), 1, fp) != 1 ||
            fwrite(entry->path, 1, path_len, fp) != path_len ||
            fwrite(entry->hash, 1, 32, fp) != 32 ||
            fwrite(&entry->mode, sizeof(mode_t), 1, fp) != 1 ||
            fwrite(&entry->size, sizeof(off_t), 1, fp) != 1 ||
            fwrite(&entry->mtime, sizeof(time_t), 1, fp) != 1) {
            fclose(fp);
            return FRACTYL_ERROR_IO;
        }
    }
    
    fclose(fp);
    return FRACTYL_OK;
}

int index_add_entry(index_t *index, const index_entry_t *entry) {
    if (!index || !entry || !entry->path) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // Search for existing entry with same path
    for (size_t i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].path, entry->path) == 0) {
            // Replace existing entry contents
            free(index->entries[i].path);
            index->entries[i].path = strdup(entry->path);
            if (!index->entries[i].path) {
                return FRACTYL_ERROR_OUT_OF_MEMORY;
            }
            memcpy(index->entries[i].hash, entry->hash, sizeof(entry->hash));
            index->entries[i].mode = entry->mode;
            index->entries[i].size = entry->size;
            index->entries[i].mtime = entry->mtime;
            return FRACTYL_OK;
        }
    }
    
    // Expand if needed
    if (index->count == index->capacity) {
        size_t newcap = index->capacity ? index->capacity * 2 : 8;
        index_entry_t *new_entries = realloc(index->entries, sizeof(index_entry_t) * newcap);
        if (!new_entries) {
            return FRACTYL_ERROR_OUT_OF_MEMORY;
        }
        index->entries = new_entries;
        index->capacity = newcap;
    }
    
    // Deep copy entry
    index_entry_t *dest = &index->entries[index->count];
    dest->path = strdup(entry->path);
    if (!dest->path) {
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    memcpy(dest->hash, entry->hash, sizeof(entry->hash));
    dest->mode = entry->mode;
    dest->size = entry->size;
    dest->mtime = entry->mtime;
    
    index->count++;
    return FRACTYL_OK;
}

int index_remove_entry(index_t *index, const char *path) {
    if (!index || !path) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    for (size_t i = 0; i < index->count; ++i) {
        if (strcmp(index->entries[i].path, path) == 0) {
            free(index->entries[i].path);
            
            // Move the last entry to this position to avoid shifting everything
            if (i != index->count - 1) {
                index->entries[i] = index->entries[index->count - 1];
            }
            
            index->count--;
            return FRACTYL_OK;
        }
    }
    return FRACTYL_ERROR_INDEX_NOT_FOUND;
}

int index_has_changes(const index_t *index, const char *workdir, int *has_changes) {
    if (!index || !workdir || !has_changes) {
        return FRACTYL_ERROR_GENERIC;
    }
    
    // TODO: implement diff check against working directory
    *has_changes = 0;
    return FRACTYL_OK;
}

void index_free(index_t *index) {
    if (!index) return;
    
    for (size_t i = 0; i < index->count; ++i) {
        free(index->entries[i].path);
        index->entries[i].path = NULL;
    }
    
    free(index->entries);
    index->entries = NULL;
    index->count = 0;
    index->capacity = 0;
}

void index_print(const index_t *index) {
    if (!index) {
        printf("Index: NULL\n");
        return;
    }
    
    printf("Index count: %zu, capacity: %zu\n", index->count, index->capacity);
    for (size_t i = 0; i < index->count; ++i) {
        const index_entry_t *entry = &index->entries[i];
        printf("  [%zu] %s (mode: %o, size: %ld, mtime: %ld)\n", 
               i, 
               entry->path ? entry->path : "(null)",
               (unsigned int)entry->mode,
               (long)entry->size,
               (long)entry->mtime);
    }
}

// Find entry by path (helper function)
const index_entry_t* index_find_entry(const index_t *index, const char *path) {
    if (!index || !path) return NULL;
    
    for (size_t i = 0; i < index->count; ++i) {
        if (index->entries[i].path && strcmp(index->entries[i].path, path) == 0) {
            return &index->entries[i];
        }
    }
    return NULL;
}

// Initialize empty index
int index_init(index_t *index) {
    if (!index) return FRACTYL_ERROR_GENERIC;
    
    memset(index, 0, sizeof(index_t));
    return FRACTYL_OK;
}

// Minimal smoke-test for add/remove/free
#ifdef FRACTYL_INDEX_TEST
static int test_index_basic(void) {
    index_t idx = {0};
    
    // Test data with string literal paths (safe for testing)
    index_entry_t e1 = { 
        .path = strdup("foo.txt"), 
        .hash = {1}, 
        .mode = 0100644, 
        .size = 123, 
        .mtime = 100 
    };
    
    index_entry_t e2 = { 
        .path = strdup("bar.bin"), 
        .hash = {2}, 
        .mode = 0100644, 
        .size = 555, 
        .mtime = 111 
    };
    
    printf("Testing index operations...\n");
    
    int result = index_add_entry(&idx, &e1);
    printf("Add e1 result: %d\n", result);
    
    result = index_add_entry(&idx, &e2);
    printf("Add e2 result: %d\n", result);
    
    printf("After add:\n");
    index_print(&idx);
    
    result = index_remove_entry(&idx, "foo.txt");
    printf("Remove foo.txt result: %d\n", result);
    
    printf("After remove foo.txt:\n");
    index_print(&idx);
    
    index_free(&idx);
    printf("After free:\n");
    index_print(&idx);
    
    // Clean up test data
    free(e1.path);
    free(e2.path);
    
    return 0;
}
#endif
