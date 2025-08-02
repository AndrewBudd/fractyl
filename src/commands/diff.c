#include "../include/commands.h"
#include "../include/core.h"
#include "../utils/json.h"
#include "../utils/paths.h"
#include "../utils/git.h"
#include "../utils/snapshots.h"
#include "../core/index.h"
#include "../core/objects.h"
#include "../core/hash.h"
#include "../vendor/xdiff/fractyl-diff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <strings.h>


static void print_hash(const unsigned char *hash) {
    for (int i = 0; i < 32; i++) {
        printf("%02x", hash[i]);
    }
}

// Magic number patterns for common binary formats
static struct {
    const unsigned char *magic;
    size_t len;
    const char *description;
} magic_patterns[] = {
    // Executables
    {(const unsigned char*)"\x7f\x45\x4c\x46", 4, "ELF"}, // ELF executables
    {(const unsigned char*)"MZ", 2, "PE/DOS"}, // PE/DOS executables
    {(const unsigned char*)"\xfe\xed\xfa\xce", 4, "Mach-O 32-bit BE"}, // Mach-O
    {(const unsigned char*)"\xce\xfa\xed\xfe", 4, "Mach-O 32-bit LE"}, // Mach-O
    {(const unsigned char*)"\xfe\xed\xfa\xcf", 4, "Mach-O 64-bit BE"}, // Mach-O 64-bit
    {(const unsigned char*)"\xcf\xfa\xed\xfe", 4, "Mach-O 64-bit LE"}, // Mach-O 64-bit
    
    // Images
    {(const unsigned char*)"\xff\xd8\xff", 3, "JPEG"}, // JPEG
    {(const unsigned char*)"\x89PNG\r\n\x1a\n", 8, "PNG"}, // PNG
    {(const unsigned char*)"GIF8", 4, "GIF"}, // GIF
    {(const unsigned char*)"BM", 2, "BMP"}, // BMP
    {(const unsigned char*)"\x00\x00\x01\x00", 4, "ICO"}, // ICO
    
    // Archives
    {(const unsigned char*)"PK\x03\x04", 4, "ZIP"}, // ZIP
    {(const unsigned char*)"PK\x05\x06", 4, "ZIP"}, // ZIP (empty)
    {(const unsigned char*)"PK\x07\x08", 4, "ZIP"}, // ZIP (spanned)
    {(const unsigned char*)"\x1f\x8b", 2, "GZIP"}, // GZIP
    {(const unsigned char*)"BZh", 3, "BZIP2"}, // BZIP2
    {(const unsigned char*)"\x37\x7a\xbc\xaf\x27\x1c", 6, "7Z"}, // 7Z
    {(const unsigned char*)"Rar!\x1a\x07\x00", 7, "RAR"}, // RAR
    {(const unsigned char*)"ustar", 5, "TAR"}, // TAR (at offset 257)
    
    // Media (simplified patterns)
    {(const unsigned char*)"ftyp", 4, "MP4"}, // MP4 (simplified - ftyp at offset 4)
    {(const unsigned char*)"ID3", 3, "MP3"}, // MP3 with ID3
    {(const unsigned char*)"\xff\xfb", 2, "MP3"}, // MP3
    {(const unsigned char*)"\xff\xf3", 2, "MP3"}, // MP3
    {(const unsigned char*)"\xff\xf2", 2, "MP3"}, // MP3
    {(const unsigned char*)"RIFF", 4, "RIFF"}, // WAV/AVI (check for WAVE/AVI later)
    
    // Documents  
    {(const unsigned char*)"%PDF", 4, "PDF"}, // PDF
    {(const unsigned char*)"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8, "MS Office"}, // MS Office
    {(const unsigned char*)"PK\x03\x04", 4, "Office XML"}, // Office XML (also ZIP)
    
    // Databases
    {(const unsigned char*)"SQLite format 3\x00", 16, "SQLite"}, // SQLite
    
    {NULL, 0, NULL} // Sentinel
};

// Check for magic number patterns
static int is_binary_magic(const void *data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    
    const unsigned char *bytes = (const unsigned char *)data;
    
    for (int i = 0; magic_patterns[i].magic; i++) {
        if (size >= magic_patterns[i].len) {
            if (memcmp(bytes, magic_patterns[i].magic, magic_patterns[i].len) == 0) {
                return 1;
            }
            
            // Special case for TAR files (magic at offset 257)
            if (strcmp(magic_patterns[i].description, "TAR") == 0 && size > 262) {
                if (memcmp(bytes + 257, magic_patterns[i].magic, magic_patterns[i].len) == 0) {
                    return 1;
                }
            }
            
            // Special case for RIFF (check if it's WAV or AVI)
            if (strcmp(magic_patterns[i].description, "RIFF") == 0 && size >= 12) {
                if (memcmp(bytes + 8, "WAVE", 4) == 0 || memcmp(bytes + 8, "AVI ", 4) == 0) {
                    return 1;
                }
            }
        }
    }
    
    return 0;
}

// Check if data is binary by looking for null bytes or non-printable characters
static int is_binary_data(const void *data, size_t size) {
    if (!data || size == 0) {
        return 0; // Empty files are considered text
    }
    
    // First check for magic numbers (most reliable)
    if (is_binary_magic(data, size)) {
        return 1;
    }
    
    const unsigned char *bytes = (const unsigned char *)data;
    size_t check_size = size < 8192 ? size : 8192; // Check first 8KB
    size_t null_count = 0;
    size_t non_printable_count = 0;
    
    for (size_t i = 0; i < check_size; i++) {
        unsigned char byte = bytes[i];
        
        // Null bytes strongly indicate binary
        if (byte == 0) {
            null_count++;
            if (null_count > 1) {
                return 1; // Multiple null bytes = binary
            }
        }
        
        // Count non-printable characters (except common whitespace)
        if (byte < 32 && byte != '\t' && byte != '\n' && byte != '\r') {
            non_printable_count++;
        } else if (byte > 126) {
            non_printable_count++;
        }
    }
    
    // If more than 30% non-printable characters, consider binary
    if (check_size > 0 && (non_printable_count * 100 / check_size) > 30) {
        return 1;
    }
    
    return 0;
}

// Check if file is binary based on extension
static int is_binary_extension(const char *path) {
    if (!path) return 0;
    
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    
    // Common binary extensions
    const char *binary_extensions[] = {
        ".exe", ".dll", ".so", ".dylib", ".a", ".lib",
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".ico", ".tiff",
        ".mp3", ".mp4", ".avi", ".mov", ".wav", ".flac",
        ".zip", ".tar", ".gz", ".bz2", ".7z", ".rar",
        ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
        ".bin", ".dat", ".db", ".sqlite", ".sqlite3",
        ".o", ".obj", ".pyc", ".class",
        NULL
    };
    
    for (int i = 0; binary_extensions[i]; i++) {
        if (strcasecmp(ext, binary_extensions[i]) == 0) {
            return 1;
        }
    }
    
    return 0;
}

// Find the most recent snapshot - use CURRENT file if available
__attribute__((unused)) static char* find_latest_snapshot(const char *fractyl_dir) {
    char current_path[2048];
    snprintf(current_path, sizeof(current_path), "%s/CURRENT", fractyl_dir);
    
    // Try to read from CURRENT file first
    FILE *f = fopen(current_path, "r");
    if (f) {
        char current_id[64];
        if (fgets(current_id, sizeof(current_id), f)) {
            fclose(f);
            // Strip newline
            size_t len = strlen(current_id);
            if (len > 0 && current_id[len-1] == '\n') {
                current_id[len-1] = '\0';
            }
            return strdup(current_id);
        }
        fclose(f);
    }
    
    // Fallback to scanning timestamps
    char snapshots_dir[2048];
    snprintf(snapshots_dir, sizeof(snapshots_dir), "%s/snapshots", fractyl_dir);
    
    DIR *d = opendir(snapshots_dir);
    if (!d) {
        return NULL;
    }
    
    char *latest_id = NULL;
    time_t latest_timestamp = 0;
    
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".json")) {
            continue;
        }
        
        char snapshot_path[2048];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", snapshots_dir, entry->d_name);
        
        snapshot_t snapshot;
        if (json_load_snapshot(&snapshot, snapshot_path) == FRACTYL_OK) {
            if (snapshot.timestamp > latest_timestamp) {
                latest_timestamp = snapshot.timestamp;
                free(latest_id);
                latest_id = strdup(snapshot.id);
            }
            json_free_snapshot(&snapshot);
        }
    }
    
    closedir(d);
    return latest_id;
}



