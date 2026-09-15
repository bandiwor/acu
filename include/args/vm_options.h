#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *bytecode_path;

    bool trace_execution;
    bool dump_stats;
    bool verify_only;

    bool show_help;
    bool show_version;

    int app_argc;
    char **app_argv;
} AcuVmOptions;

AcuVmOptions AcuVmOptions_Default(void);

bool AcuVmOptions_Parse(AcuVmOptions *opts, int argc, char **argv, char *err_buf, size_t err_cap);

void AcuVmOptions_PrintHelp(const char *program_name);

void AcuVmOptions_PrintVersion(void);
