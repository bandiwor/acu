#pragma once
#include "defines/defines.h"
#include <assert.h>

typedef u8 AcuReg;
typedef u32 AcuInstruction;

typedef struct {
    u16 raw;
} AcuTarget;

enum {
    ACU_TARGET_FLAG_AUTO = 0x0100,
    ACU_TARGET_FLAG_NONE = 0x0101,
};

#define ACU_TARGET_AUTO ((AcuTarget){.raw = ACU_TARGET_FLAG_AUTO})
#define ACU_TARGET_NONE ((AcuTarget){.raw = ACU_TARGET_FLAG_NONE})

static inline AcuTarget AcuTarget_Reg(AcuReg reg) {
    return (AcuTarget){.raw = (u16)reg};
}

static inline bool AcuTarget_IsNone(AcuTarget target) {
    return target.raw == ACU_TARGET_FLAG_NONE;
}

static inline bool AcuTarget_IsAuto(AcuTarget target) {
    return target.raw == ACU_TARGET_FLAG_AUTO;
}

static inline bool AcuTarget_IsRealReg(AcuTarget target) {
    return target.raw <= 0x00FF;
}

static inline AcuReg AcuTarget_ToRealReg(AcuTarget target) {
    assert(AcuTarget_IsRealReg(target) && "Target is not a physical register (AUTO or NONE)!");
    return (AcuReg)target.raw;
}

typedef enum {
    OP_FMT_NONE,
    OP_FMT_A,
    OP_FMT_sAx,
    OP_FMT_AB,
    OP_FMT_ABx,
    OP_FMT_AsBx,
    OP_FMT_ABC,
    OP_FMT_ABsC,
    OP_FMT_AsBsC,
    OP_FMT_ABC_WIDE,
} AcuOpcodeFormat;

typedef enum {
    OP_READ_NONE,
    OP_READ_A,
    OP_READ_B,
    OP_READ_C,
    OP_READ_AB,
    OP_READ_BC,
    OP_READ_ABC,
    OP_READ_RANGE_CALL,
    OP_READ_RANGE_SYSCALL,
} AcuOpcodeReadMode;

typedef enum {
    OP_WRITE_NONE,
    OP_WRITE_A,
    OP_WRITE_RANGE_CALL,
} AcuOpcodeWriteMode;

typedef enum {
    OP_EFFECT_NONE,
    OP_EFFECT_READ_GLOBAL,
    OP_EFFECT_WRITE_GLOBAL,
    OP_EFFECT_CONTROL_FLOW,
    OP_EFFECT_CALL,
    OP_EFFECT_SYSCALL,
    OP_EFFECT_IO,
    OP_EFFECT_TRAP,
} AcuOpcodeSideEffect;

typedef enum {
    ACU_PURITY_CONST = 0,
    ACU_PURITY_PURE = 1,
    ACU_PURITY_IMPURE = 2,
} AcuPurity;

typedef enum {
    ACU_FLAG_NONE = 0,
    ACU_FLAG_COMMUTATIVE = (1 << 0),
    ACU_FLAG_SELF_ZERO = (1 << 1),
    ACU_FLAG_SELF_ONE = (1 << 2),
    ACU_FLAG_TERMINATOR = (1 << 3),
    ACU_FLAG_BRANCH_COND = (1 << 4),
    ACU_FLAG_BRANCH_UNCOND = (1 << 5),
    ACU_FLAG_IS_IMM = (1 << 6),
    ACU_FLAG_POTENTIAL_TRAP = (1 << 7),
} AcuOpcodeFlags;

