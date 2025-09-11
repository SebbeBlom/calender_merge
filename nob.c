#define NOB_IMPLEMENTATION
#include "include/nob.h"

bool build_main_executable() {
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-o",
                   "freeslots", "calender_merge.c");
    if (!nob_cmd_run(&cmd)) return 1;

    return true;
}

bool clean() {
    nob_log(NOB_INFO, "Cleaning up...");

    // Remove binaries
    if (nob_file_exists("freeslots")) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "freeslots");
        if (!nob_cmd_run(&cmd)) return false;
    }

    if (nob_file_exists("nob.old")) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "nob.old");
        if (!nob_cmd_run(&cmd)) return false;
    }

    if (nob_file_exists("nob")) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "rm", "nob");
        if (!nob_cmd_run(&cmd)) return false;
    }

    return true;
}

void usage(const char *program) {
    nob_log(NOB_INFO, "Usage: %s [SUBCOMMAND]", program);
    nob_log(NOB_INFO, "  SUBCOMMANDS:");
    nob_log(NOB_INFO, "    main                   - Build main executable");
    nob_log(NOB_INFO, "    clean                  - Clean build artifacts");
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    const char *program = nob_shift(argv, argc);

    if (argc == 0) {
        usage(program);
        return 1;
    }

    const char *subcommand = nob_shift(argv, argc);

    if (strcmp(subcommand, "main") == 0) {
        if (!build_main_executable()) return 1;
    } else if (strcmp(subcommand, "clean") == 0) {
        if (!clean()) return 1;
    } else {
        nob_log(NOB_ERROR, "Unknown subcommand: %s", subcommand);
        usage(program);
        return 1;
    }

    return 0;
}
