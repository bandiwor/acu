#include "analyzer/analyzer.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "interner/type_interner.h"
#include "literal/literal.h"
#include "literal/literal_pool.h"
#include "module/module.h"
#include "table/symbol_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool AcuWorkspace_CanCoerce(TypeId actual, TypeId expected) {
    if (actual == TYPE_PRIMITIVE_NEVER) {
        return true;
    }

    if (actual == expected)
        return true;

    if (unlikely(actual == TYPE_PRIMITIVE_I1 || expected == TYPE_PRIMITIVE_I1)) {
        return false;
    }

    if (TypePrimitiveKind_IsInteger(actual) && TypePrimitiveKind_IsInteger(expected)) {
        bool actual_is_signed = TypePrimitiveKind_IsSignedInt(actual);
        bool expected_is_signed = TypePrimitiveKind_IsSignedInt(expected);

        if (actual_is_signed != expected_is_signed) {
            return false;
        }

        u32 actual_bits = TypePrimitiveKind_GetIntBitWidth(actual);
        u32 expected_bits = TypePrimitiveKind_GetIntBitWidth(expected);
        if (actual_bits <= expected_bits) {
            return true;
        }
    }

    return false;
}

static void AcuWorkspace_PushLoopContext(AcuWorkspace *ws, AstNodeKind kind, TypeId expected_type) {
    AcuLoopContext ctx = {.kind = kind, .expected_type = expected_type, .has_break = false};
    V_Push(&ws->loop_ctx, ctx);
}

static AcuLoopContext *AcuWorkspace_GetCurrentLoop(AcuWorkspace *ws) {
    u64 count = V_Count(&ws->loop_ctx);
    if (unlikely(count == 0)) {
        return NULL;
    }

    return &V_At(&ws->loop_ctx, count - 1);
}

static TypeId AcuWorkspace_PopLoopContext(AcuWorkspace *ws) {
    u64 count = V_Count(&ws->loop_ctx);
    if (unlikely(count == 0)) {
        return TYPE_PRIMITIVE_UNIT;
    }

    AcuLoopContext ctx = V_At(&ws->loop_ctx, count - 1);
    V_Pop(&ws->loop_ctx);

    if (ctx.kind == AST_WHILE) {
        return TYPE_PRIMITIVE_UNIT;
    }

    if (ctx.kind == AST_LOOP) {
        if (!ctx.has_break) {
            return TYPE_PRIMITIVE_NEVER;
        }

        if (ctx.expected_type == ACU_NULL_IDX) {
            return TYPE_PRIMITIVE_UNIT;
        }

        return ctx.expected_type;
    }

    return TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckExpr(AcuWorkspace *ws, ScopeId scope, AstNodeIdx expr_idx,
                                     TypeId expected_type);

#define TYPE_IN_PROGRESS ((TypeId) - 2)

static TypeId AcuWorkspace_EnsureGlobalVarChecked(AcuWorkspace *ws, SymbolId sym_id) {
    if (unlikely(sym_id == ACU_NULL_IDX)) {
        return TYPE_PRIMITIVE_UNIT;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
    if (!sym || sym->kind != ACU_SYMBOL_GLOBAL_VAR) {
        return sym ? sym->type : TYPE_PRIMITIVE_UNIT;
    }

    if (sym->flags & ACU_SYMBOL_FLAG_GLOBAL_CHECKED) {
        return sym->type;
    }

    // 2. Обнаружена циклическая зависимость ($a = $b; $b = $a)
    if (sym->flags & ACU_SYMBOL_FLAG_GLOBAL_CHECKING) {
        AcuWorkspace_PushError(ws, sym->decl_node, ACU_ERR_ANALYZER_CYCLIC_DEPENDENCY,
                               (AcuError){0});
        return (sym->type != ACU_NULL_IDX) ? sym->type : TYPE_PRIMITIVE_UNIT;
    }

    // Входим в узел
    sym->flags |= ACU_SYMBOL_FLAG_GLOBAL_CHECKING;

    ScopeId var_scope = sym->scope_id;
    AstNodeIdx decl_idx = sym->decl_node;
    AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, decl_idx);

    TypeAstNodeIdx type_hint = stmt->as.var_decl.type_hint;
    AstNodeIdx init_expr = stmt->as.var_decl.init_expr;

    TypeId expected_type = (type_hint != ACU_NULL_IDX)
                               ? AcuWorkspace_ResolveAstType(ws, var_scope, type_hint)
                               : ACU_NULL_IDX;

    // Рекурсивный обход зависимостей внутри init_expr
    // Любой идентификатор внутри init_expr вызовет EnsureGlobalVarChecked для зависимостей,
    // и они добавятся в global_init_order ДО текущей переменной (DFS post-order).
    TypeId inferred_type = AcuWorkspace_CheckExpr(ws, var_scope, init_expr, expected_type);

    if (unlikely(inferred_type == TYPE_PRIMITIVE_NEVER)) {
        AcuWorkspace_PushError(ws, decl_idx, ACU_ERR_ANALYZER_NEVER_AT_TOP_LEVEL, (AcuError){0});
    }

    TypeId final_type = (expected_type != ACU_NULL_IDX) ? expected_type : inferred_type;
    if (unlikely(final_type == ACU_NULL_IDX)) {
        AcuWorkspace_PushError(ws, decl_idx, ACU_ERR_ANALYZER_CANNOT_INFER_TYPE, (AcuError){0});
        final_type = TYPE_PRIMITIVE_UNIT;
    }

    sym->type = final_type;
    sym->flags &= ~ACU_SYMBOL_FLAG_GLOBAL_CHECKING;
    sym->flags |= ACU_SYMBOL_FLAG_GLOBAL_CHECKED;

    V_At(&ws->node_types, decl_idx) = TYPE_PRIMITIVE_UNIT;
    V_At(&ws->node_analysis, decl_idx).var_decl.sym_id = sym_id;

    // Теперь сюда гарантированно попадут ВСЕ глобальные переменные!
    V_Push(&ws->global_init_order, sym_id);

    return final_type;
}

static TypeId AcuWorkspace_CheckLiteral(AcuWorkspace *ws, AstNode *expr, TypeId expected_type) {
    LiteralIdx literal_idx = expr->as.literal.idx;
    AcuLiteral *literal = AcuLiteralPool_Get(ws->literals, literal_idx);
    TypeId result_type = TYPE_PRIMITIVE_UNIT;

    switch ((AcuLiteralKind)literal->kind) {
        case ACU_LITERAL_I64:
        case ACU_LITERAL_U64:
            if (literal->suffix != ACU_INT_SUFFIX_NONE) {
                result_type = TypePrimitiveKind_FromExplicitIntSuffix(literal->suffix);
            } else {
                if (TypePrimitiveKind_IsInteger(expected_type) ||
                    TypePrimitiveKind_IsFloat(expected_type)) {
                    result_type = expected_type;
                } else {
                    result_type = TYPE_PRIMITIVE_I64;
                }
            }
            break;

        case ACU_LITERAL_F64:
            if (literal->suffix != ACU_FLOAT_SUFFIX_NONE) {
                result_type = TypePrimitiveKind_FromExplicitFloatSuffix(literal->suffix);
            } else {
                if (TypePrimitiveKind_IsFloat(expected_type)) {
                    result_type = expected_type;
                } else {
                    result_type = TYPE_PRIMITIVE_F64;
                }
            }
            break;

        case ACU_LITERAL_STR:
            result_type = TYPE_PRIMITIVE_STR;
            break;
    }

    return result_type;
}

static TypeId AcuWorkspace_CheckIdentifier(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                           AstNodeIdx expr_idx) {
    StringId name = expr->as.identifier.name;
    SymbolId symbol_id = AcuSymbolTable_Lookup(ws->symbols, scope, name);

    V_At(&ws->node_analysis, expr_idx).reference.resolved_sym = symbol_id;

    AcuSymbol *symbol =
        (symbol_id != ACU_NULL_IDX) ? AcuSymbolTable_Get(ws->symbols, symbol_id) : NULL;

    if (!symbol) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_UNDECLARED_IDENTIFIER, (AcuError){0});
        return TYPE_PRIMITIVE_UNIT;
    }

    if (symbol->kind == ACU_SYMBOL_GLOBAL_VAR) {
        return AcuWorkspace_EnsureGlobalVarChecked(ws, symbol_id);
    }

    if (symbol->kind == ACU_SYMBOL_MODULE) {
        return TYPE_PRIMITIVE_MODULE;
    }

    return symbol->type;
}

