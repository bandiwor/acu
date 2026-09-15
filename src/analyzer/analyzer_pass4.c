#include "analyzer/analyzer.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "defines/defines.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "table/symbol_table.h"
#include <stdbool.h>
#include <stdlib.h>

typedef TypedVector(SymbolId) SymbolIdVec;

static bool AcuPurity_HasDirectSideEffects(AcuWorkspace *ws, ScopeId scope, AstNodeIdx node_idx) {
    if (node_idx == ACU_NULL_IDX) {
        return false;
    }

    AstNode *node = AcuAstBuilder_GetNode(ws->builder, node_idx);

    switch (node->type) {
        case AST_ASSIGN: {
            AstNode *target = AcuAstBuilder_GetNode(ws->builder, node->as.assign.target);

            // 1. Присваивание по идентификатору: global_var = val
            if (target->type == AST_IDENTIFIER) {
                SymbolId sym_id =
                    AcuSymbolTable_Lookup(ws->symbols, scope, target->as.identifier.name);
                if (sym_id != ACU_NULL_IDX) {
                    AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
                    if (sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
                        return true;
                    }
                }
            }
            // 2. Присваивание через модуль: mod.global_var = val
            else if (target->type == AST_DOT) {
                AstNode *obj_node = AcuAstBuilder_GetNode(ws->builder, target->as.dot.object);
                AstNode *mem_node = AcuAstBuilder_GetNode(ws->builder, target->as.dot.member);

                if (obj_node->type == AST_IDENTIFIER && mem_node->type == AST_IDENTIFIER) {
                    SymbolId mod_sym_id =
                        AcuSymbolTable_Lookup(ws->symbols, scope, obj_node->as.identifier.name);
                    AcuSymbol *mod_sym = (mod_sym_id != ACU_NULL_IDX)
                                             ? AcuSymbolTable_Get(ws->symbols, mod_sym_id)
                                             : NULL;

                    if (mod_sym && mod_sym->kind == ACU_SYMBOL_MODULE) {
                        ScopeId mod_scope = AcuModuleManager_GetModuleScope(
                            ws->modules, mod_sym->as.target_module_id);
                        SymbolId var_sym_id = AcuSymbolTable_LookupExact(
                            ws->symbols, mod_scope, mem_node->as.identifier.name);

                        if (var_sym_id != ACU_NULL_IDX) {
                            AcuSymbol *var_sym = AcuSymbolTable_Get(ws->symbols, var_sym_id);
                            if (var_sym && var_sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
                                return true;
                            }
                        }
                    }
                }
            }

            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.assign.value);
        }

        case AST_BINARY:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.binary.left) ||
                   AcuPurity_HasDirectSideEffects(ws, scope, node->as.binary.right);

        case AST_UNARY:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.unary.right);

        case AST_TYPE_CAST:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.type_cast.expr);

        case AST_DISCARD:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.discard.expr);

        case AST_VAR_DECL:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.var_decl.init_expr);

        case AST_RETURN:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.ret_stmt.expr);

        case AST_BREAK:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.break_stmt.expr);

        case AST_IF:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.if_stmt.condition) ||
                   AcuPurity_HasDirectSideEffects(ws, scope, node->as.if_stmt.then_body) ||
                   AcuPurity_HasDirectSideEffects(ws, scope, node->as.if_stmt.else_body);

        case AST_LOOP:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.loop_stmt.body);

        case AST_WHILE:
            return AcuPurity_HasDirectSideEffects(ws, scope, node->as.while_stmt.condition) ||
                   AcuPurity_HasDirectSideEffects(ws, scope, node->as.while_stmt.body);

        case AST_BLOCK: {
            ExtraIdx start = node->as.block.start_idx;
            u32 count = node->as.block.statements_count;
            for (u32 i = 0; i < count; ++i) {
                AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, start + i);
                if (AcuPurity_HasDirectSideEffects(ws, scope, stmt_idx)) {
                    return true;
                }
            }
            return false;
        }

        default:
            return false;
    }
}

// ============================================================================
// Сбор зависимостей вызовов функций (Call Graph Collector)
// ============================================================================

