#include "args/compiler_options.h"
#include "defines/version.h"
#include <stdio.h>
#include <string.h>

static inline bool str_eq(const char *a, const char *b) {
    return __builtin_strcmp(a, b) == 0;
}

static inline bool str_starts_with(const char *str, const char *prefix) {
    return __builtin_strncmp(str, prefix, __builtin_strlen(prefix)) == 0;
}

AcuCompilerOptions AcuCompilerOptions_Default(void) {
    return (AcuCompilerOptions){
        .main_module = NULL,
        .output_path = "out.acuc",
        .dump_ast = false,
        .dump_ast_types = false,
        .dump_ast_pretty = false,
        .dump_bc_pre_opt = false,
        .dump_bc_post_opt = false,
        .no_peephole = false,
        .color = true,
        .verbose = false,
        .show_help = false,
        .show_version = false,
        .include_paths = {0},
        .include_path_count = 0,
    };
}

void AcuCompilerOptions_PrintHelp(const char *program_name) {
    printf("Usage: %s [options] <main_module>\n\n", program_name);
    printf("Options:\n");
    printf("  -o, --output <file>         Set output binary path (default: a.out)\n");
    printf("  -I, --include <dir>         Add search directory for imports\n");
    printf("      --no-peephole           Disable peephole optimizer passes\n\n");

    printf("Dump & Debugging Options:\n");
    printf("      --dump-ast              Dump AST hierarchy\n");
    printf(
        "      --dump-ast-types        Include resolved types in AST dump (requires --dump-ast)\n");
    printf("      --dump-ast-pretty       Format AST output as tree (requires --dump-ast)\n");
    printf("      --dump-bc-raw           Dump raw bytecode before optimizations\n");
    printf("      --dump-bc               Dump optimized bytecode (incompatible with "
           "--no-peephole)\n\n");

    printf("General Options:\n");
    printf("      --color / --no-color    Enable/disable terminal colors\n");
    printf("      --verbose               Enable diagnostic output\n");
    printf("  -h, --help                  Print this help message\n");
    printf("  -v, --version               Print compiler version\n");
}

void AcuCompilerOptions_PrintVersion(void) {
    printf("acu compiler v%d.%d.%d (target: %s) by %s\n", ACU_CURRENT_VERSION_MAJOR,
           ACU_CURRENT_VERSION_MINOR, ACU_CURRENT_VERSION_PATCH, ACU_TARGET, ACU_COMPILER);
}

static bool validate_options(AcuCompilerOptions *opts, char *err_buf, size_t err_cap) {
    if (opts->show_help || opts->show_version) {
        return true;
    }

    if (!opts->main_module) {
        snprintf(err_buf, err_cap, "no input main module or file specified");
        return false;
    }

    if (opts->dump_ast_types && !opts->dump_ast) {
        snprintf(err_buf, err_cap, "flag '--dump-ast-types' requires '--dump-ast' to be enabled");
        return false;
    }

    if (opts->dump_ast_pretty && !opts->dump_ast) {
        snprintf(err_buf, err_cap, "flag '--dump-ast-pretty' requires '--dump-ast' to be enabled");
        return false;
    }

    if (opts->no_peephole && opts->dump_bc_post_opt) {
        snprintf(err_buf, err_cap,
                 "cannot use '--dump-bc' with '--no-peephole' (use '--dump-bc-raw' instead)");
        return false;
    }

    return true;
}

bool AcuCompilerOptions_Parse(AcuCompilerOptions *opts, int argc, char **argv, char *err_buf,
                              size_t err_cap) {
    *opts = AcuCompilerOptions_Default();

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            if (!opts->main_module) {
                opts->main_module = arg;
                continue;
            }
            snprintf(err_buf, err_cap, "unexpected positional argument: '%s'", arg);
            return false;
        }

        if (str_eq(arg, "-h") || str_eq(arg, "--help")) {
            opts->show_help = true;
            return true;
        }
        if (str_eq(arg, "-v") || str_eq(arg, "--version")) {
            opts->show_version = true;
            return true;
        }

        if (str_eq(arg, "--dump-ast")) {
            opts->dump_ast = true;
            continue;
        }
        if (str_eq(arg, "--dump-ast-types")) {
            opts->dump_ast_types = true;
            continue;
        }
        if (str_eq(arg, "--dump-ast-pretty")) {
            opts->dump_ast_pretty = true;
            continue;
        }

        if (str_eq(arg, "--dump-bc-raw")) {
            opts->dump_bc_pre_opt = true;
            continue;
        }
        if (str_eq(arg, "--dump-bc")) {
            opts->dump_bc_post_opt = true;
            continue;
        }
        if (str_eq(arg, "--no-peephole")) {
            opts->no_peephole = true;
            continue;
        }

        if (str_eq(arg, "--verbose")) {
            opts->verbose = true;
            continue;
        }
        if (str_eq(arg, "--color")) {
            opts->color = true;
            continue;
        }
        if (str_eq(arg, "--no-color")) {
            opts->color = false;
            continue;
        }

        if (str_eq(arg, "-o") || str_eq(arg, "--output")) {
            if (++i >= argc) {
                snprintf(err_buf, err_cap, "option '%s' requires an argument", arg);
                return false;
            }
            opts->output_path = argv[i];
            continue;
        }
        if (str_starts_with(arg, "--output=")) {
            opts->output_path = arg + 9;
            continue;
        }
        if (str_starts_with(arg, "-o") && arg[2] != '\0') {
            opts->output_path = arg + 2;
            continue;
        }

        if (str_eq(arg, "-I") || str_eq(arg, "--include")) {
            if (++i >= argc) {
                snprintf(err_buf, err_cap, "option '%s' requires a directory path", arg);
                return false;
            }
            if (opts->include_path_count >= ACU_MAX_INCLUDE_PATHS) {
                snprintf(err_buf, err_cap, "maximum include paths limit reached (%d)",
                         ACU_MAX_INCLUDE_PATHS);
                return false;
            }
            opts->include_paths[opts->include_path_count++] = argv[i];
            continue;
        }
        if (str_starts_with(arg, "-I") && arg[2] != '\0') {
            if (opts->include_path_count >= ACU_MAX_INCLUDE_PATHS) {
                snprintf(err_buf, err_cap, "maximum include paths limit reached (%d)",
                         ACU_MAX_INCLUDE_PATHS);
                return false;
            }
            opts->include_paths[opts->include_path_count++] = arg + 2;
            continue;
        }

        snprintf(err_buf, err_cap, "unknown command line option: '%s'", arg);
        return false;
    }

    return validate_options(opts, err_buf, err_cap);
}
