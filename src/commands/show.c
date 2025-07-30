#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../utils/paths.h"
#include "../utils/git.h"
#include "../core/hash.h"
#include "../core/objects.h"
#include "../core/index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

static void print_timestamp(time_t timestamp) {
    struct tm *tm_info = localtime(&timestamp);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("%s", time_str);
}

static void print_hash(const unsigned char *hash) {
    char hex_string[65];
    hash_to_string(hash, hex_string);
    printf("%.12s", hex_string); // Show first 12 chars
}

static void print_snapshot_header(const snapshot_t *snapshot) {
    printf("Snapshot: %s\n", snapshot->id);
    printf("Description: %s\n", snapshot->description ? snapshot->description : "(no description)");
    printf("Timestamp: ");
    print_timestamp(snapshot->timestamp);
    printf("\n");
    
    if (snapshot->parent) {
        printf("Parent: %s\n", snapshot->parent);
    } else {
        printf("Parent: (initial snapshot)\n");
    }
    
    printf("Index Hash: ");
    print_hash(snapshot->index_hash);
    printf("\n");
    
    // Git information
    if (snapshot->git_branch) {
        printf("Git Branch: %s\n", snapshot->git_branch);
        if (snapshot->git_commit) {
            printf("Git Commit: %.12s\n", snapshot->git_commit);
        }
        printf("Git Status: %s\n", snapshot->git_dirty ? "dirty (uncommitted changes)" : "clean");
    } else {
        printf("Git: (not a git repository)\n");
    }
    
    printf("\n");
}

static int show_snapshot_files(const char *fractyl_dir, const snapshot_t *snapshot) {
    // Check if index object exists
    if (!object_exists(snapshot->index_hash, fractyl_dir)) {
        printf("Warning: Index object not found\n");
        return -1;
    }
    
    // Load the index data
    void *index_data;
    size_t index_size;
    int result = object_load(snapshot->index_hash, fractyl_dir, &index_data, &index_size);
    if (result != 0) {
        printf("Error: Could not load index for snapshot\n");
        return -1;
    }
    
    printf("Files in snapshot:\n");
    printf("%-8s %-10s %-12s %s\n", "Mode", "Size", "Hash", "Path");
    printf("%-8s %-10s %-12s %s\n", "--------", "----------", "------------", "----");
    
    // For a more complete implementation, we would parse the index format
    // For now, just show summary information
    printf("(Index contains %zu bytes of file metadata)\n", index_size);
    printf("Total files tracked in this snapshot's index\n");
    
    free(index_data);
    return 0;
}

int cmd_show(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: frac show <snapshot-id>\n");
        printf("Show detailed information about a snapshot\n");
        return 1;
    }
    
    const char *snapshot_id = argv[2];
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Get current git branch for snapshot directory
    char *current_branch = git_is_repository(repo_root) ? git_get_current_branch(repo_root) : NULL;
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, current_branch);
    
    if (!snapshots_dir) {
        printf("Error: Could not determine snapshots directory\n");
        free(repo_root);
        free(current_branch);
        return 1;
    }
    
    // Build snapshot file path
    char snapshot_path[2048];
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s.json", snapshots_dir, snapshot_id);
    
    // Check if snapshot exists
    struct stat st;
    if (stat(snapshot_path, &st) != 0) {
        printf("Error: Snapshot '%s' not found\n", snapshot_id);
        free(repo_root);
        free(current_branch);
        free(snapshots_dir);
        return 1;
    }
    
    // Load snapshot metadata using existing utility
    snapshot_t snapshot = {0};
    if (json_load_snapshot(&snapshot, snapshot_path) != 0) {
        printf("Error: Could not load snapshot metadata\n");
        free(repo_root);
        free(current_branch);
        free(snapshots_dir);
        return 1;
    }
    
    // Print snapshot information
    print_snapshot_header(&snapshot);
    
    // Show files in snapshot
    show_snapshot_files(fractyl_dir, &snapshot);
    
    // Cleanup
    json_free_snapshot(&snapshot);
    free(repo_root);
    free(current_branch);
    free(snapshots_dir);
    
    return 0;
}