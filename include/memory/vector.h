#pragma once

#include "defines/defines.h"
#include <assert.h>
#include <stddef.h>

typedef struct {
    void *memory;
    u32 size;
    u32 capacity;
} Vector;

Vector Vector_Create(void);

attribute_nonnull(1) void Vector_Init(Vector *restrict vector);
attribute_nonnull(1) void Vector_InitZero(Vector *restrict vector);
attribute_nonnull(1) void Vector_InitWithCapacity(Vector *restrict vector, u32 capacity);
attribute_nonnull(1) void Vector_Free(Vector *restrict vector);
attribute_nonnull(1) void Vector_Realloc(Vector *restrict vector, u32 new_capacity);
attribute_nonnull(1) void Vector_GrowBytes(Vector *restrict vector, u32 additional_bytes);

attribute_nonnull(1, 2) void Vector_PushBytes(Vector *restrict vector, const void *restrict data,
                                              u32 size);

#define TypedVector(type)                                                                          \
    struct {                                                                                       \
        Vector base;                                                                               \
        __extension__ type marker[0];                                                              \
    }

#define V_Type(v_ptr) __typeof__((v_ptr)->marker[0])

#define V_MemAligned(v_ptr) ((V_Type(v_ptr) *)assume_aligned((v_ptr)->base.memory, 4096))

#define V_Init(v_ptr)                                                                              \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_init_v = (v_ptr);                                              \
        assert(acu_init_v != NULL && "Vector pointer is NULL");                                    \
        Vector_Init(&acu_init_v->base);                                                            \
    } while (0)

#define V_InitZero(v_ptr)                                                                          \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_iz_v = (v_ptr);                                                \
        assert(acu_iz_v != NULL && "Vector pointer is NULL");                                      \
        Vector_InitZero(&acu_iz_v->base);                                                          \
    } while (0)

#define V_InitWithCapacity(v_ptr, cap)                                                             \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_iwc_v = (v_ptr);                                               \
        assert(acu_iwc_v != NULL && "Vector pointer is NULL");                                     \
        const size_t acu_iwc_sz = sizeof(V_Type(acu_iwc_v));                                       \
        const size_t acu_iwc_cap = (size_t)(cap);                                                  \
        assert(acu_iwc_cap <= (size_t)(UINT32_MAX / acu_iwc_sz) && "Init capacity overflows u32"); \
        Vector_InitWithCapacity(&acu_iwc_v->base, (u32)(acu_iwc_cap * acu_iwc_sz));                \
    } while (0)

#define V_Free(v_ptr)                                                                              \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_free_v = (v_ptr);                                              \
        assert(acu_free_v != NULL && "Vector pointer is NULL");                                    \
        Vector_Free(&acu_free_v->base);                                                            \
    } while (0)

#define V_Count(v_ptr)                                                                             \
    __extension__({                                                                                \
        __typeof__(v_ptr) const acu_cnt_v = (v_ptr);                                               \
        assert(acu_cnt_v != NULL && "Vector pointer is NULL");                                     \
        assert(acu_cnt_v->base.size % sizeof(V_Type(acu_cnt_v)) == 0 && "Vector size unaligned!"); \
        acu_cnt_v->base.size / sizeof(V_Type(acu_cnt_v));                                          \
    })

#define V_Capacity(v_ptr)                                                                          \
    __extension__({                                                                                \
        __typeof__(v_ptr) const acu_cap_v = (v_ptr);                                               \
        assert(acu_cap_v != NULL && "Vector pointer is NULL");                                     \
        acu_cap_v->base.capacity / sizeof(V_Type(acu_cap_v));                                      \
    })

#define V_EnsureCapacity(v_ptr, required_count)                                                    \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_ec_v = (v_ptr);                                                \
        assert(acu_ec_v != NULL && "Vector pointer is NULL");                                      \
        const size_t acu_ec_sz = sizeof(V_Type(acu_ec_v));                                         \
        const size_t acu_ec_req = (size_t)(required_count);                                        \
        assert(acu_ec_req <= (size_t)(UINT32_MAX / acu_ec_sz) && "Capacity size overflows u32");   \
        const u32 acu_ec_bytes = (u32)(acu_ec_req * acu_ec_sz);                                    \
        if (unlikely(acu_ec_v->base.capacity < acu_ec_bytes)) {                                    \
            Vector_Realloc(&acu_ec_v->base, acu_ec_bytes);                                         \
        }                                                                                          \
    } while (0)

