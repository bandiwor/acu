#include "memory/vector.h"
#include "common/panic.h"
#include "memory/mmap.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

#define MMAP_PAGE_SIZE 4096U
#define VECTOR_MAX_CAPACITY (UINT32_MAX - MMAP_PAGE_SIZE + 1U)

Vector Vector_Create(void) {
    return (Vector){
        .memory = NULL,
        .size = 0,
        .capacity = 0,
    };
}

attribute_nonnull(1) void Vector_InitZero(Vector *restrict vector) {
    assert(vector != NULL && "Vector pointer must not be NULL");
    vector->size = 0;
    vector->capacity = 0;
    vector->memory = NULL;
}

void Vector_InitWithCapacity(Vector *restrict vector, const u32 capacity) {
    assert(vector != NULL && "Vector pointer must not be NULL");

    if (unlikely(capacity > VECTOR_MAX_CAPACITY)) {
        acu_panic("Vector_InitWithCapacity: requested capacity exceeds maximum limit");
    }

    const u32 aligned_capacity =
        (capacity < MMAP_PAGE_SIZE) ? MMAP_PAGE_SIZE : ALIGN_UP(capacity, MMAP_PAGE_SIZE);

    void *block = mmap_allocate(aligned_capacity);
    if (unlikely(block == NULL)) {
        acu_panic("mmap_allocate returns NULL-ptr.");
    }

    vector->size = 0;
    vector->memory = block;
    vector->capacity = aligned_capacity;
}

void Vector_Init(Vector *restrict vector) {
    Vector_InitWithCapacity(vector, MMAP_PAGE_SIZE);
}

void Vector_Free(Vector *restrict vector) {
    assert(vector != NULL && "Vector pointer must not be NULL");
    if (vector->memory) {
        assert(vector->capacity > 0 && "Vector memory is present but capacity is 0");
        mmap_deallocate(vector->memory, vector->capacity);
        vector->memory = NULL;
        vector->size = 0;
        vector->capacity = 0;
    }
}

void Vector_Realloc(Vector *restrict vector, const u32 new_capacity) {
    assert(vector != NULL && "Vector pointer must not be NULL");
    assert(new_capacity >= vector->size && "Vector_Realloc: cannot shrink below current size!");

    if (new_capacity == 0) {
        Vector_Free(vector);
        return;
    }

    if (unlikely(new_capacity > VECTOR_MAX_CAPACITY)) {
        acu_panic("Vector_Realloc: requested capacity exceeds maximum limit");
    }

    const u32 aligned_capacity = ALIGN_UP(new_capacity, MMAP_PAGE_SIZE);

    if (unlikely(vector->memory == NULL)) {
        Vector_InitWithCapacity(vector, aligned_capacity);
        return;
    }

    if (aligned_capacity == vector->capacity) {
        return;
    }

    void *new_block = mmap_reallocate(vector->memory, vector->capacity, aligned_capacity);
    if (unlikely(new_block == NULL)) {
        acu_panic("Vector reallocation failed: out of virtual address space (mremap returned "
                  "MAP_FAILED).");
    }

    vector->memory = new_block;
    vector->capacity = aligned_capacity;
}

void Vector_GrowBytes(Vector *restrict vector, const u32 additional_bytes) {
    assert(vector != NULL && "Vector pointer must not be NULL");

    if (unlikely(UINT32_MAX - vector->size < additional_bytes)) {
        acu_panic("Vector capacity overflow: size + additional_bytes exceeds UINT32_MAX");
    }

    const u32 needed = vector->size + additional_bytes;
    if (unlikely(needed > VECTOR_MAX_CAPACITY)) {
        acu_panic("Vector capacity overflow: requested size exceeds maximum page-aligned capacity");
    }

    u32 new_capacity = vector->capacity;
    if (new_capacity == 0) {
        new_capacity = MMAP_PAGE_SIZE;
    } else {
        // Геометрический x2 рост для амортизации вызовов ядра mremap
        if (new_capacity <= VECTOR_MAX_CAPACITY / 2) {
            new_capacity *= 2;
        } else {
            new_capacity = VECTOR_MAX_CAPACITY;
        }
    }

    if (new_capacity < needed) {
        new_capacity = needed;
    }

    Vector_Realloc(vector, new_capacity);
}

attribute_nonnull(1, 2) void Vector_PushBytes(Vector *restrict vector, const void *restrict data,
                                              u32 size) {
    assert(vector != NULL && "Vector pointer must not be NULL");
    assert(data != NULL && "Data pointer must not be NULL");

    if (unlikely(size == 0)) {
        return;
    }

    if (unlikely(UINT32_MAX - vector->size < size)) {
        acu_panic("Vector_PushBytes: size overflow");
    }

    const u8 *src = (const u8 *)data;
    const bool is_internal = (vector->memory != NULL) && (src >= (const u8 *)vector->memory) &&
                             (src < (const u8 *)vector->memory + vector->capacity);
    const ptrdiff_t internal_offset = is_internal ? (src - (const u8 *)vector->memory) : 0;

    if (unlikely(vector->capacity - vector->size < size)) {
        Vector_GrowBytes(vector, size);
        if (is_internal) {
            src = (const u8 *)vector->memory + internal_offset;
        }
    }

    u8 *dest = (u8 *)vector->memory + vector->size;
    __builtin_memcpy(dest, src, size);
    vector->size += size;
}
