#ifndef FRACTYL_LOCK_H
#define FRACTYL_LOCK_H

#include <sys/types.h>

// Lock handle
typedef struct {
    int fd;                    // File descriptor for lock file
    char *lock_path;          // Path to lock file
    pid_t holder_pid;         // PID of process holding lock
} fractyl_lock_t;

// Acquire exclusive lock for fractyl operations
// Returns 0 on success, negative on error
int fractyl_lock_acquire(const char *fractyl_dir, fractyl_lock_t *lock);

// Release the lock
void fractyl_lock_release(fractyl_lock_t *lock);

// Check if a lock is currently held (without trying to acquire)
// Returns 1 if locked, 0 if free, negative on error
int fractyl_lock_check(const char *fractyl_dir, pid_t *holder_pid);

// Wait for lock to become available, then acquire it
// Returns 0 on success, negative on timeout/error
int fractyl_lock_wait_acquire(const char *fractyl_dir, fractyl_lock_t *lock, int timeout_seconds);

#endif // FRACTYL_LOCK_H