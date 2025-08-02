#include "../unity/unity.h"
#include "../test_helpers.h"
#include <string.h>
#include <unistd.h>

// test_frac_executable is declared in test_helpers.h

void setUp(void) {
    // Each test gets a fresh start
}

void tearDown(void) {
    // Cleanup after each test
}

// Test basic repository initialization
void test_repository_initialization(void) {
    test_repo_t* repo = test_repo_create("init_test");
    TEST_ASSERT_NOT_NULL(repo);
    
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Initialize repository
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Check that .fractyl directory was created
    TEST_ASSERT_DIR_EXISTS(".fractyl");
    TEST_ASSERT_DIR_EXISTS(".fractyl/objects");
    TEST_ASSERT_DIR_EXISTS(".fractyl/snapshots");
    
    test_repo_destroy(repo);
}

// Test basic snapshot creation and file tracking
void test_snapshot_creation(void) {
    test_repo_t* repo = test_repo_create("snapshot_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Initialize repository
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create some files
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file1.txt", "Content of file 1"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file2.txt", "Content of file 2"));
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("subdir"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("subdir/file3.txt", "Content of file 3"));
    
    // Create snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Initial snapshot"));
    
    // Verify snapshot was created by listing
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    TEST_ASSERT_TRUE(strstr(list_output, "Initial snapshot") != NULL);
    free(list_output);
    
    test_repo_destroy(repo);
}

// Test file modification and snapshot differences
void test_file_modification_tracking(void) {
    test_repo_t* repo = test_repo_create("modify_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create initial files
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file1.txt", "Original content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file2.txt", "Another file"));
    
    // First snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "First snapshot"));
    
    // Modify files
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("file1.txt", "Modified content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file3.txt", "New file"));
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("file2.txt"));
    
    // Second snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "After modifications"));
    
    // Verify we have two snapshots
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    TEST_ASSERT_TRUE(strstr(list_output, "First snapshot") != NULL);
    TEST_ASSERT_TRUE(strstr(list_output, "After modifications") != NULL);
    free(list_output);
    
    test_repo_destroy(repo);
}

// Test snapshot restoration
void test_snapshot_restoration(void) {
    test_repo_t* repo = test_repo_create("restore_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create initial state
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file1.txt", "Original content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file2.txt", "Original file2"));
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Original state"));
    
    // Get the snapshot ID
    char* snapshot_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snapshot_id);
    
    // Modify the state
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("file1.txt", "Modified content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("file2.txt"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file3.txt", "New file"));
    
    // Verify changes
    TEST_ASSERT_FILE_CONTENT("file1.txt", "Modified content");
    TEST_ASSERT_FILE_NOT_EXISTS("file2.txt");
    TEST_ASSERT_FILE_EXISTS("file3.txt");
    
    // Restore to original state
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snapshot_id));
    
    // Verify restoration
    TEST_ASSERT_FILE_CONTENT("file1.txt", "Original content");
    TEST_ASSERT_FILE_CONTENT("file2.txt", "Original file2");
    TEST_ASSERT_FILE_NOT_EXISTS("file3.txt");
    
    free(snapshot_id);
    test_repo_destroy(repo);
}

// Test complex file operations
void test_complex_file_operations(void) {
    test_repo_t* repo = test_repo_create("complex_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create complex directory structure
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("dir1"));
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("dir1/subdir"));
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("dir2"));
    
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir1/file1.txt", "File in dir1"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir1/subdir/file2.txt", "File in subdir"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir2/file3.txt", "File in dir2"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("root.txt", "Root file"));
    
    // Snapshot 1
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Complex structure"));
    char* snap1_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap1_id);
    
    // Make complex changes
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("dir1/file1.txt", "Modified file in dir1"));
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("dir1/subdir/file2.txt"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir1/subdir/file4.txt", "New file in subdir"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir2/file5.txt", "Another new file"));
    
    // Snapshot 2
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "After complex changes"));
    char* snap2_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap2_id);
    
    // More changes
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("root.txt"));
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("dir3"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("dir3/file6.txt", "File in new dir"));
    
    // Snapshot 3
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Final state"));
    
    // Test restoring to snapshot 1
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap1_id));
    
    // Verify restoration to complex structure
    TEST_ASSERT_FILE_CONTENT("dir1/file1.txt", "File in dir1");
    TEST_ASSERT_FILE_EXISTS("dir1/subdir/file2.txt");
    TEST_ASSERT_FILE_NOT_EXISTS("dir1/subdir/file4.txt");
    TEST_ASSERT_FILE_EXISTS("dir2/file3.txt");
    TEST_ASSERT_FILE_NOT_EXISTS("dir2/file5.txt");
    TEST_ASSERT_FILE_EXISTS("root.txt");
    TEST_ASSERT_DIR_EXISTS("dir1");
    TEST_ASSERT_DIR_EXISTS("dir2");
    TEST_ASSERT_FALSE(test_dir_exists("dir3"));
    
    free(snap1_id);
    free(snap2_id);
    test_repo_destroy(repo);
}

// Test empty files and special cases
void test_edge_cases(void) {
    test_repo_t* repo = test_repo_create("edge_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Test empty files
    TEST_ASSERT_EQUAL_INT(0, test_file_create("empty.txt", ""));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("normal.txt", "content"));
    
    // Snapshot with empty file
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "With empty file"));
    char* snap_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap_id);
    
    // Modify empty file
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("empty.txt", "now has content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("normal.txt", ""));
    
    // Another snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "After empty file changes"));
    
    // Restore
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap_id));
    
    // Verify
    TEST_ASSERT_FILE_CONTENT("empty.txt", "");
    TEST_ASSERT_FILE_CONTENT("normal.txt", "content");
    
    free(snap_id);
    test_repo_destroy(repo);
}

// Test multiple snapshots and listing
void test_multiple_snapshots(void) {
    test_repo_t* repo = test_repo_create("multi_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create multiple snapshots
    for (int i = 1; i <= 5; i++) {
        char filename[32], content[64], message[64];
        snprintf(filename, sizeof(filename), "file%d.txt", i);
        snprintf(content, sizeof(content), "Content of file %d", i);
        snprintf(message, sizeof(message), "Snapshot %d", i);
        
        TEST_ASSERT_EQUAL_INT(0, test_file_create(filename, content));
        TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, message));
    }
    
    // List snapshots
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    
    // Verify all snapshots are listed
    for (int i = 1; i <= 5; i++) {
        char message[64];
        snprintf(message, sizeof(message), "Snapshot %d", i);
        TEST_ASSERT_TRUE(strstr(list_output, message) != NULL);
    }
    
    free(list_output);
    test_repo_destroy(repo);
}

int main(void) {
    // Set up the test executable path
    test_frac_executable = realpath("./frac", NULL);
    if (!test_frac_executable) {
        printf("Error: Could not find frac executable in current directory\n");
        return 1;
    }
    
    UNITY_BEGIN();
    
    RUN_TEST(test_repository_initialization);
    RUN_TEST(test_snapshot_creation);
    RUN_TEST(test_file_modification_tracking);
    RUN_TEST(test_snapshot_restoration);
    RUN_TEST(test_complex_file_operations);
    RUN_TEST(test_edge_cases);
    RUN_TEST(test_multiple_snapshots);
    
    free(test_frac_executable);
    return UNITY_END();
}