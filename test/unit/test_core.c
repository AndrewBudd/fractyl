#include "../unity/unity.h"
#include "../../src/core/hash.h"
#include "../../src/core/objects.h"
#include "../../src/core/index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {
    /* This is run before EACH test */
}

void tearDown(void) {
    /* This is run after EACH test */
}

/* Test hashing functionality */
void test_hash_data_consistency(void) {
    const char *test_string = "Hello, World!";
    unsigned char hash1[32];
    unsigned char hash2[32];
    
    /* Hash the same data twice */
    int result1 = hash_data(test_string, strlen(test_string), hash1);
    int result2 = hash_data(test_string, strlen(test_string), hash2);
    
    /* Both should succeed */
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    
    /* Hashes should be identical */
    TEST_ASSERT_EQUAL_MEMORY(hash1, hash2, 32);
}

void test_hash_data_different_inputs(void) {
    unsigned char hash1[32];
    unsigned char hash2[32];
    
    /* Hash different data */
    int result1 = hash_data("Hello", 5, hash1);
    int result2 = hash_data("World", 5, hash2);
    
    /* Both should succeed */
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    
    /* Hashes should be different */
    TEST_ASSERT_FALSE(hash_compare(hash1, hash2) == 0);
}

void test_hash_file_with_temp_file(void) {
    const char *test_content = "This is test file content for hashing.";
    const char *temp_file = "/tmp/test_hash_file.txt";
    unsigned char hash[32];
    
    /* Create temporary file with test content */
    FILE *fp = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "%s", test_content);
    fclose(fp);
    
    /* Hash the file */
    int result = hash_file(temp_file, hash);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Hash should not be all zeros */
    TEST_ASSERT_FALSE(hash_is_zero(hash));
    
    /* Hash should be consistent - hash the same file again */
    unsigned char hash2[32];
    result = hash_file(temp_file, hash2);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL_MEMORY(hash, hash2, 32);
    
    /* Clean up */
    unlink(temp_file);
}

void test_hash_file_nonexistent(void) {
    unsigned char hash[32];
    
    /* Try to hash non-existent file */
    int result = hash_file("/tmp/this_file_does_not_exist_12345.txt", hash);
    
    /* Should return error (non-zero) */
    TEST_ASSERT_NOT_EQUAL(0, result);
}

void test_hash_to_string_conversion(void) {
    unsigned char hash[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}; /* First 8 bytes */
    char hex_string[65];
    
    hash_to_string(hash, hex_string);
    
    /* Check first few characters of hex conversion */
    TEST_ASSERT_EQUAL_STRING_LEN("0123456789abcdef", hex_string, 16);
}

/* Test object storage functionality */
void test_object_path_creation(void) {
    unsigned char hash[32];
    /* Create a test hash pattern */
    for (int i = 0; i < 32; i++) {
        hash[i] = i;
    }
    
    char *path = object_path(hash, "/tmp/test_objects");
    TEST_ASSERT_NOT_NULL(path);
    
    /* Path should contain the objects directory */
    TEST_ASSERT_TRUE(strstr(path, "/tmp/test_objects") != NULL);
    
    free(path);
}

void test_object_store_and_restore_file(void) {
    const char *test_content = "This is test content for object storage.";
    const char *temp_file = "/tmp/test_object_store.txt";
    const char *objects_dir = "/tmp/test_objects";
    const char *restored_file = "/tmp/test_object_restore.txt";
    unsigned char hash[32];
    
    /* Create temporary file */
    FILE *fp = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(fp);
    fprintf(fp, "%s", test_content);
    fclose(fp);
    
    /* Create objects directory */
    mkdir(objects_dir, 0755);
    object_storage_init(objects_dir);
    
    /* Store file in object storage */
    int result = object_store_file(temp_file, objects_dir, hash);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_FALSE(hash_is_zero(hash));
    
    /* Check if object exists */
    TEST_ASSERT_EQUAL(1, object_exists(hash, objects_dir));
    
    /* Restore file from object storage */
    result = object_restore_file(hash, objects_dir, restored_file);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Verify restored content matches original */
    fp = fopen(restored_file, "r");
    TEST_ASSERT_NOT_NULL(fp);
    
    char restored_content[256];
    fgets(restored_content, sizeof(restored_content), fp);
    fclose(fp);
    
    TEST_ASSERT_EQUAL_STRING(test_content, restored_content);
    
    /* Clean up files */
    unlink(temp_file);
    unlink(restored_file);
    
    /* Clean up object storage - get path and remove */
    char *obj_path = object_path(hash, objects_dir);
    if (obj_path) {
        unlink(obj_path);
        free(obj_path);
    }
}

