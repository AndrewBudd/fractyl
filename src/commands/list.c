#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

static void print_timestamp(time_t timestamp) {
    struct tm *tm_info = localtime(&timestamp);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("%s", time_str);
}

int cmd_list(int argc, char **argv) {
    (void)argc; (void)argv; // Suppress unused parameter warnings
    
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
    
    // Open snapshots directory
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        printf("No snapshots found\n");
        return 0;
    }
    
    printf("Snapshots:\n");
    printf("%-38s %-20s %s\n", "ID", "Date", "Description");
    printf("%-38s %-20s %s\n", "------------------------------------", 
           "--------------------", "--------------------");
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        // Skip . and .. and non-.json files
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", 
                 snapshots_dir, entry->d_name);
        
        snapshot_t snapshot;
        int result = json_load_snapshot(&snapshot, snapshot_path);
        if (result != FRACTYL_OK) {
            continue; // Skip invalid snapshots
        }
        
        // Extract snapshot ID from filename (remove .json)
        char snapshot_id[256];
        size_t name_len = strlen(entry->d_name);
        if (name_len > sizeof(snapshot_id) - 1) {
            continue; // Skip if filename is too long
        }
        strcpy(snapshot_id, entry->d_name);
        char *dot = strrchr(snapshot_id, '.');
        if (dot) *dot = '\0';
        
        printf("%-38s ", snapshot_id);
        print_timestamp(snapshot.timestamp);
        printf(" %s\n", snapshot.description ? snapshot.description : "");
        
        json_free_snapshot(&snapshot);
    }
    
    closedir(d);
    return 0;
}