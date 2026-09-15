#include "codegen/const_eval.h"
#include "analyzer/analyzer.h"
#include "codegen/math_utils.h"

AcuConstVal AcuCodeGen_EvalConst(AcuWorkspace *ws, AstNodeIdx expr_idx) {
    if (expr_idx == ACU_NULL_IDX) {
        return (AcuConstVal){.kind = CONST_NONE};
    }

    AstNode *node = AcuAstBuilder_GetNode(ws->builder, expr_idx);

    switch (node->type) {
        case AST_LITERAL: {
            AcuLiteral *lit = AcuLiteralPool_Get(ws->literals, node->as.literal.idx);
            if (lit->kind == ACU_LITERAL_I64) {
                return (AcuConstVal){.kind = CONST_INT, .as.i64_val = lit->as.i64};
            }
            if (lit->kind == ACU_LITERAL_U64) {
                return (AcuConstVal){.kind = CONST_INT, .as.i64_val = (i64)lit->as.u64};
            }
            if (lit->kind == ACU_LITERAL_F64) {
                return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = lit->as.f64};
            }
            break;
        }

        case AST_UNARY: {
            AcuConstVal r = AcuCodeGen_EvalConst(ws, node->as.unary.right);
            if (r.kind == CONST_NONE)
                return r;

            if (node->as.unary.type == AST_UNARY_MINUS) {
                if (r.kind == CONST_INT)
                    return (AcuConstVal){.kind = CONST_INT, .as.i64_val = -r.as.i64_val};
                if (r.kind == CONST_FLOAT)
                    return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = -r.as.f64_val};
            }
            if (node->as.unary.type == AST_UNARY_LOGICAL_NOT) {
                if (r.kind == CONST_BOOL)
                    return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = !r.as.bool_val};
                if (r.kind == CONST_INT)
                    return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (r.as.i64_val == 0)};
            }
            if (node->as.unary.type == AST_UNARY_BITWISE_NOT && r.kind == CONST_INT) {
                return (AcuConstVal){.kind = CONST_INT, .as.i64_val = ~r.as.i64_val};
            }
            break;
        }

        case AST_BINARY: {
            AcuConstVal l = AcuCodeGen_EvalConst(ws, node->as.binary.left);
            AcuConstVal r = AcuCodeGen_EvalConst(ws, node->as.binary.right);

            if (l.kind == CONST_INT && r.kind == CONST_INT) {
                i64 a = l.as.i64_val;
                i64 b = r.as.i64_val;
                switch (node->as.binary.type) {
                    case AST_BINARY_ADD:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a + b};
                    case AST_BINARY_SUB:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a - b};
                    case AST_BINARY_MUL:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a * b};
                    case AST_BINARY_DIV:
                        if (b == 0 || (a == (i64)0x8000000000000000ULL && b == -1))
                            return (AcuConstVal){0};
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a / b};
                    case AST_BINARY_REM:
                        if (b == 0 || (a == (i64)0x8000000000000000ULL && b == -1))
                            return (AcuConstVal){0};
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a % b};
                    case AST_BINARY_POW:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = IntPow(a, b)};
                    case AST_BINARY_SHL:
                        if (b < 0 || b >= 64)
                            return (AcuConstVal){0};
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = (i64)((u64)a << b)};
                    case AST_BINARY_SHR:
                        if (b < 0 || b >= 64)
                            return (AcuConstVal){0};
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a >> b};
                    case AST_BINARY_BITWISE_AND:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a & b};
                    case AST_BINARY_BITWISE_OR:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a | b};
                    case AST_BINARY_BITWISE_XOR:
                        return (AcuConstVal){.kind = CONST_INT, .as.i64_val = a ^ b};
                    case AST_BINARY_EQ:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a == b)};
                    case AST_BINARY_NEQ:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a != b)};
                    case AST_BINARY_LT:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a < b)};
                    case AST_BINARY_LTE:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a <= b)};
                    case AST_BINARY_GT:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a > b)};
                    case AST_BINARY_GTE:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (a >= b)};
                    default:
                        break;
                }
            } else if (l.kind == CONST_FLOAT && r.kind == CONST_FLOAT) {
                f64 fa = l.as.f64_val;
                f64 fb = r.as.f64_val;
                switch (node->as.binary.type) {
                    case AST_BINARY_ADD:
                        return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = fa + fb};
                    case AST_BINARY_SUB:
                        return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = fa - fb};
                    case AST_BINARY_MUL:
                        return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = fa * fb};
                    case AST_BINARY_DIV:
                        return (AcuConstVal){.kind = CONST_FLOAT, .as.f64_val = fa / fb};
                    case AST_BINARY_EQ:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa == fb)};
                    case AST_BINARY_NEQ:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa != fb)};
                    case AST_BINARY_LT:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa < fb)};
                    case AST_BINARY_LTE:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa <= fb)};
                    case AST_BINARY_GT:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa > fb)};
                    case AST_BINARY_GTE:
                        return (AcuConstVal){.kind = CONST_BOOL, .as.bool_val = (fa >= fb)};
                    default:
                        break;
                }
            }
            break;
        }

        default:
            break;
    }

    return (AcuConstVal){.kind = CONST_NONE};
}