static bool AcuWorkspace_IsUntypedNumericExpr(AcuWorkspace *ws, AstNodeIdx expr_idx) {
    if (expr_idx == ACU_NULL_IDX)
        return false;
    AstNode *node = AcuAstBuilder_GetNode(ws->builder, expr_idx);

    if (node->type == AST_LITERAL) {
        AcuLiteral *lit = AcuLiteralPool_Get(ws->literals, node->as.literal.idx);
        if (lit->kind == ACU_LITERAL_I64 || lit->kind == ACU_LITERAL_U64) {
            return lit->suffix == ACU_INT_SUFFIX_NONE;
        }
        if (lit->kind == ACU_LITERAL_F64) {
            return lit->suffix == ACU_FLOAT_SUFFIX_NONE;
        }
        return false;
    }

    if (node->type == AST_UNARY) {
        AstOperatorCategory cat = AstUnaryKind_GetCategory(node->as.unary.type);
        if (cat == AST_OPERATOR_CATEGORY_ARITHMETIC || cat == AST_OPERATOR_CATEGORY_BITWISE) {
            return AcuWorkspace_IsUntypedNumericExpr(ws, node->as.unary.right);
        }
        return false;
    }

    if (node->type == AST_BINARY) {
        AstOperatorCategory cat = AstBinaryKind_GetCategory(node->as.binary.type);
        if (cat == AST_OPERATOR_CATEGORY_ARITHMETIC || cat == AST_OPERATOR_CATEGORY_BITWISE) {
            return AcuWorkspace_IsUntypedNumericExpr(ws, node->as.binary.left) &&
                   AcuWorkspace_IsUntypedNumericExpr(ws, node->as.binary.right);
        }
        return false;
    }

    return false;
}

static TypeId AcuWorkspace_CheckBinary(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                       AstNodeIdx expr_idx, TypeId expected_type) {
    AstOperatorCategory category = AstBinaryKind_GetCategory(expr->as.binary.type);
    TypeId left_expected = ACU_NULL_IDX;

    if (category == AST_OPERATOR_CATEGORY_ARITHMETIC || category == AST_OPERATOR_CATEGORY_BITWISE) {
        left_expected = expected_type;
        if (left_expected == TYPE_PRIMITIVE_UNIT) {
            left_expected = ACU_NULL_IDX;
        }
    } else if (category == AST_OPERATOR_CATEGORY_LOGIC) {
        left_expected = TYPE_PRIMITIVE_I1;
    } else if (category == AST_OPERATOR_CATEGORY_COMPARE) {
        left_expected = ACU_NULL_IDX;
    }

    bool left_is_untyped = AcuWorkspace_IsUntypedNumericExpr(ws, expr->as.binary.left);
    bool right_is_untyped = AcuWorkspace_IsUntypedNumericExpr(ws, expr->as.binary.right);

    TypeId left_type = TYPE_PRIMITIVE_UNIT;
    TypeId right_type = TYPE_PRIMITIVE_UNIT;

    if (left_is_untyped && !right_is_untyped) {
        right_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.binary.right, left_expected);

        TypeId expected_for_left = (left_expected != ACU_NULL_IDX) ? left_expected : right_type;
        left_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.binary.left, expected_for_left);
    } else {
        // Стандартный обход слева направо
        left_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.binary.left, left_expected);
        right_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.binary.right, left_type);
    }

    if (unlikely(left_type == TYPE_PRIMITIVE_NEVER || right_type == TYPE_PRIMITIVE_NEVER)) {
        return TYPE_PRIMITIVE_NEVER;
    }

    if (unlikely(left_type != right_type)) {
        AcuError err = {.as.type_mismatch = {.expected = left_type, .actual = right_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_TYPE_MISMATCH, err);
        return left_type;
    }

    bool is_valid_op = false;
    if (category == AST_OPERATOR_CATEGORY_LOGIC) {
        is_valid_op = (left_type == TYPE_PRIMITIVE_I1);
    } else if (category == AST_OPERATOR_CATEGORY_COMPARE) {
        is_valid_op =
            (bool)(left_type == TYPE_PRIMITIVE_I1 || TypePrimitiveKind_IsInteger(left_type) ||
                   TypePrimitiveKind_IsFloat(left_type));
    } else if (category == AST_OPERATOR_CATEGORY_ARITHMETIC) {
        is_valid_op =
            (bool)(TypePrimitiveKind_IsInteger(left_type) || TypePrimitiveKind_IsFloat(left_type));
    } else if (category == AST_OPERATOR_CATEGORY_BITWISE) {
        is_valid_op =
            (bool)(left_type == TYPE_PRIMITIVE_I1 || TypePrimitiveKind_IsInteger(left_type));
    }

    if (unlikely(!is_valid_op)) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_UNSUPPORTED_OPERATION, (AcuError){0});
        return (category == AST_OPERATOR_CATEGORY_COMPARE ||
                category == AST_OPERATOR_CATEGORY_LOGIC)
                   ? TYPE_PRIMITIVE_I1
                   : left_type;
    }

    if (category == AST_OPERATOR_CATEGORY_COMPARE || category == AST_OPERATOR_CATEGORY_LOGIC) {
        return TYPE_PRIMITIVE_I1;
    }

    return left_type;
}

