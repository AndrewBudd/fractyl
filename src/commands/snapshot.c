#include "../include/commands.h"
#include "../include/core.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../core/hash.h"
#include "../utils/json.h"
#include "../utils/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#ifdef HAVE_UUID
#include <uuid/uuid.h>
#else
// Fallback UUID generation
#include <fcntl.h>
#endif

static int scan_directory_recursive(const char *dir_path, const char *relative_path, 
                                   index_t *new_index, const char *fractyl_dir) {
    DIR *d = opendir(dir_path);
    if (!d) return FRACTYL_ERROR_IO;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Skip .fractyl and .git directories
        if (strcmp(entry->d_name, ".fractyl") == 0 || strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char full_path[2048];
        char rel_path[2048];
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (strlen(relative_path) == 0) {
            strcpy(rel_path, entry->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "%s/%s", relative_path, entry->d_name);
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Recursively scan directory
            int result = scan_directory_recursive(full_path, rel_path, new_index, fractyl_dir);
            if (result != FRACTYL_OK) {
                closedir(d);
                return result;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Skip files larger than 1GB
            if (st.st_size > 1024 * 1024 * 1024) {
                printf("Skipping large file: %s (%ld bytes)\n", rel_path, st.st_size);
                continue;
            }

            // Create index entry
            index_entry_t entry_data;
            memset(&entry_data, 0, sizeof(entry_data));
            
            entry_data.path = strdup(rel_path);
            if (!entry_data.path) {
                closedir(d);
                return FRACTYL_ERROR_OUT_OF_MEMORY;
            }
            
            entry_data.mode = st.st_mode;
            entry_data.size = st.st_size;
            entry_data.mtime = st.st_mtime;
            
            // Hash the file and store in object storage
            int result = object_store_file(full_path, fractyl_dir, entry_data.hash);
            if (result != FRACTYL_OK) {
                printf("Warning: Failed to store file %s: %d\n", rel_path, result);
                free(entry_data.path);
                continue;
            }
            
            // Add to index
            result = index_add_entry(new_index, &entry_data);
            free(entry_data.path);
            
            if (result != FRACTYL_OK) {
                closedir(d);
                return result;
            }
        }
    }

    closedir(d);
    return FRACTYL_OK;
}

static char* generate_snapshot_id(void) {
#ifdef HAVE_UUID
    uuid_t uuid;
    uuid_generate(uuid);
    
    char *uuid_str = malloc(37); // 36 chars + null terminator
    if (!uuid_str) return NULL;
    
    uuid_unparse(uuid, uuid_str);
    return uuid_str;
#else
    // Fallback: use timestamp + random number
    char *id_str = malloc(64);
    if (!id_str) return NULL;
    
    snprintf(id_str, 64, "%ld-%d", time(NULL), rand() % 10000);
    return id_str;
#endif
}

int cmd_snapshot(int argc, char **argv) {
    const char *message = NULL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            i++; // Skip next argument
        }
    }
    
    if (!message) {
        printf("Error: Snapshot message is required. Use -m \"message\"\n");
        return 1;
    }
    
    // Find or create .fractyl directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("Error: Cannot get current directory\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", cwd);
    
    // Initialize fractyl repo if needed
    struct stat st;
    if (stat(fractyl_dir, &st) != 0) {
        printf("Initializing fractyl repository...\n");
        int result = object_storage_init(fractyl_dir);
        if (result != FRACTYL_OK) {
            printf("Error: Failed to initialize repository: %d\n", result);
            return 1;
        }
        
        // Create snapshots directory
        char snapshots_dir[2048];
        snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
        if (mkdir(snapshots_dir, 0755) != 0) {
            printf("Error: Failed to create snapshots directory\n");
            return 1;
        }
    }
    
    // Load current index
    char index_path[2048];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    
    index_t current_index;
    int result = index_load(&current_index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to load index: %d\n", result);
        return 1;
    }
    
    // Scan current directory and build new index
    index_t new_index;
    index_init(&new_index);
    
    printf("Scanning directory...\n");
    
    result = scan_directory_recursive(cwd, "", &new_index, fractyl_dir);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to scan directory: %d\n", result);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    printf("Found %zu files\n", new_index.count);
    
    // Check if there are changes (simple comparison for now)
    if (current_index.count == new_index.count) {
        // TODO: More sophisticated change detection
        printf("No changes detected since last snapshot\n");
        index_free(&current_index);
        index_free(&new_index);
        return 0;
    }
    
    // Save new index
    result = index_save(&new_index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save index: %d\n", result);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    // Create snapshot metadata
    snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    
    char *snapshot_id = generate_snapshot_id();
    if (!snapshot_id) {
        printf("Error: Failed to generate snapshot ID\n");
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    strncpy(snapshot.id, snapshot_id, sizeof(snapshot.id) - 1);
    snapshot.description = strdup(message);
    snapshot.timestamp = time(NULL);
    
    // Hash the new index (use simpler approach for now)
    char index_str[64];
    snprintf(index_str, sizeof(index_str), "index-%zu", new_index.count);
    result = hash_data(index_str, strlen(index_str), snapshot.index_hash);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to hash index: %d\n", result);
        free(snapshot_id);
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    // Save snapshot metadata
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", 
             fractyl_dir, snapshot_id);
    
    result = json_save_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to save snapshot: %d\n", result);
        free(snapshot_id);
        json_free_snapshot(&snapshot);
        index_free(&current_index);
        index_free(&new_index);
        return 1;
    }
    
    printf("Created snapshot %s: \"%s\"\n", snapshot_id, message);
    printf("Stored %zu files in object storage\n", new_index.count);
    
    // Cleanup
    free(snapshot_id);
    json_free_snapshot(&snapshot);
    index_free(&current_index);
    index_free(&new_index);
    
    return 0;
}