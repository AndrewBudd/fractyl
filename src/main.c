// main.c - Entry point for Fractyl

#include <stdio.h>
#include <string.h>
#include "fractyl.h"
#include "commands.h"
#include "utils/cli.h"
#include "utils/fs.h"

int main(int argc, char **argv) {
    cli_options_t opts;
    parse_cli_args(argc, argv, &opts);

    if (opts.help) {
        printf("Fractyl -- help\n");
        printf("Usage: frac <command> [options]\n");
        printf("Commands:\n");
        printf("  snapshot -m <message>  Create a new snapshot\n");
        printf("  restore <snapshot-id>  Restore to a snapshot\n");
        printf("  list                   List all snapshots\n");
        printf("  delete <snapshot-id>   Delete a snapshot\n");
        printf("  diff <snap-a> <snap-b> Compare two snapshots\n");
        printf("  --test-utils           Run utility tests\n");
        printf("Options:\n");
        printf("  --help                 Show this help\n");
        printf("  --version              Show version\n");
        printf("  --debug                Enable debug output\n");
        return 0;
    }
    if (opts.version) {
        printf("Fractyl version %s\n", FRACTYL_VERSION);
        return 0;
    }
    if (opts.command && strcmp(opts.command, "--test-utils") == 0) {
        printf("--- Test Harness: FS/CLI utils ---\n");
        // Test: file_exists on self
        const char *testf = "src/main.c";
        printf("file_exists(%s): %d\n", testf, file_exists(testf));
        printf("is_directory(%s): %d\n", testf, is_directory(testf));
        const char *testd = "src";
        printf("is_directory(%s): %d\n", testd, is_directory(testd));
        char *fake = "thisdoesnotexist.xyz";
        printf("file_exists(%s): %d\n", fake, file_exists(fake));
        // Test: CLI parse again
        char *myargv[] = {
            "frac", "commit", "--debug", "foo.c", NULL
        };
        cli_options_t opts2;
        parse_cli_args(4, myargv, &opts2);
        printf("parse_cli_args stub: command=%s debug=%d help=%d version=%d\n", 
               opts2.command ? opts2.command : "(null)", opts2.debug, opts2.help, opts2.version);
        return 0;
    }
    if (opts.command) {
        // Dispatch to command handlers
        if (strcmp(opts.command, "snapshot") == 0) {
            return cmd_snapshot(argc, argv);
        } else if (strcmp(opts.command, "restore") == 0) {
            return cmd_restore(argc, argv);
        } else if (strcmp(opts.command, "list") == 0) {
            return cmd_list(argc, argv);
        } else if (strcmp(opts.command, "delete") == 0) {
            return cmd_delete(argc, argv);
        } else if (strcmp(opts.command, "diff") == 0) {
            return cmd_diff(argc, argv);
        } else {
            printf("Unknown command: %s\n", opts.command);
            printf("Use --help to see available commands\n");
            return 1;
        }
    }
    printf("Fractyl initialized. No action (see --help)\n");
    return 0;
}
