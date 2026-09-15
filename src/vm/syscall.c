#include "vm/syscall.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

static AcuVmStatus AcuSys_PrintI64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)out_ret;
    if (argc < 1)
        return ACU_VM_ERR_INVALID_SYSCALL;
    printf("%" PRId64, args[0].i64);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_PrintU64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)out_ret;
    if (argc < 1)
        return ACU_VM_ERR_INVALID_SYSCALL;
    printf("%" PRIu64, args[0].u64);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_PrintF64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)out_ret;
    if (argc < 1)
        return ACU_VM_ERR_INVALID_SYSCALL;
    printf("%g", args[0].f64);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_PrintStr(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    (void)out_ret;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_Println(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    (void)out_ret;
    putchar('\n');
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_ReadI64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    i64 val = 0;
    if (scanf("%" SCNd64, &val) != 1) {
        out_ret->i64 = 0;
        return ACU_VM_ERR_IO;
    }
    out_ret->i64 = val;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_ReadU64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    u64 val = 0;
    if (scanf("%" SCNu64, &val) != 1) {
        out_ret->u64 = 0;
        return ACU_VM_ERR_IO;
    }
    out_ret->u64 = val;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_ReadF64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    f64 val = 0.0;
    if (scanf("%lf", &val) != 1) {
        out_ret->f64 = 0.0;
        return ACU_VM_ERR_IO;
    }
    out_ret->f64 = val;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_ClockNs(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    out_ret->u64 = ((u64)ts.tv_sec * 1000000000ULL) + (u64)ts.tv_nsec;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_ClockMs(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    out_ret->u64 = ((u64)ts.tv_sec * 1000ULL) + (u64)(ts.tv_nsec / 1000000ULL);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_Rdtsc(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
#if defined(__x86_64__) || defined(_M_X64)
    out_ret->u64 = (u64)__rdtsc();
#else
    out_ret->u64 = 0;
#endif
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_TimeSec(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *out_ret = AcuValue_U64((u64)ts.tv_sec);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_TimeMs(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *out_ret = AcuValue_U64(((u64)ts.tv_sec * 1000ULL) + ((u64)ts.tv_nsec / 1000000ULL));
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_SleepMs(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)argc;
    (void)out_ret;

    u64 ms = args[0].u64;
    struct timespec req = {.tv_sec = (time_t)(ms / 1000ULL),
                           .tv_nsec = (long)((ms % 1000ULL) * 1000000ULL)};
    nanosleep(&req, NULL);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_SleepNs(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)argc;
    (void)out_ret;

    u64 ns = args[0].u64;
    struct timespec req = {.tv_sec = (time_t)(ns / 1000000000ULL),
                           .tv_nsec = (long)(ns % 1000000000ULL)};
    nanosleep(&req, NULL);
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_Exit(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)out_ret;
    i64 exit_code = (argc > 0) ? args[0].i64 : 0;
    exit((int)exit_code);
    return ACU_VM_HALT;
}

static AcuVmStatus AcuSys_Panic(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)out_ret;
    fprintf(stderr, "\n[Acu Runtime Panic]");
    if (argc > 0) {
        fprintf(stderr, ": code=%" PRId64, args[0].i64);
    }
    fprintf(stderr, "\n");
    return ACU_VM_ERR_PANIC;
}

static u64 g_prng_state = 0x9E3779B97F4A7C15ULL;

static AcuVmStatus AcuSys_RandomSeed(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)argc;
    (void)out_ret;

    g_prng_state = args[0].u64;
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_RandomU64(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    u64 z = (g_prng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    *out_ret = AcuValue_U64(z ^ (z >> 31));
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_Pid(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    *out_ret = AcuValue_I64((i64)getpid());
    return ACU_VM_OK;
}

static AcuVmStatus AcuSys_CpuCount(const AcuValue *args, u8 argc, AcuValue *out_ret) {
    (void)args;
    (void)argc;
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    *out_ret = AcuValue_U64((u64)(cores > 0 ? cores : 1));
    return ACU_VM_OK;
}

#define DEFINE_MATH_UNARY_F64(fn_name, c_math_fn)                                                  \
    static AcuVmStatus fn_name(const AcuValue *args, u8 argc, AcuValue *out_ret) {                 \
        (void)argc;                                                                                \
        *out_ret = AcuValue_F64(c_math_fn(args[0].f64));                                           \
        return ACU_VM_OK;                                                                          \
    }

#define DEFINE_MATH_BINARY_F64(fn_name, c_math_fn)                                                 \
    static AcuVmStatus fn_name(const AcuValue *args, u8 argc, AcuValue *out_ret) {                 \
        (void)argc;                                                                                \
        *out_ret = AcuValue_F64(c_math_fn(args[0].f64, args[1].f64));                              \
        return ACU_VM_OK;                                                                          \
    }

#define DEFINE_MATH_UNARY_F32(fn_name, c_math_fn)                                                  \
    static AcuVmStatus fn_name(const AcuValue *args, u8 argc, AcuValue *out_ret) {                 \
        (void)argc;                                                                                \
        *out_ret = AcuValue_F32(c_math_fn(args[0].f32));                                           \
        return ACU_VM_OK;                                                                          \
    }

#define DEFINE_MATH_BINARY_F32(fn_name, c_math_fn)                                                 \
    static AcuVmStatus fn_name(const AcuValue *args, u8 argc, AcuValue *out_ret) {                 \
        (void)argc;                                                                                \
        *out_ret = AcuValue_F32(c_math_fn(args[0].f32, args[1].f32));                              \
        return ACU_VM_OK;                                                                          \
    }

DEFINE_MATH_UNARY_F64(AcuSys_MathSqrtF64, sqrt)
DEFINE_MATH_UNARY_F64(AcuSys_MathCbrtF64, cbrt)
DEFINE_MATH_BINARY_F64(AcuSys_MathPowF64, pow)
DEFINE_MATH_UNARY_F64(AcuSys_MathSinF64, sin)
DEFINE_MATH_UNARY_F64(AcuSys_MathCosF64, cos)
DEFINE_MATH_UNARY_F64(AcuSys_MathTanF64, tan)
DEFINE_MATH_UNARY_F64(AcuSys_MathAsinF64, asin)
DEFINE_MATH_UNARY_F64(AcuSys_MathAcosF64, acos)
DEFINE_MATH_UNARY_F64(AcuSys_MathAtanF64, atan)
DEFINE_MATH_BINARY_F64(AcuSys_MathAtan2F64, atan2)
DEFINE_MATH_UNARY_F64(AcuSys_MathExpF64, exp)
DEFINE_MATH_UNARY_F64(AcuSys_MathLogF64, log)
DEFINE_MATH_UNARY_F64(AcuSys_MathLog2F64, log2)
DEFINE_MATH_UNARY_F64(AcuSys_MathLog10F64, log10)
DEFINE_MATH_UNARY_F64(AcuSys_MathFloorF64, floor)
DEFINE_MATH_UNARY_F64(AcuSys_MathCeilF64, ceil)
DEFINE_MATH_UNARY_F64(AcuSys_MathRoundF64, round)
DEFINE_MATH_UNARY_F64(AcuSys_MathAbsF64, fabs)
DEFINE_MATH_BINARY_F64(AcuSys_MathHypotF64, hypot)

DEFINE_MATH_UNARY_F32(AcuSys_MathSqrtF32, sqrtf)
DEFINE_MATH_UNARY_F32(AcuSys_MathCbrtF32, cbrtf)
DEFINE_MATH_BINARY_F32(AcuSys_MathPowF32, powf)
DEFINE_MATH_UNARY_F32(AcuSys_MathSinF32, sinf)
DEFINE_MATH_UNARY_F32(AcuSys_MathCosF32, cosf)
DEFINE_MATH_UNARY_F32(AcuSys_MathTanF32, tanf)
DEFINE_MATH_UNARY_F32(AcuSys_MathAsinF32, asinf)
DEFINE_MATH_UNARY_F32(AcuSys_MathAcosF32, acosf)
DEFINE_MATH_UNARY_F32(AcuSys_MathAtanF32, atanf)
DEFINE_MATH_BINARY_F32(AcuSys_MathAtan2F32, atan2f)
DEFINE_MATH_UNARY_F32(AcuSys_MathExpF32, expf)
DEFINE_MATH_UNARY_F32(AcuSys_MathLogF32, logf)
DEFINE_MATH_UNARY_F32(AcuSys_MathLog2F32, log2f)
DEFINE_MATH_UNARY_F32(AcuSys_MathLog10F32, log10f)
DEFINE_MATH_UNARY_F32(AcuSys_MathFloorF32, floorf)
DEFINE_MATH_UNARY_F32(AcuSys_MathCeilF32, ceilf)
DEFINE_MATH_UNARY_F32(AcuSys_MathRoundF32, roundf)
DEFINE_MATH_UNARY_F32(AcuSys_MathAbsF32, fabsf)
DEFINE_MATH_BINARY_F32(AcuSys_MathHypotF32, hypotf)

const AcuSyscallInfo g_acu_syscall_table[ACU_SYSCALL_COUNT] = {
#define X(id, mod, name_str, fn, p, term)                                                          \
    [id] = {                                                                                       \
        .handler = (fn),                                                                           \
        .module = (mod),                                                                           \
        .name = (name_str),                                                                        \
        .purity = (p),                                                                             \
        .is_terminator = (term),                                                                   \
    },
    ACU_SYSCALL_LIST(X)
#undef X
};
