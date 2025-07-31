#ifndef HASH_H
#define HASH_H

#include "../include/fractyl.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRACTYL_HASH_SIZE 32  // SHA-256 hash size in bytes
#define FRACTYL_HASH_HEX_SIZE (FRACTYL_HASH_SIZE * 2 + 1)  // Hex string size

// Hash a file by path
int hash_file(const char *file_path, unsigned char *hash_out);

// Hash a data buffer
int hash_data(const void *data, size_t size, unsigned char *hash_out);

// Convert hash bytes to hex string
void hash_to_string(const unsigned char *hash, char *hex_out);

// Convert hex string to hash bytes
int string_to_hash(const char *hex, unsigned char *hash_out);

// Compare two hashes
int hash_compare(const unsigned char *hash1, const unsigned char *hash2);

// Check if hash is all zeros (empty/uninitialized)
int hash_is_zero(const unsigned char *hash);

#ifdef __cplusplus
}
#endif

#endif // HASH_H