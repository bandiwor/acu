#include "literal/literal_pool.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "literal/literal.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <stddef.h>

AcuLiteralPool AcuLiteralPool_Create(void) {
    AcuLiteralPool pool = {0};
    AcuLiteralPool_Init(&pool);
    return pool;
}

void AcuLiteralPool_Init(AcuLiteralPool *pool) {
    V_Init(&pool->items);
    V_Init(&pool->string_blob);
}

void AcuLiteralPool_Free(AcuLiteralPool *pool) {
    V_Free(&pool->items);
    V_Free(&pool->string_blob);
}

LiteralIdx AcuLiteralPool_PushInt(AcuLiteralPool *pool, i64 value, AcuIntSuffix suffix) {
    AcuLiteral lit = {.kind = ACU_LITERAL_I64, .suffix = suffix, .as.i64 = value};
    LiteralIdx idx = V_Count(&pool->items);
    V_Push(&pool->items, lit);
    return idx;
}

LiteralIdx AcuLiteralPool_PushUint(AcuLiteralPool *pool, u64 value, AcuIntSuffix suffix) {
    AcuLiteral lit = {.kind = ACU_LITERAL_U64, .suffix = suffix, .as.u64 = value};
    LiteralIdx idx = V_Count(&pool->items);
    V_Push(&pool->items, lit);
    return idx;
}

LiteralIdx AcuLiteralPool_PushFloat(AcuLiteralPool *pool, f64 value, AcuFloatSuffix suffix) {
    AcuLiteral lit = {.kind = ACU_LITERAL_F64, .suffix = suffix, .as.f64 = value};
    LiteralIdx idx = V_Count(&pool->items);
    V_Push(&pool->items, lit);
    return idx;
}

LiteralIdx AcuLiteralPool_PushString(AcuLiteralPool *pool, StringView sv) {
    u32 start = V_Count(&pool->string_blob);
    V_EnsureCapacity(&pool->string_blob, start + sv.length);
    __builtin_memcpy(&V_Elements(&pool->string_blob)[start], sv.data, sv.length);
    V_AddCount(&pool->string_blob, sv.length);

    AcuLiteral lit = {0};
    lit.kind = ACU_LITERAL_STR;
    lit.as.str.offset = start;
    lit.as.str.length = sv.length;

    LiteralIdx idx = V_Count(&pool->items);
    V_Push(&pool->items, lit);
    return idx;
}

StringView AcuLiteralPool_GetString(AcuLiteralPool *pool, LiteralIdx idx) {
    AcuLiteral *literal = AcuLiteralPool_Get(pool, idx);

    if (unlikely(literal->kind != ACU_LITERAL_STR)) {
        acu_unreachable_debug("literal kind is not a string (%d)", literal->kind);
    }

    return (StringView){
        .data = V_Elements(&pool->string_blob) + literal->as.str.offset,
        .length = literal->as.str.length,
    };
}
