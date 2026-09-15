#include "defines/types.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "literal/literal.h"

#define TYPE_PRIMITIVE_KIND_OFFSET_TOKEN (TYPE_PRIMITIVE_I1 - ACU_TOKEN_I1_TYPE)
#define TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX (TYPE_PRIMITIVE_I1 - ACU_INT_SUFFIX_I1)

_Static_assert((ACU_TOKEN_I1_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_I1, "");
_Static_assert((ACU_TOKEN_I8_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_I8, "");
_Static_assert((ACU_TOKEN_I16_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_I16, "");
_Static_assert((ACU_TOKEN_I32_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_I32, "");
_Static_assert((ACU_TOKEN_I64_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_I64, "");
_Static_assert((ACU_TOKEN_U8_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_U8, "");
_Static_assert((ACU_TOKEN_U16_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_U16, "");
_Static_assert((ACU_TOKEN_U32_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_U32, "");
_Static_assert((ACU_TOKEN_U64_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_U64, "");
_Static_assert((ACU_TOKEN_F32_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_F32, "");
_Static_assert((ACU_TOKEN_F64_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_F64, "");
_Static_assert((ACU_TOKEN_STR_TYPE + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN) == TYPE_PRIMITIVE_STR, "");

_Static_assert((ACU_INT_SUFFIX_I1 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_I1, "");
_Static_assert((ACU_INT_SUFFIX_I8 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_I8, "");
_Static_assert((ACU_INT_SUFFIX_I16 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_I16, "");
_Static_assert((ACU_INT_SUFFIX_I32 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_I32, "");
_Static_assert((ACU_INT_SUFFIX_I64 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_I64, "");
_Static_assert((ACU_INT_SUFFIX_U8 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_U8, "");
_Static_assert((ACU_INT_SUFFIX_U16 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_U16, "");
_Static_assert((ACU_INT_SUFFIX_U32 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_U32, "");
_Static_assert((ACU_INT_SUFFIX_U64 + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX) == TYPE_PRIMITIVE_U64, "");

attribute_const TypePrimitiveKind TypePrimitiveKind_FromTokenType(AcuTokenType type) {
    if (unlikely(!(type >= ACU_TOKEN_I1_TYPE && type <= ACU_TOKEN_STR_TYPE))) {
        acu_unreachable_debug("token type (%d) is not primitive kind", type);
    }

    return type + TYPE_PRIMITIVE_KIND_OFFSET_TOKEN;
}

attribute_const const u8 *TypePrimitiveKind_String(TypePrimitiveKind kind) {
#define X(name, text)                                                                              \
    case name:                                                                                     \
        return (const u8 *)(text);

    switch (kind) {
        X_TYPE_PRIMITIVE_KIND(X)
        default:
            acu_unreachable_debug("Unknown TypePrimitiveKind: %d\n", kind);
    }
#undef X
}

attribute_const TypePrimitiveKind TypePrimitiveKind_FromExplicitIntSuffix(AcuIntSuffix suffix) {
    if (unlikely(suffix == ACU_INT_SUFFIX_NONE || suffix > ACU_INT_SUFFIX_U64)) {
        acu_unreachable_debug("suffix (%d) is not valid explicit int suffix", suffix);
    }

    return suffix + TYPE_PRIMITIVE_KIND_OFFSET_SUFFIX;
}

attribute_const TypePrimitiveKind TypePrimitiveKind_FromExplicitFloatSuffix(AcuFloatSuffix suffix) {
    if (unlikely(suffix == ACU_FLOAT_SUFFIX_NONE || suffix > ACU_FLOAT_SUFFIX_F64)) {
        acu_unreachable_debug("suffix (%d) is not valid explicit float suffix", suffix);
    }

    return suffix == ACU_FLOAT_SUFFIX_F32 ? TYPE_PRIMITIVE_F32 : TYPE_PRIMITIVE_F64;
}

_Static_assert(TYPE_PRIMITIVE_I16 - TYPE_PRIMITIVE_I8 == 1, "");
_Static_assert(TYPE_PRIMITIVE_I32 - TYPE_PRIMITIVE_I16 == 1, "");
_Static_assert(TYPE_PRIMITIVE_I64 - TYPE_PRIMITIVE_I32 == 1, "");
_Static_assert(TYPE_PRIMITIVE_U16 - TYPE_PRIMITIVE_U8 == 1, "");
_Static_assert(TYPE_PRIMITIVE_U32 - TYPE_PRIMITIVE_U16 == 1, "");
_Static_assert(TYPE_PRIMITIVE_U64 - TYPE_PRIMITIVE_U32 == 1, "");
