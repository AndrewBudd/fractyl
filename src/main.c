// main.c - Entry point for Fractyl

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
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
        printf("  init                   Initialize a new repository\n");
        printf("  snapshot [-m <message>] Create a new snapshot\n");
        printf("  restore <snapshot-id>  Restore to a snapshot\n");
        printf("  list                   List all snapshots\n");
        printf("  delete <snapshot-id>   Delete a snapshot\n");
        printf("  diff <snap-a> <snap-b> Compare two snapshots\n");
        printf("  show <snapshot-id>     Show detailed snapshot info\n");
        printf("  daemon <command>       Manage background daemon\n");
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
        if (strcmp(opts.command, "init") == 0) {
            return cmd_init(argc, argv);
        } else if (strcmp(opts.command, "snapshot") == 0) {
            return cmd_snapshot(argc, argv);
        } else if (strcmp(opts.command, "restore") == 0) {
            return cmd_restore(argc, argv);
        } else if (strcmp(opts.command, "list") == 0) {
            return cmd_list(argc, argv);
        } else if (strcmp(opts.command, "delete") == 0) {
            return cmd_delete(argc, argv);
        } else if (strcmp(opts.command, "diff") == 0) {
            return cmd_diff(argc, argv);
        } else if (strcmp(opts.command, "show") == 0) {
            return cmd_show(argc, argv);
        } else if (strcmp(opts.command, "daemon") == 0) {
            return cmd_daemon(argc, argv);
        } else {
            printf("Unknown command: %s\n", opts.command);
            printf("Use --help to see available commands\n");
            return 1;
        }
    }
    // Check if we're in a repository
    char *repo_root = fractyl_find_repo_root(NULL);
    if (repo_root) {
        // We're in a repository, do an auto-snapshot
        free(repo_root);
        char *args[] = {"frac", "snapshot", NULL};
        return cmd_snapshot(2, args);
    }
    
    printf("Fractyl not initialized. Use 'frac init' to initialize (see --help)\n");
    return 0;
}
