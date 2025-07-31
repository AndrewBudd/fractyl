#include "hash.h"
#include "../include/fractyl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#else
#error "OpenSSL is required for hashing functionality"
#endif

int hash_file(const char *file_path, unsigned char *hash_out) {
    if (!file_path || !hash_out) {
        return FRACTYL_ERROR_GENERIC;
    }

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        return FRACTYL_ERROR_IO;
    }

    SHA256_CTX sha256;
    if (!SHA256_Init(&sha256)) {
        fclose(fp);
        return FRACTYL_ERROR_GENERIC;
    }

    unsigned char buffer[8192];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (!SHA256_Update(&sha256, buffer, bytes_read)) {
            fclose(fp);
            return FRACTYL_ERROR_GENERIC;
        }
    }
    
    fclose(fp);

    if (!SHA256_Final(hash_out, &sha256)) {
        return FRACTYL_ERROR_GENERIC;
    }

    return FRACTYL_OK;
}

int hash_data(const void *data, size_t size, unsigned char *hash_out) {
    if (!data || !hash_out) {
        return FRACTYL_ERROR_GENERIC;
    }

    SHA256_CTX sha256;
    if (!SHA256_Init(&sha256)) {
        return FRACTYL_ERROR_GENERIC;
    }

    if (!SHA256_Update(&sha256, data, size)) {
        return FRACTYL_ERROR_GENERIC;
    }

    if (!SHA256_Final(hash_out, &sha256)) {
        return FRACTYL_ERROR_GENERIC;
    }

    return FRACTYL_OK;
}

void hash_to_string(const unsigned char *hash, char *hex_out) {
    if (!hash || !hex_out) return;
    
    for (int i = 0; i < FRACTYL_HASH_SIZE; i++) {
        sprintf(hex_out + (i * 2), "%02x", hash[i]);
    }
    hex_out[FRACTYL_HASH_HEX_SIZE - 1] = '\0';
}

int string_to_hash(const char *hex, unsigned char *hash_out) {
    if (!hex || !hash_out) {
        return FRACTYL_ERROR_GENERIC;
    }

    if (strlen(hex) != FRACTYL_HASH_HEX_SIZE - 1) {
        return FRACTYL_ERROR_GENERIC;
    }

    for (int i = 0; i < FRACTYL_HASH_SIZE; i++) {
        if (sscanf(hex + (i * 2), "%2hhx", &hash_out[i]) != 1) {
            return FRACTYL_ERROR_GENERIC;
        }
    }

    return FRACTYL_OK;
}

int hash_compare(const unsigned char *hash1, const unsigned char *hash2) {
    if (!hash1 || !hash2) return -1;
    return memcmp(hash1, hash2, FRACTYL_HASH_SIZE);
}

int hash_is_zero(const unsigned char *hash) {
    if (!hash) return 1;
    
    for (int i = 0; i < FRACTYL_HASH_SIZE; i++) {
        if (hash[i] != 0) return 0;
    }
    return 1;
}