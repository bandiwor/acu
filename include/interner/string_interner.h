#pragma once

#include "defines/defines.h"
#include "memory/vector.h"
#include "types/string_view.h"

typedef struct {
    u32 offset;
    u32 length;
} AcuInternedString;

typedef struct {
    TypedVector(u8) char_data;
    TypedVector(AcuInternedString) strings;
    TypedVector(u64) hash_table;
    u32 count;
} AcuStringInterner;

AcuStringInterner AcuStringInterner_Create(void);

void AcuStringInterner_Init(AcuStringInterner *interner);
void AcuStringInterner_Free(AcuStringInterner *interner);

StringId AcuStringInterner_Intern(AcuStringInterner *interner, StringView sv);

StringView AcuStringInterner_GetString(AcuStringInterner *interner, StringId id);
