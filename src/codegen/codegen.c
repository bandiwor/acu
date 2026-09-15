#include "codegen/codegen.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "codegen/codegen_internal.h"
#include "codegen/const_eval.h"
#include "codegen/reg_alloc.h"
#include "defines/bytecode.h"
#include "defines/defines.h"
#include "module/module_manager.h"
#include "pool/constant_pool.h"
#include "pool/string_pool.h"
#include "table/symbol_table.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

AcuChunk AcuChunk_Create(void) {
    AcuChunk chunk = {0};
    V_Init(&chunk.code);
    chunk.constants = AcuConstantPool_Create();
    chunk.strings = AcuStringPool_Create();
    chunk.globals_count = 0;
    return chunk;
}

void AcuChunk_Free(AcuChunk *chunk) {
    V_Free(&chunk->code);
    AcuConstantPool_Free(&chunk->constants);
    AcuStringPool_Free(&chunk->strings);
}

static void AcuCodeGen_InitGlobals(AcuCodeGen *cg) {
    u32 total_symbols = cg->ws->symbols->count;

    V_InitWithCapacity(&cg->sym_to_global, total_symbols);
    V_SetCount(&cg->sym_to_global, total_symbols);

    memset(V_Elements(&cg->sym_to_global), 0xFF, total_symbols * sizeof(u32));

    u32 globals_count = 0;

    const u32 modules_count = AcuModuleManager_GetCount(cg->ws->modules);
    for (u32 m = 0; m < modules_count; ++m) {
        AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(cg->ws->modules, m);
        AstNode *module_node = AcuAstBuilder_GetNode(cg->ws->builder, root_idx);

        ExtraIdx current = module_node->as.module.start_idx;
        ExtraIdx end = current + module_node->as.module.statements_count;

        while (current < end) {
            AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, current);
            AstNode *stmt = AcuAstBuilder_GetNode(cg->ws->builder, stmt_idx);

            if (stmt->type == AST_VAR_DECL) {
                SymbolId sym_id = V_At(&cg->ws->node_analysis, stmt_idx).var_decl.sym_id;

                if (V_At(&cg->sym_to_global, sym_id) == ACU_NULL_IDX) {
                    V_At(&cg->sym_to_global, sym_id) = globals_count++;
                }
            }
            ++current;
        }
    }

    cg->chunk->globals_count = globals_count;
}