// Load index from snapshot's stored index hash
static int load_snapshot_index(const unsigned char *index_hash, const char *fractyl_dir, index_t *index) {
    void *index_data;
    size_t index_size;
    
    // Load the index data from object storage
    int result = object_load(index_hash, fractyl_dir, &index_data, &index_size);
    if (result != FRACTYL_OK) {
        return result;
    }
    
    // Write to a temporary file to use index_load
    char temp_path[] = "/tmp/fractyl_index_XXXXXX";
    int temp_fd = mkstemp(temp_path);
    if (temp_fd == -1) {
        free(index_data);
        return FRACTYL_ERROR_IO;
    }
    
    if (write(temp_fd, index_data, index_size) != (ssize_t)index_size) {
        close(temp_fd);
        unlink(temp_path);
        free(index_data);
        return FRACTYL_ERROR_IO;
    }
    close(temp_fd);
    free(index_data);
    
    // Load the index from temporary file
    result = index_load(index, temp_path);
    unlink(temp_path);
    
    return result;
}

// Compare file contents between two snapshots with line-by-line diff
static int compare_file_contents(const index_entry_t *entry_a, const index_entry_t *entry_b, 
                                const char *path, const char *fractyl_dir) {
    void *data_a = NULL, *data_b = NULL;
    size_t size_a = 0, size_b = 0;
    
    // Load file content from first snapshot
    if (entry_a) {
        if (object_load(entry_a->hash, fractyl_dir, &data_a, &size_a) != FRACTYL_OK) {
            printf("Warning: Could not load content for %s from first snapshot\n", path);
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Load file content from second snapshot
    if (entry_b) {
        if (object_load(entry_b->hash, fractyl_dir, &data_b, &size_b) != FRACTYL_OK) {
            printf("Warning: Could not load content for %s from second snapshot\n", path);
            free(data_a);
            return FRACTYL_ERROR_IO;
        }
    }
    
    // Check if file is binary by extension first (faster)
    int is_binary = is_binary_extension(path);
    
    // If not binary by extension, check content for both files
    if (!is_binary) {
        if (data_a && is_binary_data(data_a, size_a)) {
            is_binary = 1;
        } else if (data_b && is_binary_data(data_b, size_b)) {
            is_binary = 1;
        }
    }
    
    if (is_binary) {
        // Handle binary files
        printf("diff --fractyl a/%s b/%s\n", path, path);
        
        if (!entry_a && entry_b) {
            // File added
            printf("Binary file b/%s added\n", path);
        } else if (entry_a && !entry_b) {
            // File deleted
            printf("Binary file a/%s deleted\n", path);
        } else if (entry_a && entry_b) {
            // File changed - compare hashes to see if actually different
            if (memcmp(entry_a->hash, entry_b->hash, 32) == 0) {
                // Same content, no output needed
            } else {
                printf("Binary files a/%s and b/%s differ\n", path, path);
                printf("Size: %zu bytes -> %zu bytes\n", size_a, size_b);
            }
        }
    } else {
        // Handle text files - perform the diff using xdiff
        int result = fractyl_diff_unified(path, data_a, size_a, path, data_b, size_b, 3);
        if (result != 0) {
            // Diff failed or found differences
            // Note: fractyl_diff_unified should handle its own output
        }
    }
    
    // Cleanup
    free(data_a);
    free(data_b);
    
    return FRACTYL_OK;
}

// Compare file contents between two snapshots  
static int compare_snapshot_contents(const snapshot_t *snap_a, const snapshot_t *snap_b, const char *fractyl_dir) {
    printf("\nFile-by-file comparison:\n");
    
    // Compare the index hashes to determine if there are differences
    if (memcmp(snap_a->index_hash, snap_b->index_hash, 32) == 0) {
        printf("No differences detected between snapshots\n");
        return FRACTYL_OK;
    }
    
    // Load indices from both snapshots
    index_t index_a, index_b;
    if (load_snapshot_index(snap_a->index_hash, fractyl_dir, &index_a) != FRACTYL_OK) {
        printf("Could not load index from first snapshot\n");
        return FRACTYL_ERROR_IO;
    }
    
    if (load_snapshot_index(snap_b->index_hash, fractyl_dir, &index_b) != FRACTYL_OK) {
        printf("Could not load index from second snapshot\n");
        index_free(&index_a);
        return FRACTYL_ERROR_IO;
    }
    
    // Create a unified list of all files from both indices
    typedef struct {
        char *path;
        const index_entry_t *entry_a;
        const index_entry_t *entry_b;
    } file_comparison_t;
    
    file_comparison_t *comparisons = NULL;
    size_t comparison_count = 0;
    size_t comparison_capacity = 0;
    
    // Add all files from index_a
    for (size_t i = 0; i < index_a.count; i++) {
        if (comparison_count >= comparison_capacity) {
            comparison_capacity = comparison_capacity ? comparison_capacity * 2 : 16;
            comparisons = realloc(comparisons, comparison_capacity * sizeof(file_comparison_t));
        }
        comparisons[comparison_count].path = strdup(index_a.entries[i].path);
        comparisons[comparison_count].entry_a = &index_a.entries[i];
        comparisons[comparison_count].entry_b = index_find_entry(&index_b, index_a.entries[i].path);
        comparison_count++;
    }
    
    // Add files that are only in index_b
    for (size_t i = 0; i < index_b.count; i++) {
        const index_entry_t *found_in_a = index_find_entry(&index_a, index_b.entries[i].path);
        if (!found_in_a) {
            if (comparison_count >= comparison_capacity) {
                comparison_capacity = comparison_capacity ? comparison_capacity * 2 : 16;
                comparisons = realloc(comparisons, comparison_capacity * sizeof(file_comparison_t));
            }
            comparisons[comparison_count].path = strdup(index_b.entries[i].path);
            comparisons[comparison_count].entry_a = NULL;
            comparisons[comparison_count].entry_b = &index_b.entries[i];
            comparison_count++;
        }
    }
    
    // Compare each file
    for (size_t i = 0; i < comparison_count; i++) {
        const file_comparison_t *comp = &comparisons[i];
        
        // Skip if files are identical (same hash)
        if (comp->entry_a && comp->entry_b && 
            memcmp(comp->entry_a->hash, comp->entry_b->hash, 32) == 0) {
            continue;
        }
        
        // Perform the actual diff
        compare_file_contents(comp->entry_a, comp->entry_b, comp->path, fractyl_dir);
    }
    
    // Cleanup
    for (size_t i = 0; i < comparison_count; i++) {
        free(comparisons[i].path);
    }
    free(comparisons);
    index_free(&index_a);
    index_free(&index_b);
    
    return FRACTYL_OK;
}

int cmd_diff(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: frac diff <snapshot-a> <snapshot-b>\n");
        printf("Compare files between two snapshots\n");
        printf("\nSnapshot identifiers can be:\n");
        printf("  abc123                          # Hash prefix (minimum 4 chars)\n");
        printf("  abc123...                       # Full hash\n");
        printf("  -1                              # Previous snapshot\n");
        printf("  -2                              # Two snapshots back\n");
        printf("\nExamples:\n");
        printf("  frac diff -2 -1                 # Compare last two snapshots\n");
        printf("  frac diff abc123 -1             # Compare prefix with latest\n");
        printf("\nUse 'frac list' to see available snapshots\n");
        return 1;
    }
    
    const char *snapshot_a_input = argv[2];
    const char *snapshot_b_input = argv[3];
    
    // Find repository root
    char *repo_root = fractyl_find_repo_root(NULL);
    if (!repo_root) {
        printf("Error: Not in a fractyl repository. Use 'frac init' to initialize.\n");
        return 1;
    }
    
    char fractyl_dir[2048];
    snprintf(fractyl_dir, sizeof(fractyl_dir), "%s/.fractyl", repo_root);
    
    // Get current git branch
    char *git_branch = paths_get_current_branch(repo_root);
    
    // Resolve snapshot identifiers (prefixes or relative notation) to full IDs
    char snapshot_a[65], snapshot_b[65];
    
    // Resolve snapshot A
    int result = resolve_snapshot_id(snapshot_a_input, fractyl_dir, git_branch, snapshot_a);
    if (result != FRACTYL_OK) {
        printf("Error: Snapshot '%s' not found\n", snapshot_a_input);
        printf("Use 'frac list' to see available snapshots\n");
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    // Resolve snapshot B
    result = resolve_snapshot_id(snapshot_b_input, fractyl_dir, git_branch, snapshot_b);
    if (result != FRACTYL_OK) {
        printf("Error: Snapshot '%s' not found\n", snapshot_b_input);
        printf("Use 'frac list' to see available snapshots\n");
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    // Check if comparing snapshot with itself
    if (strcmp(snapshot_a, snapshot_b) == 0) {
        printf("Warning: Comparing snapshot with itself\n");
        printf("Snapshot '%s' is identical to itself\n", snapshot_a);
        free(repo_root);
        free(git_branch);
        return 0;
    }
    
    // Build paths to snapshot files
    char *snapshots_dir = paths_get_snapshots_dir(fractyl_dir, git_branch);
    if (!snapshots_dir) {
        printf("Error: Failed to get snapshots directory\n");
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    char snapshot_a_path[2048], snapshot_b_path[2048];
    snprintf(snapshot_a_path, sizeof(snapshot_a_path), "%s/%s.json", snapshots_dir, snapshot_a);
    snprintf(snapshot_b_path, sizeof(snapshot_b_path), "%s/%s.json", snapshots_dir, snapshot_b);
    free(snapshots_dir);
    
    // Load snapshot metadata for display
    snapshot_t snap_a, snap_b;
    if (json_load_snapshot(&snap_a, snapshot_a_path) != FRACTYL_OK) {
        printf("Error: Cannot load snapshot '%s'\n", snapshot_a);
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    if (json_load_snapshot(&snap_b, snapshot_b_path) != FRACTYL_OK) {
        printf("Error: Cannot load snapshot '%s'\n", snapshot_b);
        json_free_snapshot(&snap_a);
        free(repo_root);
        free(git_branch);
        return 1;
    }
    
    // Note: In a complete implementation, we would load the actual index
    // from the snapshot's index_hash. For now, we'll use a simplified approach.
    printf("diff %s..%s\n", snapshot_a, snapshot_b);
    printf("--- %s (%s)\n", snapshot_a, snap_a.description ? snap_a.description : "");
    printf("+++ %s (%s)\n", snapshot_b, snap_b.description ? snap_b.description : "");
    printf("\n");
    
    // Compare snapshots
    if (memcmp(snap_a.index_hash, snap_b.index_hash, 32) == 0) {
        printf("✓ Snapshots are identical\n");
        printf("  Both snapshots have the same file content and structure.\n");
    } else {
        printf("✗ Snapshots differ\n");
        printf("\nIndex hashes:\n");
        printf("  %s: ", snapshot_a);
        print_hash(snap_a.index_hash);
        printf("\n");
        printf("  %s: ", snapshot_b);
        print_hash(snap_b.index_hash);
        printf("\n");
        
        // Show timestamps for context
        printf("\nTimeline:\n");
        if (snap_a.timestamp < snap_b.timestamp) {
            printf("  %s (older) → %s (newer)\n", snapshot_a, snapshot_b);
        } else if (snap_a.timestamp > snap_b.timestamp) {
            printf("  %s (newer) ← %s (older)\n", snapshot_a, snapshot_b);
        } else {
            printf("  Both snapshots created at same time\n");
        }
        
        // Load indices from snapshots to perform file-by-file comparison
        if (compare_snapshot_contents(&snap_a, &snap_b, fractyl_dir) != FRACTYL_OK) {
            printf("\nWarning: Could not perform detailed file comparison\n");
            printf("To see which files changed, you can:\n");
            printf("  1. Use 'frac restore %s' to restore first snapshot\n", snapshot_a);
            printf("  2. Compare with working directory\n");
            printf("  3. Use 'frac restore %s' to restore second snapshot\n", snapshot_b);
        }
    }
    
    json_free_snapshot(&snap_a);
    json_free_snapshot(&snap_b);
    free(repo_root);
    free(git_branch);
    
    return 0;
}