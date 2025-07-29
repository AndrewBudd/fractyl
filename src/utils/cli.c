// cli.c - CLI arg parsing implementation (stub)
#include "cli.h"
#include <string.h>

int parse_cli_args(int argc, char **argv, cli_options_t *opts) {
    // Very minimal stub: just zero out opts and copy first non-flag arg as command
    if (!opts) return -1;
    memset(opts, 0, sizeof(cli_options_t));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) opts->help = 1;
        else if (strcmp(argv[i], "--version") == 0) opts->version = 1;
        else if (strcmp(argv[i], "--debug") == 0) opts->debug = 1;
        else if (!opts->command) opts->command = argv[i];
    }
    return 0;
}
