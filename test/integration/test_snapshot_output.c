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

// Test snapshot output formatting
void test_snapshot_output_format(void) {
    test_repo_t* repo = test_repo_create("snapshot_output_test");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create initial files
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file1.txt", "Content 1"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file2.txt", "Content 2"));
    
    // Run snapshot and capture output
    char* snapshot_args[] = {test_frac_executable, "snapshot", "-m", "Initial snapshot", NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, snapshot_args);
    TEST_ASSERT_NOT_NULL(result);
    
    printf("=== SNAPSHOT OUTPUT ===\n");
    printf("STDOUT:\n%s\n", result->stdout_content ? result->stdout_content : "(null)");
    printf("STDERR:\n%s\n", result->stderr_content ? result->stderr_content : "(null)");
    printf("EXIT CODE: %d\n", result->exit_code);
    printf("=====================\n");
    
    TEST_ASSERT_EQUAL_INT(0, result->exit_code);
    
    // The output should be minimal - no verbose debug messages
    if (result->stdout_content) {
        // Should NOT contain verbose debug output
        TEST_ASSERT_NULL(strstr(result->stdout_content, "Using pure stat-only scanning"));
        TEST_ASSERT_NULL(strstr(result->stdout_content, "Binary index loaded"));
        TEST_ASSERT_NULL(strstr(result->stdout_content, "Parallel stat"));
        TEST_ASSERT_NULL(strstr(result->stdout_content, "Found %zu files"));
        TEST_ASSERT_NULL(strstr(result->stdout_content, "Comparing %zu files"));
        
        // Should contain essential information
        TEST_ASSERT_NOT_NULL(strstr(result->stdout_content, "Created snapshot"));
    }
    
    test_command_result_free(result);
    
    // Test subsequent snapshot with no changes
    char* snapshot_args2[] = {test_frac_executable, "snapshot", "-m", "No changes", NULL};
    test_command_result_t* result2 = test_run_command(test_frac_executable, snapshot_args2);
    TEST_ASSERT_NOT_NULL(result2);
    
    printf("=== NO CHANGES OUTPUT ===\n");
    printf("STDOUT:\n%s\n", result2->stdout_content ? result2->stdout_content : "(null)");
    printf("STDERR:\n%s\n", result2->stderr_content ? result2->stderr_content : "(null)");
    printf("EXIT CODE: %d\n", result2->exit_code);
    printf("========================\n");
    
    TEST_ASSERT_EQUAL_INT(0, result2->exit_code);
    
    // Should indicate no changes detected
    if (result2->stdout_content) {
        TEST_ASSERT_NOT_NULL(strstr(result2->stdout_content, "No changes detected"));
    }
    
    test_command_result_free(result2);
    
    // Test snapshot with changes
    TEST_ASSERT_EQUAL_INT(0, test_file_modify("file1.txt", "Modified content"));
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file3.txt", "New file"));
    TEST_ASSERT_EQUAL_INT(0, test_file_remove("file2.txt"));
    
    char* snapshot_args3[] = {test_frac_executable, "snapshot", "-m", "With changes", NULL};
    test_command_result_t* result3 = test_run_command(test_frac_executable, snapshot_args3);
    TEST_ASSERT_NOT_NULL(result3);
    
    printf("=== WITH CHANGES OUTPUT ===\n");
    printf("STDOUT:\n%s\n", result3->stdout_content ? result3->stdout_content : "(null)");
    printf("STDERR:\n%s\n", result3->stderr_content ? result3->stderr_content : "(null)");
    printf("EXIT CODE: %d\n", result3->exit_code);
    printf("==========================\n");
    
    TEST_ASSERT_EQUAL_INT(0, result3->exit_code);
    
    // Should show file changes in clean format
    if (result3->stdout_content) {
        // Should show modified files
        TEST_ASSERT_NOT_NULL(strstr(result3->stdout_content, "M file1.txt"));
        // Should show added files  
        TEST_ASSERT_NOT_NULL(strstr(result3->stdout_content, "A file3.txt"));
        // Should show that snapshot was created
        TEST_ASSERT_NOT_NULL(strstr(result3->stdout_content, "Created snapshot"));
    }
    
    test_command_result_free(result3);
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
    
    RUN_TEST(test_snapshot_output_format);
    
    free(test_frac_executable);
    return UNITY_END();
}