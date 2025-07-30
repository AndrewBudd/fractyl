#include "daemon_standalone.h"
#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/paths.h"
#include "../utils/git.h"
#include "../utils/lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

// Global state for signal handling in daemon process
static volatile int g_daemon_running = 0;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[DAEMON] Received shutdown signal\n");
    g_daemon_running = 0;
}

// Check if a PID is still running
static int is_process_running(pid_t pid) {
    if (pid <= 0) return 0;
    
    // Use kill(pid, 0) to check if process exists
    if (kill(pid, 0) == 0) {
        return 1; // Process exists
    } else {
        return (errno != ESRCH); // ESRCH means process doesn't exist
    }
}

// Read PID from PID file
static pid_t read_pid_file(const char *pid_file_path) {
    FILE *f = fopen(pid_file_path, "r");
    if (!f) return 0;
    
    pid_t pid = 0;
    if (fscanf(f, "%d", &pid) != 1) {
        pid = 0;
    }
    fclose(f);
    return pid;
}

// Write PID to PID file
static int write_pid_file(const char *pid_file_path, pid_t pid) {
    FILE *f = fopen(pid_file_path, "w");
    if (!f) {
        printf("[DAEMON] Error: Cannot create PID file %s: %s\n", 
               pid_file_path, strerror(errno));
        return -1;
    }
    
    fprintf(f, "%d\n", pid);
    fclose(f);
    return 0;
}

// Initialize daemon
int daemon_init(daemon_state_t *daemon, const char *repo_root) {
    if (!daemon || !repo_root) return -1;
    
    memset(daemon, 0, sizeof(daemon_state_t));
    
    // Set up configuration
    daemon->config.repo_root = strdup(repo_root);
    
    // Build .fractyl directory path
    char fractyl_path[2048];
    snprintf(fractyl_path, sizeof(fractyl_path), "%s/.fractyl", repo_root);
    daemon->config.fractyl_dir = strdup(fractyl_path);
    
    // Build PID file path
    char pid_file_path[2048];
    snprintf(pid_file_path, sizeof(pid_file_path), "%s/daemon.pid", fractyl_path);
    daemon->pid_file_path = strdup(pid_file_path);
    
    // Default interval: 3 minutes (180 seconds)
    daemon->config.snapshot_interval = 180;
    daemon->config.running = 0;
    daemon->config.pid = getpid();
    
    // Get current git branch
    daemon->git_branch = paths_get_current_branch(repo_root);
    
    return 0;
}

