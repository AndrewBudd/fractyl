#include "lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

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

// Read PID from lock file
static pid_t read_lock_pid(const char *lock_path) {
    FILE *f = fopen(lock_path, "r");
    if (!f) return 0;
    
    pid_t pid = 0;
    if (fscanf(f, "%d", &pid) != 1) {
        pid = 0;
    }
    fclose(f);
    return pid;
}

// Acquire exclusive lock for fractyl operations
int fractyl_lock_acquire(const char *fractyl_dir, fractyl_lock_t *lock) {
    if (!fractyl_dir || !lock) return -1;
    
    // Initialize lock structure
    memset(lock, 0, sizeof(fractyl_lock_t));
    lock->fd = -1;
    
    // Build lock file path
    char lock_path[2048];
    snprintf(lock_path, sizeof(lock_path), "%s/fractyl.lock", fractyl_dir);
    lock->lock_path = strdup(lock_path);
    
    // Try to create lock file exclusively
    lock->fd = open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (lock->fd >= 0) {
        // Successfully created lock file
        lock->holder_pid = getpid();
        
        // Write our PID to the lock file
        dprintf(lock->fd, "%d\n", lock->holder_pid);
        fsync(lock->fd);
        
        return 0; // Lock acquired successfully
    }
    
    // Lock file already exists - check if holder is still running
    if (errno == EEXIST) {
        pid_t existing_pid = read_lock_pid(lock_path);
        if (existing_pid > 0 && is_process_running(existing_pid)) {
            // Lock is held by running process
            return -1;
        } else {
            // Stale lock file - remove it and try again
            printf("[LOCK] Removing stale lock file (PID %d no longer running)\n", existing_pid);
            unlink(lock_path);
            
            // Retry lock acquisition
            lock->fd = open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
            if (lock->fd >= 0) {
                lock->holder_pid = getpid();
                dprintf(lock->fd, "%d\n", lock->holder_pid);
                fsync(lock->fd);
                return 0;
            }
        }
    }
    
    // Failed to acquire lock
    free(lock->lock_path);
    lock->lock_path = NULL;
    return -1;
}

// Release the lock
void fractyl_lock_release(fractyl_lock_t *lock) {
    if (!lock) return;
    
    if (lock->fd >= 0) {
        close(lock->fd);
        lock->fd = -1;
    }
    
    if (lock->lock_path) {
        // Only remove lock file if we're the ones who created it
        pid_t file_pid = read_lock_pid(lock->lock_path);
        if (file_pid == getpid()) {
            unlink(lock->lock_path);
        }
        
        free(lock->lock_path);
        lock->lock_path = NULL;
    }
    
    lock->holder_pid = 0;
}

// Check if a lock is currently held (without trying to acquire)
int fractyl_lock_check(const char *fractyl_dir, pid_t *holder_pid) {
    if (!fractyl_dir) return -1;
    
    char lock_path[2048];
    snprintf(lock_path, sizeof(lock_path), "%s/fractyl.lock", fractyl_dir);
    
    // Check if lock file exists
    struct stat st;
    if (stat(lock_path, &st) != 0) {
        if (holder_pid) *holder_pid = 0;
        return 0; // No lock file = not locked
    }
    
    // Read the PID from lock file
    pid_t pid = read_lock_pid(lock_path);
    if (pid <= 0) {
        if (holder_pid) *holder_pid = 0;
        return 0; // Invalid PID = not locked
    }
    
    // Check if the process is still running
    if (is_process_running(pid)) {
        if (holder_pid) *holder_pid = pid;
        return 1; // Locked by running process
    } else {
        // Stale lock file
        if (holder_pid) *holder_pid = 0;
        return 0; // Stale lock = not locked
    }
}

// Wait for lock to become available, then acquire it
int fractyl_lock_wait_acquire(const char *fractyl_dir, fractyl_lock_t *lock, int timeout_seconds) {
    if (!fractyl_dir || !lock) return -1;
    
    int attempts = 0;
    int max_attempts = timeout_seconds * 10; // Check every 100ms
    
    while (attempts < max_attempts) {
        // Try to acquire lock
        int result = fractyl_lock_acquire(fractyl_dir, lock);
        if (result == 0) {
            return 0; // Successfully acquired
        }
        
        // Check who's holding the lock
        pid_t holder_pid;
        int lock_status = fractyl_lock_check(fractyl_dir, &holder_pid);
        if (lock_status <= 0) {
            // Lock was released, try again immediately
            continue;
        }
        
        // Lock is held, wait a bit
        if (attempts == 0) {
            printf("[LOCK] Waiting for operation in progress (PID %d)...\n", holder_pid);
        }
        
        usleep(100000); // 100ms
        attempts++;
    }
    
    printf("[LOCK] Timeout waiting for lock after %d seconds\n", timeout_seconds);
    return -1; // Timeout
}