#define V_Elements(v_ptr) V_MemAligned(v_ptr)

#define V_At(v_ptr, index)                                                                         \
    (*__extension__({                                                                              \
        __typeof__(v_ptr) const acu_at_v = (v_ptr);                                                \
        assert(acu_at_v != NULL && "Vector pointer is NULL");                                      \
        assert(acu_at_v->base.memory != NULL && "Vector memory is NULL");                          \
        const size_t acu_at_idx = (size_t)(index);                                                 \
        assert(acu_at_idx < (acu_at_v->base.size / sizeof(V_Type(acu_at_v))) &&                    \
               "Vector index out of bounds!");                                                     \
        &V_MemAligned(acu_at_v)[acu_at_idx];                                                       \
    }))

#define V_SetCount(v_ptr, count)                                                                   \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_sc_v = (v_ptr);                                                \
        assert(acu_sc_v != NULL && "Vector pointer is NULL");                                      \
        const size_t acu_sc_sz = sizeof(V_Type(acu_sc_v));                                         \
        const size_t acu_sc_cnt = (size_t)(count);                                                 \
        assert(acu_sc_cnt <= (size_t)(UINT32_MAX / acu_sc_sz) && "V_SetCount size overflows u32"); \
        const u32 acu_sc_bytes = (u32)(acu_sc_cnt * acu_sc_sz);                                    \
        assert(acu_sc_bytes <= acu_sc_v->base.capacity && "V_SetCount exceeds capacity!");         \
        acu_sc_v->base.size = acu_sc_bytes;                                                        \
    } while (0)

#define V_AddCount(v_ptr, count)                                                                   \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_ac_v = (v_ptr);                                                \
        assert(acu_ac_v != NULL && "Vector pointer is NULL");                                      \
        const size_t acu_ac_sz = sizeof(V_Type(acu_ac_v));                                         \
        const size_t acu_ac_cnt = (size_t)(count);                                                 \
        assert(acu_ac_cnt <= (size_t)(UINT32_MAX / acu_ac_sz) && "V_AddCount size overflows u32"); \
        const u32 acu_ac_bytes = (u32)(acu_ac_cnt * acu_ac_sz);                                    \
        assert(acu_ac_v->base.capacity - acu_ac_v->base.size >= acu_ac_bytes && "Overflow!");      \
        acu_ac_v->base.size += acu_ac_bytes;                                                       \
    } while (0)

#define V_Push(v_ptr, value)                                                                       \
    do {                                                                                           \
        __typeof__(v_ptr) const acu_push_v = (v_ptr);                                              \
        assert(acu_push_v != NULL && "Vector pointer is NULL");                                    \
        V_Type(acu_push_v) const acu_push_val = (value);                                           \
        const u32 acu_push_sz = (u32)sizeof(V_Type(acu_push_v));                                   \
        if (unlikely(acu_push_v->base.capacity - acu_push_v->base.size < acu_push_sz)) {           \
            Vector_GrowBytes(&acu_push_v->base, acu_push_sz);                                      \
        }                                                                                          \
        u8 *const acu_push_dest =                                                                  \
            (u8 *)assume_aligned(acu_push_v->base.memory, 4096) + acu_push_v->base.size;           \
        *(V_Type(acu_push_v) *)acu_push_dest = acu_push_val;                                       \
        acu_push_v->base.size += acu_push_sz;                                                      \
    } while (0)

#define V_Pop(v_ptr)                                                                               \
    __extension__({                                                                                \
        __typeof__(v_ptr) const acu_pop_v = (v_ptr);                                               \
        assert(acu_pop_v != NULL && "Vector pointer is NULL");                                     \
        const u32 acu_pop_sz = (u32)sizeof(V_Type(acu_pop_v));                                     \
        assert(acu_pop_v->base.size >= acu_pop_sz && "Vector underflow on V_Pop!");                \
        acu_pop_v->base.size -= acu_pop_sz;                                                        \
        u8 *const acu_pop_src =                                                                    \
            (u8 *)assume_aligned(acu_pop_v->base.memory, 4096) + acu_pop_v->base.size;             \
        *(V_Type(acu_pop_v) *)acu_pop_src;                                                         \
    })