/* Test index functionality */
void test_index_create_and_load(void) {
    const char *index_file = "/tmp/test_index.dat";
    index_t index;
    
    /* Initialize empty index */
    index_init(&index);
    
    /* Create test entries */
    index_entry_t entry1;
    entry1.path = strdup("file1.txt");
    memset(entry1.hash, 0xAA, 32);  /* Fill with test pattern */
    entry1.mode = 0644;
    entry1.size = 1024;
    entry1.mtime = time(NULL);
    
    index_entry_t entry2;
    entry2.path = strdup("dir/file2.txt");
    memset(entry2.hash, 0xBB, 32);  /* Fill with different test pattern */
    entry2.mode = 0755;
    entry2.size = 2048;
    entry2.mtime = time(NULL);
    
    index_add_entry(&index, &entry1);
    index_add_entry(&index, &entry2);
    
    TEST_ASSERT_EQUAL(2, index.count);
    
    /* Save index to file */
    int result = index_save(&index, index_file);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Load index from file */
    index_t loaded_index;
    index_init(&loaded_index);
    result = index_load(&loaded_index, index_file);
    TEST_ASSERT_EQUAL(0, result);
    
    /* Verify loaded index matches original */
    TEST_ASSERT_EQUAL(index.count, loaded_index.count);
    TEST_ASSERT_EQUAL_STRING(index.entries[0].path, loaded_index.entries[0].path);
    TEST_ASSERT_EQUAL_MEMORY(index.entries[0].hash, loaded_index.entries[0].hash, 32);
    TEST_ASSERT_EQUAL(index.entries[0].mode, loaded_index.entries[0].mode);
    TEST_ASSERT_EQUAL(index.entries[0].size, loaded_index.entries[0].size);
    
    /* Clean up */
    free(entry1.path);
    free(entry2.path);
    index_free(&index);
    index_free(&loaded_index);
    unlink(index_file);
}

void test_index_find_entry(void) {
    index_t index;
    index_init(&index);
    
    /* Add test entry */
    index_entry_t entry;
    entry.path = strdup("test/file.txt");
    memset(entry.hash, 0xCC, 32);  /* Fill with test pattern */
    entry.mode = 0644;
    entry.size = 512;
    entry.mtime = time(NULL);
    
    index_add_entry(&index, &entry);
    
    /* Find existing entry */
    const index_entry_t *found = index_find_entry(&index, "test/file.txt");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("test/file.txt", found->path);
    TEST_ASSERT_EQUAL_MEMORY(entry.hash, found->hash, 32);
    
    /* Try to find non-existent entry */
    found = index_find_entry(&index, "nonexistent.txt");
    TEST_ASSERT_NULL(found);
    
    /* Clean up */
    free(entry.path);
    index_free(&index);
}

/* Unity test runner */
int main(void) {
    UNITY_BEGIN();
    
    /* Hash function tests */
    RUN_TEST(test_hash_data_consistency);
    RUN_TEST(test_hash_data_different_inputs);
    RUN_TEST(test_hash_file_with_temp_file);
    RUN_TEST(test_hash_file_nonexistent);
    RUN_TEST(test_hash_to_string_conversion);
    
    /* Object storage tests */
    RUN_TEST(test_object_path_creation);
    RUN_TEST(test_object_store_and_restore_file);
    
    /* Index tests */
    RUN_TEST(test_index_create_and_load);
    RUN_TEST(test_index_find_entry);
    
    return UNITY_END();
}