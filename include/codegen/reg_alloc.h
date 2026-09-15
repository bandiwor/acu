#pragma once

#include "defines/bytecode.h"
#include "defines/defines.h"
#include <string.h>

typedef struct {
    u64 bits[4];
} AcuRegSet;

static inline void AcuRegSet_Init(AcuRegSet *set) {
    memset(set->bits, 0, sizeof(set->bits));
}

static inline AcuReg AcuRegSet_Alloc(AcuRegSet *set) {
    for (u32 i = 0; i < 4; ++i) {
        if (~set->bits[i]) {
            u32 bit = (u32)__builtin_ctzll(~set->bits[i]);
            set->bits[i] |= (1ULL << bit);
            return (AcuReg)((i * 64) + bit);
        }
    }
    return 0;
}

static inline void AcuRegSet_Free(AcuRegSet *set, AcuReg r) {
    set->bits[r / 64] &= ~(1ULL << (r % 64));
}

static inline void AcuRegSet_MarkUsed(AcuRegSet *set, AcuReg r) {
    set->bits[r / 64] |= (1ULL << (r % 64));
}

static inline int AcuRegSet_GetHighestUsed(const AcuRegSet *set) {
    for (int i = 3; i >= 0; --i) {
        if (set->bits[i] != 0) {
            return (i * 64) + (63 - __builtin_clzll(set->bits[i]));
        }
    }
    return -1;
}

static inline bool AcuRegSet_AllocContiguous(AcuRegSet *set, u32 count, AcuReg *out_base) {
    if (count == 0) {
        *out_base = 0;
        return true;
    }
    if (count > 256)
        return false;

    for (u32 base = 0; base <= 256 - count; ++base) {
        bool free_range = true;
        for (u32 i = 0; i < count; ++i) {
            u32 r = base + i;
            if ((set->bits[r / 64] & (1ULL << (r % 64))) != 0) {
                free_range = false;
                base += i;
                break;
            }
        }

        if (free_range) {
            for (u32 i = 0; i < count; ++i) {
                u32 r = base + i;
                set->bits[r / 64] |= (1ULL << (r % 64));
            }
            *out_base = (AcuReg)base;
            return true;
        }
    }
    return false;
}