static TypeId AcuWorkspace_CheckBlock(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                      AstNodeIdx expr_idx, TypeId expected_type) {
    ExtraIdx current = expr->as.block.start_idx;
    u32 count = expr->as.block.statements_count;

    ModuleId mod_id = AcuSymbolTable_GetModule(ws->symbols, scope);
    ScopeId inner_scope = AcuSymbolTable_PushScope(ws->symbols, scope, mod_id);

    V_At(&ws->node_analysis, expr_idx).block.inner_scope = inner_scope;

    TypeId block_type = TYPE_PRIMITIVE_UNIT;
    bool has_never = false;

    for (u32 i = 0; i < count; ++i) {
        AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, current + i);

        bool is_last = (i == count - 1);
        TypeId stmt_expected = (int)is_last ? expected_type : ACU_NULL_IDX;

        TypeId stmt_type = AcuWorkspace_CheckExpr(ws, inner_scope, stmt_idx, stmt_expected);

        if (stmt_type == TYPE_PRIMITIVE_NEVER) {
            has_never = true;
        }

        if (is_last) {
            block_type = stmt_type;
        } else {
            if (unlikely(stmt_type != TYPE_PRIMITIVE_UNIT && stmt_type != TYPE_PRIMITIVE_NEVER)) {
                AcuError err = {
                    .as.type_mismatch = {.expected = TYPE_PRIMITIVE_UNIT, .actual = stmt_type}};
                AcuWorkspace_PushError(ws, stmt_idx, ACU_ERR_ANALYZER_EXPECTED_UNIT, err);
            }
        }
    }

    if (has_never) {
        return TYPE_PRIMITIVE_NEVER;
    }

    return (count > 0) ? block_type : TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckIf(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                   AstNodeIdx expr_idx, TypeId expected_type) {
    TypeId cond_type =
        AcuWorkspace_CheckExpr(ws, scope, expr->as.if_stmt.condition, TYPE_PRIMITIVE_I1);
    if (unlikely(!AcuWorkspace_CanCoerce(cond_type, TYPE_PRIMITIVE_I1))) {
        AcuError err = {.as.type_mismatch = {.expected = TYPE_PRIMITIVE_I1, .actual = cond_type}};
        AcuWorkspace_PushError(ws, expr->as.if_stmt.condition, ACU_ERR_ANALYZER_CONDITION_NOT_BOOL,
                               err);
    }

    bool has_else = (expr->as.if_stmt.else_body != ACU_NULL_IDX);
    TypeId then_expected = (int)has_else ? expected_type : ACU_NULL_IDX;
    if (then_expected == TYPE_PRIMITIVE_UNIT)
        then_expected = ACU_NULL_IDX;

    TypeId then_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.if_stmt.then_body, then_expected);

    if (has_else) {
        TypeId else_expected = (then_type == TYPE_PRIMITIVE_NEVER) ? expected_type : then_type;
        if (else_expected == TYPE_PRIMITIVE_UNIT)
            else_expected = ACU_NULL_IDX;

        TypeId else_type =
            AcuWorkspace_CheckExpr(ws, scope, expr->as.if_stmt.else_body, else_expected);

        if (then_type == TYPE_PRIMITIVE_NEVER && else_type == TYPE_PRIMITIVE_NEVER)
            return TYPE_PRIMITIVE_NEVER;
        if (then_type == TYPE_PRIMITIVE_NEVER)
            return else_type;
        if (else_type == TYPE_PRIMITIVE_NEVER)
            return then_type;

        if (AcuWorkspace_CanCoerce(else_type, then_type))
            return then_type;

        if (AcuWorkspace_CanCoerce(then_type, else_type))
            return else_type;

        AcuError err = {.as.type_mismatch = {.expected = then_type, .actual = else_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_BRANCH_TYPE_MISMATCH, err);

        return expected_type != ACU_NULL_IDX ? expected_type : then_type;
    }

    if (unlikely(then_type != TYPE_PRIMITIVE_UNIT && then_type != TYPE_PRIMITIVE_NEVER)) {
        AcuError err = {.as.type_mismatch = {.expected = TYPE_PRIMITIVE_UNIT, .actual = then_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_IF_WITHOUT_ELSE_NOT_UNIT, err);
    }

    return cond_type == TYPE_PRIMITIVE_NEVER ? TYPE_PRIMITIVE_NEVER : TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckCall(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                     AstNodeIdx expr_idx) {
    AstNodeIdx callee_idx = expr->as.call.object;

    TypeId callee_type_id = AcuWorkspace_CheckExpr(ws, scope, callee_idx, ACU_NULL_IDX);

    SymbolId callee_sym = V_At(&ws->node_analysis, callee_idx).reference.resolved_sym;
    SymbolId resolved_fn = ACU_NULL_IDX;

    if (callee_sym != ACU_NULL_IDX) {
        AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, callee_sym);
        if (sym && (sym->kind == ACU_SYMBOL_FUNCTION || sym->kind == ACU_SYMBOL_SYSTEM_FUNCTION)) {
            resolved_fn = callee_sym;
        }
    }

    bool is_callee_never = (callee_type_id == TYPE_PRIMITIVE_NEVER);
    bool is_callee_err = (callee_type_id == ACU_NULL_IDX);
    bool is_valid_func = false;

    u32 param_count = 0;
    TypeId return_type = TYPE_PRIMITIVE_UNIT;
    ExtraIdx params_start_extra = 0;

    if (!is_callee_never && !is_callee_err) {
        AcuType *callee_type = AcuTypeInterner_GetType(ws->types, callee_type_id);
        if (unlikely(callee_type->kind != ACU_TYPE_FUNC)) {
            AcuWorkspace_PushError(ws, callee_idx, ACU_ERR_ANALYZER_CALL_NON_FUNCTION,
                                   (AcuError){0});
        } else {
            is_valid_func = true;
            param_count = callee_type->as.func.count;
            return_type = callee_type->as.func.return_type;
            params_start_extra = callee_type->as.func.params_start;
        }
    }

    u32 arg_count = expr->as.call.arguments_count;
    if (is_valid_func && arg_count != param_count) {
        AcuError err = {.as.arg_count = {.expected = param_count, .actual = arg_count}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_WRONG_ARGUMENT_COUNT, err);

        is_valid_func = false;
        resolved_fn = ACU_NULL_IDX;
    }

    V_At(&ws->node_analysis, expr_idx).call.callee_sym = resolved_fn;

    ExtraIdx arg_idx = expr->as.call.start_idx;
    bool has_never = is_callee_never;

    for (u32 i = 0; i < arg_count; ++i) {
        AstNodeIdx arg_ast = AcuAstBuilder_GetIdxByExtra(ws->builder, arg_idx + i);

        TypeId expected_arg_type = ACU_NULL_IDX;
        TypeId sig_param_type = ACU_NULL_IDX;

        if (is_valid_func && i < param_count) {
            sig_param_type = AcuTypeInterner_GetTypeIdByExtra(ws->types, params_start_extra + i);
            expected_arg_type =
                (sig_param_type == TYPE_PRIMITIVE_UNIT) ? ACU_NULL_IDX : sig_param_type;
        }

        TypeId arg_type = AcuWorkspace_CheckExpr(ws, scope, arg_ast, expected_arg_type);
        if (unlikely(arg_type == TYPE_PRIMITIVE_NEVER)) {
            has_never = true;
        }

        if (is_valid_func && i < param_count) {
            if (unlikely(arg_type != ACU_NULL_IDX &&
                         !AcuWorkspace_CanCoerce(arg_type, sig_param_type))) {
                AcuError err = {
                    .as.type_mismatch = {.expected = sig_param_type, .actual = arg_type}};
                AcuWorkspace_PushError(ws, arg_ast, ACU_ERR_ANALYZER_TYPE_MISMATCH, err);
            }
        }
    }

    if (has_never) {
        return TYPE_PRIMITIVE_NEVER;
    }
    if (is_valid_func) {
        return return_type;
    }

    return TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckVarDecl(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                        AstNodeIdx expr_idx) {
    TypeAstNodeIdx type_hint = expr->as.var_decl.type_hint;
    AstNodeIdx init_expr = expr->as.var_decl.init_expr;

    TypeId var_expected = (type_hint != ACU_NULL_IDX)
                              ? AcuWorkspace_ResolveAstType(ws, scope, type_hint)
                              : ACU_NULL_IDX;

    TypeId init_type = AcuWorkspace_CheckExpr(ws, scope, init_expr, var_expected);

    TypeId final_type = (var_expected != ACU_NULL_IDX) ? var_expected : init_type;

    SymbolId sym_id =
        AcuSymbolTable_AddLocalVar(ws->symbols, scope, expr->as.var_decl.name, final_type, expr_idx,
                                   (bool)(expr->flags & AST_FLAG_MUTABLE));

    V_At(&ws->node_analysis, expr_idx).var_decl.sym_id = sym_id;

    return init_type == TYPE_PRIMITIVE_NEVER ? TYPE_PRIMITIVE_NEVER : TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckDiscard(AcuWorkspace *ws, ScopeId scope, AstNode *expr) {
    TypeId inner_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.discard.expr, ACU_NULL_IDX);
    return inner_type == TYPE_PRIMITIVE_NEVER ? TYPE_PRIMITIVE_NEVER : TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckUnit(AcuWorkspace *ws, TypeId expected_type, AstNodeIdx expr_idx) {
    if (unlikely(expected_type != ACU_NULL_IDX && expected_type != TYPE_PRIMITIVE_UNIT)) {
        AcuError err = {
            .as.type_mismatch = {.expected = expected_type, .actual = TYPE_PRIMITIVE_UNIT}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_TYPE_MISMATCH, err);
    }

    return TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckUnary(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                      AstNodeIdx expr_idx, TypeId expected_type) {
    AstOperatorCategory cat = AstUnaryKind_GetCategory(expr->as.unary.type);
    TypeId operand_expected = ACU_NULL_IDX;

    if (cat == AST_OPERATOR_CATEGORY_LOGIC) {
        operand_expected = TYPE_PRIMITIVE_I1;
    } else {
        operand_expected = expected_type;
        if (operand_expected == TYPE_PRIMITIVE_UNIT) {
            operand_expected = ACU_NULL_IDX;
        }
    }

    TypeId operand_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.unary.right, operand_expected);

    if (unlikely(operand_type == TYPE_PRIMITIVE_NEVER)) {
        return TYPE_PRIMITIVE_NEVER;
    }

    bool is_valid_op = false;

    if (cat == AST_OPERATOR_CATEGORY_LOGIC) {
        is_valid_op = (operand_type == TYPE_PRIMITIVE_I1);
    } else if (cat == AST_OPERATOR_CATEGORY_ARITHMETIC) {
        is_valid_op = (bool)(TypePrimitiveKind_IsInteger(operand_type) ||
                             TypePrimitiveKind_IsFloat(operand_type));
    } else if (cat == AST_OPERATOR_CATEGORY_BITWISE) {
        is_valid_op = TypePrimitiveKind_IsInteger(operand_type);
    }

    if (unlikely(!is_valid_op)) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_UNSUPPORTED_OPERATION, (AcuError){0});
        return (cat == AST_OPERATOR_CATEGORY_LOGIC) ? TYPE_PRIMITIVE_I1 : operand_type;
    }

    if (cat == AST_OPERATOR_CATEGORY_LOGIC) {
        return TYPE_PRIMITIVE_I1;
    }

    return operand_type;
}

static TypeId AcuWorkspace_CheckLoop(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                     AstNodeIdx expr_idx, TypeId expected_type) {
    AcuWorkspace_PushLoopContext(ws, AST_LOOP, expected_type);

    TypeId body_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.loop_stmt.body, ACU_NULL_IDX);
    if (unlikely(body_type != TYPE_PRIMITIVE_UNIT && body_type != TYPE_PRIMITIVE_NEVER)) {
        AcuError err = {.as.type_mismatch = {.expected = TYPE_PRIMITIVE_UNIT, .actual = body_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_EXPECTED_UNIT, err);
    }

    return AcuWorkspace_PopLoopContext(ws);
}

static TypeId AcuWorkspace_CheckWhile(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                      AstNodeIdx expr_idx) {
    TypeId cond_type =
        AcuWorkspace_CheckExpr(ws, scope, expr->as.while_stmt.condition, TYPE_PRIMITIVE_I1);

    if (unlikely(!AcuWorkspace_CanCoerce(cond_type, TYPE_PRIMITIVE_I1))) {
        AcuError err = {.as.type_mismatch = {.expected = TYPE_PRIMITIVE_I1, .actual = cond_type}};
        AcuWorkspace_PushError(ws, expr->as.while_stmt.condition,
                               ACU_ERR_ANALYZER_CONDITION_NOT_BOOL, err);
    }

    AcuWorkspace_PushLoopContext(ws, AST_WHILE, ACU_NULL_IDX);

    TypeId body_type = AcuWorkspace_CheckExpr(ws, scope, expr->as.while_stmt.body, ACU_NULL_IDX);
    if (unlikely(body_type != TYPE_PRIMITIVE_UNIT && body_type != TYPE_PRIMITIVE_NEVER)) {
        AcuError err = {.as.type_mismatch = {.expected = TYPE_PRIMITIVE_UNIT, .actual = body_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_EXPECTED_UNIT, err);
    }

    AcuWorkspace_PopLoopContext(ws);

    if (cond_type == TYPE_PRIMITIVE_NEVER) {
        return TYPE_PRIMITIVE_NEVER;
    }

    return TYPE_PRIMITIVE_UNIT;
}

static ModuleId AcuWorkspace_GetModuleFromExpr(AcuWorkspace *ws, ScopeId scope,
                                               AstNodeIdx expr_idx) {
    AstNode *expr = AcuAstBuilder_GetNode(ws->builder, expr_idx);

    if (expr->type == AST_IDENTIFIER) {
        StringId name = expr->as.identifier.name;
        SymbolId sym_id = AcuSymbolTable_Lookup(ws->symbols, scope, name);
        if (sym_id != ACU_NULL_IDX) {
            AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
            if (sym->kind == ACU_SYMBOL_MODULE) {
                return sym->as.target_module_id;
            }
        }
        return ACU_NULL_IDX;
    }

    if (expr->type == AST_DOT) {
        ModuleId parent_mod = AcuWorkspace_GetModuleFromExpr(ws, scope, expr->as.dot.object);
        if (parent_mod != ACU_NULL_IDX) {
            ScopeId target_scope = AcuModuleManager_GetModuleScope(ws->modules, parent_mod);
            AstNode *member_node = AcuAstBuilder_GetNode(ws->builder, expr->as.dot.member);

            if (member_node->type == AST_IDENTIFIER) {
                StringId name = member_node->as.identifier.name;
                SymbolId sym_id = AcuSymbolTable_LookupExact(ws->symbols, target_scope, name);

                if (sym_id != ACU_NULL_IDX) {
                    AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
                    if (sym->kind == ACU_SYMBOL_MODULE) {
                        return sym->as.target_module_id;
                    }
                }
            }
        }
    }

    return ACU_NULL_IDX;
}

static TypeId AcuWorkspace_CheckDot(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                    AstNodeIdx expr_idx) {
    AstNodeIdx obj_idx = expr->as.dot.object;
    AstNodeIdx member_idx = expr->as.dot.member;

    TypeId obj_type = AcuWorkspace_CheckExpr(ws, scope, obj_idx, ACU_NULL_IDX);

    if (unlikely(obj_type == TYPE_PRIMITIVE_NEVER)) {
        return TYPE_PRIMITIVE_NEVER;
    }

    if (obj_type == TYPE_PRIMITIVE_MODULE) {
        ModuleId target_mod_id = AcuWorkspace_GetModuleFromExpr(ws, scope, obj_idx);

        if (unlikely(target_mod_id == ACU_NULL_IDX)) {
            AcuWorkspace_PushError(ws, obj_idx, ACU_ERR_ANALYZER_EXPECTED_MODULE_BEFORE_DOT,
                                   (AcuError){0});
            return TYPE_PRIMITIVE_UNIT;
        }

        AstNode *member_node = AcuAstBuilder_GetNode(ws->builder, member_idx);
        if (unlikely(member_node->type != AST_IDENTIFIER)) {
            AcuWorkspace_PushError(ws, member_idx, ACU_ERR_ANALYZER_EXPECTED_IDENTIFIER_AFTER_DOT,
                                   (AcuError){0});
            return TYPE_PRIMITIVE_UNIT;
        }

        StringId member_name = member_node->as.identifier.name;
        ScopeId target_scope = AcuModuleManager_GetModuleScope(ws->modules, target_mod_id);

        SymbolId member_sym_id = AcuSymbolTable_LookupExact(ws->symbols, target_scope, member_name);
        AcuSymbol *member_sym =
            (member_sym_id != ACU_NULL_IDX) ? AcuSymbolTable_Get(ws->symbols, member_sym_id) : NULL;

        V_At(&ws->node_analysis, member_idx).reference.resolved_sym = member_sym_id;
        V_At(&ws->node_analysis, expr_idx).reference.resolved_sym = member_sym_id;

        if (!member_sym) {
            AcuWorkspace_PushError(ws, member_idx, ACU_ERR_ANALYZER_MODULE_MEMBER_NOT_FOUND,
                                   (AcuError){0});
            return TYPE_PRIMITIVE_UNIT;
        }

        if ((member_sym->flags & ACU_SYMBOL_FLAG_EXPORTED) == 0) {
            AcuWorkspace_PushError(ws, member_idx, ACU_ERR_ANALYZER_PRIVATE_MEMBER_ACCESS,
                                   (AcuError){0});
        }

        if (member_sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
            return AcuWorkspace_EnsureGlobalVarChecked(ws, member_sym_id);
        }

        return member_sym->type;
    }

    AcuWorkspace_PushError(ws, obj_idx, ACU_ERR_ANALYZER_TYPE_DOES_NOT_SUPPORT_MEMBER_ACCESS,
                           (AcuError){0});
    return TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckBreak(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                      AstNodeIdx expr_idx) {
    AcuLoopContext *current_loop = AcuWorkspace_GetCurrentLoop(ws);
    if (unlikely(!current_loop)) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_BREAK_OUTSIDE_LOOP, (AcuError){0});
        return TYPE_PRIMITIVE_NEVER;
    }

    current_loop->has_break = true;

    AstNodeIdx break_expr = expr->as.break_stmt.expr;
    TypeId break_val_type = TYPE_PRIMITIVE_UNIT;

    if (break_expr != ACU_NULL_IDX) {
        break_val_type = AcuWorkspace_CheckExpr(ws, scope, break_expr, current_loop->expected_type);
    }

    if (current_loop->kind == AST_WHILE) {
        if (unlikely(break_val_type != TYPE_PRIMITIVE_UNIT &&
                     break_val_type != TYPE_PRIMITIVE_NEVER)) {
            AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_BREAK_WITH_VALUE_IN_WHILE,
                                   (AcuError){0});
        }
    } else if (current_loop->kind == AST_LOOP) {
        if (current_loop->expected_type == ACU_NULL_IDX) {
            current_loop->expected_type = break_val_type;
        } else {
            if (unlikely(break_val_type != current_loop->expected_type &&
                         break_val_type != TYPE_PRIMITIVE_NEVER)) {
                AcuError err = {.as.type_mismatch = {.expected = current_loop->expected_type,
                                                     .actual = break_val_type}};
                AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_BREAK_TYPE_MISMATCH, err);
            }
        }
    }

    return TYPE_PRIMITIVE_NEVER;
}

