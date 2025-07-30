#include "../include/commands.h"
#include "../include/core.h"
#include "../core/objects.h"
#include "../core/index.h"
#include "../utils/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int cmd_init(int argc, char **argv) {
    (void)argc; // Unused parameter
    (void)argv; // Unused parameter
    
    // Get current working directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("Error: Cannot get current directory\n");
        return 1;
    }
    
    // Check if we're already inside a repository
    char *existing_repo = fractyl_find_repo_root(cwd);
    if (existing_repo) {
        printf("Repository already exists at %s\n", existing_repo);
        free(existing_repo);
        return 0;
    }
    
    printf("Initializing fractyl repository in %s\n", cwd);
    
    int result = fractyl_init_repo(cwd);
    if (result != FRACTYL_OK) {
        printf("Error: Failed to initialize repository: %d\n", result);
        return 1;
    }
    
    printf("Repository initialized successfully\n");
    return 0;
}