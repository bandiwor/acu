#include "analyzer/analyzer.h"
#include "args/compiler_options.h"
#include "ast/ast_builder.h"
#include "codegen/codegen.h"
#include "common/print_error.h"
#include "defines/defines.h"
#include "disasm/disasm.h"
#include "fs/file_manager.h"
#include "interner/string_interner.h"
#include "interner/type_interner.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "optimizer/optimizer.h"
#include "serializer/serializer.h"
#include "stringify/ast_stringifier.h"
#include "table/symbol_table.h"
#include "types/string_view.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    AcuCompilerOptions opts;
    char err_buf[256];

    if (!AcuCompilerOptions_Parse(&opts, argc, argv, err_buf, sizeof(err_buf))) {
        fprintf(stderr, "error: %s\n", err_buf);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (opts.show_help) {
        AcuCompilerOptions_PrintHelp(argv[0]);
        return EXIT_SUCCESS;
    }

    if (opts.show_version) {
        AcuCompilerOptions_PrintVersion();
        return EXIT_SUCCESS;
    }

    AcuAstBuilder builder = AcuAstBuilder_Create();
    AcuStringInterner interner = AcuStringInterner_Create();
    AcuLiteralPool literals = AcuLiteralPool_Create();
    AcuSymbolTable symbols = AcuSymbolTable_Create();
    AcuModuleManager modules = AcuModuleManager_Create(&symbols);
    AcuFileManager files = AcuFileManager_Create();
    AcuTypeInterner types = AcuTypeInterner_Create();

    AcuWorkspace ws =
        AcuWorkspace_Create(&builder, &interner, &literals, &modules, &files, &symbols, &types);

    StringView main_module = StringView_CStr(opts.main_module);

    bool analyze_ok = AcuWorkspace_Analyze(&ws, main_module);
    if (!analyze_ok) {
        AcuError *errors = V_Elements(&ws.errors);
        u32 errors_count = (u32)V_Count(&ws.errors);

        AcuError_PrintAll(errors, errors_count, &files, &types);
        AcuWorkspace_Free(&ws);
        return EXIT_FAILURE;
    }

    if (opts.dump_ast) {
        const u32 modules_count = AcuModuleManager_GetCount(&modules);
        for (u32 current_module = 0; current_module < modules_count; ++current_module) {
            AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(&modules, current_module);

            AcuAstStringifier stringifier = AcuAstStringifier_Create(
                &builder, &interner, &literals, &types, V_Elements(&ws.node_types),
                opts.dump_ast_pretty, opts.dump_ast_types);
            AcuAstStringifier_StringifyNode(&stringifier, root_idx);

            StringView sv = AcuAstStringifier_GetSV(&stringifier);
            fprintf(stdout, "%.*s\n", (int)sv.length, (const char *)sv.data);
        }
    }

    AcuChunk chunk = AcuChunk_Create();
    bool codegen_ok = AcuCodeGen_CompileWorkspace(&ws, &chunk);

    if (codegen_ok) {
        putchar('\n');
        if (opts.dump_bc_pre_opt) {
            AcuDisasm_DumpChunk(stdout, &chunk);
        }
        if (!opts.no_peephole) {
            AcuChunk_Optimize(&chunk);

            if (opts.dump_bc_post_opt) {
                AcuDisasm_DumpChunk(stdout, &chunk);
            }
        }
    } else {
        fprintf(stderr, "[ERROR] Code generation failed.\n");
    }

    if (!AcuChunk_SerializePath(opts.output_path, &chunk)) {
        fprintf(stderr, "[ERROR] Serialization to '%s' failed.\n", opts.output_path);
    }

    AcuChunk_Free(&chunk);
    AcuWorkspace_Free(&ws);

    return (int)codegen_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
