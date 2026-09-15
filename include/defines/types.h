#pragma once
#include "common/panic.h"
#include "defines/defines.h"
#include "lexer/token.h"
#include "literal/literal.h"

#define X_TYPE_PRIMITIVE_KIND(X)                                                                   \
    X(TYPE_PRIMITIVE_UNIT, "()")                                                                   \
    X(TYPE_PRIMITIVE_I1, "i1")                                                                     \
    X(TYPE_PRIMITIVE_I8, "i8")                                                                     \
    X(TYPE_PRIMITIVE_I16, "i16")                                                                   \
    X(TYPE_PRIMITIVE_I32, "i32")                                                                   \
    X(TYPE_PRIMITIVE_I64, "i64")                                                                   \
    X(TYPE_PRIMITIVE_U8, "u8")                                                                     \
    X(TYPE_PRIMITIVE_U16, "u16")                                                                   \
    X(TYPE_PRIMITIVE_U32, "u32")                                                                   \
    X(TYPE_PRIMITIVE_U64, "u64")                                                                   \
    X(TYPE_PRIMITIVE_F32, "f32")                                                                   \
    X(TYPE_PRIMITIVE_F64, "f64")                                                                   \
    X(TYPE_PRIMITIVE_STR, "str")                                                                   \
    X(TYPE_PRIMITIVE_NEVER, "never")                                                               \
    X(TYPE_PRIMITIVE_MODULE, "mod")

typedef enum {
#define X(name, text) name,
    X_TYPE_PRIMITIVE_KIND(X)
#undef X
} TypePrimitiveKind;

attribute_const const u8 *TypePrimitiveKind_String(TypePrimitiveKind);

attribute_const TypePrimitiveKind TypePrimitiveKind_FromTokenType(AcuTokenType);
attribute_const TypePrimitiveKind TypePrimitiveKind_FromExplicitIntSuffix(AcuIntSuffix);
attribute_const TypePrimitiveKind TypePrimitiveKind_FromExplicitFloatSuffix(AcuFloatSuffix suffix);

static inline attribute_const bool TypePrimitiveKind_IsInteger(TypePrimitiveKind kind) {
    switch (kind) {
        case TYPE_PRIMITIVE_I8:
        case TYPE_PRIMITIVE_I16:
        case TYPE_PRIMITIVE_I32:
        case TYPE_PRIMITIVE_I64:
        case TYPE_PRIMITIVE_U8:
        case TYPE_PRIMITIVE_U16:
        case TYPE_PRIMITIVE_U32:
        case TYPE_PRIMITIVE_U64:
            return true;
        default:
            return false;
    }
}

static inline attribute_const bool TypePrimitiveKind_IsFloat(TypePrimitiveKind kind) {
    switch (kind) {
        case TYPE_PRIMITIVE_F32:
        case TYPE_PRIMITIVE_F64:
            return true;
        default:
            return false;
    }
}

static inline attribute_const bool TypePrimitiveKind_IsSignedInt(TypePrimitiveKind kind) {
    return (bool)(kind >= TYPE_PRIMITIVE_I1 && kind <= TYPE_PRIMITIVE_I64);
}

static inline attribute_const bool TypePrimitiveKind_IsUnsignedInt(TypePrimitiveKind kind) {
    return (bool)(kind >= TYPE_PRIMITIVE_U8 && kind <= TYPE_PRIMITIVE_U64);
}

static inline attribute_const u32 TypePrimitiveKind_GetIntBitWidth(TypePrimitiveKind kind) {
    if (unlikely(!(kind >= TYPE_PRIMITIVE_I1 && kind <= TYPE_PRIMITIVE_U64))) {
        acu_unreachable_debug("kind (%d) is not valid int kind", kind);
    }

    if (kind == TYPE_PRIMITIVE_I1) {
        return 1;
    }

    const bool is_signed = (bool)(kind >= TYPE_PRIMITIVE_I1 && kind <= TYPE_PRIMITIVE_I64);
    if (is_signed) {
        const unsigned bits = kind - TYPE_PRIMITIVE_I8;
        return 8 * (1 << bits);
    }

    const unsigned bits = kind - TYPE_PRIMITIVE_U8;
    return 8 * (1 << bits);
}
