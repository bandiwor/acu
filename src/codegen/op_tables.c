#include "codegen/op_tables.h"
#include "defines/defines.h"
#include "defines/types.h"
#include <stddef.h>

bool AstBinary_IsCommutative(AstBinaryKind kind) {
    switch (kind) {
        case AST_BINARY_ADD:
        case AST_BINARY_MUL:
        case AST_BINARY_BITWISE_AND:
        case AST_BINARY_BITWISE_OR:
        case AST_BINARY_BITWISE_XOR:
        case AST_BINARY_EQ:
        case AST_BINARY_NEQ:
            return true;
        default:
            return false;
    }
}

bool AstBinary_IsComparison(AstBinaryKind kind) {
    switch (kind) {
        case AST_BINARY_LT:
        case AST_BINARY_LTE:
        case AST_BINARY_GT:
        case AST_BINARY_GTE:
            return true;
        default:
            return false;
    }
}

AstBinaryKind AstBinary_InvertComparison(AstBinaryKind kind) {
    switch (kind) {
        case AST_BINARY_LT:
            return AST_BINARY_GT;
        case AST_BINARY_LTE:
            return AST_BINARY_GTE;
        case AST_BINARY_GT:
            return AST_BINARY_LT;
        case AST_BINARY_GTE:
            return AST_BINARY_LTE;
        default:
            return kind;
    }
}

static const AcuBinaryOpDesc kBinaryOpTable[] = {
    [AST_BINARY_ADD] = {OP_FADD, OP_ADD, OP_ADD, OP_ADD_IMM, OP_ADD_IMM},
    [AST_BINARY_SUB] = {OP_FSUB, OP_SUB, OP_SUB, OP_SUB_IMM, OP_SUB_IMM},
    [AST_BINARY_MUL] = {OP_FMUL, OP_MUL, OP_MUL, OP_MUL_IMM, OP_MUL_IMM},
    [AST_BINARY_DIV] = {OP_FDIV, OP_DIV_S, OP_DIV_U, OP_DIV_S_IMM, OP_DIV_U_IMM},
    [AST_BINARY_REM] = {OP_FREM, OP_REM_S, OP_REM_U, OP_REM_S_IMM, OP_REM_U_IMM},
    [AST_BINARY_POW] = {OP_FPOW, OP_POW, OP_POW, OP_POW_IMM, OP_POW_IMM},
    [AST_BINARY_BITWISE_AND] = {OP_NOP, OP_AND, OP_AND, OP_AND_IMM, OP_AND_IMM},
    [AST_BINARY_BITWISE_OR] = {OP_NOP, OP_OR, OP_OR, OP_OR_IMM, OP_OR_IMM},
    [AST_BINARY_BITWISE_XOR] = {OP_NOP, OP_XOR, OP_XOR, OP_XOR_IMM, OP_XOR_IMM},
    [AST_BINARY_SHL] = {OP_NOP, OP_SHL, OP_SHL, OP_SHL_IMM, OP_SHL_IMM},
    [AST_BINARY_SHR] = {OP_NOP, OP_SHR_S, OP_SHR_U, OP_SHR_S_IMM, OP_SHR_U_IMM},
    [AST_BINARY_EQ] = {OP_FEQ, OP_EQ, OP_EQ, OP_EQ_IMM, OP_EQ_IMM},
    [AST_BINARY_NEQ] = {OP_FNEQ, OP_NEQ, OP_NEQ, OP_NEQ_IMM, OP_NEQ_IMM},
    [AST_BINARY_LT] = {OP_FLT, OP_LT_S, OP_LT_U, OP_LT_S_IMM, OP_LT_U_IMM},
    [AST_BINARY_LTE] = {OP_FLTE, OP_LTE_S, OP_LTE_U, OP_LTE_S_IMM, OP_LTE_U_IMM},
    [AST_BINARY_GT] = {OP_FGT, OP_GT_S, OP_GT_U, OP_GT_S_IMM, OP_GT_U_IMM},
    [AST_BINARY_GTE] = {OP_FGTE, OP_GTE_S, OP_GTE_U, OP_GTE_S_IMM, OP_GTE_U_IMM},
};

AcuOpcode AcuCodeGen_GetBinaryOpcode(AstBinaryKind kind, TypeId operand_type, bool is_imm) {
    if ((size_t)kind >= (sizeof(kBinaryOpTable) / sizeof(kBinaryOpTable[0]))) {
        return OP_NOP;
    }
    const AcuBinaryOpDesc *desc = &kBinaryOpTable[kind];
    if (TypePrimitiveKind_IsFloat(operand_type)) {
        return is_imm ? OP_NOP : desc->float_op;
    }
    bool is_signed = TypePrimitiveKind_IsSignedInt(operand_type);
    if (is_imm) {
        return is_signed ? desc->imm_signed_op : desc->imm_unsigned_op;
    }
    return is_signed ? desc->int_signed_op : desc->int_unsigned_op;
}
