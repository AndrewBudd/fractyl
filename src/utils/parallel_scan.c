#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>

// Ensure DT_* constants are available
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
#ifndef DT_REG  
#define DT_REG 8
#endif
#ifndef DT_DIR
#define DT_DIR 4
#endif
#include "../include/core.h"
#include "../include/fractyl.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "gitignore.h"
#include "paths.h"
#include "file_cache.h"
#include "batch_index.h"
#include "binary_index.h"

#define MAX_THREADS 8
#define MAX_STAT_THREADS 4

typedef struct work_item {
    char *dir_path;
    char *rel_path;
    struct work_item *next;
} work_item_t;

// Structure for parallel stat operations
typedef struct stat_work_item {
    const char *full_path;
    const char *rel_path;
    const index_entry_t *prev_entry;
    index_entry_t *new_entry;
    int result;  // 0=success, -1=error
    struct stat file_stat;
} stat_work_item_t;

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
        printf("Skipping large file: %s (%" PRIdMAX " bytes)\n", rel_path, (intmax_t)st->st_size);
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

// Git-style parallel stat worker (based on preload-index.c)
typedef struct {
    char **file_paths;
    const char **rel_paths;
    struct stat *stat_results;
    int *stat_success;
    size_t start_idx;
    size_t end_idx;
    int thread_id;
} preload_data_t;