static TypeId AcuWorkspace_CheckContinue(AcuWorkspace *ws, AstNodeIdx expr_idx) {
    AcuLoopContext *current_loop = AcuWorkspace_GetCurrentLoop(ws);
    if (unlikely(!current_loop)) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_CONTINUE_OUTSIDE_LOOP, (AcuError){0});
    }

    return TYPE_PRIMITIVE_NEVER;
}

static TypeId AcuWorkspace_CheckReturn(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                       AstNodeIdx expr_idx) {
    if (unlikely(ws->expected_return_type == ACU_NULL_IDX)) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_RETURN_OUTSIDE_FUNCTION,
                               (AcuError){0});
        if (expr->as.ret_stmt.expr != ACU_NULL_IDX) {
            AcuWorkspace_CheckExpr(ws, scope, expr->as.ret_stmt.expr, ACU_NULL_IDX);
        }
        return TYPE_PRIMITIVE_NEVER;
    }

    TypeId expected = ws->expected_return_type;

    if (expr->as.ret_stmt.expr != ACU_NULL_IDX) {
        AcuWorkspace_CheckExpr(ws, scope, expr->as.ret_stmt.expr, expected);
    } else if (expected != TYPE_PRIMITIVE_UNIT) {
        AcuError err = {.as.type_mismatch = {.expected = expected, .actual = TYPE_PRIMITIVE_UNIT}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_RETURN_MISSING_VALUE, err);
    }

    return TYPE_PRIMITIVE_NEVER;
}

