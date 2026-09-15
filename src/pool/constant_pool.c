#include "pool/constant_pool.h"
#include "memory/vector.h"
#include <stddef.h>

static inline u64 AcuHash_U64(u64 x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
}

AcuConstantPool AcuConstantPool_Create(void) {
    AcuConstantPool pool = {0};
    AcuConstantPool_Init(&pool);
    return pool;
}

void AcuConstantPool_Init(AcuConstantPool *pool) {
    V_Init(&pool->constants);
    V_InitWithCapacity(&pool->hash_table, 64);

    const AcuConstSlot empty_slot = {.hash = 0, .const_idx = CONST_NULL_ID};
    for (u32 i = 0; i < V_Capacity(&pool->hash_table); i++) {
        V_Push(&pool->hash_table, empty_slot);
    }

    pool->count = 0;
}

void AcuConstantPool_Free(AcuConstantPool *pool) {
    V_Free(&pool->constants);
    V_Free(&pool->hash_table);
}

static void AcuConstantPool_Rehash(AcuConstantPool *pool) {
    const u32 old_cap = V_Capacity(&pool->hash_table);
    const u32 new_cap = old_cap * 2;
    const u32 mask = new_cap - 1;

    TypedVector(AcuConstSlot) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);

    const AcuConstSlot empty_slot = {.hash = 0, .const_idx = CONST_NULL_ID};
    for (u32 i = 0; i < new_cap; i++) {
        V_Push(&new_ht, empty_slot);
    }

    const AcuConstSlot *const old_elements = V_Elements(&pool->hash_table);
    AcuConstSlot *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        AcuConstSlot slot = old_elements[i];
        if (slot.const_idx != CONST_NULL_ID) {
            u32 idx = (u32)(slot.hash & mask);

            while (new_elements[idx].const_idx != CONST_NULL_ID) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }

    V_Free(&pool->hash_table);
    pool->hash_table.base = new_ht.base;
}

ConstId AcuConstantPool_Intern(AcuConstantPool *pool, u64 raw_value) {
    u64 hash = AcuHash_U64(raw_value);
    u32 cap = V_Count(&pool->hash_table);
    u32 mask = cap - 1;
    u32 idx = (u32)(hash & mask);

    AcuConstSlot *ht = V_Elements(&pool->hash_table);
    const u64 *constants = V_Elements(&pool->constants);

    while (ht[idx].const_idx != CONST_NULL_ID) {
        if (ht[idx].hash == hash) {
            ConstId const_idx = ht[idx].const_idx;
            if (constants[const_idx] == raw_value) {
                return const_idx;
            }
        }
        idx = (idx + 1) & mask;
    }

    if (unlikely(pool->count * 4 >= cap * 3)) {
        AcuConstantPool_Rehash(pool);

        cap = V_Count(&pool->hash_table);
        mask = cap - 1;
        idx = (u32)(hash & mask);

        ht = V_Elements(&pool->hash_table);

        while (ht[idx].const_idx != CONST_NULL_ID) {
            idx = (idx + 1) & mask;
        }
    }

    ConstId new_id = (ConstId)V_Count(&pool->constants);
    V_Push(&pool->constants, raw_value);

    ht[idx].hash = hash;
    ht[idx].const_idx = new_id;
    pool->count++;

    return new_id;
}

u64 AcuConstantPool_Get(const AcuConstantPool *pool, ConstId id) {
    if (unlikely(id >= V_Count(&pool->constants))) {
        return 0;
    }
    return V_Elements(&pool->constants)[id];
}

u32 AcuConstantPool_GetCount(const AcuConstantPool *pool) {
    return pool->count;
}

ConstId AcuConstantPool_InternI64(AcuConstantPool *pool, i64 val) {
    return AcuConstantPool_Intern(pool, (u64)val);
}

ConstId AcuConstantPool_InternU64(AcuConstantPool *pool, u64 val) {
    return AcuConstantPool_Intern(pool, val);
}

ConstId AcuConstantPool_InternF64(AcuConstantPool *pool, f64 val) {
    union {
        f64 f;
        u64 u;
    } pun = {.f = val};
    return AcuConstantPool_Intern(pool, pun.u);
}

ConstId AcuConstantPool_InternF32(AcuConstantPool *pool, f32 val) {
    union {
        f32 f;
        u32 u;
    } pun = {.f = val};
    return AcuConstantPool_Intern(pool, (u64)pun.u);
}

i64 AcuConstantPool_GetI64(const AcuConstantPool *pool, ConstId id) {
    return (i64)AcuConstantPool_Get(pool, id);
}

u64 AcuConstantPool_GetU64(const AcuConstantPool *pool, ConstId id) {
    return AcuConstantPool_Get(pool, id);
}

f64 AcuConstantPool_GetF64(const AcuConstantPool *pool, ConstId id) {
    union {
        u64 u;
        f64 f;
    } pun = {.u = AcuConstantPool_Get(pool, id)};
    return pun.f;
}

f32 AcuConstantPool_GetF32(const AcuConstantPool *pool, ConstId id) {
    union {
        u32 u;
        f32 f;
    } pun = {.u = (u32)AcuConstantPool_Get(pool, id)};
    return pun.f;
}

const void *AcuConstantPool_RawData(const AcuConstantPool *pool) {
    return V_Elements(&pool->constants);
}

u32 AcuConstantPool_RawDataLength(const AcuConstantPool *pool) {
    return V_Count(&pool->constants);
}
