#include "analyzer/analyzer.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "common/error.h"
#include "defines/defines.h"
#include "fs/file_manager.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "parser/parser.h"
#include "types/string_view.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    FileId file_id;
    StringView import_name;
} AcuWorkItem;

bool AcuWorkspace_Pass1_ParseAndDiscover(AcuWorkspace *ws, StringView main_module_name) {
    AcuResult status = {0};
    FileId main_file =
        AcuFileManager_ResolveAndLoad(ws->files, ACU_NULL_IDX, main_module_name, &status);
    if (unlikely(status != ACU_ERR_OK)) {
        AcuError err = {.code = status, .file = ACU_NULL_IDX};
        V_Push(&ws->errors, err);
        return false;
    }

    TypedVector(AcuWorkItem) work_que = {0};
    V_Init(&work_que);

    AcuWorkItem initial_item = {.file_id = main_file, .import_name = main_module_name};
    V_Push(&work_que, initial_item);

    while (V_Count(&work_que) > 0) {
        AcuWorkItem current_item = V_Pop(&work_que);
        ModuleId existing_mod =
            AcuModuleManager_GetSourceModuleByFile(ws->modules, current_item.file_id);

        if (existing_mod != ACU_NULL_IDX) {
            AcuModuleStage current_stage =
                AcuModuleManager_GetSourceModuleStage(ws->modules, existing_mod);
            if (current_stage >= ACU_MODULE_STAGE_PARSED) {
                if (current_item.file_id == main_file) {
                    ws->main_module_id = existing_mod;
                }
                continue;
            }
        }

        StringView file_source = AcuFileManager_GetSource(ws->files, current_item.file_id);

        AcuParser parser = {0};
        AcuParser_Init(&parser, ws->builder, ws->interner, ws->literals, file_source,
                       current_item.file_id);

        AstNodeIdx root_node_idx = AcuParser_ParseModule(&parser);
        u32 parser_error_count = V_Count(&parser.errors);
        for (u32 i = 0; i < parser_error_count; ++i) {
            V_Push(&ws->errors, V_At(&parser.errors, i));
        }

        AcuParser_Free(&parser);

        if (unlikely(root_node_idx == ACU_NULL_IDX || parser_error_count > 0)) {
            continue;
        }

        ModuleId current_mod = AcuModuleManager_GetOrCreate(ws->modules, current_item.import_name,
                                                            current_item.file_id, root_node_idx);

        if (current_item.file_id == main_file) {
            ws->main_module_id = current_mod;
        }

        AcuModuleManager_SetSourceModuleStage(ws->modules, current_mod, ACU_MODULE_STAGE_PARSED);

        AstNode *module_node = AcuAstBuilder_GetNode(ws->builder, root_node_idx);

        ExtraIdx current_extra_idx = module_node->as.module.start_idx;
        ExtraIdx end_extra_idx = current_extra_idx + module_node->as.module.statements_count;

        while (current_extra_idx < end_extra_idx) {
            AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, current_extra_idx);
            AstNode *node = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

            if (node->type == AST_IMPORT) {
                LiteralIdx module_name_literal = node->as.import_stmt.module_name;
                StringView module_name =
                    AcuLiteralPool_GetString(ws->literals, module_name_literal);

                ModuleId v_mod = AcuModuleManager_FindVirtual(ws->modules, module_name);
                if (v_mod != ACU_NULL_IDX) {
                    ++current_extra_idx;
                    continue;
                }

                FileId new_file_id = AcuFileManager_ResolveAndLoad(ws->files, current_item.file_id,
                                                                   module_name, &status);
                if (unlikely(status != ACU_ERR_OK)) {
                    AcuWorkspace_PushError(ws, stmt_idx, status, (AcuError){0});
                    ++current_extra_idx;
                    continue;
                }

                AcuWorkItem new_item = {.file_id = new_file_id, .import_name = module_name};
                V_Push(&work_que, new_item);
            }

            ++current_extra_idx;
        }
    }

    V_Free(&work_que);
    return V_Count(&ws->errors) == 0 && ws->main_module_id != ACU_NULL_IDX;
}
