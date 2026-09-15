#pragma once
#include "defines/defines.h"

static inline i64 IntPow(i64 base, i64 exp) {
    if (exp < 0)
        return 0;
    i64 res = 1;
    while (exp > 0) {
        if (exp & 1)
            res *= base;
        base *= base;
        exp >>= 1;
    }
    return res;
}

static inline bool AcuIsPowerOfTwoU64(u64 x) {
    return x > 0 && (x & (x - 1)) == 0;
}

static inline u32 AcuLog2U64(u64 x) {
    return (u32)__builtin_ctzll(x);
}