static void* preload_stat_worker(void *arg) {
    preload_data_t *data = (preload_data_t*)arg;
    
    // Simple parallel stat loop like Git's preload_thread()
    for (size_t i = data->start_idx; i < data->end_idx; i++) {
        data->stat_success[i] = (lstat(data->file_paths[i], &data->stat_results[i]) == 0);
    }
    return NULL;
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
            
            // Git-style d_type optimization: avoid stat() when possible
            if (entry->d_type == DT_DIR) {
                // Directory - no stat() needed!
                enqueue_work(pool, full_path, new_rel_path);
            } else if (entry->d_type == DT_REG) {
                // Regular file - only stat() for metadata
                struct stat st;
                if (stat(full_path, &st) != 0) {
                    continue;
                }
                process_file(pool, full_path, new_rel_path, &st);
            } else if (entry->d_type == DT_UNKNOWN) {
                // Fallback to stat() when d_type is unknown
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
    
    printf("Using file metadata cache optimization\n");
    
    time_t phase_start = time(NULL);
    
    // Load file metadata cache
    file_cache_t file_cache;
    int result = file_cache_load(&file_cache, fractyl_dir, branch);
    
    printf("Cache loading took %.1fs\n", difftime(time(NULL), phase_start));
    if (result != FRACTYL_OK) {
        printf("Warning: Could not load file cache, building new cache\n");
        // Initialize empty cache to build during scan
        result = file_cache_init(&file_cache, branch);
        if (result != FRACTYL_OK) {
            printf("Warning: Could not initialize file cache, falling back to parallel scan\n");
            return scan_directory_parallel(root_path, new_index, prev_index, fractyl_dir);
        }
    }
    
    // Statistics
    int files_changed = 0;
    int files_new = 0;
    int files_unchanged = 0;
    time_t start_time = time(NULL);
    time_t last_progress = start_time;
    
    phase_start = time(NULL);
    printf("Phase 1: Fast metadata checking with file cache...\n");
    
    // Phase 1: Quick metadata check for known files
    if (prev_index) {  
        printf("Phase 1: Quick metadata check for %zu known files...\n", prev_index->count);
        
        // Track changed files for selective processing
        char **changed_files = malloc(prev_index->count * sizeof(char*));
        size_t changed_count = 0;
        
        // Optimized scanning 
        size_t no_changes_streak = 0;
        
        for (size_t i = 0; i < prev_index->count; i++) {
            const index_entry_t *prev_file = &prev_index->entries[i];
            
            // Build full file path
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", root_path, prev_file->path);
            
            // Check if file still exists and get current metadata
            struct stat current_stat;
            int stat_result = stat(full_path, &current_stat);
            
            if (stat_result == 0 && S_ISREG(current_stat.st_mode)) {
                // File exists, check against cache
                file_change_result_t cache_result = file_cache_check_file(&file_cache, 
                                                                        prev_file->path, 
                                                                        &current_stat);
                
                if (cache_result == FILE_UNCHANGED) {
                    // File unchanged - copy directly from previous index 
                    if (index_add_entry(new_index, prev_file) == FRACTYL_OK) {
                        files_unchanged++;
                        no_changes_streak++;
                    }
                } else {
                    // File changed - mark for processing in Phase 2
                    changed_files[changed_count] = strdup(prev_file->path);
                    changed_count++;
                    files_changed++;
                    no_changes_streak = 0; // Reset streak
                }
            } else {
                // File deleted or became inaccessible - remove from cache
                file_cache_remove_entry(&file_cache, prev_file->path);
                no_changes_streak = 0; // Reset streak
            }
            
            // Disable early exit for now to ensure we don't miss changes
            // TODO: Implement proper change-aware early exit later
            
            // Progress reporting (less frequent for better performance)
            if (i % 5000 == 0 || i == prev_index->count - 1) {
                time_t now = time(NULL);
                if (now - last_progress >= 2) {
                    printf("\rPhase 1: %d/%zu files checked, %d unchanged, %d changed...", 
                           (int)(i + 1), prev_index->count, files_unchanged, (int)changed_count);
                    fflush(stdout);
                    last_progress = now;
                }
            }
        }
        
        printf("\nPhase 1 complete: %d unchanged, %d changed/deleted files (%.1fs)\n", 
               files_unchanged, (int)changed_count, difftime(time(NULL), phase_start));
        
        // Phase 2: Process only changed files and find new files
        phase_start = time(NULL);
        if (changed_count > 0) {
            printf("Phase 2: Processing %zu changed files...\n", changed_count);
            
            // Process changed files
            for (size_t i = 0; i < changed_count; i++) {
                char full_path[2048];
                snprintf(full_path, sizeof(full_path), "%s/%s", root_path, changed_files[i]);
                
                struct stat file_stat;
                if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
                    // Hash the changed file
                    unsigned char hash[32];
                    result = object_store_file(full_path, fractyl_dir, hash);
                    if (result == FRACTYL_OK) {
                        // Add to index
                        index_entry_t entry;
                        memset(&entry, 0, sizeof(entry));
                        entry.path = strdup(changed_files[i]);
                        entry.mode = file_stat.st_mode;
                        entry.size = file_stat.st_size;
                        entry.mtime = file_stat.st_mtime;
                        memcpy(entry.hash, hash, 32);
                        
                        if (index_add_entry(new_index, &entry) == FRACTYL_OK) {
                            // Update cache
                            file_cache_update_entry(&file_cache, changed_files[i], &file_stat);
                        }
                        free(entry.path);
                    }
                }
                free(changed_files[i]);
            }
            printf("Phase 2: Processed %zu changed files (%.1fs)\n", 
                   changed_count, difftime(time(NULL), phase_start));
        } else {
            printf("Phase 2: No changed files to process\n");
        }
        free(changed_files);
        
        // Phase 3: Smart new file detection
        phase_start = time(NULL);
        
        // If no changes, only check for new files when absolutely necessary
        // Use git's strategy: assume no new files unless we detect directory changes
        
        // Quick heuristic: If cache is recent and no changed files, likely no new files
        time_t cache_age = time(NULL) - file_cache.cache_timestamp;
        int skip_new_file_scan = (changed_count == 0 && cache_age < 300); // 5 minutes
        
        if (skip_new_file_scan) {
            printf("Phase 3: Skipping new file scan (no changes, recent cache)\n");
        } else {
            printf("Phase 3: Quick scan for new files...\n");
            
            // Use parallel scan but with a minimal index to compare against
            index_t temp_index = {0};
            result = scan_directory_parallel(root_path, &temp_index, new_index, fractyl_dir);
            if (result == FRACTYL_OK) {
                // Add any new files found
                int new_files_added = 0;
                for (size_t i = 0; i < temp_index.count; i++) {
                    // Check if this file is already in our index
                    int found = 0;
                    for (size_t j = 0; j < new_index->count; j++) {
                        if (strcmp(temp_index.entries[i].path, new_index->entries[j].path) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        if (index_add_entry(new_index, &temp_index.entries[i]) == FRACTYL_OK) {
                            // Update cache for new file
                            char full_path[2048];
                            snprintf(full_path, sizeof(full_path), "%s/%s", root_path, temp_index.entries[i].path);
                            struct stat file_stat;
                            if (stat(full_path, &file_stat) == 0) {
                                file_cache_update_entry(&file_cache, temp_index.entries[i].path, &file_stat);
                            }
                            new_files_added++;
                            files_new++;
                        }
                    }
                }
                printf("Phase 3: Found %d new files (%.1fs)\n", 
                       new_files_added, difftime(time(NULL), phase_start));
                
                // Clean up temp index
                index_free(&temp_index);
            }
        }
    } else {
        // No previous index - do full scan
        printf("No previous index - performing full directory scan...\n");
        result = scan_directory_parallel(root_path, new_index, prev_index, fractyl_dir);
        if (result != FRACTYL_OK) {
            file_cache_free(&file_cache);
            return result;
        }
        
        // Update cache for all files
        for (size_t i = 0; i < new_index->count; i++) {
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s", root_path, new_index->entries[i].path);
            struct stat file_stat;
            if (stat(full_path, &file_stat) == 0) {
                file_cache_update_entry(&file_cache, new_index->entries[i].path, &file_stat);
            }
        }
        files_new = new_index->count;
    }
    
    // Save updated cache
    if (file_cache_save(&file_cache, fractyl_dir) != FRACTYL_OK) {
        printf("Warning: Could not save file cache\n");
    }
    
    // Clean up file cache
    file_cache_free(&file_cache);
    
    time_t end_time = time(NULL);  
    printf("File cache optimization: %d unchanged, %d changed, %d new files (%.1fs total)\n",
           files_unchanged, files_changed, files_new, 
           difftime(end_time, start_time));
    
    return FRACTYL_OK;
}

// Helper function declarations
static int scan_for_new_files_only(const char *root_path, binary_index_t *index,
                                   index_t *new_index, const char *fractyl_dir, int *new_count);
static int traverse_for_new_files(const char *current_path, const char *rel_path,
                                  binary_index_t *index, index_t *new_index, 
                                  const char *fractyl_dir, int *new_count);

// Pure stat-only scanning - no directory traversal, Git-style performance
int scan_directory_stat_only(const char *root_path, index_t *new_index, 
                             const index_t *prev_index, const char *fractyl_dir,
                             const char *branch);

// High-performance scanning using binary index and Git-style parallel stat
int scan_directory_binary(const char *root_path, index_t *new_index, 
                          const index_t *prev_index, const char *fractyl_dir,
                          const char *branch) {
    
    if (!root_path || !new_index || !fractyl_dir || !branch) {
        return FRACTYL_ERROR_INVALID_ARGS;
    }
    
    // Using high-performance binary index
    
    // Load binary index
    binary_index_t binary_index;
    int result = binary_index_load(&binary_index, fractyl_dir, branch);
    if (result != FRACTYL_OK) {
        printf("Binary index load failed (result=%d), initializing new binary index\n", result);
        // Force initialize empty binary index instead of falling back
        result = binary_index_init(&binary_index, branch);
        if (result != FRACTYL_OK) {
            printf("Failed to initialize binary index, falling back to file cache scan\n");
            return scan_directory_cached(root_path, new_index, prev_index, fractyl_dir, branch);
        }
        printf("Initialized empty binary index for branch: %s\n", branch);
    }
    
    // Binary index loaded
    
    // Statistics
    int files_unchanged = 0;
    int files_changed = 0;
    int files_new = 0;
    int files_deleted = 0;
    
    // Phase 1: Git-style parallel stat checking for all known files
    const int STAT_THREADS = 8; // Git uses up to 20, we'll start conservatively
    // Phase 1: Parallel stat checking
    
    // Create parallel stat work data similar to Git's preload_thread
    char **file_paths = malloc(binary_index.header.entry_count * sizeof(char*));
    const char **rel_paths = malloc(binary_index.header.entry_count * sizeof(char*));
    struct stat *stat_results = malloc(binary_index.header.entry_count * sizeof(struct stat));
    int *stat_success = malloc(binary_index.header.entry_count * sizeof(int));
    
    if (!file_paths || !rel_paths || !stat_results || !stat_success) {
        printf("Error: Could not allocate memory for parallel stat\n");
        free(file_paths); free(rel_paths); free(stat_results); free(stat_success);
        binary_index_free(&binary_index);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    // Populate file paths from binary index
    binary_index_iterator_t iter;
    binary_index_iterator_init(&iter, &binary_index);
    const char *rel_path;
    const binary_index_entry_t *entry;
    size_t file_count = 0;
    
    while (binary_index_iterator_next(&iter, &rel_path, &entry)) {
        // Allocate full path
        file_paths[file_count] = malloc(strlen(root_path) + strlen(rel_path) + 2);
        sprintf(file_paths[file_count], "%s/%s", root_path, rel_path);
        rel_paths[file_count] = rel_path; // Reference to binary index string
        file_count++;
    }
    
    // Create stat worker threads (like Git's preload_index)
    pthread_t threads[STAT_THREADS];
    preload_data_t thread_data[STAT_THREADS];
    size_t files_per_thread = file_count / STAT_THREADS;
    
    for (int i = 0; i < STAT_THREADS; i++) {
        thread_data[i].file_paths = file_paths;
        thread_data[i].rel_paths = rel_paths;
        thread_data[i].stat_results = stat_results;
        thread_data[i].stat_success = stat_success;
        thread_data[i].start_idx = i * files_per_thread;
        thread_data[i].end_idx = (i == STAT_THREADS - 1) ? file_count : (i + 1) * files_per_thread;
        thread_data[i].thread_id = i;
        
        pthread_create(&threads[i], NULL, preload_stat_worker, &thread_data[i]);
    }
    
    // Wait for all stat operations to complete
    for (int i = 0; i < STAT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Processing results
    
    // Build hash table for O(1) previous index lookups (fixes O(n²) bottleneck!)
    typedef struct prev_entry_hash {
        const char *path;
        const index_entry_t *entry;
        struct prev_entry_hash *next;
    } prev_entry_hash_t;
    
    const int HASH_SIZE = 65536; // Power of 2 for fast modulo
    prev_entry_hash_t **prev_hash_table = NULL;
    
    if (prev_index && prev_index->count > 0) {
        prev_hash_table = calloc(HASH_SIZE, sizeof(prev_entry_hash_t*));
        if (prev_hash_table) {
            // Build hash table from previous index
            for (size_t i = 0; i < prev_index->count; i++) {
                const index_entry_t *entry = &prev_index->entries[i];
                if (entry->path) {
                    // Simple hash function
                    uint32_t hash = 0;
                    for (const char *p = entry->path; *p; p++) {
                        hash = hash * 31 + *p;
                    }
                    int bucket = hash & (HASH_SIZE - 1);
                    
                    prev_entry_hash_t *hash_entry = malloc(sizeof(prev_entry_hash_t));
                    if (hash_entry) {
                        hash_entry->path = entry->path;
                        hash_entry->entry = entry;
                        hash_entry->next = prev_hash_table[bucket];
                        prev_hash_table[bucket] = hash_entry;
                    }
                }
            }
            // Built hash table for previous entries
        }
    }
    
    // Process stat results sequentially with O(1) hash lookups
    binary_index_iterator_init(&iter, &binary_index);
    size_t file_idx = 0;
    
    while (binary_index_iterator_next(&iter, &rel_path, &entry)) {
        if (!stat_success[file_idx] || !S_ISREG(stat_results[file_idx].st_mode)) {
            // File deleted or no longer regular file
            files_deleted++;
            file_idx++;
            continue;
        }
        
        // Check file status using binary index
        binary_file_status_t status = binary_index_check_file(&binary_index, rel_path, &stat_results[file_idx]);
        
        if (status == BINARY_FILE_UNCHANGED) {
            // File unchanged - copy from previous index if available
            const index_entry_t *prev_entry = NULL;
            if (prev_hash_table) {
                // O(1) hash table lookup instead of O(n) linear search
                uint32_t hash = 0;
                for (const char *p = rel_path; *p; p++) {
                    hash = hash * 31 + *p;
                }
                int bucket = hash & (HASH_SIZE - 1);
                
                for (prev_entry_hash_t *h = prev_hash_table[bucket]; h; h = h->next) {
                    if (strcmp(h->path, rel_path) == 0) {
                        prev_entry = h->entry;
                        break;
                    }
                }
            }
            
            if (prev_entry) {
                // Use fast direct append since we know no duplicates exist
                result = index_add_entry_direct(new_index, prev_entry);
                if (result != FRACTYL_OK) {
                    file_idx++;
                    continue; // Skip on error
                }
                
                files_unchanged++;
                file_idx++;
                continue;
            }
            // If no prev_index, need to hash file anyway
            status = BINARY_FILE_CHANGED;
        }
        
        if (status == BINARY_FILE_CHANGED) {
            // File changed - hash it
            unsigned char hash[32];
            result = object_store_file(file_paths[file_idx], fractyl_dir, hash);
            if (result == FRACTYL_OK) {
                index_entry_t new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
                new_entry.path = strdup(rel_path);
                new_entry.mode = stat_results[file_idx].st_mode;
                new_entry.size = stat_results[file_idx].st_size;
                new_entry.mtime = stat_results[file_idx].st_mtime;
                memcpy(new_entry.hash, hash, 32);
                
                // Fast direct assignment without O(n) duplicate checking
                // Use fast direct append since we know no duplicates exist
                result = index_add_entry_direct(new_index, &new_entry);
                if (result == FRACTYL_OK) {
                    // Update binary index
                    unsigned char sha1_hash[20];
                    memcpy(sha1_hash, hash, 20); // Use first 20 bytes for SHA-1 compatibility
                    binary_index_update_entry(&binary_index, rel_path, &stat_results[file_idx], sha1_hash);
                    files_changed++;
                }
                free(new_entry.path);
            }
        }
        file_idx++;
    }
    
    // Phase 1 complete
    
    // Phase 2: Quick new file detection (only if we have changes or it's been a while)
    time_t index_age = time(NULL) - binary_index.header.timestamp;
    int skip_new_scan = (files_changed == 0 && files_deleted == 0 && 
                        index_age < 300 && binary_index.header.entry_count > 0); // 5 minutes and not empty
    
    if (skip_new_scan) {
        printf("Phase 2: Skipping new file scan (no changes, recent index)\n");
    } else {
        // Phase 2: Quick scan for new files
        
        // Use lightweight directory traversal to find files not in index
        int new_files_found = scan_for_new_files_only(root_path, &binary_index, 
                                                     new_index, fractyl_dir, &files_new);
        
        if (new_files_found > 0) {
            // Found new files
        } else {
            // No new files found
        }
    }
    
    // Phase 3: Save updated binary index
    result = binary_index_save(&binary_index, fractyl_dir);
    if (result != FRACTYL_OK) {
        printf("Warning: Could not save binary index\n");
    } else {
        // Binary index saved
    }
    
    // Cleanup hash table
    if (prev_hash_table) {
        for (int i = 0; i < HASH_SIZE; i++) {
            prev_entry_hash_t *h = prev_hash_table[i];
            while (h) {
                prev_entry_hash_t *next = h->next;
                free(h);
                h = next;
            }
        }
        free(prev_hash_table);
    }
    
    // Cleanup
    for (size_t i = 0; i < file_count; i++) {
        free(file_paths[i]);
    }
    free(file_paths);
    free(rel_paths);
    free(stat_results);
    free(stat_success);
    binary_index_free(&binary_index);
    
    // Scan complete
    
    return FRACTYL_OK;
}

// Helper function to scan for new files only (not in binary index)
static int scan_for_new_files_only(const char *root_path, binary_index_t *index,
                                   index_t *new_index, const char *fractyl_dir, int *new_count) {
    *new_count = 0;
    
    // Quick directory traversal looking for files not in index
    return traverse_for_new_files(root_path, "", index, new_index, fractyl_dir, new_count);
}

// Recursive helper for new file detection
static int traverse_for_new_files(const char *current_path, const char *rel_path,
                                  binary_index_t *index, index_t *new_index, 
                                  const char *fractyl_dir, int *new_count) {
    DIR *d = opendir(current_path);
    if (!d) return FRACTYL_ERROR_IO;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".fractyl") == 0) {
            continue;
        }
        
        char full_path[2048];
        char new_rel_path[2048];
        
        snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry->d_name);
        
        if (strlen(rel_path) == 0) {
            strcpy(new_rel_path, entry->d_name);
        } else {
            snprintf(new_rel_path, sizeof(new_rel_path), "%s/%s", rel_path, entry->d_name);
        }
        
        // Check gitignore
        if (should_ignore_path(current_path, full_path, new_rel_path)) {
            continue;
        }
        
        // Git-style d_type optimization: avoid stat() when possible
        if (entry->d_type == DT_DIR) {
            // Directory - no stat() needed!
            traverse_for_new_files(full_path, new_rel_path, index, new_index, 
                                 fractyl_dir, new_count);
        } else if (entry->d_type == DT_REG) {
            // Regular file - only stat() for metadata
            struct stat st;
            if (stat(full_path, &st) != 0) {
                continue;
            }
            // Check if file is in binary index
            const binary_index_entry_t *existing = binary_index_find_entry(index, new_rel_path, NULL);
            if (!existing) {
                // New file - add it
                unsigned char hash[32];
                int result = object_store_file(full_path, fractyl_dir, hash);
                if (result == FRACTYL_OK) {
                    index_entry_t new_entry;
                    memset(&new_entry, 0, sizeof(new_entry));
                    new_entry.path = strdup(new_rel_path);
                    new_entry.mode = st.st_mode;
                    new_entry.size = st.st_size;
                    new_entry.mtime = st.st_mtime;
                    memcpy(new_entry.hash, hash, 32);
                    
                    // Use fast direct append since we know no duplicates exist
                    result = index_add_entry_direct(new_index, &new_entry);
                    if (result == FRACTYL_OK) {
                        // Also add to binary index for future runs
                        unsigned char sha1_hash[20];
                        memcpy(sha1_hash, hash, 20); // Use first 20 bytes for SHA-1 compatibility
                        binary_index_update_entry(index, new_rel_path, &st, sha1_hash);
                        (*new_count)++;
                    }
                    free(new_entry.path);
                }
            }
        } else if (entry->d_type == DT_UNKNOWN) {
            // Fallback to stat() when d_type is unknown
            struct stat st;
            if (stat(full_path, &st) != 0) {
                continue;
            }
            
            if (S_ISDIR(st.st_mode)) {
                traverse_for_new_files(full_path, new_rel_path, index, new_index, 
                                     fractyl_dir, new_count);
            } else if (S_ISREG(st.st_mode)) {
                // Skip large files
                if (st.st_size > 1024 * 1024 * 1024) {
                    printf("Skipping large file: %s (%" PRIdMAX " bytes)\n", new_rel_path, (intmax_t)st.st_size);
                    continue;
                }
                
                // Check if file is in binary index
                const binary_index_entry_t *existing = binary_index_find_entry(index, new_rel_path, NULL);
                if (!existing) {
                    // New file - add it
                    unsigned char hash[32];
                    int result = object_store_file(full_path, fractyl_dir, hash);
                    if (result == FRACTYL_OK) {
                        index_entry_t new_entry;
                        memset(&new_entry, 0, sizeof(new_entry));
                        new_entry.path = strdup(new_rel_path);
                        new_entry.mode = st.st_mode;
                        new_entry.size = st.st_size;
                        new_entry.mtime = st.st_mtime;
                        memcpy(new_entry.hash, hash, 32);
                        
                        // Use fast direct append since we know no duplicates exist
                        result = index_add_entry_direct(new_index, &new_entry);
                        if (result == FRACTYL_OK) {
                            // Also add to binary index for future runs
                            unsigned char sha1_hash[20];
                            memcpy(sha1_hash, hash, 20); // Use first 20 bytes for SHA-1 compatibility
                            binary_index_update_entry(index, new_rel_path, &st, sha1_hash);
                            (*new_count)++;
                        }
                        free(new_entry.path);
                    }
                }
            }
        }
    }
    
    closedir(d);
    return FRACTYL_OK;
}

