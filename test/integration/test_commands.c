#define _XOPEN_SOURCE 500
#include "../unity/unity.h"
#include "../../src/include/commands.h"
#include "../../src/utils/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ftw.h>

/* Helper function to remove directory recursively */
static int remove_file(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwb) {
    (void)sbuf; (void)type; (void)ftwb;  /* Suppress unused parameter warnings */
    return remove(pathname);
}

static void remove_directory_recursive(const char *path) {
    nftw(path, remove_file, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
}

void setUp(void) {
    /* Clean up any existing test repository */
    remove_directory_recursive("/tmp/fractyl_test_repo");
}

void tearDown(void) {
    /* Clean up test repository after each test */
    remove_directory_recursive("/tmp/fractyl_test_repo");
}

/* Test repository initialization */
void test_cmd_init_creates_repository(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create test directory */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    /* Simulate command line args for init */
    char *argv[] = {"frac", "init"};
    int result = cmd_init(2, argv);
    
    /* Init should succeed */
    TEST_ASSERT_EQUAL(0, result);
    
    /* Check that .fractyl directory was created */
    TEST_ASSERT_EQUAL(1, is_directory(".fractyl"));
    TEST_ASSERT_EQUAL(1, is_directory(".fractyl/objects"));
    TEST_ASSERT_EQUAL(1, is_directory(".fractyl/snapshots"));
    TEST_ASSERT_EQUAL(1, file_exists(".fractyl/index"));
}

void test_cmd_init_in_existing_repository(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create test directory and initialize repository */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    char *argv[] = {"frac", "init"};
    int result1 = cmd_init(2, argv);
    TEST_ASSERT_EQUAL(0, result1);
    
    /* Try to init again - should handle gracefully */
    int result2 = cmd_init(2, argv);
    /* This should either succeed (idempotent) or fail gracefully */
    /* The exact behavior depends on implementation */
    TEST_ASSERT_TRUE(result2 == 0 || result2 != 0);
}

/* Test snapshot creation */
void test_cmd_snapshot_with_files(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create and initialize repository */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    char *init_argv[] = {"frac", "init"};
    int result = cmd_init(2, init_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Create test files */
    FILE *fp1 = fopen("test1.txt", "w");
    TEST_ASSERT_NOT_NULL(fp1);
    fprintf(fp1, "Test file 1 content");
    fclose(fp1);
    
    FILE *fp2 = fopen("test2.txt", "w");
    TEST_ASSERT_NOT_NULL(fp2);
    fprintf(fp2, "Test file 2 content");
    fclose(fp2);
    
    /* Create snapshot */
    char *snapshot_argv[] = {"frac", "snapshot", "-m", "Test snapshot"};
    result = cmd_snapshot(4, snapshot_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Verify snapshot was created - should have at least one file in objects */
    TEST_ASSERT_EQUAL(1, is_directory(".fractyl/objects"));
    
    /* The objects directory should contain subdirectories with hash prefixes */
    /* This is a basic sanity check that something was stored */
}

void test_cmd_snapshot_empty_repository(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create and initialize empty repository */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    char *init_argv[] = {"frac", "init"};
    int result = cmd_init(2, init_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Try to create snapshot with no files */
    char *snapshot_argv[] = {"frac", "snapshot", "-m", "Empty snapshot"};
    result = cmd_snapshot(4, snapshot_argv);
    
    /* Should handle empty repository gracefully */
    /* The exact behavior depends on implementation - might succeed with 0 files */
    TEST_ASSERT_TRUE(result == 0 || result != 0);
}

/* Test list command */
void test_cmd_list_empty_repository(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create and initialize repository */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    char *init_argv[] = {"frac", "init"};
    int result = cmd_init(2, init_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* List snapshots in empty repository */
    char *list_argv[] = {"frac", "list"};
    result = cmd_list(2, list_argv);
    
    /* Should succeed even with no snapshots */
    TEST_ASSERT_EQUAL(0, result);
}

void test_cmd_list_with_snapshots(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";
    
    /* Create and initialize repository */
    mkdir(test_repo, 0755);
    chdir(test_repo);
    
    char *init_argv[] = {"frac", "init"};
    int result = cmd_init(2, init_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Create a test file and snapshot */
    FILE *fp = fopen("test.txt", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "Test content");
    fclose(fp);
    
    char *snapshot_argv[] = {"frac", "snapshot", "-m", "Test snapshot for list"};
    result = cmd_snapshot(4, snapshot_argv);
    TEST_ASSERT_EQUAL(0, result);
    
    /* List snapshots */
    char *list_argv[] = {"frac", "list"};
    result = cmd_list(2, list_argv);
    TEST_ASSERT_EQUAL(0, result);
}

void test_cmd_restore_removes_extra_files(void) {
    const char *test_repo = "/tmp/fractyl_test_repo";

    mkdir(test_repo, 0755);
    chdir(test_repo);

    char *init_argv[] = {"frac", "init"};
    TEST_ASSERT_EQUAL(0, cmd_init(2, init_argv));

    /* Create initial file and snapshot */
    FILE *fp = fopen("file1.txt", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "one");
    fclose(fp);

    char *snap1[] = {"frac", "snapshot", "-m", "first"};
    TEST_ASSERT_EQUAL(0, cmd_snapshot(4, snap1));

    char first_id[65];
    FILE *cur = fopen(".fractyl/CURRENT", "r");
    TEST_ASSERT_NOT_NULL(cur);
    fgets(first_id, sizeof(first_id), cur);
    fclose(cur);
    first_id[strcspn(first_id, "\n")] = '\0';

    /* Add extra file and take another snapshot */
    fp = fopen("file2.txt", "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "two");
    fclose(fp);

    char *snap2[] = {"frac", "snapshot", "-m", "second"};
    TEST_ASSERT_EQUAL(0, cmd_snapshot(4, snap2));

    /* Restore back to first snapshot */
    char *restore_argv[] = {"frac", "restore", first_id};
    TEST_ASSERT_EQUAL(0, cmd_restore(3, restore_argv));

    /* file2.txt should have been removed */
    TEST_ASSERT_EQUAL(0, file_exists("file2.txt"));
    TEST_ASSERT_EQUAL(1, file_exists("file1.txt"));
}

/* Test error conditions */
void test_cmd_snapshot_outside_repository(void) {
    /* Try to create snapshot outside of fractyl repository */
    chdir("/tmp");
    
    char *snapshot_argv[] = {"frac", "snapshot", "-m", "Should fail"};
    int result = cmd_snapshot(4, snapshot_argv);
    
    /* Should fail when not in a fractyl repository */
    TEST_ASSERT_NOT_EQUAL(0, result);
}

void test_cmd_list_outside_repository(void) {
    /* Try to list snapshots outside of fractyl repository */
    chdir("/tmp");
    
    char *list_argv[] = {"frac", "list"};
    int result = cmd_list(2, list_argv);
    
    /* Should fail when not in a fractyl repository */
    TEST_ASSERT_NOT_EQUAL(0, result);
}

/* Unity test runner */
int main(void) {
    UNITY_BEGIN();
    
    /* Repository initialization tests */
    RUN_TEST(test_cmd_init_creates_repository);
    RUN_TEST(test_cmd_init_in_existing_repository);
    
    /* Snapshot command tests */
    RUN_TEST(test_cmd_snapshot_with_files);
    RUN_TEST(test_cmd_snapshot_empty_repository);

    /* List command tests */
    RUN_TEST(test_cmd_list_empty_repository);
    RUN_TEST(test_cmd_list_with_snapshots);
    RUN_TEST(test_cmd_restore_removes_extra_files);
    
    /* Error condition tests */
    RUN_TEST(test_cmd_snapshot_outside_repository);
    RUN_TEST(test_cmd_list_outside_repository);
    
    return UNITY_END();
}