#include "args/vm_options.h"
#include "defines/version.h"
#include <stdio.h>
#include <string.h>

AcuVmOptions AcuVmOptions_Default(void) {
    return (AcuVmOptions){
        .bytecode_path = NULL,
        .trace_execution = false,
        .dump_stats = false,
        .verify_only = false,
        .show_help = false,
        .show_version = false,
        .app_argc = 0,
        .app_argv = NULL,
    };
}

static void SetError(char *err_buf, size_t err_cap, const char *msg) {
    if (err_buf && err_cap > 0) {
        snprintf(err_buf, err_cap, "%s", msg);
    }
}

static void SetErrorFmt(char *err_buf, size_t err_cap, const char *fmt, const char *arg) {
    if (err_buf && err_cap > 0) {
        snprintf(err_buf, err_cap, fmt, arg);
    }
}

bool AcuVmOptions_Parse(AcuVmOptions *opts, int argc, char **argv, char *err_buf, size_t err_cap) {
    if (!opts || argc < 1 || !argv) {
        SetError(err_buf, err_cap, "Invalid arguments passed to options parser");
        return false;
    }

    *opts = AcuVmOptions_Default();

    int i = 1;
    for (; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        if (arg[0] == '-') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                opts->show_help = true;
                return true;
            }
            if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
                opts->show_version = true;
                return true;
            }
            if (strcmp(arg, "-t") == 0 || strcmp(arg, "--trace") == 0) {
                opts->trace_execution = true;
                continue;
            }
            if (strcmp(arg, "-s") == 0 || strcmp(arg, "--stats") == 0) {
                opts->dump_stats = true;
                continue;
            }
            if (strcmp(arg, "--verify-only") == 0) {
                opts->verify_only = true;
                continue;
            }

            SetErrorFmt(err_buf, err_cap, "Unknown option: '%s'", arg);
            return false;
        }

        if (!opts->bytecode_path) {
            opts->bytecode_path = arg;
            i++;
            break;
        }
    }

    if (i < argc) {
        opts->app_argc = argc - i;
        opts->app_argv = &argv[i];
    }

    if (!opts->show_help && !opts->show_version && !opts->bytecode_path) {
        SetError(err_buf, err_cap, "No bytecode file specified");
        return false;
    }

    return true;
}

void AcuVmOptions_PrintHelp(const char *program_name) {
    const char *name = program_name ? program_name : "acuvm";
    printf("Usage: %s [options] <file.acub> [--] [args...]\n\n", name);
    printf("Options:\n");
    printf("  -t, --trace          Trace bytecode execution instruction-by-instruction\n");
    printf("  -s, --stats          Print execution statistics (time, instruction count)\n");
    printf("      --verify-only    Verify chunk integrity and exit without running\n");
    printf("  -v, --version        Show virtual machine version\n");
    printf("  -h, --help           Display this help message\n");
}

void AcuVmOptions_PrintVersion(void) {
    printf("acu VM v%d.%d.%d (target: %s) by %s\n", ACU_CURRENT_VERSION_MAJOR,
           ACU_CURRENT_VERSION_MINOR, ACU_CURRENT_VERSION_PATCH, ACU_TARGET, ACU_COMPILER);
}
