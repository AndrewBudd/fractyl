#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include "../include/core.h"
#include "../include/fractyl.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "gitignore.h"
#include "paths.h"
#include "directory_cache.h"

#define MAX_THREADS 8

typedef struct work_item {
    char *dir_path;
    char *rel_path;
    struct work_item *next;
} work_item_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t work_available;
    pthread_cond_t work_complete;
    
    work_item_t *queue_head;
    work_item_t *queue_tail;
    int queue_count;
    
    int active_threads;
    int total_threads;
    int shutdown;
    
    // Shared data
    index_t *new_index;
    const index_t *prev_index;
    const char *fractyl_dir;
    const char *repo_root;
    pthread_mutex_t index_mutex;
    
    // Statistics
    int files_processed;
    int dirs_processed;
    int files_changed;
    time_t last_progress;
    pthread_mutex_t stats_mutex;
} thread_pool_t;

static void enqueue_work(thread_pool_t *pool, const char *dir_path, const char *rel_path) {
    work_item_t *item = malloc(sizeof(work_item_t));
    if (!item) return;
    
    item->dir_path = strdup(dir_path);
    item->rel_path = strdup(rel_path);
    item->next = NULL;
    
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->queue_tail) {
        pool->queue_tail->next = item;
    } else {
        pool->queue_head = item;
    }
    pool->queue_tail = item;
    pool->queue_count++;
    
    pthread_cond_signal(&pool->work_available);
    pthread_mutex_unlock(&pool->mutex);
}

static work_item_t* dequeue_work(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);
    
    while (!pool->queue_head && !pool->shutdown) {
        pthread_cond_wait(&pool->work_available, &pool->mutex);
    }
    
    if (pool->shutdown && !pool->queue_head) {
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }
    
    work_item_t *item = pool->queue_head;
    if (item) {
        pool->queue_head = item->next;
        if (!pool->queue_head) {
            pool->queue_tail = NULL;
        }
        pool->queue_count--;
        pool->active_threads++;
    }
    
    pthread_mutex_unlock(&pool->mutex);
    return item;
}

static void process_file(thread_pool_t *pool, const char *full_path, const char *rel_path, 
                        const struct stat *st) {
    // Skip large files
    if (st->st_size > 1024 * 1024 * 1024) {
        pthread_mutex_lock(&pool->stats_mutex);
        printf("Skipping large file: %s (%ld bytes)\n", rel_path, st->st_size);
        pthread_mutex_unlock(&pool->stats_mutex);
        return;
    }
    
    index_entry_t entry_data;
    memset(&entry_data, 0, sizeof(entry_data));
    
    entry_data.path = strdup(rel_path);
    if (!entry_data.path) return;
    
    entry_data.mode = st->st_mode;
    entry_data.size = st->st_size;
    entry_data.mtime = st->st_mtime;
    
    // Check previous index
    const index_entry_t *prev_entry = pool->prev_index ? 
        index_find_entry(pool->prev_index, rel_path) : NULL;
    
    int file_changed = 1;
    
    if (prev_entry && prev_entry->size == st->st_size && 
        prev_entry->mtime == st->st_mtime) {
        // Unchanged - copy hash
        memcpy(entry_data.hash, prev_entry->hash, 32);
        file_changed = 0;
    } else {
        // Changed - compute hash
        if (object_store_file(full_path, pool->fractyl_dir, entry_data.hash) != FRACTYL_OK) {
            pthread_mutex_lock(&pool->stats_mutex);
            printf("Warning: Failed to store file %s\n", rel_path);
            pthread_mutex_unlock(&pool->stats_mutex);
            free(entry_data.path);
            return;
        }
        
        if (prev_entry && memcmp(entry_data.hash, prev_entry->hash, 32) == 0) {
            file_changed = 0;
        }
    }
    
    // Add to index (thread-safe)
    pthread_mutex_lock(&pool->index_mutex);
    index_add_entry(pool->new_index, &entry_data);
    pthread_mutex_unlock(&pool->index_mutex);
    
    free(entry_data.path);
    
    // Update statistics
    pthread_mutex_lock(&pool->stats_mutex);
    pool->files_processed++;
    if (file_changed) {
        pool->files_changed++;
        // Print first 20 changes
        if (pool->files_changed <= 20) {
            printf("  %s %s\n", prev_entry ? "M" : "A", rel_path);
        }
    }
    pthread_mutex_unlock(&pool->stats_mutex);
}

