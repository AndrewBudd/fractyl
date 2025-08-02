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

// Simple test to validate our test infrastructure works
void test_basic_infrastructure(void) {
    test_repo_t* repo = test_repo_create("validation");
    TEST_ASSERT_NOT_NULL(repo);
    
    // Test that we can enter the repo directory
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Test file operations
    TEST_ASSERT_EQUAL_INT(0, test_file_create("test.txt", "hello world"));
    TEST_ASSERT_FILE_EXISTS("test.txt");
    TEST_ASSERT_FILE_CONTENT("test.txt", "hello world");
    
    // Test directory operations
    TEST_ASSERT_EQUAL_INT(0, test_dir_create("testdir"));
    TEST_ASSERT_DIR_EXISTS("testdir");
    
    test_repo_destroy(repo);
}

// Test fractyl executable is available
void test_executable_available(void) {
    TEST_ASSERT_NOT_NULL(test_frac_executable);
    
    // Test version command
    char* argv[] = {test_frac_executable, "--version", NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_INT(0, result->exit_code);
    TEST_ASSERT_NOT_NULL(result->stdout_content);
    TEST_ASSERT_TRUE(strstr(result->stdout_content, "Fractyl") != NULL);
    
    test_command_result_free(result);
}

// Test basic fractyl workflow
void test_basic_fractyl_workflow(void) {
    test_repo_t* repo = test_repo_create("workflow");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    // Initialize
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Create file
    TEST_ASSERT_EQUAL_INT(0, test_file_create("workflow.txt", "test content"));
    
    // Snapshot
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Test snapshot"));
    
    // List
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    TEST_ASSERT_TRUE(strstr(list_output, "Test snapshot") != NULL);
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
    
    RUN_TEST(test_basic_infrastructure);
    RUN_TEST(test_executable_available);
    RUN_TEST(test_basic_fractyl_workflow);
    
    free(test_frac_executable);
    return UNITY_END();
}