#include "interner/type_interner.h"
#include "common/panic.h"
#include "memory/vector.h"
#include <stdlib.h>

#define ACU_NULL_SLOT 0xFFFFFFFFFFFFFFFFULL
#define FNV_OFFSET 2166136261u
#define FNV_PRIME 16777619u

AcuTypeInterner AcuTypeInterner_Create(void) {
    AcuTypeInterner interner = {0};
    AcuTypeInterner_Init(&interner);
    return interner;
}

void AcuTypeInterner_Init(AcuTypeInterner *interner) {
    V_Init(&interner->types);
    V_Init(&interner->extra_data);
    V_InitWithCapacity(&interner->hash_table, 4096);

    for (u32 i = 0; i < V_Capacity(&interner->hash_table); i++) {
        V_Push(&interner->hash_table, ACU_NULL_SLOT);
    }
    interner->count = 0;

#define X(name, text) AcuTypeInterner_GetPrimitive(interner, name);
    X_TYPE_PRIMITIVE_KIND(X)
#undef X
}

void AcuTypeInterner_Free(AcuTypeInterner *interner) {
    V_Free(&interner->types);
    V_Free(&interner->extra_data);
    V_Free(&interner->hash_table);
}

static u32 HashType(const AcuType *t, const TypeId *extra_slice) {
    u32 hash = FNV_OFFSET;
#define FNV_MIX(val)                                                                               \
    do {                                                                                           \
        hash ^= (u32)(val);                                                                        \
        hash *= FNV_PRIME;                                                                         \
    } while (0)

    FNV_MIX(t->kind);
    switch ((AcuTypeKind)t->kind) {
        case ACU_TYPE_PRIMITIVE:
            FNV_MIX(t->as.primitive.kind);
            break;
        case ACU_TYPE_ARRAY:
            FNV_MIX(t->as.array.inner);
            FNV_MIX(t->as.array.size);
            break;
        case ACU_TYPE_VECTOR:
            FNV_MIX(t->as.vector.inner);
            break;
        case ACU_TYPE_TUPLE:
            FNV_MIX(t->as.tuple.count);
            for (u32 i = 0; i < t->as.tuple.count; ++i)
                FNV_MIX(extra_slice[i]);
            break;
        case ACU_TYPE_FUNC:
            FNV_MIX(t->as.func.return_type);
            FNV_MIX(t->as.func.count);
            for (u32 i = 0; i < t->as.func.count; ++i)
                FNV_MIX(extra_slice[i]);
            break;
        default:
            acu_unreachable_debug("Unknown AcuTypeKind %d", t->kind);
    }

#undef FNV_MIX
    return hash;
}

static bool EqType(const AcuType *a, const AcuType *b, const TypeId *extra_base,
                   const TypeId *b_extra_slice) {
    if (a->kind != b->kind)
        return false;

    switch ((AcuTypeKind)a->kind) {
        case ACU_TYPE_PRIMITIVE:
            return a->as.primitive.kind == b->as.primitive.kind;
        case ACU_TYPE_ARRAY:
            return (bool)(a->as.array.inner == b->as.array.inner &&
                          a->as.array.size == b->as.array.size);
        case ACU_TYPE_VECTOR:
            return a->as.vector.inner == b->as.vector.inner;
        case ACU_TYPE_TUPLE:
            if (a->as.tuple.count != b->as.tuple.count)
                return false;
            if (a->as.tuple.count == 0)
                return true;
            return __builtin_memcmp(&extra_base[a->as.tuple.fields_start], b_extra_slice,
                                    a->as.tuple.count * sizeof(TypeId)) == 0;
        case ACU_TYPE_FUNC:
            if (a->as.func.return_type != b->as.func.return_type)
                return false;
            if (a->as.func.count != b->as.func.count)
                return false;
            if (a->as.func.count == 0)
                return true;
            return __builtin_memcmp(&extra_base[a->as.func.params_start], b_extra_slice,
                                    a->as.func.count * sizeof(TypeId)) == 0;
        default:
            acu_unreachable_debug("Unknown AcuTypeKind %d", a->kind);
    }
    return false;
}

