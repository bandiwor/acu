#include "ast/ast.h"
#include "codegen/codegen_internal.h"
#include "codegen/const_eval.h"
#include "codegen/math_utils.h"
#include "codegen/op_tables.h"
#include "defines/bytecode.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "literal/literal.h"
#include "pool/constant_pool.h"
#include "types/string_slice.h"
#include <assert.h>

static bool AcuCodeGen_IsSameExpr(AcuCodeGen *cg, AstNodeIdx a_idx, AstNodeIdx b_idx) {
    if (a_idx == b_idx)
        return true;
    if (a_idx == ACU_NULL_IDX || b_idx == ACU_NULL_IDX)
        return false;

    AstNode *a = AcuAstBuilder_GetNode(cg->ws->builder, a_idx);
    AstNode *b = AcuAstBuilder_GetNode(cg->ws->builder, b_idx);

    if (a->type == AST_IDENTIFIER && b->type == AST_IDENTIFIER) {
        return a->as.identifier.name == b->as.identifier.name;
    }
    return false;
}

static inline AcuTarget AcuCodeGen_EmitConstImm(AcuCodeGen *cg, i16 imm, AcuTarget target) {
    if (AcuTarget_IsNone(target))
        return ACU_TARGET_NONE;
    AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
    AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, imm));
    return AcuTarget_Reg(dst);
}

static inline AcuTarget AcuCodeGen_EmitImmOp(AcuCodeGen *cg, AstNodeIdx src_idx, AcuOpcode op,
                                             i8 imm, AcuTarget target) {
    if (AcuTarget_IsNone(target)) {
        if (!AcuCodeGen_IsPure(cg->ws, src_idx)) {
            AcuCodeGen_EmitExpr(cg, src_idx, ACU_TARGET_NONE);
        }
        return ACU_TARGET_NONE;
    }

    AcuTarget src_t = AcuCodeGen_EmitExpr(cg, src_idx, ACU_TARGET_AUTO);
    AcuReg src = AcuTarget_ToRealReg(src_t);
    AcuReg dst = (AcuTarget_IsAuto(target) && !AcuCodeGen_IsLocalReg(cg, src))
                     ? src
                     : AcuCodeGen_ResolveTarget(cg, target);

    AcuCodeGen_Emit(cg, AcuInst_Encode_ABsC(op, dst, src, imm));

    if (dst != src && !AcuCodeGen_IsLocalReg(cg, src)) {
        AcuCodeGen_FreeReg(cg, src);
    }
    return AcuTarget_Reg(dst);
}

static inline AcuReg AcuCodeGen_AllocCallWindow(AcuCodeGen *cg, u32 count) {
    int highest = AcuRegSet_GetHighestUsed(&cg->reg_set);

    u32 base = (highest < 0) ? 0 : (u32)(highest + 1);

    if (base + count > 256) {
        acu_panic("Register file overflow: window [%u..%u] exceeds 256 registers", base,
                  base + count);
    }

    for (u32 i = 0; i < count; ++i) {
        AcuRegSet_MarkUsed(&cg->reg_set, (AcuReg)(base + i));
    }

    if (base + count > cg->max_reg) {
        cg->max_reg = base + count;
    }

    return (AcuReg)base;
}

static AcuReg AcuCodeGen_EmitContiguousArgs(AcuCodeGen *cg, ExtraIdx arg_start, u32 arg_count) {
    bool outer_tail = cg->is_tail_pos;
    cg->is_tail_pos = false;

    AcuReg base_reg = AcuCodeGen_AllocCallWindow(cg, arg_count);

    for (u32 i = 0; i < arg_count; ++i) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start + i);
        AcuReg target_reg = (AcuReg)(base_reg + i);

        AcuTarget res = AcuCodeGen_EmitExpr(cg, arg_idx, AcuTarget_Reg(target_reg));
        AcuReg r = AcuTarget_ToRealReg(res);

        if (r != target_reg) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, target_reg, r));
            if (!AcuCodeGen_IsLocalReg(cg, r)) {
                AcuCodeGen_FreeReg(cg, r);
            }
        }

        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            break;
        }
    }

    cg->is_tail_pos = outer_tail;
    return base_reg;
}

static AcuTarget AcuCodeGen_EmitUnaryOp(AcuCodeGen *cg, AstNodeIdx src_idx, AcuOpcode op,
                                        AcuTarget target) {
    if (AcuTarget_IsNone(target)) {
        if (!AcuCodeGen_IsPure(cg->ws, src_idx)) {
            AcuCodeGen_EmitExpr(cg, src_idx, ACU_TARGET_NONE);
        }
        return ACU_TARGET_NONE;
    }

    AcuTarget src_t = AcuCodeGen_EmitExpr(cg, src_idx, ACU_TARGET_AUTO);
    if (AcuCodeGen_IsNever(cg, src_idx)) {
        return src_t;
    }
    AcuReg src = AcuTarget_ToRealReg(src_t);

    AcuReg dst;
    if (AcuTarget_IsAuto(target) && !AcuCodeGen_IsLocalReg(cg, src)) {
        dst = src;
    } else {
        dst = AcuCodeGen_ResolveTarget(cg, target);
    }

    AcuCodeGen_Emit(cg, AcuInst_Encode_AB(op, dst, src));

    if (dst != src && !AcuCodeGen_IsLocalReg(cg, src)) {
        AcuCodeGen_FreeReg(cg, src);
    }

    return AcuTarget_Reg(dst);
}

static bool AcuCodeGen_TryStrengthReduction(AcuCodeGen *cg, AstBinaryKind op_kind,
                                            AstNodeIdx left_idx, AstNodeIdx right_idx,
                                            AcuTarget target, AcuTarget *out_target) {
    TypeId operand_type = V_At(&cg->ws->node_types, left_idx);
    bool is_float = TypePrimitiveKind_IsFloat(operand_type);
    bool is_unsigned = !TypePrimitiveKind_IsSignedInt(operand_type);

    if (!is_float && AcuCodeGen_IsPure(cg->ws, left_idx) &&
        AcuCodeGen_IsSameExpr(cg, left_idx, right_idx)) {
        switch (op_kind) {
            case AST_BINARY_SUB:
            case AST_BINARY_BITWISE_XOR:
            case AST_BINARY_NEQ:
            case AST_BINARY_LT:
            case AST_BINARY_GT:
                *out_target = AcuCodeGen_EmitConstImm(cg, 0, target);
                return true;

            case AST_BINARY_EQ:
            case AST_BINARY_LTE:
            case AST_BINARY_GTE:
                *out_target = AcuCodeGen_EmitConstImm(cg, 1, target);
                return true;

            case AST_BINARY_BITWISE_AND:
            case AST_BINARY_BITWISE_OR:
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;

            default:
                break;
        }
    }

    AstNode *left_node = AcuAstBuilder_GetNode(cg->ws->builder, left_idx);
    if (!is_float && left_node->type == AST_LITERAL) {
        AcuLiteral *lit = AcuLiteralPool_Get(cg->ws->literals, left_node->as.literal.idx);
        if (lit->kind == ACU_LITERAL_I64 || lit->kind == ACU_LITERAL_U64) {
            i64 sval = (lit->kind == ACU_LITERAL_I64) ? lit->as.i64 : (i64)lit->as.u64;
            if (op_kind == AST_BINARY_SUB) {
                if (sval == 0) {
                    *out_target = AcuCodeGen_EmitUnaryOp(cg, right_idx, OP_NEG, target);
                    return true;
                }
                if (sval >= -128 && sval <= 127) {
                    *out_target =
                        AcuCodeGen_EmitImmOp(cg, right_idx, OP_RSUB_IMM, (i8)sval, target);
                    return true;
                }
            }
        }
    }

    AstNode *right_node = AcuAstBuilder_GetNode(cg->ws->builder, right_idx);
    if (right_node->type != AST_LITERAL) {
        return false;
    }

    AcuLiteral *lit = AcuLiteralPool_Get(cg->ws->literals, right_node->as.literal.idx);
    if (lit->kind != ACU_LITERAL_I64 && lit->kind != ACU_LITERAL_U64) {
        return false;
    }

    u64 uval = lit->as.u64;
    i64 sval = (lit->kind == ACU_LITERAL_I64) ? lit->as.i64 : (i64)uval;

    switch (op_kind) {
        case AST_BINARY_ADD:
        case AST_BINARY_SUB:
            if (sval == 0) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            break;

        case AST_BINARY_MUL:
            if (sval == 1) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            if (!is_float && sval == 0) {
                if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
                    AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
                }
                *out_target = AcuCodeGen_EmitConstImm(cg, 0, target);
                return true;
            }
            if (!is_float && sval == -1) {
                *out_target = AcuCodeGen_EmitUnaryOp(cg, left_idx, OP_NEG, target);
                return true;
            }
            if (!is_float && sval > 0 && AcuIsPowerOfTwoU64((u64)sval)) {
                u32 shift = AcuLog2U64((u64)sval);
                *out_target = AcuCodeGen_EmitImmOp(cg, left_idx, OP_SHL_IMM, (i8)shift, target);
                return true;
            }
            break;

        case AST_BINARY_DIV:
            if (sval == 1) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            if (!is_float && is_unsigned && uval > 0 && AcuIsPowerOfTwoU64(uval)) {
                u32 shift = AcuLog2U64(uval);
                *out_target = AcuCodeGen_EmitImmOp(cg, left_idx, OP_SHR_U_IMM, (i8)shift, target);
                return true;
            }
            break;

        case AST_BINARY_REM:
            if (!is_float && is_unsigned && uval > 0 && AcuIsPowerOfTwoU64(uval)) {
                u64 mask = uval - 1;
                if (mask <= 127) {
                    *out_target = AcuCodeGen_EmitImmOp(cg, left_idx, OP_AND_IMM, (i8)mask, target);
                    return true;
                }
                if (AcuTarget_IsNone(target)) {
                    if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
                        AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
                    }
                    *out_target = ACU_TARGET_NONE;
                    return true;
                }

                AcuTarget src_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
                AcuReg src = AcuTarget_ToRealReg(src_t);
                AcuReg dst = (AcuTarget_IsAuto(target) && !AcuCodeGen_IsLocalReg(cg, src))
                                 ? src
                                 : AcuCodeGen_ResolveTarget(cg, target);

                AcuReg mask_reg = AcuCodeGen_AllocReg(cg);
                if (mask <= 32767) {
                    AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, mask_reg, (i16)mask));
                } else {
                    u32 cid = AcuConstantPool_InternU64(&cg->chunk->constants, mask);
                    assert(cid <= UINT16_MAX);
                    AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, mask_reg, (u16)cid));
                }

                AcuCodeGen_Emit(cg, AcuInst_Encode_ABC(OP_AND, dst, src, mask_reg));
                AcuCodeGen_FreeReg(cg, mask_reg);

                if (dst != src && !AcuCodeGen_IsLocalReg(cg, src)) {
                    AcuCodeGen_FreeReg(cg, src);
                }
                *out_target = AcuTarget_Reg(dst);
                return true;
            }
            break;

        case AST_BINARY_POW:
            if (sval == 1) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            if (!is_float && sval == 0) {
                if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
                    AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
                }
                *out_target = AcuCodeGen_EmitConstImm(cg, 1, target);
                return true;
            }
            if (sval == 2) {
                if (AcuTarget_IsNone(target)) {
                    if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
                        AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
                    }
                    *out_target = ACU_TARGET_NONE;
                    return true;
                }

                AcuTarget src_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
                AcuReg src = AcuTarget_ToRealReg(src_t);
                AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
                AcuOpcode mul_op = is_float ? OP_FMUL : OP_MUL;

                AcuCodeGen_Emit(cg, AcuInst_Encode_ABC(mul_op, dst, src, src));

                if (dst != src && !AcuCodeGen_IsLocalReg(cg, src)) {
                    AcuCodeGen_FreeReg(cg, src);
                }
                *out_target = AcuTarget_Reg(dst);
                return true;
            }
            break;

        case AST_BINARY_SHL:
        case AST_BINARY_SHR:
            if (sval == 0) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            break;

        case AST_BINARY_BITWISE_AND:
            if (uval == 0) {
                if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
                    AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
                }
                *out_target = AcuCodeGen_EmitConstImm(cg, 0, target);
                return true;
            }
            // x & ~0 -> x
            if (uval == UINT64_MAX) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            break;

        case AST_BINARY_BITWISE_OR:
            if (uval == 0) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            break;

        case AST_BINARY_BITWISE_XOR:
            if (uval == 0) {
                *out_target = AcuCodeGen_EmitExpr(cg, left_idx, target);
                return true;
            }
            // x ^ -1 -> ~x
            if (sval == -1) {
                *out_target = AcuCodeGen_EmitUnaryOp(cg, left_idx, OP_NOT, target);
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

static AcuTarget AcuCodeGen_EmitLiteral(AcuCodeGen *cg, LiteralIdx lit_idx, TypeId type_id,
                                        AcuTarget target) {
    if (AcuTarget_IsNone(target)) {
        return ACU_TARGET_NONE;
    }

    AcuLiteral *lit = AcuLiteralPool_Get(cg->ws->literals, lit_idx);

    if (lit->kind == ACU_LITERAL_STR) {
        StringView sv = AcuLiteralPool_GetString(cg->ws->literals, lit_idx);
        StringSlice slice = AcuStringPool_Intern(&cg->chunk->strings, sv);

        u32 const_idx = AcuConstantPool_InternU64(&cg->chunk->constants, slice.raw);
        assert(const_idx <= UINT16_MAX && "Constant pool overflow: index exceeds 16-bit Bx limit!");

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_STR, dst, (u16)const_idx));
        return AcuTarget_Reg(dst);
    }

    if (TypePrimitiveKind_IsFloat(type_id) || lit->kind == ACU_LITERAL_F64) {
        f64 fval;
        if (lit->kind == ACU_LITERAL_F64) {
            fval = lit->as.f64;
        } else if (lit->kind == ACU_LITERAL_I64) {
            fval = (f64)lit->as.i64;
        } else {
            fval = (f64)lit->as.u64;
        }

        u32 cid;
        if (type_id == (TypeId)TYPE_PRIMITIVE_F32) {
            cid = AcuConstantPool_InternF32(&cg->chunk->constants, (f32)fval);
        } else {
            cid = AcuConstantPool_InternF64(&cg->chunk->constants, fval);
        }
        assert(cid <= UINT16_MAX && "Constant pool overflow: index exceeds 16-bit Bx limit!");

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)cid));
        return AcuTarget_Reg(dst);
    }

    if (lit->kind == ACU_LITERAL_I64 || lit->kind == ACU_LITERAL_U64) {
        bool is_signed = TypePrimitiveKind_IsSignedInt(type_id);
        i64 sval = (lit->kind == ACU_LITERAL_I64) ? lit->as.i64 : (i64)lit->as.u64;
        u64 uval = (lit->kind == ACU_LITERAL_U64) ? lit->as.u64 : (u64)lit->as.i64;

        bool fits_imm = is_signed ? (sval >= -32768 && sval <= 32767) : (uval <= 32767);

        if (fits_imm) {
            AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
            AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, (i16)sval));
            return AcuTarget_Reg(dst);
        }

        u64 raw_bits = (lit->kind == ACU_LITERAL_U64) ? uval : (u64)sval;
        u32 cid = AcuConstantPool_InternU64(&cg->chunk->constants, raw_bits);
        assert(cid <= UINT16_MAX && "Constant pool overflow: index exceeds 16-bit Bx limit!");

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)cid));
        return AcuTarget_Reg(dst);
    }

    acu_panic("Unhandled literal kind in code generation: %d", lit->kind);
    return ACU_TARGET_NONE;
}

