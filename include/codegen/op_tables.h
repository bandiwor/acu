#pragma once
#include "ast/ast.h"
#include "defines/bytecode.h"
#include <stdbool.h>

typedef struct {
    AcuOpcode float_op;
    AcuOpcode int_signed_op;
    AcuOpcode int_unsigned_op;
    AcuOpcode imm_signed_op;
    AcuOpcode imm_unsigned_op;
} AcuBinaryOpDesc;

bool AstBinary_IsCommutative(AstBinaryKind kind);
bool AstBinary_IsComparison(AstBinaryKind kind);
AstBinaryKind AstBinary_InvertComparison(AstBinaryKind kind);

AcuOpcode AcuCodeGen_GetBinaryOpcode(AstBinaryKind kind, TypeId operand_type, bool is_imm);