static void* worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t*)arg;
    
    while (1) {
        work_item_t *item = dequeue_work(pool);
        if (!item) break;  // Shutdown
        
        DIR *d = opendir(item->dir_path);
        if (!d) {
            pthread_mutex_lock(&pool->mutex);
            pool->active_threads--;
            if (pool->queue_count == 0 && pool->active_threads == 0) {
                pthread_cond_signal(&pool->work_complete);
            }
            pthread_mutex_unlock(&pool->mutex);
            
            free(item->dir_path);
            free(item->rel_path);
            free(item);
            continue;
        }
        
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0 ||
                strcmp(entry->d_name, ".fractyl") == 0) {
                continue;
            }
            
            char full_path[2048];
            char new_rel_path[2048];
            
            snprintf(full_path, sizeof(full_path), "%s/%s", 
                    item->dir_path, entry->d_name);
            
            if (strlen(item->rel_path) == 0) {
                strcpy(new_rel_path, entry->d_name);
            } else {
                snprintf(new_rel_path, sizeof(new_rel_path), "%s/%s", 
                        item->rel_path, entry->d_name);
            }
            
            // Check gitignore
            if (should_ignore_path(pool->repo_root, full_path, new_rel_path)) {
                continue;
            }
            
            struct stat st;
            if (stat(full_path, &st) != 0) {
                continue;
            }
            
            if (S_ISDIR(st.st_mode)) {
                enqueue_work(pool, full_path, new_rel_path);
            } else if (S_ISREG(st.st_mode)) {
                process_file(pool, full_path, new_rel_path, &st);
            }
        }
        
        closedir(d);
        
        // Update statistics
        pthread_mutex_lock(&pool->stats_mutex);
        pool->dirs_processed++;
        pthread_mutex_unlock(&pool->stats_mutex);
        
        free(item->dir_path);
        free(item->rel_path);
        free(item);
        
        // Check if all work is done
        pthread_mutex_lock(&pool->mutex);
        pool->active_threads--;
        if (pool->queue_count == 0 && pool->active_threads == 0) {
            pthread_cond_signal(&pool->work_complete);
        }
        pthread_mutex_unlock(&pool->mutex);
    }
    
    return NULL;
}

// Progress reporting thread  
static void* progress_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t*)arg;
    
    while (!pool->shutdown) {
        sleep(2);
        
        pthread_mutex_lock(&pool->stats_mutex);
        if (pool->files_processed > 0) {
            printf("\rScanning: %d directories, %d files, %d changes found...", 
                   pool->dirs_processed, pool->files_processed, pool->files_changed);
            fflush(stdout);
        }
        pthread_mutex_unlock(&pool->stats_mutex);
    }
    
    return NULL;
}

// Public API: Parallel directory scan
int scan_directory_parallel(const char *root_path, index_t *new_index, 
                           const index_t *prev_index, const char *fractyl_dir) {
    thread_pool_t pool = {0};
    pthread_mutex_init(&pool.mutex, NULL);
    pthread_mutex_init(&pool.index_mutex, NULL);
    pthread_mutex_init(&pool.stats_mutex, NULL);
    pthread_cond_init(&pool.work_available, NULL);
    pthread_cond_init(&pool.work_complete, NULL);
    
    pool.new_index = new_index;
    pool.prev_index = prev_index;
    pool.fractyl_dir = fractyl_dir;
    pool.repo_root = root_path;
    
    // Determine number of threads
    int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;
    if (num_threads < 2) num_threads = 2;
    
    printf("Using parallel scanning with %d threads\n", num_threads);
    
    // Create worker threads
    pthread_t threads[MAX_THREADS];
    pool.total_threads = num_threads;
    
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &pool);
    }
    
    // Create progress reporting thread
    pthread_t progress_tid;
    pthread_create(&progress_tid, NULL, progress_thread, &pool);
    
    // Start with root directory
    enqueue_work(&pool, root_path, "");
    
    // Wait for all work to complete with timeout
    pthread_mutex_lock(&pool.mutex);
    int max_wait = 30; // 30 seconds timeout
    int wait_count = 0;
    while ((pool.queue_count > 0 || pool.active_threads > 0) && wait_count < max_wait) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1 second timeout
        
        int result = pthread_cond_timedwait(&pool.work_complete, &pool.mutex, &ts);
        if (result == ETIMEDOUT) {
            wait_count++;
        }
    }
    pool.shutdown = 1;
    pthread_cond_broadcast(&pool.work_available); // Wake up all waiting threads
    pthread_mutex_unlock(&pool.mutex);
    
    // Wait for threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Stop progress thread
    pthread_join(progress_tid, NULL);
    
    // Clear progress line
    if (pool.files_processed > 50) {
        printf("\r");
        for (int i = 0; i < 80; i++) printf(" ");
        printf("\r");
    }
    
    printf("Found %zu files in %d directories\n", new_index->count, pool.dirs_processed);
    if (pool.files_changed > 20) {
        printf("  ... and %d more changes\n", pool.files_changed - 20);
    }
    
    // Cleanup
    pthread_mutex_destroy(&pool.mutex);
    pthread_mutex_destroy(&pool.index_mutex);
    pthread_mutex_destroy(&pool.stats_mutex);
    pthread_cond_destroy(&pool.work_available);
    pthread_cond_destroy(&pool.work_complete);
    
    return FRACTYL_OK;
}

