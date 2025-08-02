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

// Test operations outside of a repository
void test_operations_outside_repository(void) {
    test_repo_t* repo = test_repo_create("outside_repo");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Don't initialize repository - test operations without .fractyl
    
    // Snapshot should fail
    TEST_ASSERT_NOT_EQUAL(0, test_fractyl_snapshot(repo, "Should fail"));
    
    // List should fail
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NULL(list_output);  // Should fail or return empty
    
    // Restore should fail
    TEST_ASSERT_NOT_EQUAL(0, test_fractyl_restore(repo, "nonexistent"));
    
    test_repo_destroy(repo);
}

// Test invalid snapshot IDs
void test_invalid_snapshot_operations(void) {
    test_repo_t* repo = test_repo_create("invalid_snap");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Try to restore nonexistent snapshot
    TEST_ASSERT_NOT_EQUAL(0, test_fractyl_restore(repo, "nonexistent12"));
    TEST_ASSERT_NOT_EQUAL(0, test_fractyl_restore(repo, "00000000"));
    TEST_ASSERT_NOT_EQUAL(0, test_fractyl_restore(repo, ""));
    
    test_repo_destroy(repo);
}

// Test large files and paths
void test_large_files_and_paths(void) {
    test_repo_t* repo = test_repo_create("large_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create a file with a long name
    char long_filename[256];
    memset(long_filename, 'a', sizeof(long_filename) - 5);
    strcpy(long_filename + sizeof(long_filename) - 5, ".txt");
    
    TEST_ASSERT_EQUAL_INT(0, test_file_create(long_filename, "Long filename test"));
    
    // Create a moderately large file content
    char large_content[10240];
    for (size_t i = 0; i < sizeof(large_content) - 1; i++) {
        large_content[i] = 'A' + (i % 26);
    }
    large_content[sizeof(large_content) - 1] = '\0';
    
    TEST_ASSERT_EQUAL_INT(0, test_file_create("large_file.txt", large_content));
    
    // Snapshot should handle these files
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Large files test"));
    
    // Get snapshot ID and restore
    char* snap_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap_id);
    
    // Remove files and restore
    TEST_ASSERT_EQUAL_INT(0, test_file_remove(long_filename));
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("large_file.txt"));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap_id));
    
    // Verify restoration
    TEST_ASSERT_TRUE(test_file_exists(long_filename));
    TEST_ASSERT_FILE_EXISTS("large_file.txt");
    
    char* restored_content = test_file_read("large_file.txt");
    TEST_ASSERT_NOT_NULL(restored_content);
    TEST_ASSERT_EQUAL_STRING(large_content, restored_content);
    free(restored_content);
    
    free(snap_id);
    test_repo_destroy(repo);
}

// Test deep directory structures
void test_deep_directory_structure(void) {
    test_repo_t* repo = test_repo_create("deep_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create deep directory structure
    char path[512] = "level1";
    TEST_ASSERT_EQUAL_INT(0, test_dir_create(path));
    
    for (int i = 2; i <= 10; i++) {
        char new_path[512];
        snprintf(new_path, sizeof(new_path), "%s/level%d", path, i);
        TEST_ASSERT_EQUAL_INT(0, test_dir_create(new_path));
        strcpy(path, new_path);
    }
    
    // Create file at the deepest level
    char deep_file[512];
    snprintf(deep_file, sizeof(deep_file), "%s/deep_file.txt", path);
    TEST_ASSERT_EQUAL_INT(0, test_file_create(deep_file, "Deep file content"));
    
    // Snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Deep structure"));
    char* snap_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap_id);
    
    // Remove structure
    TEST_ASSERT_EQUAL_INT(0, test_dir_remove_recursive("level1"));
    
    // Restore
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap_id));
    
    // Verify
    TEST_ASSERT_TRUE(test_file_exists(deep_file));
    char* deep_content = test_file_read(deep_file);
    TEST_ASSERT_NOT_NULL(deep_content);
    TEST_ASSERT_EQUAL_STRING("Deep file content", deep_content);
    free(deep_content);
    
    free(snap_id);
    test_repo_destroy(repo);
}

