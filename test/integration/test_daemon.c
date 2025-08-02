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

// Helper function to run daemon commands
int test_daemon_command(test_repo_t* repo, const char* command) {
    char* argv[] = {test_frac_executable, "daemon", (char*)command, NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    int exit_code = result ? result->exit_code : -1;
    test_command_result_free(result);
    return exit_code;
}

int test_daemon_start_with_interval(test_repo_t* repo, int interval) {
    char interval_str[16];
    snprintf(interval_str, sizeof(interval_str), "%d", interval);
    char* argv[] = {test_frac_executable, "daemon", "start", "-i", interval_str, NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    int exit_code = result ? result->exit_code : -1;
    test_command_result_free(result);
    return exit_code;
}

char* test_daemon_status(test_repo_t* repo) {
    char* argv[] = {test_frac_executable, "daemon", "status", NULL};
    test_command_result_t* result = test_run_command(test_frac_executable, argv);
    char* output = NULL;
    if (result && result->stdout_content) {
        output = strdup(result->stdout_content);
    }
    test_command_result_free(result);
    return output;
}

// Test basic daemon start/stop
void test_daemon_basic_operations(void) {
    test_repo_t* repo = test_repo_create("daemon_basic");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Start daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "start"));
    
    // Check status
    char* status = test_daemon_status(repo);
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_TRUE(strstr(status, "running") != NULL || strstr(status, "active") != NULL);
    free(status);
    
    // Stop daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "stop"));
    
    // Check status after stop
    status = test_daemon_status(repo);
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_TRUE(strstr(status, "not running") != NULL || strstr(status, "stopped") != NULL);
    free(status);
    
    test_repo_destroy(repo);
}

// Test daemon with custom interval
void test_daemon_custom_interval(void) {
    test_repo_t* repo = test_repo_create("daemon_interval");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Start daemon with 5-second interval
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_start_with_interval(repo, 5));
    
    // Create a file to trigger snapshot
    TEST_ASSERT_EQUAL_INT(0, test_file_create("test.txt", "test content"));
    
    // Wait a bit longer than the interval
    sleep(7);
    
    // Check if auto-snapshot was created
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    // Should contain "Auto-snapshot" in the output
    TEST_ASSERT_TRUE(strstr(list_output, "Auto-snapshot") != NULL);
    free(list_output);
    
    // Stop daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "stop"));
    
    test_repo_destroy(repo);
}

// Test daemon restart
void test_daemon_restart(void) {
    test_repo_t* repo = test_repo_create("daemon_restart");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Start daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "start"));
    
    // Restart daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "restart"));
    
    // Check status
    char* status = test_daemon_status(repo);
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_TRUE(strstr(status, "running") != NULL || strstr(status, "active") != NULL);
    free(status);
    
    // Stop daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "stop"));
    
    test_repo_destroy(repo);
}

// Test that daemon handles concurrent manual operations
void test_daemon_concurrent_operations(void) {
    test_repo_t* repo = test_repo_create("daemon_concurrent");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Start daemon with longer interval to avoid conflicts
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_start_with_interval(repo, 60));
    
    // Create files and manual snapshots while daemon is running
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file1.txt", "content1"));
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Manual snapshot 1"));
    
    TEST_ASSERT_EQUAL_INT(0, test_file_create("file2.txt", "content2"));
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_snapshot(repo, "Manual snapshot 2"));
    
    // Verify snapshots were created
    char* list_output = test_fractyl_list(repo);
    TEST_ASSERT_NOT_NULL(list_output);
    TEST_ASSERT_TRUE(strstr(list_output, "Manual snapshot 1") != NULL);
    TEST_ASSERT_TRUE(strstr(list_output, "Manual snapshot 2") != NULL);
    free(list_output);
    
    // Stop daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "stop"));
    
    test_repo_destroy(repo);
}

// Test daemon error conditions
void test_daemon_error_conditions(void) {
    test_repo_t* repo = test_repo_create("daemon_errors");
    TEST_ASSERT_NOT_NULL(repo);
    TEST_ASSERT_EQUAL_INT(0, test_repo_enter(repo));
    
    TEST_ASSERT_FRACTYL_SUCCESS(test_fractyl_init(repo));
    
    // Try to stop daemon when not running
    int result = test_daemon_command(repo, "stop");
    // This might succeed or fail depending on implementation
    
    // Start daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "start"));
    
    // Try to start again (should handle gracefully)
    result = test_daemon_command(repo, "start");
    // This should either succeed (no-op) or fail gracefully
    
    // Stop daemon
    TEST_ASSERT_FRACTYL_SUCCESS(test_daemon_command(repo, "stop"));
    
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
    
    RUN_TEST(test_daemon_basic_operations);
    RUN_TEST(test_daemon_custom_interval);
    RUN_TEST(test_daemon_restart);
    RUN_TEST(test_daemon_concurrent_operations);
    RUN_TEST(test_daemon_error_conditions);
    
    free(test_frac_executable);
    return UNITY_END();
}