#include "interner/string_interner.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <stddef.h>

#define ACU_NULL_SLOT 0xFFFFFFFFFFFFFFFFULL

AcuStringInterner AcuStringInterner_Create(void) {
    AcuStringInterner interner = {0};
    AcuStringInterner_Init(&interner);
    return interner;
}

void AcuStringInterner_Init(AcuStringInterner *interner) {
    V_Init(&interner->char_data);
    V_Init(&interner->strings);
    V_InitWithCapacity(&interner->hash_table, 4096 << 1);

    for (u32 i = 0; i < V_Capacity(&interner->hash_table); i++) {
        V_Push(&interner->hash_table, ACU_NULL_SLOT);
    }

    AcuInternedString empty_str = {.offset = 0, .length = 0};
    V_Push(&interner->strings, empty_str);
    V_Push(&interner->char_data, '\0');

    interner->count = 0;
}

void AcuStringInterner_Free(AcuStringInterner *interner) {
    V_Free(&interner->char_data);
    V_Free(&interner->strings);
    V_Free(&interner->hash_table);
}

static void AcuStringInterner_Rehash(AcuStringInterner *interner) {
    const u32 old_cap = V_Capacity(&interner->hash_table);
    const u32 new_cap = old_cap * 2;
    const u32 mask = new_cap - 1;

    TypedVector(u64) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);
    for (u32 i = 0; i < new_cap; i++) {
        V_Push(&new_ht, ACU_NULL_SLOT);
    }

    const u64 *const old_elements = V_Elements(&interner->hash_table);
    u64 *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        u64 slot = old_elements[i];
        if (slot != ACU_NULL_SLOT) {
            const u32 slot_hash = (u32)(slot >> 32);
            u32 idx = slot_hash & mask;

            while (new_elements[idx] != ACU_NULL_SLOT) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }

    V_Free(&interner->hash_table);
    interner->hash_table.base = new_ht.base;
}

StringId AcuStringInterner_Intern(AcuStringInterner *interner, StringView sv) {
    if (unlikely(sv.length == 0)) {
        return 0;
    }

    u32 hash32 = (u32)StringView_Hash(sv);
    u32 cap = V_Count(&interner->hash_table);
    u32 mask = cap - 1;
    u32 idx = hash32 & mask;

    u64 *ht = V_Elements(&interner->hash_table);
    AcuInternedString *strings = V_Elements(&interner->strings);
    const u8 *chars = V_Elements(&interner->char_data);

    while (ht[idx] != ACU_NULL_SLOT) {
        u64 slot = ht[idx];
        u32 slot_hash = (u32)(slot >> 32);

        if (slot_hash == hash32) {
            u32 str_idx = (u32)(slot & 0xFFFFFFFF);
            AcuInternedString *s = &strings[str_idx];

            if (s->length == sv.length &&
                __builtin_memcmp(chars + s->offset, sv.data, sv.length) == 0) {
                return (StringId)str_idx;
            }
        }
        idx = (idx + 1) & mask;
    }

    if (unlikely(interner->count * 4 >= cap * 3)) {
        AcuStringInterner_Rehash(interner);

        cap = V_Count(&interner->hash_table);
        mask = cap - 1;
        idx = hash32 & mask;

        ht = V_Elements(&interner->hash_table);
        strings = V_Elements(&interner->strings);

        while (ht[idx] != ACU_NULL_SLOT) {
            idx = (idx + 1) & mask;
        }
    }

    u32 new_offset = V_Count(&interner->char_data);

    Vector_PushBytes(&interner->char_data.base, sv.data, sv.length);
    V_Push(&interner->char_data, '\0');

    u32 new_str_idx = V_Count(&interner->strings);
    AcuInternedString new_s = {.offset = new_offset, .length = sv.length};
    V_Push(&interner->strings, new_s);

    ht[idx] = ((u64)hash32 << 32) | new_str_idx;
    interner->count++;

    return (StringId)new_str_idx;
}

StringView AcuStringInterner_GetString(AcuStringInterner *interner, StringId id) {
    if (unlikely(id >= V_Count(&interner->strings))) {
        return StringView_CStr("");
    }

    AcuInternedString *s = &V_Elements(&interner->strings)[id];

    return StringView_Create(V_Elements(&interner->char_data) + s->offset, s->length);
}
