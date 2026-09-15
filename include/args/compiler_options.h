#pragma once

#include "defines/defines.h"
#include <stdbool.h>
#include <stddef.h>

#define ACU_MAX_INCLUDE_PATHS 32

typedef struct {
    const char *main_module;
    const char *output_path;

    bool dump_ast;
    bool dump_ast_types;
    bool dump_ast_pretty;

    bool dump_bc_pre_opt;
    bool dump_bc_post_opt;
    bool no_peephole;

    bool color;
    bool verbose;
    bool show_help;
    bool show_version;

    const char *include_paths[ACU_MAX_INCLUDE_PATHS];
    u32 include_path_count;
} AcuCompilerOptions;

AcuCompilerOptions AcuCompilerOptions_Default(void);

bool AcuCompilerOptions_Parse(AcuCompilerOptions *opts, int argc, char **argv, char *err_buf,
                              size_t err_cap);

void AcuCompilerOptions_PrintHelp(const char *program_name);

void AcuCompilerOptions_PrintVersion(void);
