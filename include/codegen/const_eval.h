#pragma once
#include "analyzer/analyzer.h"
#include "defines/defines.h"
#include <stdbool.h>

typedef enum {
    CONST_NONE,
    CONST_INT,
    CONST_FLOAT,
    CONST_BOOL,
} AcuConstKind;

typedef struct {
    AcuConstKind kind;
    union {
        i64 i64_val;
        f64 f64_val;
        bool bool_val;
    } as;
} AcuConstVal;

AcuConstVal AcuCodeGen_EvalConst(AcuWorkspace *ws, AstNodeIdx expr_idx);
bool AcuCodeGen_IsPure(AcuWorkspace *ws, AstNodeIdx idx);