static TypeId AcuWorkspace_CheckTypeCast(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                         AstNodeIdx expr_idx) {
    TypeAstNodeIdx type_hint = expr->as.type_cast.target_type;
    TypeId target_type = AcuWorkspace_ResolveAstType(ws, scope, type_hint);

    if (unlikely(target_type == ACU_NULL_IDX)) {
        return TYPE_PRIMITIVE_UNIT;
    }

    AstNodeIdx inner_expr_idx = expr->as.type_cast.expr;
    TypeId actual_type = AcuWorkspace_CheckExpr(ws, scope, inner_expr_idx, target_type);

    if (unlikely(actual_type == TYPE_PRIMITIVE_NEVER)) {
        return TYPE_PRIMITIVE_NEVER;
    }

    bool is_valid_cast = actual_type == target_type;

    if (unlikely(actual_type == TYPE_PRIMITIVE_I1 || target_type == TYPE_PRIMITIVE_I1)) {
        is_valid_cast = false;
    } else {
        bool actual_is_int = TypePrimitiveKind_IsInteger(actual_type);
        bool target_is_int = TypePrimitiveKind_IsInteger(target_type);

        bool actual_is_float = TypePrimitiveKind_IsFloat(actual_type);
        bool target_is_float = TypePrimitiveKind_IsFloat(target_type);

        is_valid_cast =
            (bool)((actual_is_int || actual_is_float) && (target_is_int || target_is_float));
    }

    if (unlikely(!is_valid_cast)) {
        AcuError err = {.as.type_cast = {.target = target_type, .actual = actual_type}};
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_INVALID_TYPE_CAST, err);
        return target_type;
    }

    return target_type;
}

