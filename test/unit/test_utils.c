#include "../unity/unity.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/cli.h"
#include "../../src/utils/git.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
/* Remove cjson.h for now to get basic tests working */

void setUp(void) {
    /* This is run before EACH test */
}

void tearDown(void) {
    /* This is run after EACH test */
}

/* Test file system utilities */
void test_file_exists_with_existing_file(void) {
    /* Create a temporary file */
    FILE *fp = fopen("/tmp/test_file_exists.txt", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "test content");
    fclose(fp);
    
    /* Test that file_exists returns 1 for existing file */
    TEST_ASSERT_EQUAL(1, file_exists("/tmp/test_file_exists.txt"));
    
    /* Clean up */
    unlink("/tmp/test_file_exists.txt");
}

void test_file_exists_with_nonexistent_file(void) {
    /* Test that file_exists returns 0 for non-existent file */
    TEST_ASSERT_EQUAL(0, file_exists("/tmp/this_file_should_not_exist_12345.txt"));
}

void test_is_directory_with_existing_directory(void) {
    /* Test with /tmp which should exist and be a directory */
    TEST_ASSERT_EQUAL(1, is_directory("/tmp"));
}

void test_is_directory_with_file(void) {
    /* Create a temporary file */
    FILE *fp = fopen("/tmp/test_is_directory.txt", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fclose(fp);
    
    /* Test that is_directory returns 0 for a file */
    TEST_ASSERT_EQUAL(0, is_directory("/tmp/test_is_directory.txt"));
    
    /* Clean up */
    unlink("/tmp/test_is_directory.txt");
}

void test_is_directory_with_nonexistent_path(void) {
    /* Test that is_directory returns 0 for non-existent path */
    TEST_ASSERT_EQUAL(0, is_directory("/tmp/this_directory_should_not_exist_12345"));
}

/* Test string utilities (basic functionality) */
void test_string_operations(void) {
    /* Test basic string operations that should work */
    char buffer[256];
    strcpy(buffer, "Hello");
    strcat(buffer, " World");
    TEST_ASSERT_EQUAL_STRING("Hello World", buffer);
    
    /* Test string length */
    TEST_ASSERT_EQUAL(11, strlen(buffer));
}

/* Test CLI parsing */
void test_parse_cli_args_help(void) {
    char *argv[] = {"frac", "--help"};
    cli_options_t opts;
    
    parse_cli_args(2, argv, &opts);
    
    TEST_ASSERT_EQUAL(1, opts.help);
    TEST_ASSERT_EQUAL(0, opts.version);
    TEST_ASSERT_EQUAL(0, opts.debug);
    TEST_ASSERT_NULL(opts.command);
}

void test_parse_cli_args_version(void) {
    char *argv[] = {"frac", "--version"};
    cli_options_t opts;
    
    parse_cli_args(2, argv, &opts);
    
    TEST_ASSERT_EQUAL(0, opts.help);
    TEST_ASSERT_EQUAL(1, opts.version);
    TEST_ASSERT_EQUAL(0, opts.debug);
    TEST_ASSERT_NULL(opts.command);
}

void test_parse_cli_args_debug(void) {
    char *argv[] = {"frac", "--debug", "snapshot"};
    cli_options_t opts;
    
    parse_cli_args(3, argv, &opts);
    
    TEST_ASSERT_EQUAL(0, opts.help);
    TEST_ASSERT_EQUAL(0, opts.version);
    TEST_ASSERT_EQUAL(1, opts.debug);
    TEST_ASSERT_EQUAL_STRING("snapshot", opts.command);
}

void test_parse_cli_args_command(void) {
    char *argv[] = {"frac", "init"};
    cli_options_t opts;
    
    parse_cli_args(2, argv, &opts);
    
    TEST_ASSERT_EQUAL(0, opts.help);
    TEST_ASSERT_EQUAL(0, opts.version);
    TEST_ASSERT_EQUAL(0, opts.debug);
    TEST_ASSERT_EQUAL_STRING("init", opts.command);
}

/* Test basic functionality - placeholder for JSON tests */
void test_basic_functionality(void) {
    /* Test basic memory allocation */
    void *ptr = malloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);
    
    /* Test basic file operations */
    const char *temp_file = "/tmp/unity_test.tmp";
    FILE *fp = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "test");
    fclose(fp);
    
    /* Verify file exists */
    TEST_ASSERT_EQUAL(1, file_exists(temp_file));
    
    /* Clean up */
    unlink(temp_file);
}