typedef struct {
    AcuOpcode reg_op;
    AcuOpcode imm_op;
} AssignOpcodePair;

static inline AssignOpcodePair AcuCodeGen_GetAssignOpcodes(AcuCodeGen *cg, AstAssignKind op,
                                                           AstNodeIdx target_idx) {
    TypeId type_id = V_At(&cg->ws->node_types, target_idx);

    bool is_unsigned = TypePrimitiveKind_IsUnsignedInt(type_id);
    bool is_float = TypePrimitiveKind_IsFloat(type_id);

    if (is_float) {
        switch (op) {
            case AST_ASSIGN_ADD_EQ:
                return (AssignOpcodePair){OP_FADD, OP_NOP};
            case AST_ASSIGN_SUB_EQ:
                return (AssignOpcodePair){OP_FSUB, OP_NOP};
            case AST_ASSIGN_MUL_EQ:
                return (AssignOpcodePair){OP_FMUL, OP_NOP};
            case AST_ASSIGN_DIV_EQ:
                return (AssignOpcodePair){OP_FDIV, OP_NOP};
            case AST_ASSIGN_REM_EQ:
                return (AssignOpcodePair){OP_FREM, OP_NOP};
            case AST_ASSIGN_POW_EQ:
                return (AssignOpcodePair){OP_FPOW, OP_NOP};
            default:
                return (AssignOpcodePair){OP_NOP, OP_NOP};
        }
    }

    switch (op) {
        case AST_ASSIGN_ADD_EQ:
            return (AssignOpcodePair){OP_ADD, OP_ADD_IMM};
        case AST_ASSIGN_SUB_EQ:
            return (AssignOpcodePair){OP_SUB, OP_SUB_IMM};
        case AST_ASSIGN_MUL_EQ:
            return (AssignOpcodePair){OP_MUL, OP_MUL_IMM};
        case AST_ASSIGN_POW_EQ:
            return (AssignOpcodePair){OP_POW, OP_POW_IMM};
        case AST_ASSIGN_BITWISE_AND_EQ:
            return (AssignOpcodePair){OP_AND, OP_AND_IMM};
        case AST_ASSIGN_BITWISE_OR_EQ:
            return (AssignOpcodePair){OP_OR, OP_OR_IMM};
        case AST_ASSIGN_BITWISE_XOR_EQ:
            return (AssignOpcodePair){OP_XOR, OP_XOR_IMM};
        case AST_ASSIGN_SHL_EQ:
            return (AssignOpcodePair){OP_SHL, OP_SHL_IMM};
        case AST_ASSIGN_SHR_EQ:
            return is_unsigned ? (AssignOpcodePair){OP_SHR_U, OP_SHR_U_IMM}
                               : (AssignOpcodePair){OP_SHR_S, OP_SHR_S_IMM};
        case AST_ASSIGN_DIV_EQ:
            return (AssignOpcodePair){is_unsigned ? OP_DIV_U : OP_DIV_S, OP_NOP};
        case AST_ASSIGN_REM_EQ:
            return (AssignOpcodePair){is_unsigned ? OP_REM_U : OP_REM_S, OP_NOP};
        case AST_ASSIGN_EQ:
        default:
            return (AssignOpcodePair){OP_NOP, OP_NOP};
    }
}

static bool AcuCodeGen_TryGetIntImm8(const AcuCodeGen *cg, AstNodeIdx expr_idx, i8 *out_imm) {
    const AstNode *node = AcuAstBuilder_GetNode(cg->ws->builder, expr_idx);

    if (node->type == AST_LITERAL) {
        LiteralIdx lit_idx = node->as.literal.idx;
        AcuLiteral *literal = AcuLiteralPool_Get(cg->ws->literals, lit_idx);
        if (literal->kind == ACU_LITERAL_I64) {
            i64 val = literal->as.i64;
            if (val >= -128 && val <= 127) {
                *out_imm = (i8)val;
                return true;
            }
            return false;
        }
        if (literal->kind == ACU_LITERAL_U64) {
            u64 val = literal->as.u64;
            if (val <= 127) {
                *out_imm = (i8)val;
                return true;
            }
            return false;
        }
        return false;
    }

    return false;
}