// Test binary files
void test_binary_files(void) {
    test_repo_t* repo = test_repo_create("binary_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create a binary file with null bytes and various byte values
    FILE* binary_file = fopen("binary_test.bin", "wb");
    TEST_ASSERT_NOT_NULL(binary_file);
    
    unsigned char binary_data[256];
    for (int i = 0; i < 256; i++) {
        binary_data[i] = (unsigned char)i;
    }
    
    size_t written = fwrite(binary_data, 1, sizeof(binary_data), binary_file);
    TEST_ASSERT_EQUAL_INT(sizeof(binary_data), written);
    fclose(binary_file);
    
    // Snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Binary file test"));
    char* snap_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap_id);
    
    // Remove and restore
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("binary_test.bin"));
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap_id));
    
    // Verify binary content
    FILE* restored_file = fopen("binary_test.bin", "rb");
    TEST_ASSERT_NOT_NULL(restored_file);
    
    unsigned char restored_data[256];
    size_t read_size = fread(restored_data, 1, sizeof(restored_data), restored_file);
    TEST_ASSERT_EQUAL_INT(sizeof(binary_data), read_size);
    fclose(restored_file);
    
    // Compare byte by byte
    for (int i = 0; i < 256; i++) {
        TEST_ASSERT_EQUAL_INT(binary_data[i], restored_data[i]);
    }
    
    free(snap_id);
    test_repo_destroy(repo);
}

// Test file permissions (where possible)
void test_file_permissions(void) {
    test_repo_t* repo = test_repo_create("perms_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create files with different permissions
    TEST_ASSERT_EQUAL_INT(0, test_file_create("readonly.txt", "Read only file"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("writable.txt", "Writable file"));
    
    // Change permissions
    chmod("readonly.txt", 0444);  // Read-only
    chmod("writable.txt", 0644);  // Read-write
    
    // Snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Permission test"));
    char* snap_id = test_fractyl_get_latest_snapshot_id(repo);
    TEST_ASSERT_NOT_NULL(snap_id);
    
    // Change permissions
    chmod("readonly.txt", 0644);
    chmod("writable.txt", 0444);
    
    // Restore (permissions might not be fully restored, but files should exist)
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_restore(repo, snap_id));
    
    // Verify files exist and content is correct
    TEST_ASSERT_FILE_EXISTS("readonly.txt");
    TEST_ASSERT_FILE_EXISTS("writable.txt");
    TEST_ASSERT_FILE_CONTENT("readonly.txt", "Read only file");
    TEST_ASSERT_FILE_CONTENT("writable.txt", "Writable file");
    
    free(snap_id);
    test_repo_destroy(repo);
}

// Test rapid successive operations
void test_rapid_operations(void) {
    test_repo_t* repo = test_repo_create("rapid_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Rapid file creation and snapshotting
    for (int i = 0; i < 10; i++) {
        char filename[32], content[64], message[64];
        snprintf(filename, sizeof(filename), "rapid_%d.txt", i);
        snprintf(content, sizeof(content), "Content %d", i);
        snprintf(message, sizeof(message), "Rapid snapshot %d", i);
        
        TEST_ASSERT_EQUAL_INT(0, test_file_create(filename, content));
        TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, message));
    }
    
    // Verify all snapshots exist
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    
    for (int i = 0; i < 10; i++) {
        char message[64];
        snprintf(message, sizeof(message), "Rapid snapshot %d", i);
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
    
    RUN_TEST(test_operations_outside_repository);
    RUN_TEST(test_invalid_snapshot_operations);
    RUN_TEST(test_large_files_and_paths);
    RUN_TEST(test_deep_directory_structure);
    RUN_TEST(test_binary_files);
    RUN_TEST(test_file_permissions);
    RUN_TEST(test_rapid_operations);
    
    free(test_frac_executable);
    return UNITY_END();
}