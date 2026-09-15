#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif // !NULL

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define attribute_hot __attribute__((hot))
#define attribute_cold __attribute__((cold))
#define attribute_pure __attribute__((pure))
#define attribute_const __attribute__((const))
#define attribute_noinline __attribute__((noinline))
#define attribute_always_inline __attribute__((always_inline))
#define attribute_unused __attribute__((unused))
#define attribute_nonnull(...) __attribute__((nonnull(__VA_ARGS__)))
#define attribute_returns_nonnull __attribute__((returns_nonnull))
#define attribute_malloc __attribute__((malloc))
#define attribute_nodiscard __attribute__((warn_unused_result))
#define attribute_noreturn __attribute__((noreturn))
#define attribute_alloc_size(x) __attribute__((alloc_size(x)))
#define attribute_format_printf(fmt_idx, first_idx)                                                \
    __attribute__((format(printf, fmt_idx, first_idx)))
#define attribute_nullable __attribute__((_Nullable))
#define attribute_aligned(x) __attribute__((aligned(x)))
#define attribute_alloc_align(x) __attribute__((alloc_align(x)))
#define assume_aligned(x, align) (__builtin_assume_aligned(x, align))
#define attribute_format(archetype, string_idx, first_to_check)                                    \
    __attribute__((format(archetype, string_idx, first_to_check)))

#ifdef NDEBUG
#define attribute_const_release attribute_const
#else
#define attribute_const_release
#endif

#define WRAPPER_START do {
#define WRAPPER_END                                                                                \
    }                                                                                              \
    while (0)

#define ALIGN_UP(addr, size)                                                                       \
    ((__typeof__(addr))(((uintptr_t)(addr) + (uintptr_t)(size) - 1) & ~((uintptr_t)(size) - 1)))

#define ALIGN_DOWN(addr, size) ((addr) & ~((size) - 1))

#define COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef int8_t i1;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef u32 AstNodeIdx;
typedef u32 ExtraIdx;
typedef u32 TypeAstNodeIdx;
typedef u32 AstPayloadIdx;
typedef u32 LiteralIdx;
typedef u32 ScopeId;
typedef u32 TypeId;
typedef u32 FileId;
typedef u32 StringId;
typedef u32 SymbolId;
typedef u32 ModuleId;

typedef float f32;
typedef double f64;

#define ACU_NULL_IDX ((u32) - 1)
#define IS_ACU_NULL_IDX(idx) (ACU_NULL_IDX == (idx))