bool AcuCodeGen_IsPure(AcuWorkspace *ws, AstNodeIdx idx) {
    if (idx == ACU_NULL_IDX)
        return true;
    AstNode *n = AcuAstBuilder_GetNode(ws->builder, idx);

    switch (n->type) {
        case AST_LITERAL:
        case AST_IDENTIFIER:
            return true;

        case AST_UNARY:
            return AcuCodeGen_IsPure(ws, n->as.unary.right);

        case AST_BINARY: {
            AstBinaryKind kind = n->as.binary.type;
            AstNodeIdx left_idx = n->as.binary.left;
            AstNodeIdx right_idx = n->as.binary.right;

            if (!AcuCodeGen_IsPure(ws, left_idx) || !AcuCodeGen_IsPure(ws, right_idx)) {
                return false;
            }

            if (kind == AST_BINARY_DIV || kind == AST_BINARY_REM) {
                TypeId operand_type = V_At(&ws->node_types, left_idx);

                if (TypePrimitiveKind_IsFloat(operand_type)) {
                    return true;
                }

                AcuConstVal r_const = AcuCodeGen_EvalConst(ws, right_idx);
                if (r_const.kind != CONST_INT) {
                    return false;
                }

                i64 div_val = r_const.as.i64_val;
                if (div_val == 0) {
                    return false;
                }

                bool is_signed = TypePrimitiveKind_IsSignedInt(operand_type);
                if (is_signed) {
                    if (div_val == -1) {
                        AcuConstVal l_const = AcuCodeGen_EvalConst(ws, left_idx);
                        if (l_const.kind != CONST_INT ||
                            l_const.as.i64_val == (i64)0x8000000000000000ULL) {
                            return false;
                        }
                    }
                    return true;
                }

                return true;
            }

            return true;
        }

        case AST_TYPE_CAST:
            return AcuCodeGen_IsPure(ws, n->as.type_cast.expr);

        case AST_IF: {
            bool cond_pure = AcuCodeGen_IsPure(ws, n->as.if_stmt.condition);
            bool then_pure = AcuCodeGen_IsPure(ws, n->as.if_stmt.then_body);
            bool else_pure = (n->as.if_stmt.else_body == ACU_NULL_IDX) ||
                             AcuCodeGen_IsPure(ws, n->as.if_stmt.else_body);
            return cond_pure && then_pure && else_pure;
        }

        case AST_WHILE:
            return AcuCodeGen_IsPure(ws, n->as.while_stmt.condition) &&
                   AcuCodeGen_IsPure(ws, n->as.while_stmt.body);

        case AST_CALL: {
            SymbolId sym_id = V_At(&ws->node_analysis, idx).call.callee_sym;
            if (sym_id == ACU_NULL_IDX)
                return false;

            AcuSymbol *sym = AcuSymbolTable_Get(ws->symbols, sym_id);
            if ((sym->flags & ACU_SYMBOL_FLAG_PURE) == 0)
                return false;

            u32 arg_count = n->as.call.arguments_count;
            ExtraIdx arg_start = n->as.call.start_idx;
            for (u32 i = 0; i < arg_count; ++i) {
                AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(ws->builder, arg_start + i);
                if (!AcuCodeGen_IsPure(ws, arg_idx))
                    return false;
            }
            return true;
        }

        default:
            return false;
    }
}
