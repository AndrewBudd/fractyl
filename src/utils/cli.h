// cli.h - Command line parsing helpers
#ifndef CLI_H
#define CLI_H

typedef struct {
    int help;
    int version;
    int debug;
    char *command;
    char **args;
    int arg_count;
} cli_options_t;

int parse_cli_args(int argc, char **argv, cli_options_t *opts);

#endif // CLI_H