bool AcuCodeGen_CompileWorkspace(AcuWorkspace *ws, AcuChunk *out_chunk) {
    AcuCodeGen cg = {
        .ws = ws,
        .chunk = out_chunk,
        .max_reg = 0,
        .last_stored_global = ACU_NULL_IDX,
        .last_stored_reg = 0,
        .current_fn_sym = ACU_NULL_IDX,
        .current_fn_entry_ip = 0,
        .is_tail_pos = false,
        .self_tail_emitted = false,
    };

    AcuCodeGen_InitGlobals(&cg);

    AcuRegSet_Init(&cg.reg_set);
    V_Init(&cg.locals);
    V_Init(&cg.loops);
    V_Init(&cg.fn_patches);
    V_Init(&cg.fn_offsets);

    u32 sym_count = (u32)V_Count(&ws->symbols->symbols);
    V_SetCount(&cg.fn_offsets, sym_count);
    memset(V_Elements(&cg.fn_offsets), 0, sizeof(u32) * sym_count);

    u32 globals_count = (u32)V_Count(&ws->global_init_order);
    out_chunk->globals_count = globals_count;

    for (u32 i = 0; i < globals_count; ++i) {
        SymbolId sym_id = V_At(&ws->global_init_order, i);
        AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);

        AcuCodeGen_EmitExpr(&cg, sym->decl_node, ACU_TARGET_NONE);

        if (AcuCodeGen_IsNever(&cg, sym->decl_node)) {
            break;
        }
    }

    AstNodeIdx main_root = AcuModuleManager_GetSourceModuleNode(ws->modules, ws->main_module_id);
    AstNode *main_node = AcuAstBuilder_GetNode(ws->builder, main_root);

    ExtraIdx cur_extra = main_node->as.module.start_idx;
    ExtraIdx end_extra = cur_extra + main_node->as.module.statements_count;

    while (cur_extra < end_extra) {
        AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, cur_extra);
        AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

        if (stmt->type != AST_VAR_DECL && stmt->type != AST_FN_DECL && stmt->type != AST_IMPORT) {
            if (!AcuCodeGen_IsPure(cg.ws, stmt_idx)) {
                cg.is_tail_pos = false;
                AcuCodeGen_EmitExpr(&cg, stmt_idx, ACU_TARGET_NONE);
                AcuCodeGen_InvalidateGlobalCache(&cg);

                if (AcuCodeGen_IsNever(&cg, stmt_idx)) {
                    break;
                }
            }
        }
        ++cur_extra;
    }

    AcuCodeGen_Emit(&cg, AcuInst_Encode_NONE(OP_HALT));

    for (SymbolId s = 0; s < sym_count; ++s) {
        AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, s);
        if (!sym || sym->kind != ACU_SYMBOL_FUNCTION) {
            continue;
        }

        if ((sym->flags & ACU_SYMBOL_FLAG_REACHABLE) == 0 || sym->decl_node == ACU_NULL_IDX) {
            continue;
        }

        AstNode *fn_node = AcuAstBuilder_GetNode(ws->builder, sym->decl_node);
        if (fn_node->type != AST_FN_DECL) {
            continue;
        }

        AstFnDeclPayload *payload =
            AcuAstBuilder_GetFnDeclPayload(ws->builder, fn_node->as.fn_decl.payload_idx);

        u32 fn_entry_ip = (u32)V_Count(&out_chunk->code);
        V_At(&cg.fn_offsets, s) = fn_entry_ip;

        cg.current_fn_sym = s;
        cg.current_fn_entry_ip = fn_entry_ip;
        cg.is_tail_pos = true;
        cg.self_tail_emitted = false;

        V_SetCount(&cg.locals, 0);
        AcuRegSet_Init(&cg.reg_set);
        AcuCodeGen_InvalidateGlobalCache(&cg);

        assert(payload->params_count <= 256 && "Functions cannot accept more than 256 parameters!");
        if (payload->params_count > cg.max_reg) {
            cg.max_reg = payload->params_count;
        }

        for (u32 p = 0; p < payload->params_count; ++p) {
            AstNodeIdx p_idx =
                AcuAstBuilder_GetIdxByExtra(ws->builder, payload->params_start_idx + p);

            SymbolId param_sym_id = V_At(&ws->node_analysis, p_idx).var_decl.sym_id;
            AcuLocalBinding param_bind = {
                .sym_id = param_sym_id,
                .reg = (AcuReg)p,
            };

            V_Push(&cg.locals, param_bind);
            AcuRegSet_MarkUsed(&cg.reg_set, (AcuReg)p);
        }

        AstNodeIdx body_idx = fn_node->as.fn_decl.body;
        bool has_return_val = (payload->return_type != ACU_NULL_IDX);
        AcuTarget body_target = has_return_val ? ACU_TARGET_AUTO : ACU_TARGET_NONE;

        if (body_idx != ACU_NULL_IDX) {
            AcuTarget ret_target = AcuCodeGen_EmitExpr(&cg, body_idx, body_target);

            if (!cg.self_tail_emitted && !AcuCodeGen_IsNever(&cg, body_idx)) {
                if (has_return_val && AcuTarget_IsRealReg(ret_target)) {
                    AcuReg r = AcuTarget_ToRealReg(ret_target);
                    AcuCodeGen_Emit(&cg, AcuInst_Encode_A(OP_RET, r));
                    if (!AcuCodeGen_IsLocalReg(&cg, r)) {
                        AcuCodeGen_FreeReg(&cg, r);
                    }
                } else {
                    AcuCodeGen_Emit(&cg, AcuInst_Encode_NONE(OP_RET_VOID));
                }
            }
        } else {
            AcuCodeGen_Emit(&cg, AcuInst_Encode_NONE(OP_RET_VOID));
        }
    }

    u32 patches_count = (u32)V_Count(&cg.fn_patches);
    for (u32 p = 0; p < patches_count; ++p) {
        AcuFuncCallPatch patch = V_At(&cg.fn_patches, p);
        assert(patch.fn_sym_id < sym_count && "Callee symbol ID out of bounds!");

        u32 target_ip = V_At(&cg.fn_offsets, patch.fn_sym_id);
        assert(patch.inst_idx + 1 < V_Count(&out_chunk->code) &&
               "Patch instruction offset out of bounds!");

        V_At(&out_chunk->code, patch.inst_idx + 1) = (AcuInstruction)target_ip;
    }

    assert(V_Count(&cg.loops) == 0 && "Compiler error: unclosed loop context leak!");

    V_Free(&cg.locals);
    V_Free(&cg.loops);
    V_Free(&cg.fn_patches);
    V_Free(&cg.fn_offsets);
    V_Free(&cg.sym_to_global);

    return true;
}
