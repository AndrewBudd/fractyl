#include "test_helpers.h"
#include "../src/include/commands.h"
#include <sys/wait.h>

// Global path to frac executable - will be set by test files
char* test_frac_executable = NULL;

test_repo_t* test_repo_create(const char* name) {
    test_repo_t* repo = malloc(sizeof(test_repo_t));
    if (!repo) return NULL;
    
    // Generate unique test directory name with timestamp
    time_t now = time(NULL);
    snprintf(repo->path, sizeof(repo->path), "/tmp/fractyl_test_%s_%ld_%d", 
             name, now, getpid());
    
    // Save current working directory
    repo->original_cwd = getcwd(NULL, 0);
    
    // Create test directory
    if (mkdir(repo->path, 0755) != 0) {
        free(repo->original_cwd);
        free(repo);
        return NULL;
    }
    
    return repo;
}

void test_repo_destroy(test_repo_t* repo) {
    if (!repo) return;
    
    // Restore original directory
    if (repo->original_cwd) {
        chdir(repo->original_cwd);
        free(repo->original_cwd);
    }
    
    // Remove test directory
    test_dir_remove_recursive(repo->path);
    
    free(repo);
}

int test_repo_enter(test_repo_t* repo) {
    if (!repo) return -1;
    return chdir(repo->path);
}

int test_file_create(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    
    if (content) {
        if (fputs(content, f) == EOF) {
            fclose(f);
            return -1;
        }
    }
    
    fclose(f);
    return 0;
}

int test_file_modify(const char* path, const char* content) {
    return test_file_create(path, content); // Same implementation
}

int test_file_remove(const char* path) {
    return unlink(path);
}

int test_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char* test_file_read(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, f);
    content[read_size] = '\0';
    
    fclose(f);
    return content;
}

int test_dir_create(const char* path) {
    return mkdir(path, 0755);
}

int test_dir_remove_recursive(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) return -1;
    
    struct dirent* entry;
    char full_path[1024];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                test_dir_remove_recursive(full_path);
            } else {
                unlink(full_path);
            }
        }
    }
    
    closedir(dir);
    return rmdir(path);
}

int test_dir_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

test_command_result_t* test_run_command(const char* command, char* const argv[]) {
    test_command_result_t* result = malloc(sizeof(test_command_result_t));
    if (!result) return NULL;
    
    result->stdout_content = NULL;
    result->stderr_content = NULL;
    result->exit_code = -1;
    
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        free(result);
        return NULL;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        execv(command, argv);
        exit(127);
    } else if (pid > 0) {
        // Parent process
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Read stdout
        char buffer[4096];
        ssize_t bytes_read;
        size_t stdout_size = 0, stderr_size = 0;
        
        // Read stdout
        while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            result->stdout_content = realloc(result->stdout_content, stdout_size + bytes_read + 1);
            memcpy(result->stdout_content + stdout_size, buffer, bytes_read);
            stdout_size += bytes_read;
        }
        if (result->stdout_content) {
            result->stdout_content[stdout_size] = '\0';
        }
        
        // Read stderr
        while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            result->stderr_content = realloc(result->stderr_content, stderr_size + bytes_read + 1);
            memcpy(result->stderr_content + stderr_size, buffer, bytes_read);
            stderr_size += bytes_read;
        }
        if (result->stderr_content) {
            result->stderr_content[stderr_size] = '\0';
        }
        
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        // Wait for child
        int status;
        waitpid(pid, &status, 0);
        result->exit_code = WEXITSTATUS(status);
    } else {
        // Fork failed
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        free(result);
        return NULL;
    }
    
    return result;
}

void test_command_result_free(test_command_result_t* result) {
    if (!result) return;
    free(result->stdout_content);
    free(result->stderr_content);
    free(result);
}

int test_fractyl_init(test_repo_t* repo) {
    (void)repo; // Suppress unused parameter warning
    char* argv[] = {test_frac_executable, "init", NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    int exit_code = result ? result->exit_code : -1;
    test_command_result_free(result);
    return exit_code;
}

int test_fractyl_snapshot(test_repo_t* repo, const char* message) {
    (void)repo; // Suppress unused parameter warning
    char* argv[] = {test_frac_executable, "snapshot", "-m", (char*)message, NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    int exit_code = result ? result->exit_code : -1;
    test_command_result_free(result);
    return exit_code;
}

int test_fractyl_restore(test_repo_t* repo, const char* snapshot_id) {
    (void)repo; // Suppress unused parameter warning
    char* argv[] = {test_frac_executable, "restore", (char*)snapshot_id, NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    int exit_code = result ? result->exit_code : -1;
    test_command_result_free(result);
    return exit_code;
}

char* test_fractyl_list(test_repo_t* repo) {
    (void)repo; // Suppress unused parameter warning
    char* argv[] = {test_frac_executable, "list", NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    char* output = NULL;
    if (result && result->stdout_content) {
        output = strdup(result->stdout_content);
    }
    test_command_result_free(result);
    return output;
}

char* test_fractyl_get_latest_snapshot_id(test_repo_t* repo) {
    char* list_output = test_fractyl_list(repo);
    if (!list_output) return NULL;
    
    // Skip header line "Snapshot History:" and find first snapshot line
    char* line = list_output;
    char* newline = strchr(line, '\n');
    if (newline) {
        line = newline + 1; // Move to next line
        newline = strchr(line, '\n');
        if (newline) *newline = '\0'; // Terminate at end of snapshot line
    }
    
    // Extract first 8 characters as snapshot ID
    if (strlen(line) >= 8) {
        char* snapshot_id = malloc(9);
        strncpy(snapshot_id, line, 8);
        snapshot_id[8] = '\0';
        free(list_output);
        return snapshot_id;
    }
    
    free(list_output);
    return NULL;
}