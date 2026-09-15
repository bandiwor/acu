#pragma once

#include "defines/defines.h"

#define X_ACU_LITERAL_KIND(X)                                                                      \
    X(ACU_LITERAL_I64, "i64")                                                                      \
    X(ACU_LITERAL_U64, "u64")                                                                      \
    X(ACU_LITERAL_F64, "f64")                                                                      \
    X(ACU_LITERAL_STR, "str")

#define X_ACU_INT_SUFFIX(X)                                                                        \
    X(ACU_INT_SUFFIX_NONE, "none")                                                                 \
    X(ACU_INT_SUFFIX_I1, "i1")                                                                     \
    X(ACU_INT_SUFFIX_I8, "i8")                                                                     \
    X(ACU_INT_SUFFIX_I16, "i16")                                                                   \
    X(ACU_INT_SUFFIX_I32, "i32")                                                                   \
    X(ACU_INT_SUFFIX_I64, "i64")                                                                   \
    X(ACU_INT_SUFFIX_U8, "u8")                                                                     \
    X(ACU_INT_SUFFIX_U16, "u16")                                                                   \
    X(ACU_INT_SUFFIX_U32, "u32")                                                                   \
    X(ACU_INT_SUFFIX_U64, "u64")

#define X_ACU_FLOAT_SUFFIX(X)                                                                      \
    X(ACU_FLOAT_SUFFIX_NONE, "none")                                                               \
    X(ACU_FLOAT_SUFFIX_F32, "f32")                                                                 \
    X(ACU_FLOAT_SUFFIX_F64, "f64")

typedef enum {
#define X(name, text) name,
    X_ACU_LITERAL_KIND(X)
#undef X
} AcuLiteralKind;

attribute_const const u8 *AcuLiteralKind_String(AcuLiteralKind);

typedef enum {
#define X(name, text) name,
    X_ACU_INT_SUFFIX(X)
#undef X
} AcuIntSuffix;

attribute_const const u8 *AcuIntSuffix_String(AcuIntSuffix);

typedef enum {
#define X(name, text) name,
    X_ACU_FLOAT_SUFFIX(X)
#undef X
} AcuFloatSuffix;

attribute_const const u8 *AcuFloatSuffix_String(AcuFloatSuffix);

typedef struct {
    u16 kind;
    u16 suffix;

    union {
        i64 i64;
        u64 u64;
        f64 f64;

        struct {
            u32 offset;
            u32 length;
        } str;
    } as;
} AcuLiteral;
