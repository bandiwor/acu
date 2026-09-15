#pragma once

#include "defines/bytecode.h"
#include "defines/defines.h"
#include "types/acu_value.h"
#include "types/acu_vm_status.h"

typedef AcuVmStatus (*AcuSyscallHandler)(const AcuValue *args, u8 argc, AcuValue *out_ret);

#define ACU_SYSCALL_LIST(X)                                                                        \
    X(ACU_SYSCALL_IO_PRINT_I64, "io", "printI64", AcuSys_PrintI64, ACU_PURITY_IMPURE, false)       \
    X(ACU_SYSCALL_IO_PRINT_U64, "io", "printU64", AcuSys_PrintU64, ACU_PURITY_IMPURE, false)       \
    X(ACU_SYSCALL_IO_PRINT_F64, "io", "printF64", AcuSys_PrintF64, ACU_PURITY_IMPURE, false)       \
    X(ACU_SYSCALL_IO_PRINT_STR, "io", "printStr", AcuSys_PrintStr, ACU_PURITY_IMPURE, false)       \
    X(ACU_SYSCALL_IO_PRINTLN, "io", "println", AcuSys_Println, ACU_PURITY_IMPURE, false)           \
    X(ACU_SYSCALL_IO_READ_I64, "io", "readI64", AcuSys_ReadI64, ACU_PURITY_IMPURE, false)          \
    X(ACU_SYSCALL_IO_READ_U64, "io", "readU64", AcuSys_ReadU64, ACU_PURITY_IMPURE, false)          \
    X(ACU_SYSCALL_IO_READ_F64, "io", "readF64", AcuSys_ReadF64, ACU_PURITY_IMPURE, false)          \
                                                                                                   \
    X(ACU_SYSCALL_SYS_CLOCK_NS, "sys", "clockNs", AcuSys_ClockNs, ACU_PURITY_PURE, false)          \
    X(ACU_SYSCALL_SYS_CLOCK_MS, "sys", "clockMs", AcuSys_ClockMs, ACU_PURITY_PURE, false)          \
    X(ACU_SYSCALL_SYS_RDTSC, "sys", "rdtsc", AcuSys_Rdtsc, ACU_PURITY_PURE, false)                 \
    X(ACU_SYSCALL_SYS_TIME_SEC, "sys", "timeSec", AcuSys_TimeSec, ACU_PURITY_PURE, false)          \
    X(ACU_SYSCALL_SYS_TIME_MS, "sys", "timeMs", AcuSys_TimeMs, ACU_PURITY_PURE, false)             \
    X(ACU_SYSCALL_SYS_SLEEP_MS, "sys", "sleepMs", AcuSys_SleepMs, ACU_PURITY_IMPURE, false)        \
    X(ACU_SYSCALL_SYS_SLEEP_NS, "sys", "sleepNs", AcuSys_SleepNs, ACU_PURITY_IMPURE, false)        \
    X(ACU_SYSCALL_SYS_EXIT, "sys", "exit", AcuSys_Exit, ACU_PURITY_IMPURE, true)                   \
    X(ACU_SYSCALL_SYS_PANIC, "sys", "panic", AcuSys_Panic, ACU_PURITY_IMPURE, true)                \
    X(ACU_SYSCALL_SYS_RANDOM_U64, "sys", "randomU64", AcuSys_RandomU64, ACU_PURITY_PURE, false)    \
    X(ACU_SYSCALL_SYS_RANDOM_SEED, "sys", "randomSeed", AcuSys_RandomSeed, ACU_PURITY_IMPURE,      \
      false)                                                                                       \
    X(ACU_SYSCALL_SYS_PID, "sys", "pid", AcuSys_Pid, ACU_PURITY_PURE, false)                       \
    X(ACU_SYSCALL_SYS_CPU_COUNT, "sys", "cpuCount", AcuSys_CpuCount, ACU_PURITY_PURE, false)       \
                                                                                                   \
    X(ACU_SYSCALL_MATH_SQRT_F64, "math", "sqrt", AcuSys_MathSqrtF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_CBRT_F64, "math", "cbrt", AcuSys_MathCbrtF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_POW_F64, "math", "pow", AcuSys_MathPowF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_SIN_F64, "math", "sin", AcuSys_MathSinF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_COS_F64, "math", "cos", AcuSys_MathCosF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_TAN_F64, "math", "tan", AcuSys_MathTanF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_ASIN_F64, "math", "asin", AcuSys_MathAsinF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_ACOS_F64, "math", "acos", AcuSys_MathAcosF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_ATAN_F64, "math", "atan", AcuSys_MathAtanF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_ATAN2_F64, "math", "atan2", AcuSys_MathAtan2F64, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_EXP_F64, "math", "exp", AcuSys_MathExpF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_LOG_F64, "math", "log", AcuSys_MathLogF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_LOG2_F64, "math", "log2", AcuSys_MathLog2F64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_LOG10_F64, "math", "log10", AcuSys_MathLog10F64, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_FLOOR_F64, "math", "floor", AcuSys_MathFloorF64, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_CEIL_F64, "math", "ceil", AcuSys_MathCeilF64, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_ROUND_F64, "math", "round", AcuSys_MathRoundF64, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_ABS_F64, "math", "abs", AcuSys_MathAbsF64, ACU_PURITY_CONST, false)         \
    X(ACU_SYSCALL_MATH_HYPOT_F64, "math", "hypot", AcuSys_MathHypotF64, ACU_PURITY_CONST, false)   \
                                                                                                   \
    X(ACU_SYSCALL_MATH_SQRT_F32, "math", "sqrtF32", AcuSys_MathSqrtF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_CBRT_F32, "math", "cbrtF32", AcuSys_MathCbrtF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_POW_F32, "math", "powF32", AcuSys_MathPowF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_SIN_F32, "math", "sinF32", AcuSys_MathSinF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_COS_F32, "math", "cosF32", AcuSys_MathCosF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_TAN_F32, "math", "tanF32", AcuSys_MathTanF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_ASIN_F32, "math", "asinF32", AcuSys_MathAsinF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_ACOS_F32, "math", "acosF32", AcuSys_MathAcosF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_ATAN_F32, "math", "atanF32", AcuSys_MathAtanF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_ATAN2_F32, "math", "atan2F32", AcuSys_MathAtan2F32, ACU_PURITY_CONST,       \
      false)                                                                                       \
    X(ACU_SYSCALL_MATH_EXP_F32, "math", "expF32", AcuSys_MathExpF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_LOG_F32, "math", "logF32", AcuSys_MathLogF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_LOG2_F32, "math", "log2F32", AcuSys_MathLog2F32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_LOG10_F32, "math", "log10F32", AcuSys_MathLog10F32, ACU_PURITY_CONST,       \
      false)                                                                                       \
    X(ACU_SYSCALL_MATH_FLOOR_F32, "math", "floorF32", AcuSys_MathFloorF32, ACU_PURITY_CONST,       \
      false)                                                                                       \
    X(ACU_SYSCALL_MATH_CEIL_F32, "math", "ceilF32", AcuSys_MathCeilF32, ACU_PURITY_CONST, false)   \
    X(ACU_SYSCALL_MATH_ROUND_F32, "math", "roundF32", AcuSys_MathRoundF32, ACU_PURITY_CONST,       \
      false)                                                                                       \
    X(ACU_SYSCALL_MATH_ABS_F32, "math", "absF32", AcuSys_MathAbsF32, ACU_PURITY_CONST, false)      \
    X(ACU_SYSCALL_MATH_HYPOT_F32, "math", "hypotF32", AcuSys_MathHypotF32, ACU_PURITY_CONST, false)

typedef enum {
#define X(id, mod, name, handler, purity, term) id,
    ACU_SYSCALL_LIST(X)
#undef X
        ACU_SYSCALL_COUNT
} AcuSyscallId;

typedef struct {
    AcuSyscallHandler handler;
    const char *module;
    const char *name;
    AcuPurity purity;
    bool is_terminator;
} AcuSyscallInfo;

extern const AcuSyscallInfo g_acu_syscall_table[ACU_SYSCALL_COUNT];

static inline const AcuSyscallInfo *AcuSyscall_GetInfo(AcuSyscallId id) {
    if ((u32)id < (u32)ACU_SYSCALL_COUNT) {
        return &g_acu_syscall_table[id];
    }
    return NULL;
}
