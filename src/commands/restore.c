#include "../include/commands.h"
#include "../include/core.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../utils/json.h"
#include "../utils/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

int cmd_restore(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: fractyl restore <snapshot-id>\n");
        return 1;
    }
    
    const char *snapshot_id = argv[2];
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[PATH_MAX];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Load snapshot
    char snapshot_path[PATH_MAX];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", 
             fractyl_dir, snapshot_id);
    
    snapshot_t snapshot;
    int result = json_load_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Snapshot '%s' not found or invalid\n", snapshot_id);
        free(repo_root);
        return 1;
    }
    
    // Check if current state differs from last snapshot and auto-snapshot if needed
    char current_path[PATH_MAX];
    snprintf(current_path, sizeof(current_path), "%s/CURRENT", fractyl_dir);
    
    FILE *current_file = fopen(current_path, "r");
    if (current_file) {
        char current_snapshot_id[64];
        if (fgets(current_snapshot_id, sizeof(current_snapshot_id), current_file)) {
            fclose(current_file);
            // Strip newline
            size_t len = strlen(current_snapshot_id);
            if (len > 0 && current_snapshot_id[len-1] == '\n') {
                current_snapshot_id[len-1] = '\0';
            }
            
            // Check if we're restoring to a different snapshot than current
            if (strcmp(current_snapshot_id, snapshot_id) != 0) {
                printf("Current state differs from target snapshot. Creating safety snapshot...\n");
                
                // Create auto-snapshot of current state
                char *snapshot_args[] = {"frac", "snapshot", NULL};
                int snapshot_result = cmd_snapshot(2, snapshot_args);
                if (snapshot_result != 0) {
                    printf("Warning: Failed to create safety snapshot. Proceeding with restore...\n");
                } else {
                    printf("Safety snapshot created.\n");
                }
            }
        } else {
            fclose(current_file);
        }
    }
    
    printf("Restoring snapshot %s: \"%s\"\n", snapshot_id, 
           snapshot.description ? snapshot.description : "");
    
    // Load the index from the snapshot's stored index hash
    index_t index;
    void *index_data;
    size_t index_size;
    
    // Load the index data from object storage
    result = object_load(snapshot.index_hash, fractyl_dir, &index_data, &index_size);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to load snapshot index: %d\n", result);
        free(repo_root);
        json_free_snapshot(&snapshot);
        return 1;
    }
    
    // Write to a temporary file to use index_load
    char temp_path[] = "/tmp/fractyl_restore_index_XXXXXX";
    int temp_fd = mkstemp(temp_path);
    if (temp_fd == -1) {
        printf("Error: Failed to create temporary file\n");
        free(index_data);
        free(repo_root);
        json_free_snapshot(&snapshot);
        return 1;
    }
    
    if (write(temp_fd, index_data, index_size) != (ssize_t)index_size) {
        close(temp_fd);
        unlink(temp_path);
        free(index_data);
        free(repo_root);
        json_free_snapshot(&snapshot);
        return 1;
    }
    close(temp_fd);
    free(index_data);
    
    // Load the index from temporary file
    result = index_load(&index, temp_path);
    unlink(temp_path);
    
    if (result != FRACTYL_OK) {
        printf("Error: Failed to parse snapshot index: %d\n", result);
        free(repo_root);
        json_free_snapshot(&snapshot);
        return 1;
    }
    
    // Restore each file from object storage
    for (size_t i = 0; i < index.count; i++) {
        const index_entry_t *entry = &index.entries[i];
        if (!entry->path) continue;
        
        printf("Restoring %s...\n", entry->path);
        
        result = object_restore_file(entry->hash, fractyl_dir, entry->path);
        if (result != FRACTYL_OK) {
            printf("Warning: Failed to restore %s: %d\n", entry->path, result);
            continue;
        }
        
        // Set file permissions
        if (chmod(entry->path, entry->mode) != 0) {
            printf("Warning: Failed to set permissions for %s\n", entry->path);
        }
    }
    
    printf("Restored %zu files from snapshot %s\n", index.count, snapshot_id);
    
    // Save the restored index as the current index
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    result = index_save(&index, index_path);
    if (result != FRACTYL_OK) {
        printf("Warning: Failed to update current index: %d\n", result);
    }
    
    // Update CURRENT file to reflect the restored snapshot
    char update_current_path[PATH_MAX];
    snprintf(update_current_path, sizeof(update_current_path), "%s/CURRENT", fractyl_dir);
    FILE *update_current_file = fopen(update_current_path, "w");
    if (update_current_file) {
        fprintf(update_current_file, "%s\n", snapshot_id);
        fclose(update_current_file);
    }
    
    // Cleanup
    free(repo_root);
    json_free_snapshot(&snapshot);
    index_free(&index);
    
    return 0;
}