static AcuTarget AcuCodeGen_EmitFoldedConstant(AcuCodeGen *cg, AcuConstVal folded, TypeId type_id,
                                               AcuTarget target) {
    if (AcuTarget_IsNone(target)) {
        return ACU_TARGET_NONE;
    }

    if (folded.kind == CONST_INT || folded.kind == CONST_BOOL) {
        i64 val = (folded.kind == CONST_BOOL) ? (i64)folded.as.bool_val : folded.as.i64_val;
        if (val >= -32768 && val <= 32767) {
            return AcuCodeGen_EmitConstImm(cg, (i16)val, target);
        }

        u32 cid = AcuConstantPool_InternI64(&cg->chunk->constants, val);
        assert(cid <= UINT16_MAX && "Constant pool overflow: index exceeds 16-bit Bx limit!");

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)cid));
        return AcuTarget_Reg(dst);
    }

    if (folded.kind == CONST_FLOAT) {
        u32 cid = (type_id == (TypeId)TYPE_PRIMITIVE_F32)
                      ? AcuConstantPool_InternF32(&cg->chunk->constants, (f32)folded.as.f64_val)
                      : AcuConstantPool_InternF64(&cg->chunk->constants, folded.as.f64_val);
        assert(cid <= UINT16_MAX && "Constant pool overflow: index exceeds 16-bit Bx limit!");

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)cid));
        return AcuTarget_Reg(dst);
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitSymbolRead(AcuCodeGen *cg, SymbolId sym_id, TypeId node_type,
                                           AcuTarget target) {
    if (AcuTarget_IsNone(target)) {
        return ACU_TARGET_NONE;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(cg->ws->symbols, sym_id);

    switch (sym->kind) {
        case ACU_SYMBOL_LOCAL_VAR: {
            AcuLocalBinding *local = AcuCodeGen_FindLocal(cg, sym_id);
            if (!local) {
                return ACU_TARGET_NONE;
            }

            if (AcuTarget_IsAuto(target) ||
                (AcuTarget_IsRealReg(target) && AcuTarget_ToRealReg(target) == local->reg)) {
                return AcuTarget_Reg(local->reg);
            }

            AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, local->reg));
            return AcuTarget_Reg(dst);
        }

        case ACU_SYMBOL_GLOBAL_VAR: {
            if (cg->last_stored_global == sym_id) {
                AcuReg cached_reg = cg->last_stored_reg;
                AcuRegSet_MarkUsed(&cg->reg_set, cached_reg);

                if (AcuTarget_IsAuto(target) ||
                    (AcuTarget_IsRealReg(target) && AcuTarget_ToRealReg(target) == cached_reg)) {
                    return AcuTarget_Reg(cached_reg);
                }

                AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
                AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, cached_reg));
                return AcuTarget_Reg(dst);
            }

            u16 slot = (u16)AcuCodeGen_GetGlobalSlot(cg, sym_id);
            AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
            AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_GLOBAL, dst, slot));
            return AcuTarget_Reg(dst);
        }

        case ACU_SYMBOL_CONSTANT:
            return AcuCodeGen_EmitLiteral(cg, sym->as.const_val, node_type, target);

        default:
            return ACU_TARGET_NONE;
    }
}

static void AcuCodeGen_CanonicalizeBinary(AcuCodeGen *cg, AstBinaryKind *op_kind,
                                          AstNodeIdx *left_idx, AstNodeIdx *right_idx) {
    AstNode *left_node = AcuAstBuilder_GetNode(cg->ws->builder, *left_idx);
    AstNode *right_node = AcuAstBuilder_GetNode(cg->ws->builder, *right_idx);

    if (left_node->type == AST_LITERAL && right_node->type != AST_LITERAL) {
        if (AstBinary_IsCommutative(*op_kind)) {
            AstNodeIdx tmp = *left_idx;
            *left_idx = *right_idx;
            *right_idx = tmp;
        } else if (AstBinary_IsComparison(*op_kind)) {
            AstNodeIdx tmp = *left_idx;
            *left_idx = *right_idx;
            *right_idx = tmp;
            *op_kind = AstBinary_InvertComparison(*op_kind);
        }
    }
}

static bool AcuCodeGen_TryGetBinaryImm(AcuCodeGen *cg, AstBinaryKind op_kind, AstNodeIdx right_idx,
                                       TypeId operand_type, AcuOpcode *out_op, i8 *out_imm) {
    if (TypePrimitiveKind_IsFloat(operand_type)) {
        return false;
    }

    AstNode *right_node = AcuAstBuilder_GetNode(cg->ws->builder, right_idx);
    if (right_node->type != AST_LITERAL) {
        return false;
    }

    AcuLiteral *lit = AcuLiteralPool_Get(cg->ws->literals, right_node->as.literal.idx);
    if (lit->kind != ACU_LITERAL_I64 && lit->kind != ACU_LITERAL_U64) {
        return false;
    }

    i64 sval = (lit->kind == ACU_LITERAL_I64) ? lit->as.i64 : (i64)lit->as.u64;

    if (sval < -128 || sval > 127) {
        return false;
    }

    AcuOpcode imm_op = AcuCodeGen_GetBinaryOpcode(op_kind, operand_type, true);
    if (imm_op == OP_NOP) {
        return false;
    }

    *out_op = imm_op;
    *out_imm = (i8)sval;
    return true;
}

static AcuTarget AcuCodeGen_EmitBinaryInst(AcuCodeGen *cg, AcuOpcode op, AcuReg left_reg,
                                           AcuReg right_reg, bool is_imm, i8 imm_val,
                                           AcuTarget target) {
    AcuReg dst;
    if (AcuTarget_IsAuto(target) && !AcuCodeGen_IsLocalReg(cg, left_reg)) {
        dst = left_reg;
    } else {
        dst = AcuCodeGen_ResolveTarget(cg, target);
    }

    if (is_imm) {
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABsC(op, dst, left_reg, (u8)imm_val));
    } else {
        AcuCodeGen_Emit(cg, AcuInst_Encode_ABC(op, dst, left_reg, right_reg));
    }

    if (dst != left_reg && !AcuCodeGen_IsLocalReg(cg, left_reg)) {
        AcuCodeGen_FreeReg(cg, left_reg);
    }
    if (!is_imm && dst != right_reg && right_reg != left_reg &&
        !AcuCodeGen_IsLocalReg(cg, right_reg)) {
        AcuCodeGen_FreeReg(cg, right_reg);
    }

    return AcuTarget_Reg(dst);
}

static inline AcuOpcode AcuCodeGen_GetUnaryOpcode(AstUnaryKind kind, TypeId type_id) {
    switch (kind) {
        case AST_UNARY_MINUS:
            return TypePrimitiveKind_IsFloat(type_id) ? OP_FNEG : OP_NEG;
        case AST_UNARY_BITWISE_NOT:
            return OP_NOT;
        case AST_UNARY_LOGICAL_NOT:
            return OP_LNOT;
        case AST_UNARY_PLUS:
        default:
            return OP_NOP;
    }
}

static AcuTarget AcuCodeGen_EmitLogicalAnd(AcuCodeGen *cg, AstNodeIdx left_idx,
                                           AstNodeIdx right_idx, AcuTarget target) {
    bool outer_tail = cg->is_tail_pos;
    cg->is_tail_pos = false;

    if (AcuTarget_IsNone(target)) {
        AcuTarget left_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
        AcuReg left_r = AcuTarget_ToRealReg(left_t);

        u32 jump_false_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_JUMP_IF_FALSE, left_r, 0));
        if (!AcuCodeGen_IsLocalReg(cg, left_r))
            AcuCodeGen_FreeReg(cg, left_r);

        AcuCodeGen_EmitExpr(cg, right_idx, ACU_TARGET_NONE);

        u32 end_ip = (u32)V_Count(&cg->chunk->code);
        i32 offset = (i32)end_ip - (i32)jump_false_ip - 1;
        assert(offset >= -32768 && offset <= 32767 && "Jump offset overflow!");
        V_At(&cg->chunk->code, jump_false_ip) =
            AcuInst_Encode_AsBx(OP_JUMP_IF_FALSE, left_r, (i16)offset);

        cg->is_tail_pos = outer_tail;
        return ACU_TARGET_NONE;
    }

    AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
    AcuRegSet_MarkUsed(&cg->reg_set, dst);

    AcuTarget left_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
    AcuReg left_r = AcuTarget_ToRealReg(left_t);

    u32 jump_false_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_JUMP_IF_FALSE, left_r, 0));
    if (!AcuCodeGen_IsLocalReg(cg, left_r))
        AcuCodeGen_FreeReg(cg, left_r);

    AcuTarget right_t = AcuCodeGen_EmitExpr(cg, right_idx, AcuTarget_Reg(dst));
    AcuReg right_r = AcuTarget_ToRealReg(right_t);
    if (right_r != dst) {
        AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, right_r));
        if (!AcuCodeGen_IsLocalReg(cg, right_r))
            AcuCodeGen_FreeReg(cg, right_r);
    }

    u32 jump_end_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, 0));

    u32 false_ip = (u32)V_Count(&cg->chunk->code);
    i32 offset_false = (i32)false_ip - (i32)jump_false_ip - 1;
    assert(offset_false >= -32768 && offset_false <= 32767 && "Jump offset overflow!");
    V_At(&cg->chunk->code, jump_false_ip) =
        AcuInst_Encode_AsBx(OP_JUMP_IF_FALSE, left_r, (i16)offset_false);

    AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0));

    u32 end_ip = (u32)V_Count(&cg->chunk->code);
    i32 offset_end = (i32)end_ip - (i32)jump_end_ip - 1;
    assert(offset_end >= -8388608 && offset_end <= 8388607 && "Jump offset overflow!");
    V_At(&cg->chunk->code, jump_end_ip) = AcuInst_Encode_sAx(OP_JUMP, offset_end);

    cg->is_tail_pos = outer_tail;
    AcuRegSet_MarkUsed(&cg->reg_set, dst);
    return AcuTarget_Reg(dst);
}

static AcuTarget AcuCodeGen_EmitLogicalOr(AcuCodeGen *cg, AstNodeIdx left_idx, AstNodeIdx right_idx,
                                          AcuTarget target) {
    bool outer_tail = cg->is_tail_pos;
    cg->is_tail_pos = false;

    if (AcuTarget_IsNone(target)) {
        AcuTarget left_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
        AcuReg left_r = AcuTarget_ToRealReg(left_t);

        u32 jump_true_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_JUMP_IF_TRUE, left_r, 0));
        if (!AcuCodeGen_IsLocalReg(cg, left_r))
            AcuCodeGen_FreeReg(cg, left_r);

        AcuCodeGen_EmitExpr(cg, right_idx, ACU_TARGET_NONE);

        u32 end_ip = (u32)V_Count(&cg->chunk->code);
        i32 offset = (i32)end_ip - (i32)jump_true_ip - 1;
        assert(offset >= -32768 && offset <= 32767 && "Jump offset overflow!");
        V_At(&cg->chunk->code, jump_true_ip) =
            AcuInst_Encode_AsBx(OP_JUMP_IF_TRUE, left_r, (i16)offset);

        cg->is_tail_pos = outer_tail;
        return ACU_TARGET_NONE;
    }

    AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
    AcuRegSet_MarkUsed(&cg->reg_set, dst);

    AcuTarget left_t = AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_AUTO);
    AcuReg left_r = AcuTarget_ToRealReg(left_t);

    u32 jump_true_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_JUMP_IF_TRUE, left_r, 0));
    if (!AcuCodeGen_IsLocalReg(cg, left_r))
        AcuCodeGen_FreeReg(cg, left_r);

    AcuTarget right_t = AcuCodeGen_EmitExpr(cg, right_idx, AcuTarget_Reg(dst));
    AcuReg right_r = AcuTarget_ToRealReg(right_t);
    if (right_r != dst) {
        AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, right_r));
        if (!AcuCodeGen_IsLocalReg(cg, right_r))
            AcuCodeGen_FreeReg(cg, right_r);
    }

    u32 jump_end_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, 0));

    u32 true_ip = (u32)V_Count(&cg->chunk->code);
    i32 offset_true = (i32)true_ip - (i32)jump_true_ip - 1;
    assert(offset_true >= -32768 && offset_true <= 32767 && "Jump offset overflow!");
    V_At(&cg->chunk->code, jump_true_ip) =
        AcuInst_Encode_AsBx(OP_JUMP_IF_TRUE, left_r, (i16)offset_true);

    AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 1));

    u32 end_ip = (u32)V_Count(&cg->chunk->code);
    i32 offset_end = (i32)end_ip - (i32)jump_end_ip - 1;
    assert(offset_end >= -8388608 && offset_end <= 8388607 && "Jump offset overflow!");
    V_At(&cg->chunk->code, jump_end_ip) = AcuInst_Encode_sAx(OP_JUMP, offset_end);

    cg->is_tail_pos = outer_tail;
    AcuRegSet_MarkUsed(&cg->reg_set, dst);
    return AcuTarget_Reg(dst);
}

