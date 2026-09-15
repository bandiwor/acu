#pragma once
#include "defines/defines.h"
#include "defines/types.h"
#include "memory/vector.h"

typedef enum {
    ACU_TYPE_PRIMITIVE,
    ACU_TYPE_ARRAY,
    ACU_TYPE_VECTOR,
    ACU_TYPE_TUPLE,
    ACU_TYPE_FUNC,
} AcuTypeKind;

typedef struct {
    u16 kind;
    u16 flags;

    union {
        struct {
            TypePrimitiveKind kind;
        } primitive;

        struct {
            TypeId inner;
            u32 size;
        } array;

        struct {
            TypeId inner;
        } vector;

        struct {
            ExtraIdx fields_start;
            u32 count;
        } tuple;

        struct {
            TypeId return_type;
            ExtraIdx params_start;
            u32 count;
        } func;
    } as;
} AcuType;

typedef struct {
    TypedVector(AcuType) types;
    TypedVector(TypeId) extra_data;
    TypedVector(u64) hash_table;
    u32 count;
} AcuTypeInterner;

AcuTypeInterner AcuTypeInterner_Create(void);
void AcuTypeInterner_Init(AcuTypeInterner *interner);
void AcuTypeInterner_Free(AcuTypeInterner *interner);

TypeId AcuTypeInterner_GetPrimitive(AcuTypeInterner *interner, TypePrimitiveKind kind);
TypeId AcuTypeInterner_GetVector(AcuTypeInterner *interner, TypeId inner);
TypeId AcuTypeInterner_GetArray(AcuTypeInterner *interner, TypeId inner, u32 size);
TypeId AcuTypeInterner_GetTuple(AcuTypeInterner *interner, const TypeId *fields, u32 count);
TypeId AcuTypeInterner_GetFunc(AcuTypeInterner *interner, TypeId ret_type, const TypeId *params,
                               u32 count);

#define AcuTypeInterner_GetType(interner_ptr, id) (&V_Elements(&(interner_ptr)->types)[id])
#define AcuTypeInterner_GetTypeIdByExtra(interner_ptr, extra_idx)                                  \
    (V_Elements(&(interner_ptr)->extra_data)[extra_idx])
