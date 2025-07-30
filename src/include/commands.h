#ifndef COMMANDS_H
#define COMMANDS_H

#include "fractyl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Command function signatures
int cmd_init(int argc, char **argv);
int cmd_snapshot(int argc, char **argv);
int cmd_restore(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_delete(int argc, char **argv);
int cmd_diff(int argc, char **argv);

// Helper functions
int fractyl_init_repo(const char *path);
char* fractyl_find_repo_root(const char *start_path);

#ifdef __cplusplus
}
#endif

#endif // COMMANDS_H