// Pure stat-only scanning - ultimate Git-style performance
// Only stats files known to binary index, never traverses directories
int scan_directory_stat_only(const char *root_path, index_t *new_index, 
                             const index_t *prev_index, const char *fractyl_dir,
                             const char *branch) {
    
    if (!root_path || !new_index || !fractyl_dir || !branch) {
        return FRACTYL_ERROR_INVALID_ARGS;
    }
    
    // Using pure stat-only scanning
    
    time_t phase_start = time(NULL);
    
    // Load binary index - this is required for stat-only mode
    binary_index_t binary_index;
    int result = binary_index_load(&binary_index, fractyl_dir, branch);
    if (result != FRACTYL_OK) {
        printf("No binary index found, falling back to binary scan with traversal\n");
        return scan_directory_binary(root_path, new_index, prev_index, fractyl_dir, branch);
    }
    
    // Binary index loaded silently
    
    if (binary_index.header.entry_count == 0) {
        // Empty binary index, falling back to binary scan
        binary_index_free(&binary_index);
        return scan_directory_binary(root_path, new_index, prev_index, fractyl_dir, branch);
    }
    
    // Statistics
    int files_unchanged = 0;
    int files_changed = 0;
    int files_deleted = 0;
    
    phase_start = time(NULL);
    
    // Pure parallel stat checking - the fastest possible approach
    const int STAT_THREADS = 8; // Optimal for most systems
    // Pure stat-only mode scanning
    
    // Create parallel stat work data similar to Git's preload_thread
    char **file_paths = malloc(binary_index.header.entry_count * sizeof(char*));
    const char **rel_paths = malloc(binary_index.header.entry_count * sizeof(char*));
    struct stat *stat_results = malloc(binary_index.header.entry_count * sizeof(struct stat));
    int *stat_success = malloc(binary_index.header.entry_count * sizeof(int));
    
    if (!file_paths || !rel_paths || !stat_results || !stat_success) {
        printf("Error: Could not allocate memory for parallel stat\n");
        free(file_paths); free(rel_paths); free(stat_results); free(stat_success);
        binary_index_free(&binary_index);
        return FRACTYL_ERROR_OUT_OF_MEMORY;
    }
    
    // Populate file paths from binary index (no directory scanning needed!)
    binary_index_iterator_t iter;
    binary_index_iterator_init(&iter, &binary_index);
    const char *rel_path;
    const binary_index_entry_t *entry;
    size_t file_count = 0;
    
    while (binary_index_iterator_next(&iter, &rel_path, &entry)) {
        // Allocate full path
        file_paths[file_count] = malloc(strlen(root_path) + strlen(rel_path) + 2);
        sprintf(file_paths[file_count], "%s/%s", root_path, rel_path);
        rel_paths[file_count] = rel_path; // Reference to binary index string
        file_count++;
    }
    
    // Parallel stat phase
    
    // Create stat worker threads (like Git's preload_index)
    pthread_t threads[STAT_THREADS];
    preload_data_t thread_data[STAT_THREADS];
    size_t files_per_thread = file_count / STAT_THREADS;
    
    for (int i = 0; i < STAT_THREADS; i++) {
        thread_data[i].file_paths = file_paths;
        thread_data[i].rel_paths = rel_paths;
        thread_data[i].stat_results = stat_results;
        thread_data[i].stat_success = stat_success;
        thread_data[i].start_idx = i * files_per_thread;
        thread_data[i].end_idx = (i == STAT_THREADS - 1) ? file_count : (i + 1) * files_per_thread;
        thread_data[i].thread_id = i;
        
        pthread_create(&threads[i], NULL, preload_stat_worker, &thread_data[i]);
    }
    
    // Wait for all stat operations to complete
    for (int i = 0; i < STAT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Processing results
    
    // Build hash table for O(1) previous index lookups (fixes O(n²) bottleneck!)
    typedef struct prev_entry_hash {
        const char *path;
        const index_entry_t *entry;
        struct prev_entry_hash *next;
    } prev_entry_hash_t;
    
    const int HASH_SIZE = 65536; // Power of 2 for fast modulo
    prev_entry_hash_t **prev_hash_table = NULL;
    
    if (prev_index && prev_index->count > 0) {
        prev_hash_table = calloc(HASH_SIZE, sizeof(prev_entry_hash_t*));
        if (prev_hash_table) {
            // Build hash table from previous index
            for (size_t i = 0; i < prev_index->count; i++) {
                const index_entry_t *entry = &prev_index->entries[i];
                if (entry->path) {
                    // Simple hash function
                    uint32_t hash = 0;
                    for (const char *p = entry->path; *p; p++) {
                        hash = hash * 31 + *p;
                    }
                    int bucket = hash & (HASH_SIZE - 1);
                    
                    prev_entry_hash_t *hash_entry = malloc(sizeof(prev_entry_hash_t));
                    if (hash_entry) {
                        hash_entry->path = entry->path;
                        hash_entry->entry = entry;
                        hash_entry->next = prev_hash_table[bucket];
                        prev_hash_table[bucket] = hash_entry;
                    }
                }
            }
            // Built hash table for previous entries
        }
    }
    
    // Process stat results sequentially with O(1) hash lookups
    binary_index_iterator_init(&iter, &binary_index);
    size_t file_idx = 0;
    
    while (binary_index_iterator_next(&iter, &rel_path, &entry)) {
        if (!stat_success[file_idx] || !S_ISREG(stat_results[file_idx].st_mode)) {
            // File deleted or no longer regular file
            files_deleted++;
            file_idx++;
            continue;
        }
        
        // Check file status using binary index
        binary_file_status_t status = binary_index_check_file(&binary_index, rel_path, &stat_results[file_idx]);
        
        if (status == BINARY_FILE_UNCHANGED) {
            // File unchanged - copy from previous index if available
            const index_entry_t *prev_entry = NULL;
            if (prev_hash_table) {
                // O(1) hash table lookup instead of O(n) linear search
                uint32_t hash = 0;
                for (const char *p = rel_path; *p; p++) {
                    hash = hash * 31 + *p;
                }
                int bucket = hash & (HASH_SIZE - 1);
                
                for (prev_entry_hash_t *h = prev_hash_table[bucket]; h; h = h->next) {
                    if (strcmp(h->path, rel_path) == 0) {
                        prev_entry = h->entry;
                        break;
                    }
                }
            }
            
            if (prev_entry) {
                // Use fast direct append since we know no duplicates exist
                result = index_add_entry_direct(new_index, prev_entry);
                if (result != FRACTYL_OK) {
                    file_idx++;
                    continue; // Skip on error
                }
                
                files_unchanged++;
                file_idx++;
                continue;
            }
            // If no prev_index, need to hash file anyway
            status = BINARY_FILE_CHANGED;
        }
        
        if (status == BINARY_FILE_CHANGED) {
            // File changed - hash it
            unsigned char hash[32];
            result = object_store_file(file_paths[file_idx], fractyl_dir, hash);
            if (result == FRACTYL_OK) {
                index_entry_t new_entry;
                memset(&new_entry, 0, sizeof(new_entry));
                new_entry.path = strdup(rel_path);
                new_entry.mode = stat_results[file_idx].st_mode;
                new_entry.size = stat_results[file_idx].st_size;
                new_entry.mtime = stat_results[file_idx].st_mtime;
                memcpy(new_entry.hash, hash, 32);
                
                // Fast direct assignment without O(n) duplicate checking
                // Use fast direct append since we know no duplicates exist
                result = index_add_entry_direct(new_index, &new_entry);
                if (result == FRACTYL_OK) {
                    // Update binary index
                    unsigned char sha1_hash[20];
                    memcpy(sha1_hash, hash, 20); // Use first 20 bytes for SHA-1 compatibility
                    binary_index_update_entry(&binary_index, rel_path, &stat_results[file_idx], sha1_hash);
                    files_changed++;
                }
                free(new_entry.path);
            }
        }
        file_idx++;
    }
    
    // Stat-only phase complete
    
    // NOTE: Stat-only mode doesn't detect new files by design
    // This is the tradeoff for maximum performance - like git status --porcelain=v1
    // Users can run full scan when they want to detect new files
    
    // Always do new file detection unless explicitly disabled
    // This ensures we catch new files even when existing files are unchanged
    // Quick new file check
        
        phase_start = time(NULL);
        int files_new = 0;
        int new_files_found = scan_for_new_files_only(root_path, &binary_index, 
                                                     new_index, fractyl_dir, &files_new);
        
        if (new_files_found > 0) {
            printf("Found %d new files (%.3fs)\n", 
                   files_new, difftime(time(NULL), phase_start));
        } else {
            // No new files found
        }
    
    // Save updated binary index
    phase_start = time(NULL);
    result = binary_index_save(&binary_index, fractyl_dir);
    if (result != FRACTYL_OK) {
        printf("Warning: Could not save binary index\n");
    } else {
        // Binary index saved
    }
    
    // Cleanup hash table
    if (prev_hash_table) {
        for (int i = 0; i < HASH_SIZE; i++) {
            prev_entry_hash_t *h = prev_hash_table[i];
            while (h) {
                prev_entry_hash_t *next = h->next;
                free(h);
                h = next;
            }
        }
        free(prev_hash_table);
    }
    
    // Cleanup
    for (size_t i = 0; i < file_count; i++) {
        free(file_paths[i]);
    }
    free(file_paths);
    free(rel_paths);
    free(stat_results);
    free(stat_success);
    binary_index_free(&binary_index);
    
    // Stat-only scan complete
    
    return FRACTYL_OK;
}