static AcuTarget AcuCodeGen_EmitTuple(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    (void)cg;
    (void)node;
    (void)target;

    acu_unreachable("Tuples are not implemented yet");
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitBinary(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    AstBinaryKind op_kind = node->as.binary.type;
    AstNodeIdx left_idx = node->as.binary.left;
    AstNodeIdx right_idx = node->as.binary.right;

    if (op_kind == AST_BINARY_LOGICAL_AND) {
        return AcuCodeGen_EmitLogicalAnd(cg, left_idx, right_idx, target);
    }
    if (op_kind == AST_BINARY_LOGICAL_OR) {
        return AcuCodeGen_EmitLogicalOr(cg, left_idx, right_idx, target);
    }

    bool outer_tail = cg->is_tail_pos;
    cg->is_tail_pos = false;

    if (AcuTarget_IsNone(target)) {
        if (!AcuCodeGen_IsPure(cg->ws, left_idx)) {
            AcuCodeGen_EmitExpr(cg, left_idx, ACU_TARGET_NONE);
        }
        if (!AcuCodeGen_IsPure(cg->ws, right_idx)) {
            AcuCodeGen_EmitExpr(cg, right_idx, ACU_TARGET_NONE);
        }
        return ACU_TARGET_NONE;
    }

    AcuCodeGen_CanonicalizeBinary(cg, &op_kind, &left_idx, &right_idx);

    AcuTarget reduced_target = ACU_TARGET_NONE;
    if (AcuCodeGen_TryStrengthReduction(cg, op_kind, left_idx, right_idx, target,
                                        &reduced_target)) {
        return reduced_target;
    }

    TypeId operand_type = V_At(&cg->ws->node_types, left_idx);
    AcuOpcode imm_op = OP_NOP;
    i8 imm_val = 0;
    bool is_right_imm =
        AcuCodeGen_TryGetBinaryImm(cg, op_kind, right_idx, operand_type, &imm_op, &imm_val);

    AcuTarget left_target = ACU_TARGET_AUTO;
    if (is_right_imm && !AcuTarget_IsAuto(target)) {
        AstNode *left_node = AcuAstBuilder_GetNode(cg->ws->builder, left_idx);
        if (left_node->type != AST_IDENTIFIER) {
            left_target = target;
        }
    }

    AcuTarget left_res = AcuCodeGen_EmitExpr(cg, left_idx, left_target);
    if (AcuCodeGen_IsNever(cg, left_idx)) {
        return left_res;
    }
    AcuReg left_reg = AcuTarget_ToRealReg(left_res);

    if (is_right_imm) {
        return AcuCodeGen_EmitBinaryInst(cg, imm_op, left_reg, 0, true, imm_val, target);
    }

    AcuTarget right_res = AcuCodeGen_EmitExpr(cg, right_idx, ACU_TARGET_AUTO);
    if (AcuCodeGen_IsNever(cg, right_idx)) {
        if (!AcuCodeGen_IsLocalReg(cg, left_reg)) {
            AcuCodeGen_FreeReg(cg, left_reg);
        }
        return right_res;
    }
    AcuReg right_reg = AcuTarget_ToRealReg(right_res);

    AcuOpcode reg_op = AcuCodeGen_GetBinaryOpcode(op_kind, operand_type, /*is_imm=*/false);

    cg->is_tail_pos = outer_tail;
    return AcuCodeGen_EmitBinaryInst(cg, reg_op, left_reg, right_reg, false, 0, target);
}

static AcuTarget AcuCodeGen_EmitUnary(AcuCodeGen *cg, AstNodeIdx expr_idx, const AstNode *node,
                                      AcuTarget target) {
    AstNodeIdx right_idx = node->as.unary.right;
    AstUnaryKind unary_kind = node->as.unary.type;

    if (unary_kind == AST_UNARY_PLUS) {
        return AcuCodeGen_EmitExpr(cg, right_idx, target);
    }

    TypeId node_type = V_At(&cg->ws->node_types, expr_idx);

    AcuOpcode op = AcuCodeGen_GetUnaryOpcode(unary_kind, node_type);
    assert(op != OP_NOP && "Unhandled or invalid unary operator!");

    return AcuCodeGen_EmitUnaryOp(cg, right_idx, op, target);
}

static AcuTarget AcuCodeGen_EmitVarDecl(AcuCodeGen *cg, AstNodeIdx expr_idx, const AstNode *node,
                                        AcuTarget target) {
    SymbolId sym_id = V_At(&cg->ws->node_analysis, expr_idx).var_decl.sym_id;
    if (sym_id == ACU_NULL_IDX) {
        return ACU_TARGET_NONE;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(cg->ws->symbols, sym_id);
    AstNodeIdx init_idx = node->as.var_decl.init_expr;

    if (sym->kind == ACU_SYMBOL_LOCAL_VAR) {
        AcuReg var_reg = AcuCodeGen_AllocReg(cg);
        AcuLocalBinding binding = {.sym_id = sym_id, .reg = var_reg};
        V_Push(&cg->locals, binding);

        AcuTarget init_res = AcuCodeGen_EmitExpr(cg, init_idx, AcuTarget_Reg(var_reg));
        if (AcuCodeGen_IsNever(cg, init_idx)) {
            return init_res;
        }

        AcuReg res_reg = AcuTarget_ToRealReg(init_res);
        if (res_reg != var_reg) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, var_reg, res_reg));
            if (!AcuCodeGen_IsLocalReg(cg, res_reg)) {
                AcuCodeGen_FreeReg(cg, res_reg);
            }
        }

        if (AcuTarget_IsNone(target)) {
            return ACU_TARGET_NONE;
        }

        if (AcuTarget_IsAuto(target) ||
            (AcuTarget_IsRealReg(target) && AcuTarget_ToRealReg(target) == var_reg)) {
            return AcuTarget_Reg(var_reg);
        }

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, var_reg));
        return AcuTarget_Reg(dst);
    }

    if (sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
        u16 slot = (u16)AcuCodeGen_GetGlobalSlot(cg, sym_id);
        AcuReg val_reg;

        if (init_idx != ACU_NULL_IDX) {
            AcuTarget init_res = AcuCodeGen_EmitExpr(cg, init_idx, ACU_TARGET_AUTO);
            if (AcuCodeGen_IsNever(cg, init_idx)) {
                return init_res;
            }
            val_reg = AcuTarget_ToRealReg(init_res);
        } else {
            val_reg = AcuCodeGen_AllocReg(cg);
            AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, val_reg, 0));
        }

        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_STORE_GLOBAL, val_reg, slot));
        cg->last_stored_global = sym_id;
        cg->last_stored_reg = val_reg;

        AcuTarget final_target = ACU_TARGET_NONE;
        if (!AcuTarget_IsNone(target)) {
            AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
            if (dst != val_reg) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, val_reg));
            }
            final_target = AcuTarget_Reg(dst);
        }

        if (!AcuCodeGen_IsLocalReg(cg, val_reg) &&
            (!AcuTarget_IsRealReg(final_target) || AcuTarget_ToRealReg(final_target) != val_reg)) {
            AcuCodeGen_FreeReg(cg, val_reg);
        }

        return final_target;
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitAssign(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    AstNodeIdx target_idx = node->as.assign.target;
    AstNodeIdx value_idx = node->as.assign.value;
    AstAssignKind op = node->as.assign.type;

    SymbolId sym_id = V_At(&cg->ws->node_analysis, target_idx).reference.resolved_sym;
    if (sym_id == ACU_NULL_IDX) {
        return ACU_TARGET_NONE;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(cg->ws->symbols, sym_id);

    if (sym->kind == ACU_SYMBOL_LOCAL_VAR) {
        AcuLocalBinding *local = AcuCodeGen_FindLocal(cg, sym_id);
        if (!local) {
            return ACU_TARGET_NONE;
        }

        AcuReg var_reg = local->reg;

        if (op == AST_ASSIGN_EQ) {
            AcuTarget val_target = AcuCodeGen_EmitExpr(cg, value_idx, AcuTarget_Reg(var_reg));
            if (AcuCodeGen_IsNever(cg, value_idx)) {
                return val_target;
            }

            AcuReg val_reg = AcuTarget_ToRealReg(val_target);
            if (val_reg != var_reg) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, var_reg, val_reg));
                if (!AcuCodeGen_IsLocalReg(cg, val_reg)) {
                    AcuCodeGen_FreeReg(cg, val_reg);
                }
            }
        } else {
            AssignOpcodePair pair = AcuCodeGen_GetAssignOpcodes(cg, op, target_idx);
            assert(pair.reg_op != OP_NOP && "Invalid compound assignment operator!");

            i8 imm8 = 0;
            if (pair.imm_op != OP_NOP && AcuCodeGen_TryGetIntImm8(cg, value_idx, &imm8)) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_ABsC(pair.imm_op, var_reg, var_reg, (u8)imm8));
            } else {
                AcuTarget rhs_target = AcuCodeGen_EmitExpr(cg, value_idx, ACU_TARGET_AUTO);
                if (AcuCodeGen_IsNever(cg, value_idx)) {
                    return rhs_target;
                }
                AcuReg rhs_reg = AcuTarget_ToRealReg(rhs_target);

                AcuCodeGen_Emit(cg, AcuInst_Encode_ABC(pair.reg_op, var_reg, var_reg, rhs_reg));
                if (!AcuCodeGen_IsLocalReg(cg, rhs_reg)) {
                    AcuCodeGen_FreeReg(cg, rhs_reg);
                }
            }
        }

        if (AcuTarget_IsNone(target)) {
            return ACU_TARGET_NONE;
        }

        if (AcuTarget_IsAuto(target) ||
            (AcuTarget_IsRealReg(target) && AcuTarget_ToRealReg(target) == var_reg)) {
            return AcuTarget_Reg(var_reg);
        }

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, var_reg));
        return AcuTarget_Reg(dst);
    }

    if (sym->kind == ACU_SYMBOL_GLOBAL_VAR) {
        u16 slot = (u16)AcuCodeGen_GetGlobalSlot(cg, sym_id);
        AcuReg work_reg;
        bool work_reg_is_temp = false;

        if (op == AST_ASSIGN_EQ) {
            AcuTarget val_target = AcuCodeGen_EmitExpr(cg, value_idx, ACU_TARGET_AUTO);
            if (AcuCodeGen_IsNever(cg, value_idx)) {
                return val_target;
            }
            work_reg = AcuTarget_ToRealReg(val_target);
            work_reg_is_temp = !AcuCodeGen_IsLocalReg(cg, work_reg);
        } else {
            work_reg = AcuCodeGen_AllocReg(cg);
            work_reg_is_temp = true;
            AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_GLOBAL, work_reg, slot));

            AssignOpcodePair pair = AcuCodeGen_GetAssignOpcodes(cg, op, target_idx);
            assert(pair.reg_op != OP_NOP && "Invalid compound assignment operator!");

            i8 imm8 = 0;
            if (pair.imm_op != OP_NOP && AcuCodeGen_TryGetIntImm8(cg, value_idx, &imm8)) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_ABsC(pair.imm_op, work_reg, work_reg, (u8)imm8));
            } else {
                AcuTarget rhs_target = AcuCodeGen_EmitExpr(cg, value_idx, ACU_TARGET_AUTO);
                if (AcuCodeGen_IsNever(cg, value_idx)) {
                    AcuCodeGen_FreeReg(cg, work_reg);
                    return rhs_target;
                }
                AcuReg rhs_reg = AcuTarget_ToRealReg(rhs_target);

                AcuCodeGen_Emit(cg, AcuInst_Encode_ABC(pair.reg_op, work_reg, work_reg, rhs_reg));
                if (!AcuCodeGen_IsLocalReg(cg, rhs_reg)) {
                    AcuCodeGen_FreeReg(cg, rhs_reg);
                }
            }
        }

        AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_STORE_GLOBAL, work_reg, slot));
        cg->last_stored_global = sym_id;
        cg->last_stored_reg = work_reg;

        if (AcuTarget_IsNone(target)) {
            if (work_reg_is_temp) {
                AcuCodeGen_FreeReg(cg, work_reg);
            }
            return ACU_TARGET_NONE;
        }

        if (AcuTarget_IsAuto(target)) {
            return AcuTarget_Reg(work_reg);
        }

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        if (dst != work_reg) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, work_reg));
            if (work_reg_is_temp) {
                AcuCodeGen_FreeReg(cg, work_reg);
            }
        }
        return AcuTarget_Reg(dst);
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitBlock(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    ExtraIdx start = node->as.block.start_idx;
    u32 count = node->as.block.statements_count;
    u32 locals_watermark = (u32)V_Count(&cg->locals);
    AcuRegSet saved_regs = cg->reg_set;
    bool outer_tail = cg->is_tail_pos;

    if (count == 0) {
        if (AcuTarget_IsNone(target)) {
            return ACU_TARGET_NONE;
        }
        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0));
        return AcuTarget_Reg(dst);
    }

    AcuTarget last_res = ACU_TARGET_NONE;

    for (u32 i = 0; i < count; ++i) {
        AstNodeIdx stmt_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, start + i);
        bool is_last = (i == count - 1);
        AcuTarget stmt_target = is_last ? target : ACU_TARGET_NONE;

        cg->is_tail_pos = is_last ? outer_tail : false;

        if (!is_last) {
            AstNode *stmt_node = AcuAstBuilder_GetNode(cg->ws->builder, stmt_idx);
            if (stmt_node->type != AST_VAR_DECL && AcuCodeGen_IsPure(cg->ws, stmt_idx)) {
                continue;
            }
        }

        last_res = AcuCodeGen_EmitExpr(cg, stmt_idx, stmt_target);

        if (AcuCodeGen_IsNever(cg, stmt_idx)) {
            break;
        }
    }

    V_SetCount(&cg->locals, locals_watermark);

    cg->reg_set = saved_regs;
    cg->is_tail_pos = outer_tail;

    if (!AcuTarget_IsNone(target) && AcuTarget_IsRealReg(last_res)) {
        AcuReg res_reg = AcuTarget_ToRealReg(last_res);
        AcuRegSet_MarkUsed(&cg->reg_set, res_reg);
        return AcuTarget_Reg(res_reg);
    }

    return last_res;
}