static TypeId AcuWorkspace_CheckAssign(AcuWorkspace *ws, ScopeId scope, AstNode *expr,
                                       AstNodeIdx expr_idx) {
    AstNodeIdx target_idx = expr->as.assign.target;
    AstNodeIdx value_idx = expr->as.assign.value;

    AstNode *target_node = AcuAstBuilder_GetNode(ws->builder, target_idx);

    if (target_node->type != AST_IDENTIFIER) {
        AcuWorkspace_PushError(ws, target_idx, ACU_ERR_ANALYZER_INVALID_LVALUE, (AcuError){0});
        return TYPE_PRIMITIVE_UNIT;
    }

    StringId name = target_node->as.identifier.name;
    SymbolId sym_id = AcuSymbolTable_Lookup(ws->symbols, scope, name);

    V_At(&ws->node_analysis, target_idx).reference.resolved_sym = sym_id;
    V_At(&ws->node_analysis, expr_idx).reference.resolved_sym = sym_id;

    if (sym_id == ACU_NULL_IDX) {
        AcuWorkspace_PushError(ws, target_idx, ACU_ERR_ANALYZER_UNDECLARED_IDENTIFIER,
                               (AcuError){0});
        return TYPE_PRIMITIVE_UNIT;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);

    if (sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
        AcuWorkspace_EnsureGlobalVarChecked(ws, sym_id);
        sym = AcuSymbolTable_Get(ws->symbols, sym_id);
    }

    if ((sym->flags & ACU_SYMBOL_FLAG_MUTABLE) == 0) {
        AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_ASSIGN_TO_IMMUTABLE, (AcuError){0});
    }

    TypeId target_type = sym->type;
    TypeId val_type = AcuWorkspace_CheckExpr(ws, scope, value_idx, target_type);

    if (unlikely(!AcuWorkspace_CanCoerce(val_type, target_type))) {
        AcuError err = {.as.type_mismatch = {.expected = target_type, .actual = val_type}};
        AcuWorkspace_PushError(ws, value_idx, ACU_ERR_ANALYZER_TYPE_MISMATCH, err);
    }

    return val_type == TYPE_PRIMITIVE_NEVER ? TYPE_PRIMITIVE_NEVER : TYPE_PRIMITIVE_UNIT;
}

