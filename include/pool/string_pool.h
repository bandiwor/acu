#pragma once

#include "defines/defines.h"
#include "memory/vector.h"
#include "types/string_slice.h"
#include "types/string_view.h"

typedef u16 StringConstId;
#define STRING_CONST_NULL ((StringConstId)0xFFFF)

typedef struct {
    u32 offset;
    u32 length;
} AcuStringEntry;

typedef struct {
    u64 hash;
    StringConstId entry_idx;
} AcuStringSlot;

typedef struct {
    TypedVector(u8) bytes;
    TypedVector(AcuStringEntry) entries;
    TypedVector(AcuStringSlot) hash_table;
    u32 count;
} AcuStringPool;

AcuStringPool AcuStringPool_Create(void);
void AcuStringPool_Init(AcuStringPool *pool);
void AcuStringPool_Free(AcuStringPool *pool);

StringSlice AcuStringPool_Intern(AcuStringPool *pool, StringView sv);
StringView AcuStringPool_Get(const AcuStringPool *pool, StringSlice slice);

StringView AcuStringPool_GetById(const AcuStringPool *pool, StringConstId id);
StringSlice AcuStringPool_GetSliceById(const AcuStringPool *pool, StringConstId id);

const void *AcuStringPool_RawData(const AcuStringPool *pool);
u32 AcuStringPool_RawDataLength(const AcuStringPool *pool);