// Optimized scan using directory cache - two-phase approach
int scan_directory_cached(const char *root_path, index_t *new_index, 
                         const index_t *prev_index, const char *fractyl_dir,
                         const char *branch) {
    
    if (!root_path || !new_index || !fractyl_dir) return FRACTYL_ERROR_INVALID_ARGS;
    
    printf("Using cached directory scanning optimization\n");
    
    // Load directory cache
    directory_cache_t dir_cache;
    int result = dir_cache_load(&dir_cache, fractyl_dir, branch);
    if (result != FRACTYL_OK) {
        printf("Warning: Could not load directory cache, building new cache\n");
        // Initialize empty cache to build during scan
        result = dir_cache_init(&dir_cache, branch);
        if (result != FRACTYL_OK) {
            printf("Warning: Could not initialize directory cache, falling back to parallel scan\n");
            return scan_directory_parallel(root_path, new_index, prev_index, fractyl_dir);
        }
    }
    
    // Statistics
    int files_processed = 0;
    int files_changed = 0;
    int dirs_checked = 0;
    int dirs_skipped = 0;
    int dirs_scanned = 0;
    time_t start_time = time(NULL);
    time_t last_progress = start_time;
    
    printf("Phase 1: Fast file-only check for known files...\n");
    
    // Phase 1: Fast file-only stat() check for files from previous snapshot
    if (prev_index) {
        for (size_t i = 0; i < prev_index->count; i++) {
            const index_entry_t *prev_entry = &prev_index->entries[i];
            
            // Construct full path
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", root_path, prev_entry->path);
            
            // Check if this path should be ignored
            if (should_ignore_path(root_path, full_path, prev_entry->path)) {
                continue;
            }
            
            // Direct stat() of the file (no directory traversal)
            struct stat st;
            if (stat(full_path, &st) != 0) {
                // File was deleted - we'll catch this in phase 2
                continue;
            }
            
            // Check if it's still a regular file
            if (!S_ISREG(st.st_mode)) {
                continue;
            }
            
            // Create new index entry
            index_entry_t new_entry;
            memset(&new_entry, 0, sizeof(new_entry));
            new_entry.path = strdup(prev_entry->path);
            if (!new_entry.path) {
                dir_cache_free(&dir_cache);
                return FRACTYL_ERROR_OUT_OF_MEMORY;
            }
            
            new_entry.mode = st.st_mode;
            new_entry.size = st.st_size;
            new_entry.mtime = st.st_mtime;
            
            // Quick check: size or mtime changed?
            if (prev_entry->size == st.st_size && prev_entry->mtime == st.st_mtime) {
                // File unchanged - copy hash from previous entry
                memcpy(new_entry.hash, prev_entry->hash, 32);
            } else {
                // File changed - need to hash
                result = object_store_file(full_path, fractyl_dir, new_entry.hash);
                if (result != FRACTYL_OK) {
                    printf("Warning: Failed to store file %s: %d\n", prev_entry->path, result);
                    free(new_entry.path);
                    continue;
                }
                
                files_changed++;
                
                // Print first few changes
                if (files_changed <= 20) {
                    printf("  M %s\n", prev_entry->path);
                }
            }
            
            // Add to new index
            result = index_add_entry(new_index, &new_entry);
            free(new_entry.path);
            if (result != FRACTYL_OK) {
                dir_cache_free(&dir_cache);
                return result;
            }
            
            files_processed++;
            
            // Progress reporting
            time_t now = time(NULL);
            if (now - last_progress >= 2) {
                printf("\rPhase 1: %d files checked, %d changes found...", 
                       files_processed, files_changed);
                fflush(stdout);
                last_progress = now;
            }
        }
    }
    
    if (files_processed > 0) {
        printf("\rPhase 1: %d files checked, %d changes found\n", 
               files_processed, files_changed);
    }
    
    printf("Phase 2: Selective directory traversal...\n");
    
    // Phase 2: Selective directory traversal using cache
    // We need to recursively check directories, but skip unchanged ones
    typedef struct dir_work {
        char *path;
        char *rel_path;
        struct dir_work *next;
    } dir_work_t;
    
    // Start with root directory
    dir_work_t *work_queue = malloc(sizeof(dir_work_t));
    work_queue->path = strdup(root_path);
    work_queue->rel_path = strdup("");
    work_queue->next = NULL;
    
    while (work_queue) {
        dir_work_t *current = work_queue;
        work_queue = work_queue->next;
        
        // Check directory mtime against cache
        struct stat dir_stat;
        if (stat(current->path, &dir_stat) != 0) {
            free(current->path);
            free(current->rel_path);
            free(current);
            continue;
        }
        
        dirs_checked++;
        
        // Check cache to see if we need to scan this directory
        dir_scan_result_t scan_result = dir_cache_check_directory(&dir_cache, 
                                                                  current->rel_path, 
                                                                  dir_stat.st_mtime, 
                                                                  -1); // We'll count files during scan
        
        if (scan_result == DIR_UNCHANGED) {
            // Directory unchanged - skip traversal
            dirs_skipped++;
            free(current->path);
            free(current->rel_path);
            free(current);
            continue;
        }
        
        // Directory changed or new - need to scan it
        dirs_scanned++;
        
        DIR *d = opendir(current->path);
        if (!d) {
            free(current->path);
            free(current->rel_path);
            free(current);
            continue;
        }
        
        int file_count = 0;
        struct dirent *entry;
        
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0 ||
                strcmp(entry->d_name, ".fractyl") == 0) {
                continue;
            }
            
            char full_path[2048];
            char rel_path[2048];
            
            snprintf(full_path, sizeof(full_path), "%s/%s", current->path, entry->d_name);
            if (strlen(current->rel_path) == 0) {
                strcpy(rel_path, entry->d_name);
            } else {
                snprintf(rel_path, sizeof(rel_path), "%s/%s", current->rel_path, entry->d_name);
            }
            
            // Check if this path should be ignored
            if (should_ignore_path(root_path, full_path, rel_path)) {
                continue;
            }
            
            struct stat st;
            if (stat(full_path, &st) != 0) {
                continue;
            }
            
            if (S_ISDIR(st.st_mode)) {
                // Add directory to work queue for later processing
                dir_work_t *new_work = malloc(sizeof(dir_work_t));
                new_work->path = strdup(full_path);
                new_work->rel_path = strdup(rel_path);
                new_work->next = work_queue;
                work_queue = new_work;
            } else if (S_ISREG(st.st_mode)) {
                file_count++;
                
                // Skip large files
                if (st.st_size > 1024 * 1024 * 1024) {
                    printf("Skipping large file: %s (%ld bytes)\n", rel_path, st.st_size);
                    continue;
                }
                
                // Check if this file is already in our index (from phase 1)
                const index_entry_t *existing = index_find_entry(new_index, rel_path);
                if (existing) {
                    // File already processed in phase 1
                    continue;
                }
                
                // New file - process it
                index_entry_t new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
                
                new_entry.path = strdup(rel_path);
                if (!new_entry.path) {
                    break;
                }
                
                new_entry.mode = st.st_mode;
                new_entry.size = st.st_size;
                new_entry.mtime = st.st_mtime;
                
                // Hash and store the file
                result = object_store_file(full_path, fractyl_dir, new_entry.hash);
                if (result != FRACTYL_OK) {
                    printf("Warning: Failed to store file %s: %d\n", rel_path, result);
                    free(new_entry.path);
                    continue;
                }
                
                // Add to index
                result = index_add_entry(new_index, &new_entry);
                free(new_entry.path);
                if (result != FRACTYL_OK) {
                    break;
                }
                
                files_changed++;
                if (files_changed <= 20) {
                    printf("  A %s\n", rel_path);
                }
            }
        }
        
        closedir(d);
        
        // Update directory cache with new mtime and file count
        dir_cache_update_entry(&dir_cache, current->rel_path, dir_stat.st_mtime, file_count);
        
        free(current->path);
        free(current->rel_path);
        free(current);
        
        // Progress reporting
        time_t now = time(NULL);
        if (now - last_progress >= 2) {
            printf("\rPhase 2: %d dirs checked, %d dirs skipped, %d dirs scanned...", 
                   dirs_checked, dirs_skipped, dirs_scanned);
            fflush(stdout);
            last_progress = now;
        }
    }
    
    // Clean up any remaining work items
    while (work_queue) {
        dir_work_t *current = work_queue;
        work_queue = work_queue->next;
        free(current->path);
        free(current->rel_path);
        free(current);
    }
    
    printf("\rPhase 2: %d dirs checked, %d dirs skipped, %d dirs scanned\n", 
           dirs_checked, dirs_skipped, dirs_scanned);
    
    // Save updated cache
    result = dir_cache_save(&dir_cache, fractyl_dir);
    if (result != FRACTYL_OK) {
        printf("Warning: Could not save directory cache\n");
    }
    
    dir_cache_free(&dir_cache);
    
    time_t end_time = time(NULL);
    printf("Cache optimization: %d files, %d changes, %d dirs skipped (%.1fs)\n",
           files_processed + (int)new_index->count - files_processed,
           files_changed, dirs_skipped, difftime(end_time, start_time));
    
    if (files_changed > 20) {
        printf("  ... and %d more changes\n", files_changed - 20);
    }
    
    return FRACTYL_OK;
}