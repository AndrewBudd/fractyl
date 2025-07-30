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

// Tree node for building snapshot hierarchy
typedef struct tree_node {
    char id[64];
    char *parent_id;
    char *description;
    time_t timestamp;
    struct tree_node **children;
    size_t child_count;
    size_t child_capacity;
} tree_node_t;

// Add a child to a tree node
static void add_child(tree_node_t *parent, tree_node_t *child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        parent->children = realloc(parent->children, 
                                 parent->child_capacity * sizeof(tree_node_t*));
    }
    parent->children[parent->child_count++] = child;
}

// Compare function for sorting children by timestamp
static int compare_children(const void *a, const void *b) {
    const tree_node_t *node_a = *(const tree_node_t **)a;
    const tree_node_t *node_b = *(const tree_node_t **)b;
    
    if (node_a->timestamp < node_b->timestamp) return -1;
    if (node_a->timestamp > node_b->timestamp) return 1;
    return 0;
}

// Sort children of a node by timestamp
static void sort_children(tree_node_t *node) {
    if (node->child_count > 1) {
        qsort(node->children, node->child_count, sizeof(tree_node_t*), compare_children);
    }
    
    // Recursively sort children of children
    for (size_t i = 0; i < node->child_count; i++) {
        sort_children(node->children[i]);
    }
}

// Print snapshot with formatting
static void print_snapshot(tree_node_t *node, const char *prefix, const char *connector) {
    char short_id[9];
    strncpy(short_id, node->id, 8);
    short_id[8] = '\0';
    
    printf("%s%s%s ", prefix, connector, short_id);
    print_timestamp(node->timestamp);
    printf(" %s\n", node->description ? node->description : "");
}

// Print the tree structure - linear chains straight down, branches indented
static void print_tree_structure(tree_node_t *node, const char *prefix, int is_last) {
    if (!node) return;
    
    // Check if this is part of a linear chain (single parent -> single child)
    tree_node_t *current = node;
    int in_linear_chain = 1;
    
    while (current && in_linear_chain) {
        if (current == node) {
            // First node - use connector if we're in a branch
            const char *connector = (strlen(prefix) == 0) ? "" : (is_last ? "└── " : "├── ");
            print_snapshot(current, prefix, connector);
        } else {
            // Continuing linear chain - no indentation or connectors
            print_snapshot(current, "", "");
        }
        
        if (current->child_count == 0) {
            // End of chain
            break;
        } else if (current->child_count == 1) {
            // Single child - continue linear progression
            current = current->children[0];
        } else {
            // Multiple children - branch point, need indentation
            in_linear_chain = 0;
            
            char child_prefix[512];
            snprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix, 
                    (strlen(prefix) == 0) ? "" : (is_last ? "    " : "│   "));
            
            // Print each branch with indentation
            for (size_t i = 0; i < current->child_count; i++) {
                print_tree_structure(current->children[i], child_prefix, i == current->child_count - 1);
            }
        }
    }
}

// Free tree node and its children
static void free_tree_node(tree_node_t *node) {
    if (!node) return;
    
    for (size_t i = 0; i < node->child_count; i++) {
        free_tree_node(node->children[i]);
    }
    
    free(node->children);
    free(node->parent_id);
    free(node->description);
    free(node);
}

int cmd_list(int argc, char **argv) {
    (void)argc; (void)argv; // Suppress unused parameter warnings
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Open snapshots directory
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        printf("No snapshots found\n");
        free(repo_root);
        return 0;
    }
    
    // First pass: collect all snapshots
    tree_node_t **all_nodes = NULL;
    size_t node_count = 0;
    size_t node_capacity = 0;
    
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
            json_free_snapshot(&snapshot);
            continue; // Skip if filename is too long
        }
        strcpy(snapshot_id, entry->d_name);
        char *dot = strrchr(snapshot_id, '.');
        if (dot) *dot = '\0';
        
        // Create tree node
        tree_node_t *node = malloc(sizeof(tree_node_t));
        if (!node) {
            json_free_snapshot(&snapshot);
            continue;
        }
        
        strcpy(node->id, snapshot_id);
        node->parent_id = snapshot.parent ? strdup(snapshot.parent) : NULL;
        node->description = snapshot.description ? strdup(snapshot.description) : NULL;
        node->timestamp = snapshot.timestamp;
        node->children = NULL;
        node->child_count = 0;
        node->child_capacity = 0;
        
        // Add to collection
        if (node_count >= node_capacity) {
            node_capacity = node_capacity ? node_capacity * 2 : 16;
            all_nodes = realloc(all_nodes, node_capacity * sizeof(tree_node_t*));
        }
        all_nodes[node_count++] = node;
        
        json_free_snapshot(&snapshot);
    }
    closedir(d);
    
    if (node_count == 0) {
        printf("No snapshots found\n");
        free(repo_root);
        return 0;
    }
    
    // Second pass: build parent-child relationships
    for (size_t i = 0; i < node_count; i++) {
        tree_node_t *child = all_nodes[i];
        if (child->parent_id) {
            // Find parent
            for (size_t j = 0; j < node_count; j++) {
                tree_node_t *parent = all_nodes[j];
                if (strcmp(parent->id, child->parent_id) == 0) {
                    add_child(parent, child);
                    break;
                }
            }
        }
    }
    
    // Third pass: find root nodes (nodes with no parent) and sort them
    tree_node_t **roots = NULL;
    size_t root_count = 0;
    size_t root_capacity = 0;
    
    for (size_t i = 0; i < node_count; i++) {
        tree_node_t *node = all_nodes[i];
        if (!node->parent_id) {
            if (root_count >= root_capacity) {
                root_capacity = root_capacity ? root_capacity * 2 : 4;
                roots = realloc(roots, root_capacity * sizeof(tree_node_t*));
            }
            roots[root_count++] = node;
        }
    }
    
    // Sort root nodes by timestamp
    if (root_count > 1) {
        qsort(roots, root_count, sizeof(tree_node_t*), compare_children);
    }
    
    // Sort all children recursively
    for (size_t i = 0; i < root_count; i++) {
        sort_children(roots[i]);
    }
    
    // Print the snapshot history
    printf("Snapshot History:\n");
    for (size_t i = 0; i < root_count; i++) {
        if (i > 0) printf("\n"); // Separate different root chains
        print_tree_structure(roots[i], "", i == root_count - 1);
    }
    
    // Cleanup - only free root nodes as they recursively free children
    for (size_t i = 0; i < root_count; i++) {
        free_tree_node(roots[i]);
    }
    free(all_nodes);
    free(roots);
    free(repo_root);
    
    return 0;
}