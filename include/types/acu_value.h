#pragma once
#include "defines/defines.h"
#include "types/string_slice.h"

typedef union {
    u64 raw;
    f32 f32;
    f64 f64;
    i64 i64;
    u64 u64;
    StringSlice string_slice;
} AcuValue;

attribute_const AcuValue AcuValue_PowU64(AcuValue a, AcuValue b);

static inline AcuValue AcuValue_Zero(void) {
    return (AcuValue){.raw = 0};
}

static inline AcuValue AcuValue_I64(i64 val) {
    return (AcuValue){.i64 = val};
}

static inline AcuValue AcuValue_U64(u64 val) {
    return (AcuValue){.u64 = val};
}

static inline AcuValue AcuValue_F64(f64 val) {
    return (AcuValue){.f64 = val};
}

static inline AcuValue AcuValue_F32(f32 val) {
    return (AcuValue){.f32 = val};
}