// Attempt to create a snapshot in daemon process
static void attempt_snapshot(daemon_state_t *daemon) {
    (void)daemon; // Currently unused
    
    time_t now = time(NULL);
    printf("[DAEMON] %s", ctime(&now)); // ctime includes newline
    printf("[DAEMON] Attempting periodic snapshot...\n");
    fflush(stdout);
    
    // Create auto-generated snapshot description
    struct tm *tm_info = localtime(&now);
    char description[128];
    snprintf(description, sizeof(description), "Auto-snapshot %04d-%02d-%02d %02d:%02d:%02d",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    
    // Create snapshot using existing command
    // cmd_snapshot handles its own locking and will display appropriate messages
    char *snapshot_args[] = {"frac", "snapshot", "-m", description, NULL};
    int result = cmd_snapshot(4, snapshot_args);
    
    if (result == 0) {
        printf("[DAEMON] ✅ Snapshot created: %s\n", description);
    } else {
        // This is normal - it means no changes were detected or lock couldn't be acquired
        printf("[DAEMON] ⏭️  No snapshot created (no changes or operation in progress)\n");
    }
    fflush(stdout);
}

// Main daemon loop (runs in child process)
static void daemon_main_loop(daemon_state_t *daemon) {
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_daemon_running = 1;
    
    // Log startup information with timestamp
    time_t start_time = time(NULL);
    printf("[DAEMON] Started at %s", ctime(&start_time));
    printf("[DAEMON] PID: %d\n", getpid());
    printf("[DAEMON] Repository: %s\n", daemon->config.repo_root);
    printf("[DAEMON] Snapshot interval: %u seconds\n", daemon->config.snapshot_interval);
    printf("[DAEMON] Log file: %s/daemon.log\n", daemon->config.fractyl_dir);
    fflush(stdout);
    
    // Main daemon loop - attempt snapshots at regular intervals
    while (g_daemon_running) {
        attempt_snapshot(daemon);
        
        // Sleep for the specified interval, but check for shutdown signal periodically
        uint32_t remaining = daemon->config.snapshot_interval;
        while (remaining > 0 && g_daemon_running) {
            uint32_t sleep_time = remaining > 10 ? 10 : remaining;
            sleep(sleep_time);
            remaining -= sleep_time;
        }
    }
    
    printf("[DAEMON] Main loop exited\n");
}

// Start daemon in background
int daemon_start_background(daemon_state_t *daemon) {
    if (!daemon) return -1;
    
    // Check if another daemon is already running
    pid_t existing_pid = read_pid_file(daemon->pid_file_path);
    if (existing_pid > 0 && is_process_running(existing_pid)) {
        printf("Error: Daemon already running (PID: %d)\n", existing_pid);
        printf("PID file: %s\n", daemon->pid_file_path);
        return -1;
    }
    
    // Remove stale PID file if it exists
    if (existing_pid > 0) {
        printf("Removing stale PID file (PID %d no longer running)\n", existing_pid);
        unlink(daemon->pid_file_path);
    }
    
    // Fork to create daemon process
    pid_t daemon_pid = fork();
    if (daemon_pid < 0) {
        printf("Error: Failed to fork daemon process: %s\n", strerror(errno));
        return -1;
    }
    
    if (daemon_pid == 0) {
        // Child process - become daemon
        
        // Create new session and process group
        if (setsid() < 0) {
            exit(1);
        }
        
        // Change to repository root directory (not system root) to maintain context
        if (chdir(daemon->config.repo_root) < 0) {
            exit(1);
        }
        
        // Close standard input
        close(STDIN_FILENO);
        
        // Redirect stdin to /dev/null
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            close(null_fd);
        }
        
        // Redirect stdout and stderr to daemon log file
        char log_path[2048];
        snprintf(log_path, sizeof(log_path), "%s/daemon.log", daemon->config.fractyl_dir);
        int log_fd = open(log_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        
        // Update daemon config with new PID
        daemon->config.pid = getpid();
        
        // Write PID file
        if (write_pid_file(daemon->pid_file_path, daemon->config.pid) != 0) {
            exit(1);
        }
        
        // Run daemon main loop
        daemon_main_loop(daemon);
        
        // Cleanup on exit
        unlink(daemon->pid_file_path);
        exit(0);
    } else {
        // Parent process
        printf("Daemon started successfully (PID: %d)\n", daemon_pid);
        return 0;
    }
}

// Stop daemon
int daemon_stop(const char *fractyl_dir) {
    if (!fractyl_dir) return -1;
    
    char pid_file_path[2048];
    snprintf(pid_file_path, sizeof(pid_file_path), "%s/daemon.pid", fractyl_dir);
    
    pid_t daemon_pid = read_pid_file(pid_file_path);
    if (daemon_pid <= 0) {
        printf("No daemon running\n");
        return 0;
    }
    
    if (!is_process_running(daemon_pid)) {
        printf("Daemon PID %d is not running, removing stale PID file\n", daemon_pid);
        unlink(pid_file_path);
        return 0;
    }
    
    printf("Stopping daemon (PID: %d)...\n", daemon_pid);
    
    // Send SIGTERM for graceful shutdown
    if (kill(daemon_pid, SIGTERM) < 0) {
        printf("Error: Failed to send SIGTERM to daemon: %s\n", strerror(errno));
        return -1;
    }
    
    // Wait for graceful shutdown (up to 10 seconds)
    for (int i = 0; i < 100; i++) {
        if (!is_process_running(daemon_pid)) {
            printf("Daemon stopped successfully\n");
            return 0;
        }
        usleep(100000); // 100ms
    }
    
    // Force kill if still running
    printf("Daemon didn't stop gracefully, forcing shutdown...\n");
    if (kill(daemon_pid, SIGKILL) < 0) {
        printf("Error: Failed to send SIGKILL to daemon: %s\n", strerror(errno));
        return -1;
    }
    
    printf("Daemon force-stopped\n");
    return 0;
}

// Check daemon status
int daemon_status(const char *fractyl_dir, pid_t *daemon_pid) {
    if (!fractyl_dir) return -1;
    
    char pid_file_path[2048];
    snprintf(pid_file_path, sizeof(pid_file_path), "%s/daemon.pid", fractyl_dir);
    
    pid_t pid = read_pid_file(pid_file_path);
    if (daemon_pid) *daemon_pid = pid;
    
    if (pid <= 0) {
        return 0; // Not running
    }
    
    if (is_process_running(pid)) {
        return 1; // Running
    } else {
        // Stale PID file
        return 0; // Not running
    }
}

// Set snapshot interval
void daemon_set_interval(daemon_state_t *daemon, uint32_t seconds) {
    if (!daemon) return;
    
    if (seconds < 30) {
        printf("Warning: Minimum interval is 30 seconds, adjusting\n");
        seconds = 30;
    }
    
    daemon->config.snapshot_interval = seconds;
}

// Cleanup daemon resources
void daemon_cleanup(daemon_state_t *daemon) {
    if (!daemon) return;
    
    // Free configuration
    free(daemon->config.repo_root);
    free(daemon->config.fractyl_dir);
    free(daemon->git_branch);
    free(daemon->pid_file_path);
    
    memset(daemon, 0, sizeof(daemon_state_t));
}