static AcuTarget AcuCodeGen_EmitIf(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    AcuCodeGen_InvalidateGlobalCache(cg);

    AstNodeIdx cond_idx = node->as.if_stmt.condition;
    AstNodeIdx then_idx = node->as.if_stmt.then_body;
    AstNodeIdx else_idx = node->as.if_stmt.else_body;

    AcuConstVal cond_const = AcuCodeGen_EvalConst(cg->ws, cond_idx);
    if (cond_const.kind == CONST_BOOL || cond_const.kind == CONST_INT) {
        bool is_true =
            (cond_const.kind == CONST_BOOL) ? cond_const.as.bool_val : (cond_const.as.i64_val != 0);
        if (is_true) {
            return AcuCodeGen_EmitExpr(cg, then_idx, target);
        }
        if (else_idx != ACU_NULL_IDX) {
            return AcuCodeGen_EmitExpr(cg, else_idx, target);
        }
        if (AcuTarget_IsNone(target)) {
            return ACU_TARGET_NONE;
        }
        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0));
        return AcuTarget_Reg(dst);
    }

    if (AcuTarget_IsNone(target) && !cg->is_tail_pos) {
        bool then_pure = AcuCodeGen_IsPure(cg->ws, then_idx);
        bool else_pure = (else_idx == ACU_NULL_IDX) || AcuCodeGen_IsPure(cg->ws, else_idx);
        if (then_pure && else_pure) {
            if (!AcuCodeGen_IsPure(cg->ws, cond_idx)) {
                AcuCodeGen_EmitExpr(cg, cond_idx, ACU_TARGET_NONE);
            }
            return ACU_TARGET_NONE;
        }
    }

    bool jump_on_true = false;
    AstNode *cond_node = AcuAstBuilder_GetNode(cg->ws->builder, cond_idx);

    if (cond_node->type == AST_UNARY && cond_node->as.unary.type == AST_UNARY_LOGICAL_NOT) {
        if (else_idx != ACU_NULL_IDX) {
            cond_idx = cond_node->as.unary.right;
            AstNodeIdx tmp = then_idx;
            then_idx = else_idx;
            else_idx = tmp;
        } else {
            cond_idx = cond_node->as.unary.right;
            jump_on_true = true;
        }
    }

    bool is_tail = cg->is_tail_pos;
    bool is_tail_return = is_tail && (else_idx != ACU_NULL_IDX);

    cg->is_tail_pos = false;
    AcuTarget cond_target = AcuCodeGen_EmitExpr(cg, cond_idx, ACU_TARGET_AUTO);
    AcuReg cond_reg = AcuTarget_ToRealReg(cond_target);

    AcuOpcode branch_op = jump_on_true ? OP_JUMP_IF_TRUE : OP_JUMP_IF_FALSE;
    u32 cond_jump_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(branch_op, cond_reg, 0));

    if (!AcuCodeGen_IsLocalReg(cg, cond_reg)) {
        AcuCodeGen_FreeReg(cg, cond_reg);
    }

    bool then_is_never = AcuCodeGen_IsNever(cg, then_idx);

    AcuRegSet base_regs = cg->reg_set;

    if (is_tail_return) {
        cg->is_tail_pos = true;
        cg->self_tail_emitted = false;
        AcuTarget then_res = AcuCodeGen_EmitExpr(cg, then_idx, ACU_TARGET_AUTO);

        if (!cg->self_tail_emitted && !then_is_never) {
            if (AcuTarget_IsRealReg(then_res)) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_A(OP_RET, AcuTarget_ToRealReg(then_res)));
            } else {
                AcuCodeGen_Emit(cg, AcuInst_Encode_NONE(OP_RET_VOID));
            }
        }
        cg->self_tail_emitted = false;

        u32 else_ip = (u32)V_Count(&cg->chunk->code);
        i32 offset_else = (i32)else_ip - (i32)cond_jump_ip - 1;
        assert(offset_else >= -32768 && offset_else <= 32767 && "Branch offset overflow!");
        V_At(&cg->chunk->code, cond_jump_ip) =
            AcuInst_Encode_AsBx(branch_op, cond_reg, (i16)offset_else);

        cg->reg_set = base_regs;
        AcuCodeGen_InvalidateGlobalCache(cg);

        cg->is_tail_pos = true;
        cg->self_tail_emitted = false;
        AcuTarget else_res = AcuCodeGen_EmitExpr(cg, else_idx, ACU_TARGET_AUTO);
        bool else_is_never = AcuCodeGen_IsNever(cg, else_idx);

        if (!cg->self_tail_emitted && !else_is_never) {
            if (AcuTarget_IsRealReg(else_res)) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_A(OP_RET, AcuTarget_ToRealReg(else_res)));
            } else {
                AcuCodeGen_Emit(cg, AcuInst_Encode_NONE(OP_RET_VOID));
            }
        }
        cg->self_tail_emitted = false;

        cg->self_tail_emitted = true;
        cg->is_tail_pos = is_tail;
        AcuCodeGen_InvalidateGlobalCache(cg);
        return ACU_TARGET_NONE;
    }

    AcuReg dst = 0;
    AcuTarget branch_target = ACU_TARGET_NONE;

    if (!AcuTarget_IsNone(target)) {
        dst = AcuCodeGen_ResolveTarget(cg, target);
        branch_target = AcuTarget_Reg(dst);
        AcuRegSet_MarkUsed(&base_regs, dst);
    }

    cg->is_tail_pos = false;
    AcuTarget then_res = AcuCodeGen_EmitExpr(cg, then_idx, branch_target);

    if (!AcuTarget_IsNone(branch_target) && !then_is_never && AcuTarget_IsRealReg(then_res)) {
        AcuReg r = AcuTarget_ToRealReg(then_res);
        if (r != dst) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, r));
            if (!AcuCodeGen_IsLocalReg(cg, r))
                AcuCodeGen_FreeReg(cg, r);
        }
    }

    u32 jump_end_ip = ACU_NULL_IDX;
    if (else_idx != ACU_NULL_IDX) {
        if (!then_is_never) {
            jump_end_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, 0));
        }

        u32 else_ip = (u32)V_Count(&cg->chunk->code);
        i32 offset_else = (i32)else_ip - (i32)cond_jump_ip - 1;
        assert(offset_else >= -32768 && offset_else <= 32767 && "Branch offset overflow!");
        V_At(&cg->chunk->code, cond_jump_ip) =
            AcuInst_Encode_AsBx(branch_op, cond_reg, (i16)offset_else);

        cg->reg_set = base_regs;
        AcuCodeGen_InvalidateGlobalCache(cg);

        cg->is_tail_pos = false;
        AcuTarget else_res = AcuCodeGen_EmitExpr(cg, else_idx, branch_target);
        bool else_is_never = AcuCodeGen_IsNever(cg, else_idx);

        if (!AcuTarget_IsNone(branch_target) && !else_is_never && AcuTarget_IsRealReg(else_res)) {
            AcuReg r = AcuTarget_ToRealReg(else_res);
            if (r != dst) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, dst, r));
                if (!AcuCodeGen_IsLocalReg(cg, r))
                    AcuCodeGen_FreeReg(cg, r);
            }
        }

        if (jump_end_ip != ACU_NULL_IDX) {
            u32 end_ip = (u32)V_Count(&cg->chunk->code);
            i32 offset_end = (i32)end_ip - (i32)jump_end_ip - 1;
            if (offset_end == 0) {
                V_At(&cg->chunk->code, jump_end_ip) = AcuInst_Encode_NONE(OP_NOP);
            } else {
                V_At(&cg->chunk->code, jump_end_ip) = AcuInst_Encode_sAx(OP_JUMP, offset_end);
            }
        }
    } else {
        u32 end_ip = (u32)V_Count(&cg->chunk->code);
        i32 offset = (i32)end_ip - (i32)cond_jump_ip - 1;
        assert(offset >= -32768 && offset <= 32767 && "Branch offset overflow!");
        V_At(&cg->chunk->code, cond_jump_ip) =
            AcuInst_Encode_AsBx(branch_op, cond_reg, (i16)offset);
    }

    cg->reg_set = base_regs;
    cg->is_tail_pos = is_tail;
    AcuCodeGen_InvalidateGlobalCache(cg);

    if (!AcuTarget_IsNone(target)) {
        AcuRegSet_MarkUsed(&cg->reg_set, dst);
        return AcuTarget_Reg(dst);
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitBreak(AcuCodeGen *cg, const AstNode *node) {
    assert(V_Count(&cg->loops) > 0 && "AST_BREAK encountered outside of active loop!");
    AcuLoopCtx *active_loop = &V_At(&cg->loops, V_Count(&cg->loops) - 1);

    AstNodeIdx expr_idx = node->as.break_stmt.expr;

    if (expr_idx != ACU_NULL_IDX) {
        if (!AcuTarget_IsNone(active_loop->target_reg)) {
            AcuTarget val_target =
                AcuCodeGen_EmitExpr(cg, expr_idx, AcuTarget_Reg(active_loop->result_reg));
            if (AcuCodeGen_IsNever(cg, expr_idx)) {
                return val_target;
            }

            AcuReg val_reg = AcuTarget_ToRealReg(val_target);
            if (val_reg != active_loop->result_reg) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, active_loop->result_reg, val_reg));
                if (!AcuCodeGen_IsLocalReg(cg, val_reg)) {
                    AcuCodeGen_FreeReg(cg, val_reg);
                }
            }
        } else {
            if (!AcuCodeGen_IsPure(cg->ws, expr_idx)) {
                AcuCodeGen_EmitExpr(cg, expr_idx, ACU_TARGET_NONE);
            }
        }
    } else {
        if (!AcuTarget_IsNone(active_loop->target_reg)) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, active_loop->result_reg, 0));
        }
    }

    u32 jmp_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, 0));
    V_Push(&active_loop->break_jumps, jmp_ip);

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitContinue(AcuCodeGen *cg) {
    assert(V_Count(&cg->loops) > 0 && "AST_CONTINUE encountered outside of active loop!");
    AcuLoopCtx *active_loop = &V_At(&cg->loops, V_Count(&cg->loops) - 1);

    u32 current_ip = (u32)V_Count(&cg->chunk->code);
    i32 loop_offset = (i32)active_loop->start_ip - (i32)current_ip - 1;
    assert(loop_offset >= -8388608 && loop_offset <= 8388607 &&
           "Continue offset exceeds 24-bit sAx limit!");

    AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, loop_offset));
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitLoop(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    AcuCodeGen_InvalidateGlobalCache(cg);

    bool is_tail = cg->is_tail_pos;
    AcuReg dst = 0;
    AcuTarget loop_target = ACU_TARGET_NONE;

    if (!AcuTarget_IsNone(target)) {
        dst = AcuCodeGen_ResolveTarget(cg, target);
        loop_target = AcuTarget_Reg(dst);
        AcuRegSet_MarkUsed(&cg->reg_set, dst);
    }

    AcuRegSet saved_regs = cg->reg_set;

    AcuLoopCtx loop = {
        .start_ip = (u32)V_Count(&cg->chunk->code),
        .target_reg = loop_target,
        .result_reg = dst,
        .is_tail = is_tail,
    };
    V_Init(&loop.break_jumps);
    V_Push(&cg->loops, loop);

    cg->is_tail_pos = false;
    AstNodeIdx body_idx = node->as.loop_stmt.body;
    AcuCodeGen_EmitExpr(cg, body_idx, ACU_TARGET_NONE);

    if (!AcuCodeGen_IsNever(cg, body_idx)) {
        u32 current_ip = (u32)V_Count(&cg->chunk->code);
        i32 loop_offset = (i32)loop.start_ip - (i32)current_ip - 1;
        assert(loop_offset >= -8388608 && loop_offset <= 8388607 &&
               "Loop offset exceeds 24-bit sAx limit!");
        AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, loop_offset));
    }

    u32 end_ip = (u32)V_Count(&cg->chunk->code);
    AcuLoopCtx *active_loop = &V_At(&cg->loops, V_Count(&cg->loops) - 1);
    u32 break_count = (u32)V_Count(&active_loop->break_jumps);

    for (u32 b = 0; b < break_count; ++b) {
        u32 jmp_ip = V_At(&active_loop->break_jumps, b);
        i32 b_offset = (i32)end_ip - (i32)jmp_ip - 1;
        assert(b_offset >= -8388608 && b_offset <= 8388607 &&
               "Break jump offset exceeds 24-bit sAx limit!");
        V_At(&cg->chunk->code, jmp_ip) = AcuInst_Encode_sAx(OP_JUMP, b_offset);
    }

    V_Free(&active_loop->break_jumps);
    V_Pop(&cg->loops);

    cg->reg_set = saved_regs;
    cg->is_tail_pos = is_tail;
    AcuCodeGen_InvalidateGlobalCache(cg);

    if (break_count == 0) {
        return ACU_TARGET_NONE;
    }

    if (is_tail) {
        if (!AcuTarget_IsNone(target)) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_A(OP_RET, dst));
        } else {
            AcuCodeGen_Emit(cg, AcuInst_Encode_NONE(OP_RET_VOID));
        }
        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    if (!AcuTarget_IsNone(target)) {
        AcuRegSet_MarkUsed(&cg->reg_set, dst);
        return AcuTarget_Reg(dst);
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitWhile(AcuCodeGen *cg, const AstNode *node, AcuTarget target) {
    AcuCodeGen_InvalidateGlobalCache(cg);

    AstNodeIdx cond_idx = node->as.while_stmt.condition;
    AstNodeIdx body_idx = node->as.while_stmt.body;

    AcuConstVal cond_const = AcuCodeGen_EvalConst(cg->ws, cond_idx);
    if (cond_const.kind == CONST_BOOL || cond_const.kind == CONST_INT) {
        bool is_true =
            (cond_const.kind == CONST_BOOL) ? cond_const.as.bool_val : (cond_const.as.i64_val != 0);
        if (!is_true) {
            if (!AcuCodeGen_IsPure(cg->ws, cond_idx)) {
                AcuCodeGen_EmitExpr(cg, cond_idx, ACU_TARGET_NONE);
            }
            if (AcuTarget_IsNone(target)) {
                return ACU_TARGET_NONE;
            }
            AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
            AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0));
            return AcuTarget_Reg(dst);
        }
    }

    bool is_always_true = (cond_const.kind == CONST_BOOL && cond_const.as.bool_val == true);

    bool jump_on_true = false;
    AstNode *cond_node = AcuAstBuilder_GetNode(cg->ws->builder, cond_idx);
    if (!is_always_true && cond_node->type == AST_UNARY &&
        cond_node->as.unary.type == AST_UNARY_LOGICAL_NOT) {
        cond_idx = cond_node->as.unary.right;
        jump_on_true = true;
    }

    AcuRegSet saved_regs = cg->reg_set;

    AcuLoopCtx loop = {
        .start_ip = (u32)V_Count(&cg->chunk->code),
        .target_reg = ACU_TARGET_NONE,
        .result_reg = 0,
        .is_tail = false,
    };
    V_Init(&loop.break_jumps);
    V_Push(&cg->loops, loop);

    u32 jump_exit_ip = ACU_NULL_IDX;
    AcuOpcode exit_branch_op = jump_on_true ? OP_JUMP_IF_TRUE : OP_JUMP_IF_FALSE;
    AcuReg cond_reg = 0;

    if (!is_always_true) {
        cg->is_tail_pos = false;
        AcuTarget cond_target = AcuCodeGen_EmitExpr(cg, cond_idx, ACU_TARGET_AUTO);
        cond_reg = AcuTarget_ToRealReg(cond_target);

        jump_exit_ip = AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(exit_branch_op, cond_reg, 0));

        if (!AcuCodeGen_IsLocalReg(cg, cond_reg)) {
            AcuCodeGen_FreeReg(cg, cond_reg);
        }
    }

    cg->is_tail_pos = false;
    AcuCodeGen_EmitExpr(cg, body_idx, ACU_TARGET_NONE);

    if (!AcuCodeGen_IsNever(cg, body_idx)) {
        u32 current_ip = (u32)V_Count(&cg->chunk->code);
        i32 loop_offset = (i32)loop.start_ip - (i32)current_ip - 1;
        assert(loop_offset >= -8388608 && loop_offset <= 8388607 &&
               "While offset exceeds 24-bit sAx limit!");
        AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, loop_offset));
    }

    u32 end_ip = (u32)V_Count(&cg->chunk->code);

    if (jump_exit_ip != ACU_NULL_IDX) {
        i32 exit_offset = (i32)end_ip - (i32)jump_exit_ip - 1;
        assert(exit_offset >= -32768 && exit_offset <= 32767 &&
               "While body exceeds 16-bit jump offset limit for AsBx!");
        V_At(&cg->chunk->code, jump_exit_ip) =
            AcuInst_Encode_AsBx(exit_branch_op, cond_reg, (i16)exit_offset);
    }

    AcuLoopCtx *active_loop = &V_At(&cg->loops, V_Count(&cg->loops) - 1);
    u32 break_count = (u32)V_Count(&active_loop->break_jumps);
    for (u32 b = 0; b < break_count; ++b) {
        u32 jmp_ip = V_At(&active_loop->break_jumps, b);
        i32 b_offset = (i32)end_ip - (i32)jmp_ip - 1;
        assert(b_offset >= -8388608 && b_offset <= 8388607 &&
               "Break offset exceeds 24-bit sAx limit!");
        V_At(&cg->chunk->code, jmp_ip) = AcuInst_Encode_sAx(OP_JUMP, b_offset);
    }

    V_Free(&active_loop->break_jumps);
    V_Pop(&cg->loops);

    cg->reg_set = saved_regs;
    AcuCodeGen_InvalidateGlobalCache(cg);

    if (!AcuTarget_IsNone(target)) {
        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0));
        return AcuTarget_Reg(dst);
    }

    return ACU_TARGET_NONE;
}

