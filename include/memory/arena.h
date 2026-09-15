#pragma once

#include "defines/defines.h"

typedef struct AcuArenaBlock AcuArenaBlock;

typedef struct {
    AcuArenaBlock *current;
} AcuArena;

AcuArena AcuArena_Create(void);

attribute_nonnull(1) void AcuArena_Destroy(AcuArena *arena);

attribute_nonnull(1) void AcuArena_Clear(AcuArena *arena);

attribute_nonnull(1) attribute_alloc_align(3) attribute_malloc
    void *AcuArena_AllocateAligned(AcuArena *arena, u64 size, u64 alignment);

#define AcuArena_Type(arena, type)                                                                 \
    (type *)AcuArena_AllocateAligned(arena, sizeof(type), _Alignof(type))

#define AcuArena_Array(arena, type, count)                                                         \
    (type *)AcuArena_AllocateAligned(arena, sizeof(type) * (count), _Alignof(type))
