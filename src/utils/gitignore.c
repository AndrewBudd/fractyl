#include "gitignore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/stat.h>

// Structure to hold gitignore rules
typedef struct {
    char *pattern;
    int is_directory_only;  // Pattern ends with /
    int is_negation;        // Pattern starts with !
} gitignore_rule_t;

typedef struct {
    gitignore_rule_t *rules;
    size_t count;
    size_t capacity;
} gitignore_t;

// Parse a gitignore file and return rules
static gitignore_t* parse_gitignore_file(const char *gitignore_path) {
    FILE *f = fopen(gitignore_path, "r");
    if (!f) {
        return NULL;
    }
    
    gitignore_t *gi = malloc(sizeof(gitignore_t));
    if (!gi) {
        fclose(f);
        return NULL;
    }
    
    gi->rules = NULL;
    gi->count = 0;
    gi->capacity = 0;
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        
        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') {
            continue;
        }
        
        // Skip lines with only whitespace
        int only_whitespace = 1;
        for (size_t i = 0; i < len; i++) {
            if (line[i] != ' ' && line[i] != '\t') {
                only_whitespace = 0;
                break;
            }
        }
        if (only_whitespace) {
            continue;
        }
        
        // Expand capacity if needed
        if (gi->count >= gi->capacity) {
            gi->capacity = gi->capacity ? gi->capacity * 2 : 16;
            gi->rules = realloc(gi->rules, gi->capacity * sizeof(gitignore_rule_t));
        }
        
        gitignore_rule_t *rule = &gi->rules[gi->count];
        
        // Check for negation
        rule->is_negation = 0;
        char *pattern_start = line;
        if (line[0] == '!') {
            rule->is_negation = 1;
            pattern_start = line + 1;
        }
        
        // Check for directory-only pattern
        rule->is_directory_only = 0;
        size_t pattern_len = strlen(pattern_start);
        if (pattern_len > 0 && pattern_start[pattern_len-1] == '/') {
            rule->is_directory_only = 1;
            pattern_start[pattern_len-1] = '\0';  // Remove trailing slash
        }
        
        rule->pattern = strdup(pattern_start);
        if (!rule->pattern) {
            continue;  // Skip on allocation failure
        }
        
        gi->count++;
    }
    
    fclose(f);
    return gi;
}

// Free gitignore structure
static void free_gitignore(gitignore_t *gi) {
    if (!gi) return;
    
    for (size_t i = 0; i < gi->count; i++) {
        free(gi->rules[i].pattern);
    }
    free(gi->rules);
    free(gi);
}

// Check if a path matches a gitignore pattern
static int matches_pattern(const char *path, const gitignore_rule_t *rule, int is_directory) {
    if (rule->is_directory_only && !is_directory) {
        return 0;
    }
    
    const char *pattern = rule->pattern;
    
    // Handle absolute patterns (starting with /)
    if (pattern[0] == '/') {
        pattern++; // Skip leading slash
        return fnmatch(pattern, path, FNM_PATHNAME) == 0;
    }
    
    // Handle patterns that should match anywhere in the path
    if (strchr(pattern, '/') == NULL) {
        // No slash in pattern - should match basename or any directory component
        const char *basename = strrchr(path, '/');
        basename = basename ? basename + 1 : path;
        
        if (fnmatch(pattern, basename, 0) == 0) {
            return 1;
        }
        
        // Also check if any directory component matches
        char *path_copy = strdup(path);
        char *token = strtok(path_copy, "/");
        while (token) {
            if (fnmatch(pattern, token, 0) == 0) {
                free(path_copy);
                return 1;
            }
            token = strtok(NULL, "/");
        }
        free(path_copy);
        return 0;
    } else {
        // Pattern contains slash - match against full path
        return fnmatch(pattern, path, FNM_PATHNAME) == 0;
    }
}

// Check if a path should be ignored according to gitignore rules
int gitignore_should_ignore(const char *repo_root, const char *relative_path, int is_directory) {
    if (!repo_root || !relative_path) {
        return 0;
    }
    
    // Always ignore .git directory
    if (strcmp(relative_path, ".git") == 0 || strstr(relative_path, ".git/") == relative_path) {
        return 1;
    }
    
    char gitignore_path[2048];
    snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", repo_root);
    
    gitignore_t *gi = parse_gitignore_file(gitignore_path);
    if (!gi) {
        return 0;  // No .gitignore file or couldn't parse it
    }
    
    int ignored = 0;
    
    // Apply rules in order
    for (size_t i = 0; i < gi->count; i++) {
        gitignore_rule_t *rule = &gi->rules[i];
        
        if (matches_pattern(relative_path, rule, is_directory)) {
            if (rule->is_negation) {
                ignored = 0;  // Negation rule - don't ignore
            } else {
                ignored = 1;  // Regular rule - ignore
            }
        }
    }
    
    free_gitignore(gi);
    return ignored;
}

// Check if a path should be ignored (convenience function that determines if it's a directory)
int gitignore_should_ignore_path(const char *repo_root, const char *full_path, const char *relative_path) {
    if (!repo_root || !full_path || !relative_path) {
        return 0;
    }
    
    struct stat st;
    int is_directory = 0;
    if (stat(full_path, &st) == 0) {
        is_directory = S_ISDIR(st.st_mode);
    }
    
    return gitignore_should_ignore(repo_root, relative_path, is_directory);
}