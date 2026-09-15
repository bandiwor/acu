#pragma once
#include "defines/defines.h"

typedef union {
    struct {
        u32 offset;
        u32 length;
    } slice;
    u64 raw;
} StringSlice;

static inline StringSlice StringSlice_Pack(u32 offset, u32 length) {
    return (StringSlice){.slice = {
                             .offset = offset,
                             .length = length,
                         }};
}
