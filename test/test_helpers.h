#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

// Global path to frac executable
extern char* test_frac_executable;

// Test repository management
typedef struct {
    char path[512];
    char *original_cwd;
} test_repo_t;

// Initialize a test repository in /tmp
test_repo_t* test_repo_create(const char* name);

// Clean up test repository and restore original directory
void test_repo_destroy(test_repo_t* repo);

// Change to test repository directory
int test_repo_enter(test_repo_t* repo);

// File manipulation helpers
int test_file_create(const char* path, const char* content);
int test_file_modify(const char* path, const char* content);
int test_file_remove(const char* path);
int test_file_exists(const char* path);
char* test_file_read(const char* path);

// Directory helpers
int test_dir_create(const char* path);
int test_dir_remove_recursive(const char* path);
int test_dir_exists(const char* path);

// Command execution helpers
typedef struct {
    int exit_code;
    char* stdout_content;
    char* stderr_content;
} test_command_result_t;

test_command_result_t* test_run_command(const char* command, char* const argv[]);
void test_command_result_free(test_command_result_t* result);

// Fractyl-specific helpers
int test_fractyl_init(test_repo_t* repo);
int test_fractyl_snapshot(test_repo_t* repo, const char* message);
int test_fractyl_restore(test_repo_t* repo, const char* snapshot_id);
char* test_fractyl_list(test_repo_t* repo);
char* test_fractyl_get_latest_snapshot_id(test_repo_t* repo);

// Assertion helpers for tests
#define TEST_ASSERT_FILE_EXISTS(path) \
    TEST_ASSERT_TRUE_MESSAGE(test_file_exists(path), "File should exist: " path)

#define TEST_ASSERT_FILE_NOT_EXISTS(path) \
    TEST_ASSERT_FALSE_MESSAGE(test_file_exists(path), "File should not exist: " path)

#define TEST_ASSERT_FILE_CONTENT(path, expected) do { \
    char* content = test_file_read(path); \
    TEST_ASSERT_NOT_NULL_MESSAGE(content, "Could not read file: " path); \
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, content, "File content mismatch: " path); \
    free(content); \
} while(0)

#define TEST_ASSERT_DIR_EXISTS(path) \
    TEST_ASSERT_TRUE_MESSAGE(test_dir_exists(path), "Directory should exist: " path)

#define TEST_ASSERT_FRACTYL_SUCCESS(cmd) \
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cmd, "Fractyl command should succeed")

#endif // TEST_HELPERS_H