static TypeId AcuWorkspace_CheckExpr(AcuWorkspace *ws, ScopeId scope, AstNodeIdx expr_idx,
                                     TypeId expected_type) {
    if (expr_idx == ACU_NULL_IDX) {
        return TYPE_PRIMITIVE_UNIT;
    }

    AstNode *expr = AcuAstBuilder_GetNode(ws->builder, expr_idx);
    TypeId result_type = TYPE_PRIMITIVE_UNIT;

    switch (expr->type) {
        case AST_LITERAL:
            result_type = AcuWorkspace_CheckLiteral(ws, expr, expected_type);
            break;

        case AST_IDENTIFIER:
            result_type = AcuWorkspace_CheckIdentifier(ws, scope, expr, expr_idx);
            break;

        case AST_BINARY:
            result_type = AcuWorkspace_CheckBinary(ws, scope, expr, expr_idx, expected_type);
            break;

        case AST_ASSIGN:
            result_type = AcuWorkspace_CheckAssign(ws, scope, expr, expr_idx);
            break;

        case AST_BLOCK:
            result_type = AcuWorkspace_CheckBlock(ws, scope, expr, expr_idx, expected_type);
            break;

        case AST_IF:
            result_type = AcuWorkspace_CheckIf(ws, scope, expr, expr_idx, expected_type);
            break;

        case AST_CALL:
            result_type = AcuWorkspace_CheckCall(ws, scope, expr, expr_idx);
            break;

        case AST_VAR_DECL:
            result_type = AcuWorkspace_CheckVarDecl(ws, scope, expr, expr_idx);
            break;

        case AST_DISCARD:
            result_type = AcuWorkspace_CheckDiscard(ws, scope, expr);
            break;

        case AST_UNIT:
            result_type = AcuWorkspace_CheckUnit(ws, expected_type, expr_idx);
            break;

        case AST_UNARY:
            result_type = AcuWorkspace_CheckUnary(ws, scope, expr, expr_idx, expected_type);
            break;

        case AST_DOT:
            result_type = AcuWorkspace_CheckDot(ws, scope, expr, expr_idx);
            break;

        case AST_LOOP:
            result_type = AcuWorkspace_CheckLoop(ws, scope, expr, expr_idx, expected_type);
            break;

        case AST_WHILE:
            result_type = AcuWorkspace_CheckWhile(ws, scope, expr, expr_idx);
            break;

        case AST_BREAK:
            result_type = AcuWorkspace_CheckBreak(ws, scope, expr, expr_idx);
            break;

        case AST_CONTINUE:
            result_type = AcuWorkspace_CheckContinue(ws, expr_idx);
            break;

        case AST_RETURN:
            result_type = AcuWorkspace_CheckReturn(ws, scope, expr, expr_idx);
            break;

        case AST_TYPE_CAST:
            result_type = AcuWorkspace_CheckTypeCast(ws, scope, expr, expr_idx);
            break;

        default:
            acu_unreachable_debug("Unknown node kind (%d)", expr->type);
    }

    TypeId final_type;
    if (expected_type != ACU_NULL_IDX) {
        if (unlikely(!AcuWorkspace_CanCoerce(result_type, expected_type))) {
            AcuError err = {.as.type_mismatch = {.expected = expected_type, .actual = result_type}};
            AcuWorkspace_PushError(ws, expr_idx, ACU_ERR_ANALYZER_TYPE_MISMATCH, err);
            final_type = expected_type;
        } else {
            if (unlikely(result_type == TYPE_PRIMITIVE_NEVER)) {
                final_type = TYPE_PRIMITIVE_NEVER;
            } else {
                final_type = expected_type;
            }
        }
    } else {
        final_type = result_type;
    }

    V_At(&ws->node_types, expr_idx) = final_type;
    return final_type;
}

