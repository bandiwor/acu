#include "memory/arena.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "memory/mmap.h"
#include <string.h>

struct AcuArenaBlock {
    u64 size;
    u64 used;
    AcuArenaBlock *next;
    u64 unused_padding;
};

#define ARENA_BLOCK_PAGE_SIZE ((u64)(64 * 1024))
#define ARENA_BLOCK_DATA(block) ((u8 *)(block) + sizeof(AcuArenaBlock))

static AcuArenaBlock *AcuArenaBlock_Create(const u64 required_size) {
    const u64 header_size = sizeof(AcuArenaBlock);
    const u64 total_needed = required_size + header_size;

    const u64 total_to_alloc = (total_needed > ARENA_BLOCK_PAGE_SIZE)
                                   ? ALIGN_UP(total_needed, ARENA_BLOCK_PAGE_SIZE)
                                   : ARENA_BLOCK_PAGE_SIZE;

    void *ptr = mmap_allocate(total_to_alloc);
    if (unlikely(ptr == NULL)) {
        acu_panic("mmap_allocate returns NULL-ptr.");
    }

    AcuArenaBlock *block = (AcuArenaBlock *)ptr;

    block->size = total_to_alloc - header_size;
    block->used = 0;
    block->next = NULL;

    return block;
}

static void ArenaBlock_FreePage(const AcuArenaBlock *const block) {
    mmap_deallocate((void *)block, block->size + sizeof(AcuArenaBlock));
}

AcuArena AcuArena_Create(void) {
    return (AcuArena){
        .current = NULL,
    };
}

attribute_nonnull(1) void AcuArena_Destroy(AcuArena *arena) {
    if (arena->current == NULL) {
        return;
    }

    const AcuArenaBlock *current = arena->current;
    while (current != NULL) {
        const AcuArenaBlock *next = current->next;
        ArenaBlock_FreePage(current);
        current = next;
    }

    arena->current = NULL;
}

attribute_nonnull(1) void AcuArena_Clear(AcuArena *arena) {
    AcuArenaBlock *block = arena->current;
    while (block != NULL) {
        block->used = 0;
        block = block->next;
    }
}

attribute_nonnull(1) attribute_alloc_align(3) attribute_malloc
    void *AcuArena_AllocateAligned(AcuArena *arena, u64 size, u64 alignment) {
    AcuArenaBlock *curr_block = arena->current;

    if (likely(curr_block != NULL)) {
        u8 *data_start = ARENA_BLOCK_DATA(curr_block);
        uintptr_t curr_ptr = (uintptr_t)(data_start + curr_block->used);
        uintptr_t aligned_ptr = ALIGN_UP(curr_ptr, alignment);

        u64 new_used = (aligned_ptr - (uintptr_t)data_start) + size;

        if (likely(new_used <= curr_block->size)) {
            curr_block->used = new_used;
            return (void *)aligned_ptr;
        }
    }

    AcuArenaBlock *new_block = AcuArenaBlock_Create(size + alignment);
    new_block->next = curr_block;
    arena->current = new_block;

    u8 *data_start = ARENA_BLOCK_DATA(new_block);
    uintptr_t aligned_ptr = ALIGN_UP((uintptr_t)data_start, alignment);

    new_block->used = (aligned_ptr - (uintptr_t)data_start) + size;
    return (void *)aligned_ptr;
}
