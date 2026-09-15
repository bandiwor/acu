#include "analyzer/analyzer.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "defines/defines.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "table/symbol_table.h"
#include <assert.h>
#include <stddef.h>

typedef TypedVector(SymbolId) SymbolIdVec;

static void AcuPass5_CollectCalls(AcuWorkspace *ws, AstNodeIdx expr_idx, SymbolIdVec *worklist) {
    if (expr_idx == ACU_NULL_IDX)
        return;

    AstNode *node = AcuAstBuilder_GetNode(ws->builder, expr_idx);

    switch (node->type) {
        case AST_CALL: {
            SymbolId callee_id = V_At(&ws->node_analysis, expr_idx).call.callee_sym;
            if (callee_id != ACU_NULL_IDX) {
                AcuSymbol *callee_sym = AcuSymbolTable_Get(ws->symbols, callee_id);
                if (callee_sym && callee_sym->kind == ACU_SYMBOL_FUNCTION) {
                    if ((callee_sym->flags & ACU_SYMBOL_FLAG_REACHABLE) == 0) {
                        callee_sym->flags |= ACU_SYMBOL_FLAG_REACHABLE;
                        V_Push(worklist, callee_id);
                    }
                }
            }

            u32 arg_count = node->as.call.arguments_count;
            ExtraIdx arg_start = node->as.call.start_idx;
            for (u32 i = 0; i < arg_count; ++i) {
                AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, arg_start + i);
                AcuPass5_CollectCalls(ws, arg_idx, worklist);
            }
            break;
        }

        case AST_UNARY:
            AcuPass5_CollectCalls(ws, node->as.unary.right, worklist);
            break;

        case AST_BINARY:
            AcuPass5_CollectCalls(ws, node->as.binary.left, worklist);
            AcuPass5_CollectCalls(ws, node->as.binary.right, worklist);
            break;

        case AST_ASSIGN:
            AcuPass5_CollectCalls(ws, node->as.assign.value, worklist);
            break;

        case AST_BLOCK: {
            ExtraIdx start = node->as.block.start_idx;
            u32 count = node->as.block.statements_count;
            for (u32 i = 0; i < count; ++i) {
                AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, start + i);
                AcuPass5_CollectCalls(ws, stmt_idx, worklist);
            }
            break;
        }

        case AST_IF:
            AcuPass5_CollectCalls(ws, node->as.if_stmt.condition, worklist);
            AcuPass5_CollectCalls(ws, node->as.if_stmt.then_body, worklist);
            if (node->as.if_stmt.else_body != ACU_NULL_IDX) {
                AcuPass5_CollectCalls(ws, node->as.if_stmt.else_body, worklist);
            }
            break;

        case AST_LOOP:
            AcuPass5_CollectCalls(ws, node->as.loop_stmt.body, worklist);
            break;

        case AST_WHILE:
            AcuPass5_CollectCalls(ws, node->as.while_stmt.condition, worklist);
            AcuPass5_CollectCalls(ws, node->as.while_stmt.body, worklist);
            break;

        case AST_RETURN:
            if (node->as.ret_stmt.expr != ACU_NULL_IDX) {
                AcuPass5_CollectCalls(ws, node->as.ret_stmt.expr, worklist);
            }
            break;

        case AST_BREAK:
            if (node->as.break_stmt.expr != ACU_NULL_IDX) {
                AcuPass5_CollectCalls(ws, node->as.break_stmt.expr, worklist);
            }
            break;

        case AST_TYPE_CAST:
            AcuPass5_CollectCalls(ws, node->as.type_cast.expr, worklist);
            break;

        case AST_DISCARD:
            AcuPass5_CollectCalls(ws, node->as.discard.expr, worklist);
            break;

        case AST_VAR_DECL:
            AcuPass5_CollectCalls(ws, node->as.var_decl.init_expr, worklist);
            break;

        default:
            break;
    }
}

void AcuWorkspace_Pass5_Reachability(AcuWorkspace *ws) {
    SymbolIdVec worklist = {0};
    V_Init(&worklist);

    u32 globals_count = (u32)V_Count(&ws->global_init_order);
    for (u32 i = 0; i < globals_count; ++i) {
        SymbolId sym_id = V_At(&ws->global_init_order, i);
        AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
        AstNode *decl = AcuAstBuilder_GetNode(ws->builder, sym->decl_node);
        AcuPass5_CollectCalls(ws, decl->as.var_decl.init_expr, &worklist);
    }

    ModuleId main_mod_id = AcuModuleManager_GetSourceModuleByFile(ws->modules, 0);
    AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(ws->modules, main_mod_id);
    AstNode *main_node = AcuAstBuilder_GetNode(ws->builder, root_idx);

    ExtraIdx cur = main_node->as.module.start_idx;
    ExtraIdx end = cur + main_node->as.module.statements_count;

    while (cur < end) {
        AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, cur);
        AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

        if (stmt->type != AST_FN_DECL && stmt->type != AST_VAR_DECL && stmt->type != AST_IMPORT) {
            AcuPass5_CollectCalls(ws, stmt_idx, &worklist);
        }
        ++cur;
    }

    while (V_Count(&worklist) > 0) {
        SymbolId fn_sym_id = V_Pop(&worklist);
        AcuSymbol *fn_sym = AcuSymbolTable_Get(ws->symbols, fn_sym_id);
        AstNode *fn_node = AcuAstBuilder_GetNode(ws->builder, fn_sym->decl_node);

        if (fn_node->as.fn_decl.body != ACU_NULL_IDX) {
            AcuPass5_CollectCalls(ws, fn_node->as.fn_decl.body, &worklist);
        }
    }

    V_Free(&worklist);
}
