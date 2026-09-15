#pragma once

#include "defines/defines.h"
#include "memory/arena.h"
#include <stdbool.h>

#define SV(literal_str)                                                                            \
    ((StringView){.data = (const u8 *)(literal_str), .length = sizeof(literal_str) - 1})

#define SV_Fmt ".*s"
#define SV_Arg(sv) (int)(sv).length, (const char *)(sv).data

typedef struct {
    const u8 *data;
    u64 length;
} StringView;

StringView StringView_Create(const u8 *data, u64 length);

attribute_pure bool StringView_Equals(StringView a, StringView b);

attribute_pure bool StringView_StartsWith(StringView sv, StringView prefix);

attribute_pure bool StringView_EndsWith(StringView sv, StringView suffix);

attribute_pure StringView StringView_Substr(StringView sv, u64 start, u64 length);

attribute_pure StringView StringView_ChopLeft(StringView sv, u64 n);

attribute_pure StringView StringView_ChopRight(StringView sv, u64 n);

attribute_pure StringView StringView_Trim(StringView sv);

bool StringView_Split(StringView *sv, u8 delimiter, StringView *out_part);

attribute_pure u64 StringView_Hash(StringView sv);

attribute_nonnull(1) StringView StringView_CStr(const char *c_str);

attribute_nonnull(1, 2) StringView StringView_FromCStr(AcuArena *arena, const char *c_str);

attribute_nonnull(1, 2) StringView
    StringView_FromBuffer(AcuArena *arena, const u8 *data, u64 length);

attribute_nonnull(1) StringView StringView_Clone(AcuArena *arena, StringView other);

attribute_const StringView StringView_Empty(void);