static void AcuTypeInterner_Rehash(AcuTypeInterner *interner) {
    const u32 old_cap = V_Capacity(&interner->hash_table);
    const u32 new_cap = old_cap * 2;
    const u32 mask = new_cap - 1;

    TypedVector(u64) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);
    for (u32 i = 0; i < new_cap; i++)
        V_Push(&new_ht, ACU_NULL_SLOT);

    const u64 *const old_elements = V_Elements(&interner->hash_table);
    u64 *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        u64 slot = old_elements[i];
        if (slot != ACU_NULL_SLOT) {
            u32 idx = (u32)(slot >> 32) & mask;
            while (new_elements[idx] != ACU_NULL_SLOT) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }

    V_Free(&interner->hash_table);
    interner->hash_table.base = new_ht.base;
}

static TypeId AcuTypeInterner_Intern(AcuTypeInterner *interner, AcuType lookup,
                                     const TypeId *extra_slice, u32 extra_count) {
    u32 hash32 = HashType(&lookup, extra_slice);
    u32 cap = V_Count(&interner->hash_table);
    u32 mask = cap - 1;
    u32 idx = hash32 & mask;

    u64 *ht = V_Elements(&interner->hash_table);
    AcuType *types = V_Elements(&interner->types);
    const TypeId *extra_base = V_Elements(&interner->extra_data);

    while (ht[idx] != ACU_NULL_SLOT) {
        u64 slot = ht[idx];
        u32 slot_hash = (u32)(slot >> 32);

        if (slot_hash == hash32) {
            u32 type_id = (u32)(slot & 0xFFFFFFFF);
            if (EqType(&types[type_id], &lookup, extra_base, extra_slice)) {
                return (TypeId)type_id;
            }
        }
        idx = (idx + 1) & mask;
    }

    if (unlikely(interner->count * 4 >= cap * 3)) {
        AcuTypeInterner_Rehash(interner);
        cap = V_Count(&interner->hash_table);
        mask = cap - 1;
        idx = hash32 & mask;
        ht = V_Elements(&interner->hash_table);
        types = V_Elements(&interner->types);

        while (ht[idx] != ACU_NULL_SLOT) {
            idx = (idx + 1) & mask;
        }
    }

    if (extra_count > 0) {
        ExtraIdx start_idx = V_Count(&interner->extra_data);
        Vector_PushBytes(&interner->extra_data.base, extra_slice, extra_count * sizeof(TypeId));

        if (lookup.kind == ACU_TYPE_TUPLE) {
            lookup.as.tuple.fields_start = start_idx;
        } else if (lookup.kind == ACU_TYPE_FUNC) {
            lookup.as.func.params_start = start_idx;
        }
    }

    u32 new_type_id = V_Count(&interner->types);
    V_Push(&interner->types, lookup);

    ht[idx] = ((u64)hash32 << 32) | new_type_id;
    interner->count++;

    return (TypeId)new_type_id;
}

TypeId AcuTypeInterner_GetPrimitive(AcuTypeInterner *interner, TypePrimitiveKind kind) {
    AcuType t = {.kind = ACU_TYPE_PRIMITIVE, .flags = 0};
    t.as.primitive.kind = kind;
    return AcuTypeInterner_Intern(interner, t, NULL, 0);
}

TypeId AcuTypeInterner_GetVector(AcuTypeInterner *interner, TypeId inner) {
    AcuType t = {.kind = ACU_TYPE_VECTOR, .flags = 0};
    t.as.vector.inner = inner;
    return AcuTypeInterner_Intern(interner, t, NULL, 0);
}

TypeId AcuTypeInterner_GetArray(AcuTypeInterner *interner, TypeId inner, u32 size) {
    AcuType t = {.kind = ACU_TYPE_ARRAY, .flags = 0, .as.array = {.inner = inner, .size = size}};
    return AcuTypeInterner_Intern(interner, t, NULL, 0);
}

TypeId AcuTypeInterner_GetTuple(AcuTypeInterner *interner, const TypeId *fields, u32 count) {
    AcuType t = {.kind = ACU_TYPE_TUPLE, .flags = 0, .as.tuple.count = count};
    return AcuTypeInterner_Intern(interner, t, fields, count);
}

TypeId AcuTypeInterner_GetFunc(AcuTypeInterner *interner, TypeId ret_type, const TypeId *params,
                               u32 count) {
    AcuType t = {
        .kind = ACU_TYPE_FUNC, .flags = 0, .as.func = {.return_type = ret_type, .count = count}};
    return AcuTypeInterner_Intern(interner, t, params, count);
}
