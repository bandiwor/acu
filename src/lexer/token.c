#include "lexer/token.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "types/string_view.h"

const u8 *AcuTokenType_String(const AcuTokenType type) {
#define X(name, msg, prec)                                                                         \
    case name:                                                                                     \
        return (const u8 *)(msg);

    switch (type) {
        X_ACU_TOKEN_TYPE(X)
        default:
            acu_unreachable_debug("Unknown AcuTokenType: %d\n", type);
    }
#undef X
}

attribute_const StringView AcuToken_GetStringView(AcuToken token, StringView source) {
    if (unlikely((u64)token.offset + token.length > source.length)) {
        acu_unreachable_debug("Token out of bounds: offset %u, length %u, source length %llu",
                              token.offset, token.length, (unsigned long long)source.length);
    }

    return (StringView){.data = source.data + token.offset, .length = token.length};
}

attribute_const_release StringView AcuToken_GetStringText(AcuToken token, StringView source) {
    const StringView sv = AcuToken_GetStringView(token, source);

    if (unlikely(!(sv.length >= 2 && sv.data[0] == '"' && sv.data[sv.length - 1] == '"'))) {
        acu_unreachable_debug("Invalid string");
    }

    return (StringView){
        .data = sv.data + 1,
        .length = sv.length - 2,
    };
}