#define X_ACU_OPCODES(X)                                                                           \
    X(OP_NOP, "NOP", 1, OP_FMT_NONE, OP_READ_NONE, OP_WRITE_NONE, OP_EFFECT_NONE,                  \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_HALT, "HALT", 1, OP_FMT_NONE, OP_READ_NONE, OP_WRITE_NONE, OP_EFFECT_CONTROL_FLOW,        \
      ACU_PURITY_IMPURE, ACU_FLAG_TERMINATOR)                                                      \
    X(OP_RET, "RET", 1, OP_FMT_A, OP_READ_A, OP_WRITE_NONE, OP_EFFECT_CONTROL_FLOW,                \
      ACU_PURITY_IMPURE, ACU_FLAG_TERMINATOR)                                                      \
    X(OP_RET_VOID, "RET_VOID", 1, OP_FMT_NONE, OP_READ_NONE, OP_WRITE_NONE,                        \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_TERMINATOR)                              \
    X(OP_JUMP, "JUMP", 1, OP_FMT_sAx, OP_READ_NONE, OP_WRITE_NONE, OP_EFFECT_CONTROL_FLOW,         \
      ACU_PURITY_IMPURE, ACU_FLAG_TERMINATOR | ACU_FLAG_BRANCH_UNCOND)                             \
    X(OP_JUMP_IF_TRUE, "JUMP_IF_TRUE", 1, OP_FMT_AsBx, OP_READ_A, OP_WRITE_NONE,                   \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_IF_FALSE, "JUMP_IF_FALSE", 1, OP_FMT_AsBx, OP_READ_A, OP_WRITE_NONE,                 \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
                                                                                                   \
    X(OP_MOVE, "MOVE", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
    X(OP_LOAD_CONST, "LOAD_CONST", 1, OP_FMT_ABx, OP_READ_NONE, OP_WRITE_A, OP_EFFECT_NONE,        \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_LOAD_STR, "LOAD_STR", 1, OP_FMT_ABx, OP_READ_NONE, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_LOAD_IMM, "LOAD_IMM", 1, OP_FMT_AsBx, OP_READ_NONE, OP_WRITE_A, OP_EFFECT_NONE,           \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_LOAD_GLOBAL, "LOAD_GLOBAL", 1, OP_FMT_ABx, OP_READ_NONE, OP_WRITE_A,                      \
      OP_EFFECT_READ_GLOBAL, ACU_PURITY_PURE, ACU_FLAG_NONE)                                       \
    X(OP_STORE_GLOBAL, "STORE_GLOBAL", 1, OP_FMT_ABx, OP_READ_A, OP_WRITE_NONE,                    \
      OP_EFFECT_WRITE_GLOBAL, ACU_PURITY_IMPURE, ACU_FLAG_NONE)                                    \
                                                                                                   \
    X(OP_CALL, "CALL", 2, OP_FMT_ABC_WIDE, OP_READ_RANGE_CALL, OP_WRITE_A, OP_EFFECT_CALL,         \
      ACU_PURITY_IMPURE, ACU_FLAG_NONE)                                                            \
    X(OP_CALL_VOID, "CALL_VOID", 2, OP_FMT_ABC_WIDE, OP_READ_RANGE_CALL, OP_WRITE_NONE,            \
      OP_EFFECT_CALL, ACU_PURITY_IMPURE, ACU_FLAG_NONE)                                            \
    X(OP_TAILCALL, "TAILCALL", 2, OP_FMT_ABC_WIDE, OP_READ_RANGE_CALL, OP_WRITE_NONE,              \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_TERMINATOR)                              \
    X(OP_SYSCALL, "SYSCALL", 2, OP_FMT_ABC_WIDE, OP_READ_RANGE_SYSCALL, OP_WRITE_A,                \
      OP_EFFECT_SYSCALL, ACU_PURITY_IMPURE, ACU_FLAG_NONE)                                         \
    X(OP_SYSCALL_VOID, "SYSCALL_VOID", 2, OP_FMT_ABC_WIDE, OP_READ_RANGE_SYSCALL, OP_WRITE_NONE,   \
      OP_EFFECT_SYSCALL, ACU_PURITY_IMPURE, ACU_FLAG_NONE)                                         \
                                                                                                   \
    X(OP_JUMP_EQ, "JUMP_EQ", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE, OP_EFFECT_CONTROL_FLOW,    \
      ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_COMMUTATIVE)                              \
    X(OP_JUMP_NEQ, "JUMP_NEQ", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE, OP_EFFECT_CONTROL_FLOW,  \
      ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_COMMUTATIVE)                              \
    X(OP_JUMP_LT_S, "JUMP_LT_S", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                        \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_LT_U, "JUMP_LT_U", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                        \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_LTE_S, "JUMP_LTE_S", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                      \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_LTE_U, "JUMP_LTE_U", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                      \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_GT_S, "JUMP_GT_S", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                        \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_GT_U, "JUMP_GT_U", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                        \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_GTE_S, "JUMP_GTE_S", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                      \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
    X(OP_JUMP_GTE_U, "JUMP_GTE_U", 1, OP_FMT_ABsC, OP_READ_AB, OP_WRITE_NONE,                      \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND)                             \
                                                                                                   \
    X(OP_JUMP_EQ_IMM, "JUMP_EQ_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                    \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_NEQ_IMM, "JUMP_NEQ_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                  \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_LT_S_IMM, "JUMP_LT_S_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_LT_U_IMM, "JUMP_LT_U_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_LTE_S_IMM, "JUMP_LTE_S_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,              \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_LTE_U_IMM, "JUMP_LTE_U_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,              \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_GT_S_IMM, "JUMP_GT_S_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_GT_U_IMM, "JUMP_GT_U_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,                \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_GTE_S_IMM, "JUMP_GTE_S_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,              \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
    X(OP_JUMP_GTE_U_IMM, "JUMP_GTE_U_IMM", 1, OP_FMT_AsBsC, OP_READ_A, OP_WRITE_NONE,              \
      OP_EFFECT_CONTROL_FLOW, ACU_PURITY_IMPURE, ACU_FLAG_BRANCH_COND | ACU_FLAG_IS_IMM)           \
                                                                                                   \
    X(OP_ADD, "ADD", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_SUB, "SUB", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_SELF_ZERO)                                                                          \
    X(OP_MUL, "MUL", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_DIV_S, "DIV_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_TRAP, ACU_PURITY_IMPURE, \
      ACU_FLAG_POTENTIAL_TRAP)                                                                     \
    X(OP_DIV_U, "DIV_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_TRAP, ACU_PURITY_IMPURE, \
      ACU_FLAG_POTENTIAL_TRAP)                                                                     \
    X(OP_REM_S, "REM_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_TRAP, ACU_PURITY_IMPURE, \
      ACU_FLAG_POTENTIAL_TRAP)                                                                     \
    X(OP_REM_U, "REM_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_TRAP, ACU_PURITY_IMPURE, \
      ACU_FLAG_POTENTIAL_TRAP)                                                                     \
    X(OP_POW, "POW", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FPOW, "FPOW", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_AND, "AND", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_OR, "OR", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,        \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_XOR, "XOR", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE | ACU_FLAG_SELF_ZERO)                                                   \
    X(OP_SHL, "SHL", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
    X(OP_SHR_S, "SHR_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_SHR_U, "SHR_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
                                                                                                   \
    X(OP_ADD_IMM, "ADD_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_SUB_IMM, "SUB_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_RSUB_IMM, "RSUB_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,              \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_MUL_IMM, "MUL_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_DIV_S_IMM, "DIV_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_TRAP,            \
      ACU_PURITY_IMPURE, ACU_FLAG_IS_IMM | ACU_FLAG_POTENTIAL_TRAP)                                \
    X(OP_DIV_U_IMM, "DIV_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_TRAP,            \
      ACU_PURITY_IMPURE, ACU_FLAG_IS_IMM | ACU_FLAG_POTENTIAL_TRAP)                                \
    X(OP_REM_S_IMM, "REM_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_TRAP,            \
      ACU_PURITY_IMPURE, ACU_FLAG_IS_IMM | ACU_FLAG_POTENTIAL_TRAP)                                \
    X(OP_REM_U_IMM, "REM_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_TRAP,            \
      ACU_PURITY_IMPURE, ACU_FLAG_IS_IMM | ACU_FLAG_POTENTIAL_TRAP)                                \
    X(OP_POW_IMM, "POW_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_AND_IMM, "AND_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_OR_IMM, "OR_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                  \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_XOR_IMM, "XOR_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_SHL_IMM, "SHL_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_SHR_S_IMM, "SHR_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_SHR_U_IMM, "SHR_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
                                                                                                   \
    X(OP_FADD, "FADD", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_FSUB, "FSUB", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FMUL, "FMUL", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_FDIV, "FDIV", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FREM, "FREM", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FNEG, "FNEG", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
                                                                                                   \
    X(OP_NEG, "NEG", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,        \
      ACU_FLAG_NONE)                                                                               \
    X(OP_NOT, "NOT", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,        \
      ACU_FLAG_NONE)                                                                               \
    X(OP_LNOT, "LNOT", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
                                                                                                   \
    X(OP_EQ, "EQ", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,        \
      ACU_FLAG_COMMUTATIVE | ACU_FLAG_SELF_ONE)                                                    \
    X(OP_NEQ, "NEQ", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE | ACU_FLAG_SELF_ZERO)                                                   \
    X(OP_LT_S, "LT_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_SELF_ZERO)                                                                          \
    X(OP_LT_U, "LT_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_SELF_ZERO)                                                                          \
    X(OP_LTE_S, "LTE_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_SELF_ONE)                                                                           \
    X(OP_LTE_U, "LTE_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_SELF_ONE)                                                                           \
    X(OP_GT_S, "GT_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_SELF_ZERO)                                                                          \
    X(OP_GT_U, "GT_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_SELF_ZERO)                                                                          \
    X(OP_GTE_S, "GTE_S", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_SELF_ONE)                                                                           \
    X(OP_GTE_U, "GTE_U", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_SELF_ONE)                                                                           \
                                                                                                   \
    X(OP_EQ_IMM, "EQ_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                  \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_NEQ_IMM, "NEQ_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_LT_S_IMM, "LT_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,              \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_LT_U_IMM, "LT_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,              \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_LTE_S_IMM, "LTE_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_LTE_U_IMM, "LTE_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_GT_S_IMM, "GT_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,              \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_GT_U_IMM, "GT_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,              \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_GTE_S_IMM, "GTE_S_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
    X(OP_GTE_U_IMM, "GTE_U_IMM", 1, OP_FMT_ABsC, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_IS_IMM)                                                           \
                                                                                                   \
    X(OP_FEQ, "FEQ", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_FNEQ, "FNEQ", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_COMMUTATIVE)                                                                        \
    X(OP_FLT, "FLT", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FLTE, "FLTE", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FGT, "FGT", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,      \
      ACU_FLAG_NONE)                                                                               \
    X(OP_FGTE, "FGTE", 1, OP_FMT_ABC, OP_READ_BC, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
                                                                                                   \
    X(OP_SEXT8, "SEXT8", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_SEXT16, "SEXT16", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_SEXT32, "SEXT32", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_ZEXT8, "ZEXT8", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,    \
      ACU_FLAG_NONE)                                                                               \
    X(OP_ZEXT16, "ZEXT16", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_ZEXT32, "ZEXT32", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_TRUNC8, "TRUNC8", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE, ACU_PURITY_CONST,  \
      ACU_FLAG_NONE)                                                                               \
    X(OP_TRUNC16, "TRUNC16", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                  \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_TRUNC32, "TRUNC32", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,                  \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_I64_TO_F64, "I64_TO_F64", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_U64_TO_F64, "U64_TO_F64", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_F64_TO_I64, "F64_TO_I64", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_F64_TO_U64, "F64_TO_U64", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_F32_TO_F64, "F32_TO_F64", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)                                                             \
    X(OP_F64_TO_F32, "F64_TO_F32", 1, OP_FMT_AB, OP_READ_B, OP_WRITE_A, OP_EFFECT_NONE,            \
      ACU_PURITY_CONST, ACU_FLAG_NONE)

typedef enum {
#define X(name, str, slots, fmt, rmode, wmode, effect, purity, flags) name,
    X_ACU_OPCODES(X)
#undef X
        ACU_OPCODE_COUNT,
} AcuOpcode;

typedef struct {
    const char *name;
    AcuOpcodeFormat format;
    AcuOpcodeReadMode read_mode;
    AcuOpcodeWriteMode write_mode;
    AcuOpcodeSideEffect side_effect;
    AcuPurity default_purity;
    AcuOpcodeFlags flags;
    u8 slots;
} AcuOpcodeInfo;

extern const AcuOpcodeInfo g_acu_opcode_info[ACU_OPCODE_COUNT];

static inline attribute_always_inline const AcuOpcodeInfo *AcuOpcode_GetInfo(AcuOpcode op) {
    assert(op < ACU_OPCODE_COUNT);
    return &g_acu_opcode_info[op];
}

static inline attribute_always_inline u8 AcuInst_GetSlots(AcuInstruction inst) {
    return g_acu_opcode_info[inst & 0xFF].slots;
}

static inline attribute_always_inline bool AcuOpcode_HasFlag(AcuOpcode op, AcuOpcodeFlags flag) {
    return (g_acu_opcode_info[op].flags & flag) != 0;
}

static inline AcuOpcode AcuOpcode_GetInverted(AcuOpcode op) {
    switch (op) {
        case OP_JUMP_IF_TRUE:
            return OP_JUMP_IF_FALSE;
        case OP_JUMP_IF_FALSE:
            return OP_JUMP_IF_TRUE;
        case OP_JUMP_EQ:
            return OP_JUMP_NEQ;
        case OP_JUMP_NEQ:
            return OP_JUMP_EQ;
        case OP_JUMP_LT_S:
            return OP_JUMP_GTE_S;
        case OP_JUMP_LTE_S:
            return OP_JUMP_GT_S;
        case OP_JUMP_GT_S:
            return OP_JUMP_LTE_S;
        case OP_JUMP_GTE_S:
            return OP_JUMP_LT_S;
        case OP_JUMP_LT_U:
            return OP_JUMP_GTE_U;
        case OP_JUMP_LTE_U:
            return OP_JUMP_GT_U;
        case OP_JUMP_GT_U:
            return OP_JUMP_LTE_U;
        case OP_JUMP_GTE_U:
            return OP_JUMP_LT_U;

        case OP_JUMP_EQ_IMM:
            return OP_JUMP_NEQ_IMM;
        case OP_JUMP_NEQ_IMM:
            return OP_JUMP_EQ_IMM;
        case OP_JUMP_LT_S_IMM:
            return OP_JUMP_GTE_S_IMM;
        case OP_JUMP_LTE_S_IMM:
            return OP_JUMP_GT_S_IMM;
        case OP_JUMP_GT_S_IMM:
            return OP_JUMP_LTE_S_IMM;
        case OP_JUMP_GTE_S_IMM:
            return OP_JUMP_LT_S_IMM;
        case OP_JUMP_LT_U_IMM:
            return OP_JUMP_GTE_U_IMM;
        case OP_JUMP_LTE_U_IMM:
            return OP_JUMP_GT_U_IMM;
        case OP_JUMP_GT_U_IMM:
            return OP_JUMP_LTE_U_IMM;
        case OP_JUMP_GTE_U_IMM:
            return OP_JUMP_LT_U_IMM;

        case OP_EQ:
            return OP_NEQ;
        case OP_NEQ:
            return OP_EQ;
        case OP_LT_S:
            return OP_GTE_S;
        case OP_LTE_S:
            return OP_GT_S;
        case OP_GT_S:
            return OP_LTE_S;
        case OP_GTE_S:
            return OP_LT_S;
        default:
            return OP_NOP;
    }
}

static inline AcuOpcode AcuOpcode_GetSwapped(AcuOpcode op) {
    if (AcuOpcode_HasFlag(op, ACU_FLAG_COMMUTATIVE)) {
        return op;
    }
    switch (op) {
        case OP_LT_S:
            return OP_GT_S;
        case OP_LTE_S:
            return OP_GTE_S;
        case OP_GT_S:
            return OP_LT_S;
        case OP_GTE_S:
            return OP_LTE_S;
        case OP_LT_U:
            return OP_GT_U;
        case OP_LTE_U:
            return OP_GTE_U;
        case OP_GT_U:
            return OP_LT_U;
        case OP_GTE_U:
            return OP_LTE_U;
        case OP_JUMP_LT_S:
            return OP_JUMP_GT_S;
        case OP_JUMP_LTE_S:
            return OP_JUMP_GTE_S;
        case OP_JUMP_GT_S:
            return OP_JUMP_LT_S;
        case OP_JUMP_GTE_S:
            return OP_JUMP_LTE_S;
        case OP_JUMP_LT_U:
            return OP_JUMP_GT_U;
        case OP_JUMP_LTE_U:
            return OP_JUMP_GTE_U;
        case OP_JUMP_GT_U:
            return OP_JUMP_LT_U;
        case OP_JUMP_GTE_U:
            return OP_JUMP_LTE_U;
        default:
            return OP_NOP;
    }
}

static inline AcuOpcode AcuOpcode_ToImmVariant(AcuOpcode op) {
    switch (op) {
        case OP_ADD:
            return OP_ADD_IMM;
        case OP_SUB:
            return OP_SUB_IMM;
        case OP_MUL:
            return OP_MUL_IMM;
        case OP_DIV_S:
            return OP_DIV_S_IMM;
        case OP_DIV_U:
            return OP_DIV_U_IMM;
        case OP_REM_S:
            return OP_REM_S_IMM;
        case OP_REM_U:
            return OP_REM_U_IMM;
        case OP_POW:
            return OP_POW_IMM;
        case OP_AND:
            return OP_AND_IMM;
        case OP_OR:
            return OP_OR_IMM;
        case OP_XOR:
            return OP_XOR_IMM;
        case OP_SHL:
            return OP_SHL_IMM;
        case OP_SHR_S:
            return OP_SHR_S_IMM;
        case OP_SHR_U:
            return OP_SHR_U_IMM;
        case OP_EQ:
            return OP_EQ_IMM;
        case OP_NEQ:
            return OP_NEQ_IMM;
        case OP_LT_S:
            return OP_LT_S_IMM;
        case OP_LT_U:
            return OP_LT_U_IMM;
        case OP_LTE_S:
            return OP_LTE_S_IMM;
        case OP_LTE_U:
            return OP_LTE_U_IMM;
        case OP_GT_S:
            return OP_GT_S_IMM;
        case OP_GT_U:
            return OP_GT_U_IMM;
        case OP_GTE_S:
            return OP_GTE_S_IMM;
        case OP_GTE_U:
            return OP_GTE_U_IMM;
        case OP_JUMP_EQ:
            return OP_JUMP_EQ_IMM;
        case OP_JUMP_NEQ:
            return OP_JUMP_NEQ_IMM;
        default:
            return OP_NOP;
    }
}

static inline AcuOpcode AcuOpcode_FuseCmpWithBranch(AcuOpcode cmp_op, bool is_jump_if_true) {
    AcuOpcode target = OP_NOP;
    switch (cmp_op) {
        case OP_EQ:
            target = OP_JUMP_EQ;
            break;
        case OP_NEQ:
            target = OP_JUMP_NEQ;
            break;
        case OP_LT_S:
            target = OP_JUMP_LT_S;
            break;
        case OP_LT_U:
            target = OP_JUMP_LT_U;
            break;
        case OP_LTE_S:
            target = OP_JUMP_LTE_S;
            break;
        case OP_LTE_U:
            target = OP_JUMP_LTE_U;
            break;
        case OP_GT_S:
            target = OP_JUMP_GT_S;
            break;
        case OP_GT_U:
            target = OP_JUMP_GT_U;
            break;
        case OP_GTE_S:
            target = OP_JUMP_GTE_S;
            break;
        case OP_GTE_U:
            target = OP_JUMP_GTE_U;
            break;
        default:
            return OP_NOP;
    }

    if (!is_jump_if_true) {
        target = AcuOpcode_GetInverted(target);
    }
    return target;
}

static inline attribute_always_inline AcuOpcodeFormat AcuOpcode_GetFormat(AcuOpcode op) {
    assert(op < ACU_OPCODE_COUNT && "Invalid opcode!");
    return g_acu_opcode_info[op].format;
}

static inline attribute_always_inline AcuOpcodeFormat AcuInst_GetFormat(AcuInstruction inst) {
    return AcuOpcode_GetFormat((AcuOpcode)(inst & 0xFF));
}

static inline attribute_always_inline bool AcuInst_HasFormat(AcuInstruction inst,
                                                             AcuOpcodeFormat fmt) {
    return AcuInst_GetFormat(inst) == fmt;
}

static inline attribute_always_inline bool AcuFormat_HasFieldA(AcuOpcodeFormat fmt) {
    switch (fmt) {
        case OP_FMT_A:
        case OP_FMT_AB:
        case OP_FMT_ABx:
        case OP_FMT_AsBx:
        case OP_FMT_ABC:
        case OP_FMT_ABsC:
        case OP_FMT_AsBsC:
        case OP_FMT_ABC_WIDE:
            return true;
        default:
            return false;
    }
}

static inline attribute_always_inline bool AcuFormat_HasFieldB(AcuOpcodeFormat fmt) {
    switch (fmt) {
        case OP_FMT_AB:
        case OP_FMT_ABC:
        case OP_FMT_ABsC:
        case OP_FMT_ABC_WIDE:
            return true;
        default:
            return false;
    }
}

static inline attribute_always_inline bool AcuFormat_HasFieldsB(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_AsBsC;
}

static inline attribute_always_inline bool AcuFormat_HasFieldBx(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_ABx;
}

static inline attribute_always_inline bool AcuFormat_HasFieldsBx(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_AsBx;
}

static inline attribute_always_inline bool AcuFormat_HasFieldC(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_ABC || fmt == OP_FMT_ABC_WIDE;
}

static inline attribute_always_inline bool AcuFormat_HasFieldsC(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_ABsC || fmt == OP_FMT_AsBsC;
}

static inline attribute_always_inline bool AcuFormat_HasFieldsAx(AcuOpcodeFormat fmt) {
    return fmt == OP_FMT_sAx;
}

static inline attribute_always_inline AcuOpcode AcuInst_GetOp(AcuInstruction inst) {
    return (AcuOpcode)(inst & 0xFFU);
}

static inline attribute_always_inline AcuReg AcuInst_GetA(AcuInstruction inst) {
    return (AcuReg)((inst >> 8) & 0xFFU);
}

static inline attribute_always_inline i8 AcuInst_GetsA(AcuInstruction inst) {
    return (i8)(u8)((inst >> 8) & 0xFFU);
}

static inline attribute_always_inline AcuReg AcuInst_GetB(AcuInstruction inst) {
    return (AcuReg)((inst >> 16) & 0xFFU);
}

static inline attribute_always_inline i8 AcuInst_GetsB(AcuInstruction inst) {
    return (i8)(u8)((inst >> 16) & 0xFFU);
}

static inline attribute_always_inline AcuReg AcuInst_GetC(AcuInstruction inst) {
    return (AcuReg)((inst >> 24) & 0xFFU);
}

static inline attribute_always_inline i8 AcuInst_GetsC(AcuInstruction inst) {
    return (i8)(u8)((inst >> 24) & 0xFFU);
}

static inline attribute_always_inline u16 AcuInst_GetBx(AcuInstruction inst) {
    return (u16)((inst >> 16) & 0xFFFFU);
}

static inline attribute_always_inline i16 AcuInst_GetsBx(AcuInstruction inst) {
    return (i16)(u16)((inst >> 16) & 0xFFFFU);
}

static inline attribute_always_inline u32 AcuInst_GetAx(AcuInstruction inst) {
    return (inst >> 8) & 0x00FFFFFFU;
}

static inline attribute_always_inline i32 AcuInst_GetsAx(AcuInstruction inst) {
    return (i32)inst >> 8;
}

static inline attribute_always_inline void AcuInst_SetOp(AcuInstruction *inst, AcuOpcode op) {
    *inst = (*inst & ~0x000000FFU) | ((u32)op & 0xFFU);
}

static inline attribute_always_inline void AcuInst_SetA(AcuInstruction *inst, AcuReg a) {
    *inst = (*inst & ~0x0000FF00U) | (((u32)a & 0xFFU) << 8);
}

static inline attribute_always_inline void AcuInst_SetsA(AcuInstruction *inst, i8 sa) {
    *inst = (*inst & ~0x0000FF00U) | (((u32)(u8)sa) << 8);
}

static inline attribute_always_inline void AcuInst_SetB(AcuInstruction *inst, AcuReg b) {
    *inst = (*inst & ~0x00FF0000U) | (((u32)b & 0xFFU) << 16);
}

static inline attribute_always_inline void AcuInst_SetsB(AcuInstruction *inst, i8 sb) {
    *inst = (*inst & ~0x00FF0000U) | (((u32)(u8)sb) << 16);
}

static inline attribute_always_inline void AcuInst_SetC(AcuInstruction *inst, AcuReg c) {
    *inst = (*inst & ~0xFF000000U) | (((u32)c & 0xFFU) << 24);
}

static inline attribute_always_inline void AcuInst_SetsC(AcuInstruction *inst, i8 sc) {
    *inst = (*inst & ~0xFF000000U) | (((u32)(u8)sc) << 24);
}

static inline attribute_always_inline void AcuInst_SetBx(AcuInstruction *inst, u16 bx) {
    *inst = (*inst & ~0xFFFF0000U) | (((u32)bx & 0xFFFFU) << 16);
}

static inline attribute_always_inline void AcuInst_SetsBx(AcuInstruction *inst, i16 sbx) {
    *inst = (*inst & ~0xFFFF0000U) | (((u32)(u16)sbx) << 16);
}

static inline attribute_always_inline void AcuInst_SetAx(AcuInstruction *inst, u32 ax) {
    *inst = (*inst & 0x000000FFU) | ((ax & 0x00FFFFFFU) << 8);
}

static inline attribute_always_inline void AcuInst_SetsAx(AcuInstruction *inst, i32 sax) {
    *inst = (*inst & 0x000000FFU) | (((u32)sax & 0x00FFFFFFU) << 8);
}

#if defined(DEBUG) || !defined(NDEBUG)

static inline attribute_always_inline AcuReg AcuInst_CheckedGetA(AcuInstruction inst) {
    assert(AcuFormat_HasFieldA(AcuInst_GetFormat(inst)) && "Instruction format has no field A!");
    return AcuInst_GetA(inst);
}

static inline attribute_always_inline AcuReg AcuInst_CheckedGetB(AcuInstruction inst) {
    assert(AcuFormat_HasFieldB(AcuInst_GetFormat(inst)) && "Instruction format has no field B!");
    return AcuInst_GetB(inst);
}

static inline attribute_always_inline AcuReg AcuInst_CheckedGetC(AcuInstruction inst) {
    assert(AcuFormat_HasFieldC(AcuInst_GetFormat(inst)) && "Instruction format has no field C!");
    return AcuInst_GetC(inst);
}

static inline attribute_always_inline i8 AcuInst_CheckedGetsC(AcuInstruction inst) {
    assert(AcuFormat_HasFieldsC(AcuInst_GetFormat(inst)) && "Instruction format has no field sC!");
    return AcuInst_GetsC(inst);
}

static inline attribute_always_inline u16 AcuInst_CheckedGetBx(AcuInstruction inst) {
    assert(AcuFormat_HasFieldBx(AcuInst_GetFormat(inst)) && "Instruction format has no field Bx!");
    return AcuInst_GetBx(inst);
}

static inline attribute_always_inline i16 AcuInst_CheckedGetsBx(AcuInstruction inst) {
    assert(AcuFormat_HasFieldsBx(AcuInst_GetFormat(inst)) &&
           "Instruction format has no field sBx!");
    return AcuInst_GetsBx(inst);
}

static inline attribute_always_inline i32 AcuInst_CheckedGetsAx(AcuInstruction inst) {
    assert(AcuFormat_HasFieldsAx(AcuInst_GetFormat(inst)) &&
           "Instruction format has no field sAx!");
    return AcuInst_GetsAx(inst);
}

#else

#define AcuInst_CheckedGetA AcuInst_GetA
#define AcuInst_CheckedGetB AcuInst_GetB
#define AcuInst_CheckedGetC AcuInst_GetC
#define AcuInst_CheckedGetsC AcuInst_GetsC
#define AcuInst_CheckedGetBx AcuInst_GetBx
#define AcuInst_CheckedGetsBx AcuInst_GetsBx
#define AcuInst_CheckedGetsAx AcuInst_GetsAx

#endif

static inline attribute_always_inline bool AcuOpcode_CheckFormat(AcuOpcode op,
                                                                 AcuOpcodeFormat expected_fmt) {
    assert(op < ACU_OPCODE_COUNT && "Invalid opcode!");
    return g_acu_opcode_info[op].format == expected_fmt;
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_NONE(AcuOpcode op) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_NONE) && "Opcode format is not OP_FMT_NONE!");
    return (AcuInstruction)((u32)op & 0xFFU);
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_A(AcuOpcode op, AcuReg a) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_A) && "Opcode format is not OP_FMT_A!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_sAx(AcuOpcode op, i32 sax) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_sAx) && "Opcode format is not OP_FMT_sAx!");
    assert(sax >= -8388608 && sax <= 8388607 && "sAx operand out of 24-bit range!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)sax & 0x00FFFFFFU) << 8));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_AB(AcuOpcode op, AcuReg a,
                                                                       AcuReg b) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_AB) && "Opcode format is not OP_FMT_AB!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) | (((u32)b & 0xFFU) << 16));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_ABx(AcuOpcode op, AcuReg a,
                                                                        u16 bx) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_ABx) && "Opcode format is not OP_FMT_ABx!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) |
                            (((u32)bx & 0xFFFFU) << 16));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_AsBx(AcuOpcode op, AcuReg a,
                                                                         i16 sbx) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_AsBx) && "Opcode format is not OP_FMT_AsBx!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) |
                            (((u32)(u16)sbx & 0xFFFFU) << 16));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_ABC(AcuOpcode op, AcuReg a,
                                                                        AcuReg b, AcuReg c) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_ABC) && "Opcode format is not OP_FMT_ABC!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) | (((u32)b & 0xFFU) << 16) |
                            (((u32)c & 0xFFU) << 24));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_ABsC(AcuOpcode op, AcuReg a,
                                                                         AcuReg b, i8 sc) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_ABsC) && "Opcode format is not OP_FMT_ABsC!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) | (((u32)b & 0xFFU) << 16) |
                            (((u32)(u8)sc & 0xFFU) << 24));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_AsBsC(AcuOpcode op, AcuReg a,
                                                                          i8 sb, i8 sc) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_AsBsC) && "Opcode format is not OP_FMT_AsBsC!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) |
                            (((u32)(u8)sb & 0xFFU) << 16) | (((u32)(u8)sc & 0xFFU) << 24));
}