static AcuOpcode AcuCodeGen_GetTypeCastOpcode(TypeId src_type, TypeId target_type) {
    if (src_type == target_type) {
        return OP_NOP;
    }

    if (TypePrimitiveKind_IsInteger(src_type) && TypePrimitiveKind_IsFloat(target_type)) {
        return TypePrimitiveKind_IsSignedInt(src_type) ? OP_I64_TO_F64 : OP_U64_TO_F64;
    }

    if (TypePrimitiveKind_IsFloat(src_type) && TypePrimitiveKind_IsInteger(target_type)) {
        return TypePrimitiveKind_IsSignedInt(target_type) ? OP_F64_TO_I64 : OP_F64_TO_U64;
    }

    if (TypePrimitiveKind_IsFloat(src_type) && TypePrimitiveKind_IsFloat(target_type)) {
        if (src_type == (TypeId)TYPE_PRIMITIVE_F32 && target_type == (TypeId)TYPE_PRIMITIVE_F64) {
            return OP_F32_TO_F64;
        }
        if (src_type == (TypeId)TYPE_PRIMITIVE_F64 && target_type == (TypeId)TYPE_PRIMITIVE_F32) {
            return OP_F64_TO_F32;
        }
        return OP_NOP;
    }

    if (TypePrimitiveKind_IsInteger(src_type) && TypePrimitiveKind_IsInteger(target_type)) {
        u32 sbits = TypePrimitiveKind_GetIntBitWidth(src_type);
        u32 tbits = TypePrimitiveKind_GetIntBitWidth(target_type);
        bool s_signed = TypePrimitiveKind_IsSignedInt(src_type);
        bool t_signed = TypePrimitiveKind_IsSignedInt(target_type);

        if (tbits < sbits) {
            if (tbits == 8)
                return OP_TRUNC8;
            if (tbits == 16)
                return OP_TRUNC16;
            if (tbits == 32)
                return OP_TRUNC32;
        }

        if (tbits > sbits) {
            if (sbits == 8)
                return s_signed ? OP_SEXT8 : OP_ZEXT8;
            if (sbits == 16)
                return s_signed ? OP_SEXT16 : OP_ZEXT16;
            if (sbits == 32)
                return s_signed ? OP_SEXT32 : OP_ZEXT32;
        }

        if (sbits < 64 && s_signed != t_signed) {
            if (t_signed) {
                if (tbits == 8)
                    return OP_SEXT8;
                if (tbits == 16)
                    return OP_SEXT16;
                if (tbits == 32)
                    return OP_SEXT32;
            } else {
                if (tbits == 8)
                    return OP_ZEXT8;
                if (tbits == 16)
                    return OP_ZEXT16;
                if (tbits == 32)
                    return OP_ZEXT32;
            }
        }

        return OP_NOP;
    }

    return OP_NOP;
}

