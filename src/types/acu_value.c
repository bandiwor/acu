#include "types/acu_value.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline int acu_ctz64(u64 val) {
    unsigned long idx;
    _BitScanForward64(&idx, val);
    return (int)idx;
}
#define acu_unlikely(x) (x)
#else
static inline int acu_ctz64(u64 val) {
    return __builtin_ctzll(val);
}
#endif

attribute_const AcuValue AcuValue_PowU64(AcuValue a, AcuValue b) {
    u64 exp = b.u64;

    if (unlikely(exp == 0)) {
        return AcuValue_U64(1);
    }

    u64 base = a.u64;

    if (unlikely(base <= 2)) {
        if (base == 2) {
            return AcuValue_U64((exp < 64) ? (1ULL << exp) : 0);
        }
        return AcuValue_U64(base);
    }

    if ((exp & 1) == 0) {
        int tz = acu_ctz64(exp);
        exp >>= tz;
        do {
            base *= base;
        } while (--tz);
    }

    u64 res = base;

    while (exp >>= 1) {
        base *= base;
        if (exp & 1) {
            res *= base;
        }
    }

    return AcuValue_U64(res);
}