static inline attribute_always_inline AcuInstruction AcuInst_Encode_ABC_WIDE(AcuOpcode op, AcuReg a,
                                                                             AcuReg b, AcuReg c) {
    assert(AcuOpcode_CheckFormat(op, OP_FMT_ABC_WIDE) && "Opcode format is not OP_FMT_ABC_WIDE!");
    return (AcuInstruction)(((u32)op & 0xFFU) | (((u32)a & 0xFFU) << 8) | (((u32)b & 0xFFU) << 16) |
                            (((u32)c & 0xFFU) << 24));
}

static inline u32 AcuOpt_GetJumpTarget(u32 ip, AcuInstruction inst) {
    AcuOpcode op = AcuInst_GetOp(inst);
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);

    if (!(info->flags & (ACU_FLAG_BRANCH_COND | ACU_FLAG_BRANCH_UNCOND))) {
        return UINT32_MAX;
    }

    i32 offset = 0;
    switch (info->format) {
        case OP_FMT_sAx:
            offset = AcuInst_GetsAx(inst);
            break;
        case OP_FMT_AsBx:
            offset = (i32)AcuInst_GetsBx(inst);
            break;
        case OP_FMT_ABsC:
        case OP_FMT_AsBsC:
            offset = (i32)AcuInst_GetsC(inst);
            break;
        default:
            return UINT32_MAX;
    }

    i32 target = (i32)ip + 1 + offset;
    return (target >= 0) ? (u32)target : UINT32_MAX;
}