static AcuTarget AcuCodeGen_EmitTypeCast(AcuCodeGen *cg, AstNodeIdx expr_idx, const AstNode *node,
                                         AcuTarget target) {
    AstNodeIdx inner_idx = node->as.type_cast.expr;

    if (AcuTarget_IsNone(target)) {
        if (!AcuCodeGen_IsPure(cg->ws, inner_idx)) {
            AcuCodeGen_EmitExpr(cg, inner_idx, ACU_TARGET_NONE);
        }
        return ACU_TARGET_NONE;
    }

    TypeId target_type = V_At(&cg->ws->node_types, expr_idx);
    TypeId src_type = V_At(&cg->ws->node_types, inner_idx);

    AcuOpcode op = AcuCodeGen_GetTypeCastOpcode(src_type, target_type);

    if (op == OP_NOP) {
        return AcuCodeGen_EmitExpr(cg, inner_idx, target);
    }

    AcuTarget src_target = AcuCodeGen_EmitExpr(cg, inner_idx, ACU_TARGET_AUTO);
    if (AcuCodeGen_IsNever(cg, inner_idx)) {
        return src_target;
    }
    AcuReg src = AcuTarget_ToRealReg(src_target);

    AcuReg dst;
    if (AcuTarget_IsAuto(target) && !AcuCodeGen_IsLocalReg(cg, src)) {
        dst = src;
    } else {
        dst = AcuCodeGen_ResolveTarget(cg, target);
    }

    AcuCodeGen_Emit(cg, AcuInst_Encode_AB(op, dst, src));

    if (dst != src && !AcuCodeGen_IsLocalReg(cg, src)) {
        AcuCodeGen_FreeReg(cg, src);
    }

    return AcuTarget_Reg(dst);
}

static AcuTarget AcuCodeGen_EmitSelfTailCall(AcuCodeGen *cg, ExtraIdx arg_start, u32 arg_count) {
    if (arg_count == 0) {
        i32 diff = (i32)cg->current_fn_entry_ip - (i32)V_Count(&cg->chunk->code) - 1;
        assert(diff >= -8388608 && diff <= 8388607 && "Self-tail jump offset overflow!");
        AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, diff));
        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    if (arg_count == 1) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start);

        cg->is_tail_pos = false;
        AcuTarget a0_t = AcuCodeGen_EmitExpr(cg, arg_idx, ACU_TARGET_AUTO);
        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            return a0_t;
        }
        AcuReg a0 = AcuTarget_ToRealReg(a0_t);

        if (a0 != 0) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, 0, a0));
        }
        if (!AcuCodeGen_IsLocalReg(cg, a0)) {
            AcuCodeGen_FreeReg(cg, a0);
        }

        i32 diff = (i32)cg->current_fn_entry_ip - (i32)V_Count(&cg->chunk->code) - 1;
        assert(diff >= -8388608 && diff <= 8388607 && "Self-tail jump offset overflow!");
        AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, diff));
        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    AcuRegSet saved_regs = cg->reg_set;
    AcuReg first_arg = AcuCodeGen_EmitContiguousArgs(cg, arg_start, arg_count);

    for (u32 i = 0; i < arg_count; ++i) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start + i);
        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            cg->reg_set = saved_regs;
            return ACU_TARGET_NONE;
        }
    }

    for (u32 i = 0; i < arg_count; ++i) {
        AcuReg src = (AcuReg)(first_arg + i);
        AcuReg param_reg = (AcuReg)i;
        if (src != param_reg) {
            AcuCodeGen_Emit(cg, AcuInst_Encode_AB(OP_MOVE, param_reg, src));
        }
    }

    cg->reg_set = saved_regs;

    i32 diff = (i32)cg->current_fn_entry_ip - (i32)V_Count(&cg->chunk->code) - 1;
    assert(diff >= -8388608 && diff <= 8388607 && "Self-tail jump offset overflow!");
    AcuCodeGen_Emit(cg, AcuInst_Encode_sAx(OP_JUMP, diff));
    cg->self_tail_emitted = true;
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitTailCall(AcuCodeGen *cg, SymbolId callee_sym_id, ExtraIdx arg_start,
                                         u32 arg_count) {
    if (arg_count == 0) {
        u32 ip = AcuCodeGen_EmitWide(cg, AcuInst_Encode_ABC(OP_TAILCALL, 0, 0, 0), 0);

        AcuFuncCallPatch patch = (AcuFuncCallPatch){.inst_idx = ip, .fn_sym_id = callee_sym_id};
        V_Push(&cg->fn_patches, patch);

        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    if (arg_count == 1) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start);
        AcuTarget a0_t = AcuCodeGen_EmitExpr(cg, arg_idx, ACU_TARGET_AUTO);
        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            return a0_t;
        }
        AcuReg a0 = AcuTarget_ToRealReg(a0_t);

        u32 ip = AcuCodeGen_EmitWide(cg, AcuInst_Encode_ABC(OP_TAILCALL, 0, a0, 1), 0);

        AcuFuncCallPatch patch = (AcuFuncCallPatch){.inst_idx = ip, .fn_sym_id = callee_sym_id};
        V_Push(&cg->fn_patches, patch);

        if (!AcuCodeGen_IsLocalReg(cg, a0)) {
            AcuCodeGen_FreeReg(cg, a0);
        }
        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    AcuRegSet saved_regs = cg->reg_set;
    AcuReg first_arg = AcuCodeGen_EmitContiguousArgs(cg, arg_start, arg_count);

    for (u32 i = 0; i < arg_count; ++i) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start + i);
        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            cg->reg_set = saved_regs;
            return ACU_TARGET_NONE;
        }
    }

    u32 ip =
        AcuCodeGen_EmitWide(cg, AcuInst_Encode_ABC(OP_TAILCALL, 0, first_arg, (u8)arg_count), 0);

    AcuFuncCallPatch patch = (AcuFuncCallPatch){.inst_idx = ip, .fn_sym_id = callee_sym_id};
    V_Push(&cg->fn_patches, patch);

    cg->reg_set = saved_regs;
    cg->self_tail_emitted = true;
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitStandardCall(AcuCodeGen *cg, SymbolId callee_sym_id,
                                             ExtraIdx arg_start, u32 arg_count, AcuTarget target) {
    assert(arg_count <= 255 && "Call exceeds maximum argument count (255)!");

    bool is_void = AcuTarget_IsNone(target);

    AcuRegSet saved_regs = cg->reg_set;

    AcuReg dst = 0;
    if (!is_void) {
        dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuRegSet_MarkUsed(&cg->reg_set, dst);
    }

    AcuReg first_arg = AcuCodeGen_EmitContiguousArgs(cg, arg_start, arg_count);

    for (u32 i = 0; i < arg_count; ++i) {
        AstNodeIdx arg_idx = AcuAstBuilder_GetIdxByExtra(cg->ws->builder, arg_start + i);
        if (AcuCodeGen_IsNever(cg, arg_idx)) {
            cg->reg_set = saved_regs;
            return ACU_TARGET_NONE;
        }
    }

    AcuOpcode op = is_void ? OP_CALL_VOID : OP_CALL;
    u32 ip = AcuCodeGen_EmitWide(cg, AcuInst_Encode_ABC(op, dst, first_arg, (u8)arg_count), 0);

    AcuFuncCallPatch patch = (AcuFuncCallPatch){.inst_idx = ip, .fn_sym_id = callee_sym_id};
    V_Push(&cg->fn_patches, patch);

    cg->reg_set = saved_regs;
    if (is_void) {
        return ACU_TARGET_NONE;
    }

    AcuRegSet_MarkUsed(&cg->reg_set, dst);
    return AcuTarget_Reg(dst);
}

