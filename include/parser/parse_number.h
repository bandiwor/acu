#pragma once

#include "defines/defines.h"
#include "types/string_view.h"
#include <stdbool.h>

typedef struct {
    bool is_float;
    bool is_overflow;
    bool has_suffix;
    bool is_suffix_valid;
    u32 suffix_kind;
    union {
        u64 u64;
        f64 f64;
    } as;
} AcuNumericResult;

AcuNumericResult Acu_ParseNumericLiteral(StringView sv);
