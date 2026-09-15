#include "analyzer/analyzer.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "common/error.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "interner/type_interner.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "types/string_view.h"

static bool AcuWorkspace_CheckModuleLevelRedefinition(AcuWorkspace *ws, ScopeId mod_scope,
                                                      StringId name, AstNodeIdx stmt_idx) {
    SymbolId existing = AcuSymbolTable_LookupExact(ws->symbols, mod_scope, name);
    if (unlikely(existing != ACU_NULL_IDX)) {
        AcuError err = {0};
        AcuWorkspace_PushError(ws, stmt_idx, ACU_ERR_ANALYZER_REDEFINITION_OF_SYMBOL, err);
        return false;
    }
    return true;
}

bool AcuWorkspace_Pass2_BindSymbols(AcuWorkspace *ws) {
    u32 modules_count = AcuModuleManager_GetCount(ws->modules);

    TypedVector(TypeId) param_types = {0};
    V_Init(&param_types);

    for (ModuleId m = 0; m < modules_count; ++m) {
        if (AcuModuleManager_GetSourceModuleStage(ws->modules, m) < ACU_MODULE_STAGE_PARSED) {
            continue;
        }

        FileId file_id = AcuModuleManager_GetSourceModuleFile(ws->modules, m);
        AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(ws->modules, m);
        ScopeId mod_scope = AcuModuleManager_GetModuleScope(ws->modules, m);
        AstNode *module_node = AcuAstBuilder_GetNode(ws->builder, root_idx);

        ExtraIdx current_extra_idx = module_node->as.module.start_idx;
        ExtraIdx end_extra_idx = current_extra_idx + module_node->as.module.statements_count;

        while (current_extra_idx < end_extra_idx) {
            AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, current_extra_idx);
            AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

            switch (stmt->type) {
                case AST_FN_DECL: {
                    StringId name = stmt->as.fn_decl.name;
                    if (!AcuWorkspace_CheckModuleLevelRedefinition(ws, mod_scope, name, stmt_idx)) {
                        break;
                    }

                    AstFnDeclPayload *payload =
                        AcuAstBuilder_GetFnDeclPayload(ws->builder, stmt->as.fn_decl.payload_idx);

                    V_SetCount(&param_types, 0);

                    TypeId ret_type = TYPE_PRIMITIVE_UNIT;
                    if (payload->return_type != ACU_NULL_IDX) {
                        ret_type = AcuWorkspace_ResolveAstType(ws, mod_scope, payload->return_type);
                        if (unlikely(ret_type == ACU_NULL_IDX)) {
                            ret_type = TYPE_PRIMITIVE_UNIT;
                        }
                    }

                    for (u32 i = 0; i < payload->params_count; ++i) {
                        AstNodeIdx param_node_idx =
                            AcuAstBuilder_GetIdxByExtra(ws->builder, payload->params_start_idx + i);
                        AstNode *param_node = AcuAstBuilder_GetNode(ws->builder, param_node_idx);

                        TypeAstNodeIdx param_ast_type = param_node->as.fn_param_decl.type;

                        TypeId param_type_id =
                            AcuWorkspace_ResolveAstType(ws, mod_scope, param_ast_type);

                        if (unlikely(param_type_id == ACU_NULL_IDX)) {
                            param_type_id = TYPE_PRIMITIVE_UNIT;
                        }

                        V_Push(&param_types, param_type_id);
                    }

                    TypeId fn_type_id = AcuTypeInterner_GetFunc(
                        ws->types, ret_type, V_Elements(&param_types), V_Count(&param_types));

                    bool is_exported = (stmt->flags & AST_FLAG_EXPORTED) != 0;

                    AcuSymbolTable_AddFn(ws->symbols, mod_scope, name, fn_type_id, stmt_idx,
                                         is_exported);
                    break;
                }

                case AST_VAR_DECL: {
                    StringId name = stmt->as.var_decl.name;
                    if (!AcuWorkspace_CheckModuleLevelRedefinition(ws, mod_scope, name, stmt_idx)) {
                        break;
                    }

                    TypeAstNodeIdx type_hint = stmt->as.var_decl.type_hint;

                    TypeId declared_type = ACU_NULL_IDX;
                    if (type_hint != ACU_NULL_IDX) {
                        declared_type = AcuWorkspace_ResolveAstType(ws, mod_scope, type_hint);
                    }

                    bool is_exported = (stmt->flags & AST_FLAG_EXPORTED) != 0;
                    bool is_mutable = (stmt->flags & AST_FLAG_MUTABLE) != 0;

                    AcuSymbolTable_AddGlobalVar(ws->symbols, mod_scope, name, declared_type,
                                                stmt_idx, is_exported, is_mutable);
                    break;
                }

                case AST_IMPORT: {
                    StringId alias = stmt->as.import_stmt.alias;
                    if (!AcuWorkspace_CheckModuleLevelRedefinition(ws, mod_scope, alias,
                                                                   stmt_idx)) {
                        break;
                    }

                    LiteralIdx module_path_literal = stmt->as.import_stmt.module_name;
                    StringView module_path =
                        AcuLiteralPool_GetString(ws->literals, module_path_literal);

                    ModuleId target_module_id =
                        AcuModuleManager_FindVirtual(ws->modules, module_path);
                    if (target_module_id == ACU_NULL_IDX) {
                        AcuResult status;
                        FileId target_file_id =
                            AcuFileManager_ResolveAndLoad(ws->files, file_id, module_path, &status);

                        if (unlikely(status != ACU_ERR_OK)) {
                            AcuWorkspace_PushError(ws, stmt_idx, status, (AcuError){0});
                            break;
                        }

                        target_module_id =
                            AcuModuleManager_GetSourceModuleByFile(ws->modules, target_file_id);

                        if (unlikely(target_module_id == ACU_NULL_IDX)) {
                            break;
                        }
                    }

                    bool is_exported = (stmt->flags & AST_FLAG_EXPORTED) != 0;

                    AcuSymbolTable_AddModule(ws->symbols, mod_scope, alias, stmt_idx,
                                             target_module_id, is_exported);
                    break;
                }

                default:
                    break;
            }

            ++current_extra_idx;
        }

        AcuModuleManager_SetSourceModuleStage(ws->modules, m, ACU_MODULE_STAGE_SYMBOLS_BOUND);
    }

    V_Free(&param_types);

    return V_Count(&ws->errors) == 0;
}