bool AcuWorkspace_Pass3_TypeCheck(AcuWorkspace *ws) {
    ws->expected_return_type = ACU_NULL_IDX;

    u64 node_count = V_Count(&ws->node_types);
    if (node_count == 0 && ws->builder) {
        node_count = V_Count(&ws->builder->nodes);
    }
    if (node_count > 0) {
        V_SetCount(&ws->node_analysis, node_count);
        memset(V_Elements(&ws->node_analysis), 0, sizeof(AstNodeAnalysisData) * node_count);
    }

    u32 modules_count = AcuModuleManager_GetCount(ws->modules);

    for (ModuleId m = 0; m < modules_count; ++m) {
        if (AcuModuleManager_GetSourceModuleStage(ws->modules, m) <
            ACU_MODULE_STAGE_SYMBOLS_BOUND) {
            continue;
        }

        AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(ws->modules, m);
        ScopeId mod_scope = AcuModuleManager_GetModuleScope(ws->modules, m);
        AstNode *module_node = AcuAstBuilder_GetNode(ws->builder, root_idx);

        V_At(&ws->node_types, root_idx) = TYPE_PRIMITIVE_UNIT;

        ExtraIdx current_extra_idx = module_node->as.module.start_idx;
        ExtraIdx end_extra_idx = current_extra_idx + module_node->as.module.statements_count;

        while (current_extra_idx < end_extra_idx) {
            AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, current_extra_idx);
            AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

            switch (stmt->type) {
                case AST_VAR_DECL: {
                    StringId name = stmt->as.var_decl.name;
                    SymbolId sym_id = AcuSymbolTable_LookupExact(ws->symbols, mod_scope, name);
                    V_At(&ws->node_analysis, stmt_idx).var_decl.sym_id = sym_id;
                    AcuWorkspace_EnsureGlobalVarChecked(ws, sym_id);
                    break;
                }

                case AST_FN_DECL:
                case AST_IMPORT:
                    V_At(&ws->node_types, stmt_idx) = TYPE_PRIMITIVE_UNIT;
                    break;

                default: {
                    AcuWorkspace_CheckExpr(ws, mod_scope, stmt_idx, ACU_NULL_IDX);
                    break;
                }
            }
            ++current_extra_idx;
        }
    }

    for (ModuleId m = 0; m < modules_count; ++m) {
        if (AcuModuleManager_GetSourceModuleStage(ws->modules, m) <
            ACU_MODULE_STAGE_SYMBOLS_BOUND) {
            continue;
        }

        AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(ws->modules, m);
        ScopeId mod_scope = AcuModuleManager_GetModuleScope(ws->modules, m);
        AstNode *module_node = AcuAstBuilder_GetNode(ws->builder, root_idx);

        ExtraIdx current_extra_idx = module_node->as.module.start_idx;
        ExtraIdx end_extra_idx = current_extra_idx + module_node->as.module.statements_count;

        while (current_extra_idx < end_extra_idx) {
            AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, current_extra_idx);
            AstNode *stmt = AcuAstBuilder_GetNode(ws->builder, stmt_idx);

            if (stmt->type == AST_FN_DECL) {
                StringId fn_name = stmt->as.fn_decl.name;
                SymbolId fn_sym_id = AcuSymbolTable_LookupExact(ws->symbols, mod_scope, fn_name);

                AstFnDeclPayload *payload =
                    AcuAstBuilder_GetFnDeclPayload(ws->builder, stmt->as.fn_decl.payload_idx);

                ScopeId body_scope = AcuSymbolTable_PushScope(ws->symbols, mod_scope, m);

                V_At(&ws->node_analysis, stmt_idx).fn_decl.sym_id = fn_sym_id;
                V_At(&ws->node_analysis, stmt_idx).fn_decl.body_scope = body_scope;

                TypeId ret_type =
                    (payload->return_type != ACU_NULL_IDX)
                        ? AcuWorkspace_ResolveAstType(ws, mod_scope, payload->return_type)
                        : TYPE_PRIMITIVE_UNIT;

                for (u32 i = 0; i < payload->params_count; ++i) {
                    AstNodeIdx param_node_idx =
                        AcuAstBuilder_GetIdxByExtra(ws->builder, payload->params_start_idx + i);
                    AstNode *param_node = AcuAstBuilder_GetNode(ws->builder, param_node_idx);

                    StringId param_name = param_node->as.fn_param_decl.name;
                    TypeAstNodeIdx param_ast_type = param_node->as.fn_param_decl.type;

                    TypeId param_type = AcuWorkspace_ResolveAstType(ws, mod_scope, param_ast_type);
                    if (unlikely(param_type == ACU_NULL_IDX)) {
                        param_type = TYPE_PRIMITIVE_UNIT;
                    }

                    bool is_mut = (param_node->flags & AST_FLAG_MUTABLE) != 0;

                    if (unlikely(AcuSymbolTable_LookupExact(ws->symbols, body_scope, param_name) !=
                                 ACU_NULL_IDX)) {
                        AcuWorkspace_PushError(ws, param_node_idx,
                                               ACU_ERR_ANALYZER_REDEFINITION_OF_SYMBOL,
                                               (AcuError){0});
                        V_At(&ws->node_analysis, param_node_idx).var_decl.sym_id = ACU_NULL_IDX;
                        V_At(&ws->node_types, param_node_idx) = param_type;
                        continue;
                    }

                    SymbolId param_sym_id = AcuSymbolTable_AddLocalVar(
                        ws->symbols, body_scope, param_name, param_type, param_node_idx, is_mut);

                    V_At(&ws->node_analysis, param_node_idx).var_decl.sym_id = param_sym_id;
                    V_At(&ws->node_types, param_node_idx) = param_type;
                }

                if (stmt->as.fn_decl.body != ACU_NULL_IDX) {
                    TypeId prev_expected = ws->expected_return_type;
                    ws->expected_return_type = ret_type;

                    AcuWorkspace_CheckExpr(ws, body_scope, stmt->as.fn_decl.body, ret_type);

                    ws->expected_return_type = prev_expected;
                }
            }
            ++current_extra_idx;
        }

        AcuModuleManager_SetSourceModuleStage(ws->modules, m, ACU_MODULE_STAGE_TYPE_CHECKED);
    }

    return V_Count(&ws->errors) == 0;
}
