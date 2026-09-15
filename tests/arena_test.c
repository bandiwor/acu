#include "acu_test.h"
#include "memory/arena.h"
#include <stdint.h>
#include <string.h>

typedef struct AcuArenaBlock_Internal {
    u64 size;
    u64 used;
    struct AcuArenaBlock_Internal *next;
    u64 unused_padding;
} AcuArenaBlock_Internal;

TEST(Arena, CreateAndDestroy) {
    AcuArena arena = AcuArena_Create();

    ASSERT_TRUE(arena.current == NULL);

    AcuArena_Destroy(&arena);
    ASSERT_TRUE(arena.current == NULL);
}

TEST(Arena, BasicAllocation) {
    AcuArena arena = AcuArena_Create();

    void *ptr = AcuArena_AllocateAligned(&arena, 16, 1);
    ASSERT_NOT_NULL(ptr);

    memset(ptr, 0xAA, 16);

    AcuArenaBlock_Internal *block = (AcuArenaBlock_Internal *)arena.current;
    ASSERT_NOT_NULL(block);
    ASSERT_TRUE(block->used >= 16);
    ASSERT_TRUE(block->next == NULL);

    AcuArena_Destroy(&arena);
}

TEST(Arena, Alignment) {
    AcuArena arena = AcuArena_Create();

    void *p1 = AcuArena_AllocateAligned(&arena, 1, 1);
    ASSERT_NOT_NULL(p1);

    void *p2 = AcuArena_AllocateAligned(&arena, 4, 8);
    ASSERT_NOT_NULL(p2);

    uintptr_t addr2 = (uintptr_t)p2;

    ASSERT_EQ_INT(0, addr2 % 8);

    ASSERT_TRUE(addr2 >= ((uintptr_t)p1 + 1));

    void *p3 = AcuArena_AllocateAligned(&arena, 16, 4096);
    ASSERT_EQ_INT(0, (uintptr_t)p3 % 4096);

    AcuArena_Destroy(&arena);
}

TEST(Arena, BlockChaining) {
    AcuArena arena = AcuArena_Create();

    void *p1 = AcuArena_AllocateAligned(&arena, 60 * 1024, 8);
    ASSERT_NOT_NULL(p1);

    AcuArenaBlock_Internal *block1 = (AcuArenaBlock_Internal *)arena.current;
    ASSERT_TRUE(block1->next == NULL);

    void *p2 = AcuArena_AllocateAligned(&arena, 10 * 1024, 8);
    ASSERT_NOT_NULL(p2);

    AcuArenaBlock_Internal *block2 = (AcuArenaBlock_Internal *)arena.current;

    ASSERT_TRUE(block1 != block2);
    ASSERT_TRUE(block2->next == block1);

    AcuArena_Destroy(&arena);
}

TEST(Arena, GiantAllocation) {
    AcuArena arena = AcuArena_Create();

    void *giant_ptr = AcuArena_AllocateAligned(&arena, 1024 * 1024, 16);
    ASSERT_NOT_NULL(giant_ptr);

    u8 *mem = (u8 *)giant_ptr;
    mem[0] = 0xAA;
    mem[(1024 * 1024) - 1] = 0xBB;

    AcuArenaBlock_Internal *block = (AcuArenaBlock_Internal *)arena.current;
    ASSERT_TRUE(block->size >= 1024 * 1024);

    AcuArena_Destroy(&arena);
}

TEST(Arena, Clear) {
    AcuArena arena = AcuArena_Create();

    void *p1 = AcuArena_AllocateAligned(&arena, 100, 8);
    ASSERT_NOT_NULL(p1);

    AcuArenaBlock_Internal *block = (AcuArenaBlock_Internal *)arena.current;
    ASSERT_TRUE(block->used > 0);

    AcuArena_Clear(&arena);

    ASSERT_EQ_INT(0, block->used);

    void *p2 = AcuArena_AllocateAligned(&arena, 100, 8);

    ASSERT_TRUE(p1 == p2);

    AcuArena_Destroy(&arena);
}

typedef struct {
    u32 a;
    u64 b;
    u8 c;
} ArenaMockStruct;

TEST(Arena, Macros) {
    AcuArena arena = AcuArena_Create();

    ArenaMockStruct *ts = AcuArena_Type(&arena, ArenaMockStruct);
    ASSERT_NOT_NULL(ts);

    uintptr_t ts_addr = (uintptr_t)ts;
    ASSERT_EQ_INT(0, ts_addr % _Alignof(ArenaMockStruct));

    u64 *arr = AcuArena_Array(&arena, u64, 100);
    ASSERT_NOT_NULL(arr);

    uintptr_t arr_addr = (uintptr_t)arr;
    ASSERT_EQ_INT(0, arr_addr % _Alignof(u64));

    for (int i = 0; i < 100; i++) {
        arr[i] = 0xDEADBEEF;
    }

    AcuArena_Destroy(&arena);
}
