#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int cmd_delete(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: fractyl delete <snapshot-id>\n");
        return 1;
    }
    
    const char *snapshot_id = argv[2];
    
    // Find .fractyl directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("Error: Cannot get current directory\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", cwd);
    
    struct stat st;
    if (stat(fractyl_dir, &st) != 0) {
        printf("Error: Not a fractyl repository (no .fractyl directory found)\n");
        return 1;
    }
    
    // Check if snapshot exists
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/snapshots/%s.json", 
             fractyl_dir, snapshot_id);
    
    if (stat(snapshot_path, &st) != 0) {
        printf("Error: Snapshot '%s' not found\n", snapshot_id);
        return 1;
    }
    
    // Load snapshot to show info
    snapshot_t snapshot;
    int result = json_load_snapshot(&snapshot, snapshot_path);
    if (result != FRACTYL_OK) {
        printf("Error: Invalid snapshot file\n");
        return 1;
    }
    
    printf("Deleting snapshot %s: \"%s\"\n", snapshot_id,
           snapshot.description ? snapshot.description : "");
    
    // Delete snapshot file
    if (unlink(snapshot_path) != 0) {
        printf("Error: Failed to delete snapshot file\n");
        json_free_snapshot(&snapshot);
        return 1;
    }
    
    printf("Snapshot %s deleted successfully\n", snapshot_id);
    printf("Note: Object files are not garbage collected yet\n");
    
    json_free_snapshot(&snapshot);
    return 0;
}