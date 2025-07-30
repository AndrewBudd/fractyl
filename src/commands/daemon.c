#include "../include/commands.h"
#include "../include/core.h"
#include "../daemon/daemon_standalone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void print_daemon_usage(void) {
    printf("Usage: frac daemon <command> [options]\n\n");
    printf("Commands:\n");
    printf("  start     Start the daemon\n");
    printf("  stop      Stop the daemon\n");
    printf("  status    Show daemon status\n");
    printf("  restart   Restart the daemon\n\n");
    printf("Options for 'start':\n");
    printf("  -i, --interval SECONDS    Set snapshot interval in seconds (default: 180)\n\n");
    printf("Examples:\n");
    printf("  frac daemon start         # Start daemon with 3-minute intervals\n");
    printf("  frac daemon start -i 60   # Start daemon with 1-minute intervals\n");
    printf("  frac daemon stop          # Stop the daemon\n");
    printf("  frac daemon status        # Check if daemon is running\n");
}

int cmd_daemon(int argc, char **argv) {
    if (argc < 3) {
        print_daemon_usage();
        return 1;
    }
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    const char *command = argv[2];
    
    if (strcmp(command, "start") == 0) {
        // Parse start options
        uint32_t interval_seconds = 0;
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval") == 0) {
                if (i + 1 < argc) {
                    interval_seconds = (uint32_t)atoi(argv[++i]);
                    if (interval_seconds == 0) {
                        printf("Error: Invalid interval value\n");
                        free(repo_root);
                        return 1;
                    }
                } else {
                    printf("Error: --interval requires a value\n");
                    free(repo_root);
                    return 1;
                }
            } else {
                printf("Error: Unknown option '%s'\n", argv[i]);
                print_daemon_usage();
                free(repo_root);
                return 1;
            }
        }
        
        // Initialize and start daemon
        daemon_state_t daemon;
        int result = daemon_init(&daemon, repo_root);
        if (result != 0) {
            printf("Error: Failed to initialize daemon\n");
            free(repo_root);
            return 1;
        }
        
        // Set custom interval if specified
        if (interval_seconds > 0) {
            daemon_set_interval(&daemon, interval_seconds);
        }
        
        printf("Starting Fractyl daemon...\n");
        printf("Repository: %s\n", repo_root);
        printf("Snapshot interval: %u seconds\n", daemon.config.snapshot_interval);
        
        result = daemon_start_background(&daemon);
        
        daemon_cleanup(&daemon);
        free(repo_root);
        return result;
        
    } else if (strcmp(command, "stop") == 0) {
        int result = daemon_stop(fractyl_dir);
        free(repo_root);
        return result;
        
    } else if (strcmp(command, "status") == 0) {
        pid_t daemon_pid;
        int is_running = daemon_status(fractyl_dir, &daemon_pid);
        
        if (is_running) {
            printf("Daemon is running (PID: %d)\n", daemon_pid);
            printf("Repository: %s\n", repo_root);
        } else {
            printf("Daemon is not running\n");
        }
        
        free(repo_root);
        return 0;
        
    } else if (strcmp(command, "restart") == 0) {
        printf("Restarting Fractyl daemon...\n");
        
        // Stop first
        daemon_stop(fractyl_dir);
        
        // Parse restart options (same as start)
        uint32_t interval_seconds = 0;
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interval") == 0) {
                if (i + 1 < argc) {
                    interval_seconds = (uint32_t)atoi(argv[++i]);
                    if (interval_seconds == 0) {
                        printf("Error: Invalid interval value\n");
                        free(repo_root);
                        return 1;
                    }
                } else {
                    printf("Error: --interval requires a value\n");
                    free(repo_root);
                    return 1;
                }
            }
        }
        
        // Initialize and start daemon
        daemon_state_t daemon;
        int result = daemon_init(&daemon, repo_root);
        if (result != 0) {
            printf("Error: Failed to initialize daemon\n");
            free(repo_root);
            return 1;
        }
        
        // Set custom interval if specified
        if (interval_seconds > 0) {
            daemon_set_interval(&daemon, interval_seconds);
        }
        
        printf("Repository: %s\n", repo_root);
        printf("Snapshot interval: %u seconds\n", daemon.config.snapshot_interval);
        
        result = daemon_start_background(&daemon);
        
        daemon_cleanup(&daemon);
        free(repo_root);
        return result;
        
    } else {
        printf("Error: Unknown daemon command '%s'\n", command);
        print_daemon_usage();
        free(repo_root);
        return 1;
    }
}