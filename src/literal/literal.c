#include "literal/literal.h"
#include "common/panic.h"

attribute_const const u8 *AcuLiteralKind_String(AcuLiteralKind kind) {
#define X(name, text)                                                                              \
    case name:                                                                                     \
        return (const u8 *)(text);

    switch (kind) {
        X_ACU_LITERAL_KIND(X)

        default:
            acu_unreachable_debug("Unknown AcuLiteralKind: %d\n", kind);
    }
#undef X
}

attribute_const const u8 *AcuIntSuffix_String(AcuIntSuffix suffix) {
#define X(name, text)                                                                              \
    case name:                                                                                     \
        return (const u8 *)(text);

    switch (suffix) {
        X_ACU_INT_SUFFIX(X)

        default:
            acu_unreachable_debug("Unknown AcuIntSuffix: %d\n", suffix);
    }
#undef X
}

attribute_const const u8 *AcuFloatSuffix_String(AcuFloatSuffix suffix) {
#define X(name, text)                                                                              \
    case name:                                                                                     \
        return (const u8 *)(text);

    switch (suffix) {
        X_ACU_FLOAT_SUFFIX(X)

        default:
            acu_unreachable_debug("Unknown AcuFloatSuffix: %d\n", suffix);
    }
#undef X
}
