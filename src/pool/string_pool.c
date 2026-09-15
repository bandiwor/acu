#include "pool/string_pool.h"
#include "memory/vector.h"

AcuStringPool AcuStringPool_Create(void) {
    AcuStringPool pool = {0};
    AcuStringPool_Init(&pool);
    return pool;
}

void AcuStringPool_Init(AcuStringPool *pool) {
    V_Init(&pool->bytes);
    V_Init(&pool->entries);
    V_InitWithCapacity(&pool->hash_table, 64);

    const AcuStringSlot empty_slot = {.hash = 0, .entry_idx = STRING_CONST_NULL};
    for (u32 i = 0; i < V_Capacity(&pool->hash_table); i++) {
        V_Push(&pool->hash_table, empty_slot);
    }

    // Слот #0 резервируем под пустую строку "" (offset=0, length=0)
    const AcuStringEntry empty_entry = {.offset = 0, .length = 0};
    V_Push(&pool->entries, empty_entry);
    V_Push(&pool->bytes, '\0');

    pool->count = 0;
}

void AcuStringPool_Free(AcuStringPool *pool) {
    V_Free(&pool->bytes);
    V_Free(&pool->entries);
    V_Free(&pool->hash_table);
}

static void AcuStringPool_Rehash(AcuStringPool *pool) {
    const u32 old_cap = V_Capacity(&pool->hash_table);
    const u32 new_cap = old_cap * 2;
    const u32 mask = new_cap - 1;

    TypedVector(AcuStringSlot) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);

    const AcuStringSlot empty_slot = {.hash = 0, .entry_idx = STRING_CONST_NULL};
    for (u32 i = 0; i < new_cap; i++) {
        V_Push(&new_ht, empty_slot);
    }

    const AcuStringSlot *const old_elements = V_Elements(&pool->hash_table);
    AcuStringSlot *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        AcuStringSlot slot = old_elements[i];
        if (slot.entry_idx != STRING_CONST_NULL) {
            u32 idx = (u32)(slot.hash & mask);

            while (new_elements[idx].entry_idx != STRING_CONST_NULL) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }

    V_Free(&pool->hash_table);
    pool->hash_table.base = new_ht.base;
}

StringSlice AcuStringPool_Intern(AcuStringPool *pool, StringView sv) {
    // Пустая строка всегда имеет raw = 0 (offset=0, length=0)
    if (unlikely(sv.length == 0)) {
        return (StringSlice){.raw = 0};
    }

    const u64 hash = StringView_Hash(sv);
    u32 cap = V_Count(&pool->hash_table);
    u32 mask = cap - 1;
    u32 idx = (u32)(hash & mask);

    AcuStringSlot *ht = V_Elements(&pool->hash_table);
    AcuStringEntry *entries = V_Elements(&pool->entries);
    const u8 *bytes = V_Elements(&pool->bytes);

    while (ht[idx].entry_idx != STRING_CONST_NULL) {
        AcuStringSlot slot = ht[idx];

        if (slot.hash == hash) {
            StringConstId entry_idx = slot.entry_idx;
            AcuStringEntry *e = &entries[entry_idx];

            if (e->length == sv.length &&
                __builtin_memcmp(bytes + e->offset, sv.data, sv.length) == 0) {
                return (StringSlice){.slice = {.offset = e->offset, .length = e->length}};
            }
        }
        idx = (idx + 1) & mask;
    }

    // Рехэш при факторе загрузки >= 75%
    if (unlikely(pool->count * 4 >= cap * 3)) {
        AcuStringPool_Rehash(pool);

        cap = V_Count(&pool->hash_table);
        mask = cap - 1;
        idx = (u32)(hash & mask);

        ht = V_Elements(&pool->hash_table);
        entries = V_Elements(&pool->entries);

        while (ht[idx].entry_idx != STRING_CONST_NULL) {
            idx = (idx + 1) & mask;
        }
    }

    const u32 new_offset = V_Count(&pool->bytes);

    Vector_PushBytes(&pool->bytes.base, sv.data, sv.length);
    V_Push(&pool->bytes, '\0');

    const StringConstId new_entry_idx = (StringConstId)V_Count(&pool->entries);
    const AcuStringEntry new_e = {.offset = new_offset, .length = sv.length};
    V_Push(&pool->entries, new_e);

    ht[idx].hash = hash;
    ht[idx].entry_idx = new_entry_idx;
    pool->count++;

    return (StringSlice){.slice = {.offset = new_offset, .length = sv.length}};
}

StringView AcuStringPool_Get(const AcuStringPool *pool, StringSlice slice) {
    if (unlikely(slice.slice.offset + slice.slice.length > V_Count(&pool->bytes))) {
        return StringView_CStr("");
    }

    return StringView_Create(V_Elements(&pool->bytes) + slice.slice.offset, slice.slice.length);
}

StringView AcuStringPool_GetById(const AcuStringPool *pool, StringConstId id) {
    if (unlikely(id >= V_Count(&pool->entries))) {
        return StringView_CStr("");
    }

    const AcuStringEntry *e = &V_Elements(&pool->entries)[id];
    return StringView_Create(V_Elements(&pool->bytes) + e->offset, e->length);
}

StringSlice AcuStringPool_GetSliceById(const AcuStringPool *pool, StringConstId id) {
    if (unlikely(id >= V_Count(&pool->entries))) {
        return (StringSlice){.raw = 0};
    }

    const AcuStringEntry *e = &V_Elements(&pool->entries)[id];
    return (StringSlice){.slice = {.offset = e->offset, .length = e->length}};
}

const void *AcuStringPool_RawData(const AcuStringPool *pool) {
    return V_Elements(&pool->bytes);
}

u32 AcuStringPool_RawDataLength(const AcuStringPool *pool) {
    return V_Count(&pool->bytes);
}
