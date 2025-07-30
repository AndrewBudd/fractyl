#ifndef FRACTYL_DAEMON_STANDALONE_H
#define FRACTYL_DAEMON_STANDALONE_H

#include <stdint.h>
#include <sys/types.h>

// Daemon configuration
typedef struct {
    char *fractyl_dir;
    char *repo_root;
    uint32_t snapshot_interval;
    int running;
    pid_t pid;
} daemon_config_t;

// Daemon state
typedef struct {
    daemon_config_t config;
    char *pid_file_path;
    char *git_branch;
} daemon_state_t;

// Initialize daemon
int daemon_init(daemon_state_t *daemon, const char *repo_root);

// Start daemon in background
int daemon_start_background(daemon_state_t *daemon);

// Stop daemon
int daemon_stop(const char *fractyl_dir);

// Check daemon status
int daemon_status(const char *fractyl_dir, pid_t *daemon_pid);

// Set snapshot interval
void daemon_set_interval(daemon_state_t *daemon, uint32_t seconds);

// Cleanup daemon resources
void daemon_cleanup(daemon_state_t *daemon);

#endif // FRACTYL_DAEMON_STANDALONE_H