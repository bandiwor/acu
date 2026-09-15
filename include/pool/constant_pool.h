#pragma once

#include "defines/defines.h"
#include "memory/vector.h"

typedef u16 ConstId;
#define CONST_NULL_ID ((ConstId)0xFFFF)

typedef struct {
    u64 hash;
    ConstId const_idx;
} AcuConstSlot;

typedef struct {
    TypedVector(u64) constants;
    TypedVector(AcuConstSlot) hash_table;
    u32 count;
} AcuConstantPool;

AcuConstantPool AcuConstantPool_Create(void);
void AcuConstantPool_Init(AcuConstantPool *pool);
void AcuConstantPool_Free(AcuConstantPool *pool);

ConstId AcuConstantPool_Intern(AcuConstantPool *pool, u64 raw_value);
u64 AcuConstantPool_Get(const AcuConstantPool *pool, ConstId id);
u32 AcuConstantPool_GetCount(const AcuConstantPool *pool);

ConstId AcuConstantPool_InternI64(AcuConstantPool *pool, i64 val);
ConstId AcuConstantPool_InternU64(AcuConstantPool *pool, u64 val);
ConstId AcuConstantPool_InternF64(AcuConstantPool *pool, f64 val);
ConstId AcuConstantPool_InternF32(AcuConstantPool *pool, f32 val);
i64 AcuConstantPool_GetI64(const AcuConstantPool *pool, ConstId id);
u64 AcuConstantPool_GetU64(const AcuConstantPool *pool, ConstId id);
f64 AcuConstantPool_GetF64(const AcuConstantPool *pool, ConstId id);
f32 AcuConstantPool_GetF32(const AcuConstantPool *pool, ConstId id);

const void *AcuConstantPool_RawData(const AcuConstantPool *pool);
u32 AcuConstantPool_RawDataLength(const AcuConstantPool *pool);