static AcuTarget AcuCodeGen_EmitCall(AcuCodeGen *cg, AstNodeIdx expr_idx, const AstNode *node,
                                     AcuTarget target) {
    AcuConstVal const_val = AcuCodeGen_EvalConst(cg->ws, expr_idx);
    if (const_val.kind != CONST_NONE) {
        if (AcuTarget_IsNone(target)) {
            return ACU_TARGET_NONE;
        }

        AcuReg dst = AcuCodeGen_ResolveTarget(cg, target);
        AcuRegSet_MarkUsed(&cg->reg_set, dst);

        if (const_val.kind == CONST_FLOAT) {
            u32 const_idx = AcuConstantPool_InternF64(&cg->chunk->constants, const_val.as.f64_val);
            AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)const_idx));
        } else if (const_val.kind == CONST_INT) {
            i64 val = const_val.as.i64_val;
            if (val >= -32768 && val <= 32767) {
                AcuCodeGen_Emit(cg, AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, (i16)val));
            } else {
                u32 const_idx = AcuConstantPool_InternI64(&cg->chunk->constants, val);
                AcuCodeGen_Emit(cg, AcuInst_Encode_ABx(OP_LOAD_CONST, dst, (u16)const_idx));
            }
        } else if (const_val.kind == CONST_BOOL) {
            AcuCodeGen_Emit(cg,
                            AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, const_val.as.bool_val ? 1 : 0));
        }

        return AcuTarget_Reg(dst);
    }

    if (AcuTarget_IsNone(target) && AcuCodeGen_IsPure(cg->ws, expr_idx)) {
        return ACU_TARGET_NONE;
    }

    SymbolId callee_sym_id = V_At(&cg->ws->node_analysis, expr_idx).call.callee_sym;
    if (callee_sym_id == ACU_NULL_IDX) {
        AcuCodeGen_InvalidateGlobalCache(cg);
        return ACU_TARGET_NONE;
    }

    AcuSymbol *sym = AcuSymbolTable_Get(cg->ws->symbols, callee_sym_id);
    bool is_pure = (sym->flags & ACU_SYMBOL_FLAG_PURE) != 0;

    if (!is_pure) {
        AcuCodeGen_InvalidateGlobalCache(cg);
    }

    u32 arg_count = node->as.call.arguments_count;
    ExtraIdx arg_start = node->as.call.start_idx;

    if (sym->kind == ACU_SYMBOL_SYSTEM_FUNCTION) {
        bool is_void = AcuTarget_IsNone(target);
        AcuReg dst = 0;
        if (!is_void) {
            dst = AcuCodeGen_ResolveTarget(cg, target);
            AcuRegSet_MarkUsed(&cg->reg_set, dst);
        }

        AcuRegSet saved_regs = cg->reg_set;
        AcuReg first_arg =
            (arg_count > 0) ? AcuCodeGen_EmitContiguousArgs(cg, arg_start, arg_count) : 0;
        AcuOpcode op = is_void ? OP_SYSCALL_VOID : OP_SYSCALL;

        AcuCodeGen_EmitWide(cg, AcuInst_Encode_ABC_WIDE(op, dst, first_arg, (u8)arg_count),
                            sym->as.syscall_tag);

        cg->reg_set = saved_regs;

        if (!is_pure) {
            AcuCodeGen_InvalidateGlobalCache(cg);
        }

        if (is_void) {
            return ACU_TARGET_NONE;
        }
        AcuRegSet_MarkUsed(&cg->reg_set, dst);
        return AcuTarget_Reg(dst);
    }

    if (sym->kind == ACU_SYMBOL_FUNCTION) {
        AcuTarget call_res;

        if (cg->is_tail_pos) {
            if (callee_sym_id == cg->current_fn_sym) {
                call_res = AcuCodeGen_EmitSelfTailCall(cg, arg_start, arg_count);
            } else {
                call_res = AcuCodeGen_EmitTailCall(cg, callee_sym_id, arg_start, arg_count);
            }
        } else {
            call_res = AcuCodeGen_EmitStandardCall(cg, callee_sym_id, arg_start, arg_count, target);
        }

        if (!is_pure) {
            AcuCodeGen_InvalidateGlobalCache(cg);
        }

        return call_res;
    }

    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitReturn(AcuCodeGen *cg, const AstNode *node) {
    AstNodeIdx ret_expr_idx = node->as.ret_stmt.expr;

    if (ret_expr_idx == ACU_NULL_IDX) {
        AcuCodeGen_Emit(cg, AcuInst_Encode_NONE(OP_RET_VOID));
        cg->self_tail_emitted = true;
        return ACU_TARGET_NONE;
    }

    cg->is_tail_pos = true;
    cg->self_tail_emitted = false;

    AcuTarget res = AcuCodeGen_EmitExpr(cg, ret_expr_idx, ACU_TARGET_AUTO);

    if (cg->self_tail_emitted || AcuCodeGen_IsNever(cg, ret_expr_idx)) {
        return ACU_TARGET_NONE;
    }

    if (AcuTarget_IsRealReg(res)) {
        AcuReg r = AcuTarget_ToRealReg(res);
        AcuCodeGen_Emit(cg, AcuInst_Encode_A(OP_RET, r));
        if (!AcuCodeGen_IsLocalReg(cg, r)) {
            AcuCodeGen_FreeReg(cg, r);
        }
    } else {
        AcuCodeGen_Emit(cg, AcuInst_Encode_NONE(OP_RET_VOID));
    }

    cg->self_tail_emitted = true;
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitDiscard(AcuCodeGen *cg, AstNode *node) {
    AstNodeIdx inner_idx = node->as.discard.expr;
    if (!AcuCodeGen_IsPure(cg->ws, inner_idx)) {
        AcuCodeGen_EmitExpr(cg, inner_idx, ACU_TARGET_NONE);
    }
    return ACU_TARGET_NONE;
}

static AcuTarget AcuCodeGen_EmitIdentifierAndDot(AcuCodeGen *cg, AstNodeIdx expr_idx,
                                                 AcuTarget target) {
    SymbolId sym_id = V_At(&cg->ws->node_analysis, expr_idx).reference.resolved_sym;
    if (sym_id == ACU_NULL_IDX) {
        return ACU_TARGET_NONE;
    }

    TypeId node_type = V_At(&cg->ws->node_types, expr_idx);
    return AcuCodeGen_EmitSymbolRead(cg, sym_id, node_type, target);
}

AcuTarget AcuCodeGen_EmitExpr(AcuCodeGen *cg, AstNodeIdx expr_idx, AcuTarget target) {
    if (unlikely(expr_idx == ACU_NULL_IDX)) {
        return ACU_TARGET_NONE;
    }

    AstNode *node = AcuAstBuilder_GetNode(cg->ws->builder, expr_idx);
    TypeId node_type = V_At(&cg->ws->node_types, expr_idx);

    if (node->type == AST_BINARY || node->type == AST_UNARY || node->type == AST_TYPE_CAST ||
        node->type == AST_CALL) {
        AcuConstVal folded = AcuCodeGen_EvalConst(cg->ws, expr_idx);
        if (folded.kind != CONST_NONE) {
            return AcuCodeGen_EmitFoldedConstant(cg, folded, node_type, target);
        }
    }

    switch (node->type) {
        case AST_LITERAL:
            return AcuCodeGen_EmitLiteral(cg, node->as.literal.idx, node_type, target);

        case AST_IDENTIFIER:
        case AST_DOT:
            return AcuCodeGen_EmitIdentifierAndDot(cg, expr_idx, target);

        case AST_TUPLE:
            return AcuCodeGen_EmitTuple(cg, node, target);

        case AST_BINARY:
            return AcuCodeGen_EmitBinary(cg, node, target);

        case AST_UNARY:
            return AcuCodeGen_EmitUnary(cg, expr_idx, node, target);

        case AST_VAR_DECL:
            return AcuCodeGen_EmitVarDecl(cg, expr_idx, node, target);

        case AST_ASSIGN:
            return AcuCodeGen_EmitAssign(cg, node, target);

        case AST_BLOCK:
            return AcuCodeGen_EmitBlock(cg, node, target);

        case AST_IF:
            return AcuCodeGen_EmitIf(cg, node, target);

        case AST_LOOP:
            return AcuCodeGen_EmitLoop(cg, node, target);

        case AST_WHILE:
            return AcuCodeGen_EmitWhile(cg, node, target);

        case AST_CONTINUE:
            return AcuCodeGen_EmitContinue(cg);

        case AST_BREAK:
            return AcuCodeGen_EmitBreak(cg, node);

        case AST_CALL:
            return AcuCodeGen_EmitCall(cg, expr_idx, node, target);

        case AST_RETURN:
            return AcuCodeGen_EmitReturn(cg, node);

        case AST_TYPE_CAST:
            return AcuCodeGen_EmitTypeCast(cg, expr_idx, node, target);

        case AST_DISCARD:
            return AcuCodeGen_EmitDiscard(cg, node);

        default:
            acu_panic("Unhandled AST node type in AcuCodeGen_EmitExpr: %d", node->type);
            return ACU_TARGET_NONE;
    }
}