static void AcuPurity_CollectCallees(AcuWorkspace *ws, ScopeId scope, AstNodeIdx node_idx,
                                     SymbolIdVec *out_callees, bool *has_unresolved_callee) {
    if (node_idx == ACU_NULL_IDX) {
        return;
    }

    AstNode *node = AcuAstBuilder_GetNode(ws->builder, node_idx);

    switch (node->type) {
        case AST_CALL: {
            AstNode *callee = AcuAstBuilder_GetNode(ws->builder, node->as.call.object);
            SymbolId callee_sym_id = ACU_NULL_IDX;

            // Вызов по прямому имени: foo(...)
            if (callee->type == AST_IDENTIFIER) {
                callee_sym_id =
                    AcuSymbolTable_Lookup(ws->symbols, scope, callee->as.identifier.name);
            }
            // Вызов через модуль: mod.foo(...)
            else if (callee->type == AST_DOT) {
                AstNode *obj_node = AcuAstBuilder_GetNode(ws->builder, callee->as.dot.object);
                AstNode *mem_node = AcuAstBuilder_GetNode(ws->builder, callee->as.dot.member);

                if (obj_node->type == AST_IDENTIFIER && mem_node->type == AST_IDENTIFIER) {
                    SymbolId mod_sym_id =
                        AcuSymbolTable_Lookup(ws->symbols, scope, obj_node->as.identifier.name);
                    AcuSymbol *mod_sym = (mod_sym_id != ACU_NULL_IDX)
                                             ? AcuSymbolTable_Get(ws->symbols, mod_sym_id)
                                             : NULL;

                    if (mod_sym && mod_sym->kind == ACU_SYMBOL_MODULE) {
                        ScopeId mod_scope = AcuModuleManager_GetModuleScope(
                            ws->modules, mod_sym->as.target_module_id);
                        callee_sym_id = AcuSymbolTable_LookupExact(ws->symbols, mod_scope,
                                                                   mem_node->as.identifier.name);
                    }
                }
            }

            if (callee_sym_id != ACU_NULL_IDX) {
                V_Push(out_callees, callee_sym_id);
            } else {
                // Вызов не удалось разрешить статически -> гарантированно считаем функцию грязной
                *has_unresolved_callee = true;
            }

            // Обход аргументов вызова
            u32 arg_count = node->as.call.arguments_count;
            ExtraIdx arg_start = node->as.call.start_idx;
            for (u32 i = 0; i < arg_count; ++i) {
                AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, arg_start + i);
                AcuPurity_CollectCallees(ws, scope, arg_idx, out_callees, has_unresolved_callee);
            }
            break;
        }

        case AST_BINARY:
            AcuPurity_CollectCallees(ws, scope, node->as.binary.left, out_callees,
                                     has_unresolved_callee);
            AcuPurity_CollectCallees(ws, scope, node->as.binary.right, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_UNARY:
            AcuPurity_CollectCallees(ws, scope, node->as.unary.right, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_TYPE_CAST:
            AcuPurity_CollectCallees(ws, scope, node->as.type_cast.expr, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_DISCARD:
            AcuPurity_CollectCallees(ws, scope, node->as.discard.expr, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_ASSIGN:
            AcuPurity_CollectCallees(ws, scope, node->as.assign.value, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_VAR_DECL:
            AcuPurity_CollectCallees(ws, scope, node->as.var_decl.init_expr, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_RETURN:
            AcuPurity_CollectCallees(ws, scope, node->as.ret_stmt.expr, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_BREAK:
            AcuPurity_CollectCallees(ws, scope, node->as.break_stmt.expr, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_IF:
            AcuPurity_CollectCallees(ws, scope, node->as.if_stmt.condition, out_callees,
                                     has_unresolved_callee);
            AcuPurity_CollectCallees(ws, scope, node->as.if_stmt.then_body, out_callees,
                                     has_unresolved_callee);
            AcuPurity_CollectCallees(ws, scope, node->as.if_stmt.else_body, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_LOOP:
            AcuPurity_CollectCallees(ws, scope, node->as.loop_stmt.body, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_WHILE:
            AcuPurity_CollectCallees(ws, scope, node->as.while_stmt.condition, out_callees,
                                     has_unresolved_callee);
            AcuPurity_CollectCallees(ws, scope, node->as.while_stmt.body, out_callees,
                                     has_unresolved_callee);
            break;

        case AST_BLOCK: {
            ExtraIdx start = node->as.block.start_idx;
            u32 count = node->as.block.statements_count;
            for (u32 i = 0; i < count; ++i) {
                AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, start + i);
                AcuPurity_CollectCallees(ws, scope, stmt_idx, out_callees, has_unresolved_callee);
            }
            break;
        }

        default:
            break;
    }
}

typedef struct {
    SymbolIdVec callees;
} AcuFnCallNode;

void AcuWorkspace_Pass4_AnalyzePurity(AcuWorkspace *ws) {
    u32 sym_count = (u32)V_Count(&ws->symbols->symbols);
    if (sym_count == 0) {
        return;
    }

    AcuFnCallNode *call_graph = (AcuFnCallNode *)malloc(sizeof(AcuFnCallNode) * sym_count);
    if (!call_graph) {
        return;
    }

    // ШАГ 1: Оптимистичная инициализация и анализ локального тела функций
    for (SymbolId s = 0; s < sym_count; ++s) {
        V_Init(&call_graph[s].callees);
        AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, s);
        if (!sym) {
            continue;
        }

        if (sym->kind == ACU_SYMBOL_FUNCTION) {
            AstNode *fn_node = AcuAstBuilder_GetNode(ws->builder, sym->decl_node);
            AstNodeIdx body_idx = fn_node->as.fn_decl.body;

            // Внешние/пустые функции без тела считаются IMPURE
            if (body_idx == ACU_NULL_IDX) {
                sym->flags &= ~ACU_SYMBOL_FLAG_PURE;
                continue;
            }

            // Прямая мутация глобалов делает функцию IMPURE
            if (AcuPurity_HasDirectSideEffects(ws, sym->scope_id, body_idx)) {
                sym->flags &= ~ACU_SYMBOL_FLAG_PURE;
            } else {
                sym->flags |= ACU_SYMBOL_FLAG_PURE; // Оптимистично чистая
            }

            bool has_unresolved = false;
            AcuPurity_CollectCallees(ws, sym->scope_id, body_idx, &call_graph[s].callees,
                                     &has_unresolved);

            if (has_unresolved) {
                sym->flags &= ~ACU_SYMBOL_FLAG_PURE;
            }
        } else {
            // Системные функции, переменные, модули не являются PURE-функциями
            sym->flags &= ~ACU_SYMBOL_FLAG_PURE;
        }
    }

    // ШАГ 2: Итеративное распространение нечистоты (Fixed-Point Propagation)
    bool changed = true;
    while (changed) {
        changed = false;

        for (SymbolId s = 0; s < sym_count; ++s) {
            AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, s);
            if (!sym || sym->kind != ACU_SYMBOL_FUNCTION) {
                continue;
            }

            // Если функция уже помечена как IMPURE, её статус не изменится
            if ((sym->flags & ACU_SYMBOL_FLAG_PURE) == 0) {
                continue;
            }

            u32 callee_count = (u32)V_Count(&call_graph[s].callees);
            for (u32 c = 0; c < callee_count; ++c) {
                SymbolId callee_id = V_At(&call_graph[s].callees, c);
                AcuSymbol *callee_sym = AcuSymbolTable_Get(ws->symbols, callee_id);

                // Если вызываемая функция нечистая (или системный вызов), текущая тоже IMPURE
                if (!callee_sym || (callee_sym->flags & ACU_SYMBOL_FLAG_PURE) == 0) {
                    sym->flags &= ~ACU_SYMBOL_FLAG_PURE;
                    changed = true;
                    break;
                }
            }
        }
    }

    // ШАГ 3: Освобождение графа вызовов
    for (SymbolId s = 0; s < sym_count; ++s) {
        V_Free(&call_graph[s].callees);
    }
    free(call_graph);
}
