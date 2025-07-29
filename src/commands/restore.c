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

int cmd_restore(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: fractyl restore <snapshot-id>\n");
        return 1;
    }
    
    const char *snapshot_id = argv[2];
    
    // Find .fractyl directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("Error: Cannot get current directory\n");
        return 1;
    }
    
    char fractyl_dir[PATH_MAX];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", cwd);
    
    struct stat st;
    if (stat(fractyl_dir, &st) != 0) {
        printf("Error: Not a fractyl repository (no .fractyl directory found)\n");
        return 1;
    }
    
    // Load snapshot
    char snapshot_path[PATH_MAX];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", 
             fractyl_dir, snapshot_id);
    
    snapshot_t snapshot;
    int result = json_load_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Snapshot '%s' not found or invalid\n", snapshot_id);
        return 1;
    }
    
    printf("Restoring snapshot %s: \"%s\"\n", snapshot_id, 
           snapshot.description ? snapshot.description : "");
    
    // Load the index that was current at snapshot time
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/index", fractyl_dir);
    
    index_t index;
    result = index_load(&index, index_path);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to load index: %d\n", result);
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
    
    // Cleanup
    json_free_snapshot(&snapshot);
    index_free(&index);
    
    return 0;
}