/* Test git repository boundary detection */
void test_git_is_repository_root_with_git_directory(void) {
    /* Create test directory structure */
    const char *test_dir = "/tmp/test_git_repo";
    mkdir(test_dir, 0755);
    
    /* Create .git directory */
    char git_dir[256];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", test_dir);
    mkdir(git_dir, 0755);
    
    /* Test should detect as repository root */
    TEST_ASSERT_EQUAL(1, git_is_repository_root(test_dir));
    
    /* Clean up */
    rmdir(git_dir);
    rmdir(test_dir);
}

void test_git_is_repository_root_with_git_file(void) {
    /* Create test directory structure */
    const char *test_dir = "/tmp/test_git_worktree";
    mkdir(test_dir, 0755);
    
    /* Create .git file (like in worktrees/submodules) */
    char git_file[256];
    snprintf(git_file, sizeof(git_file), "%s/.git", test_dir);
    FILE *fp = fopen(git_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "gitdir: /path/to/real/git\n");
    fclose(fp);
    
    /* Test should detect as repository root */
    TEST_ASSERT_EQUAL(1, git_is_repository_root(test_dir));
    
    /* Clean up */
    unlink(git_file);
    rmdir(test_dir);
}

void test_git_is_repository_root_with_no_git(void) {
    /* Create test directory without .git */
    const char *test_dir = "/tmp/test_no_git_clean";
    
    /* First, clean up any existing test directory */
    char rm_cmd[256];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", test_dir);
    system(rm_cmd);
    
    mkdir(test_dir, 0755);
    
    /* Test should NOT detect as repository root */
    TEST_ASSERT_EQUAL(0, git_is_repository_root(test_dir));
    
    /* Clean up */
    rmdir(test_dir);
}

void test_git_is_repository_root_with_null_path(void) {
    /* Test with NULL path */
    TEST_ASSERT_EQUAL(0, git_is_repository_root(NULL));
}

void test_git_is_repository_root_with_nonexistent_path(void) {
    /* Test with non-existent path */
    TEST_ASSERT_EQUAL(0, git_is_repository_root("/tmp/this_should_not_exist_98765"));
}

/* Unity test runner */
int main(void) {
    UNITY_BEGIN();
    
    /* File system tests */
    RUN_TEST(test_file_exists_with_existing_file);
    RUN_TEST(test_file_exists_with_nonexistent_file);
    RUN_TEST(test_is_directory_with_existing_directory);
    RUN_TEST(test_is_directory_with_file);
    RUN_TEST(test_is_directory_with_nonexistent_path);
    
    /* String utility tests */
    RUN_TEST(test_string_operations);
    
    /* CLI parsing tests */
    RUN_TEST(test_parse_cli_args_help);
    RUN_TEST(test_parse_cli_args_version);
    RUN_TEST(test_parse_cli_args_debug);
    RUN_TEST(test_parse_cli_args_command);
    
    /* Basic functionality tests */
    RUN_TEST(test_basic_functionality);
    
    /* Git utility tests */
    RUN_TEST(test_git_is_repository_root_with_git_directory);
    RUN_TEST(test_git_is_repository_root_with_git_file);
    RUN_TEST(test_git_is_repository_root_with_no_git);
    RUN_TEST(test_git_is_repository_root_with_null_path);
    RUN_TEST(test_git_is_repository_root_with_nonexistent_path);
    
    return UNITY_END();
}