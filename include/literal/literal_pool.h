#pragma once

#include "defines/defines.h"
#include "literal/literal.h"
#include "memory/vector.h"
#include "types/string_view.h"

typedef TypedVector(AcuLiteral) AcuLiteralVector;
typedef TypedVector(u8) AcuLiteralPoolBlobVector;

typedef struct {
    AcuLiteralVector items;
    AcuLiteralPoolBlobVector string_blob;
} AcuLiteralPool;

AcuLiteralPool AcuLiteralPool_Create(void);

void AcuLiteralPool_Init(AcuLiteralPool *pool);
void AcuLiteralPool_Free(AcuLiteralPool *pool);

LiteralIdx AcuLiteralPool_PushInt(AcuLiteralPool *pool, i64 value, AcuIntSuffix suffix);
LiteralIdx AcuLiteralPool_PushUint(AcuLiteralPool *pool, u64 value, AcuIntSuffix suffix);
LiteralIdx AcuLiteralPool_PushFloat(AcuLiteralPool *pool, f64 value, AcuFloatSuffix suffix);
LiteralIdx AcuLiteralPool_PushString(AcuLiteralPool *pool, StringView sv);

StringView AcuLiteralPool_GetString(AcuLiteralPool *pool, LiteralIdx idx);

#define AcuLiteralPool_Get(pool, idx) (&V_Elements(&(pool)->items)[idx])
