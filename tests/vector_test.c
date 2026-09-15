#include "acu_test.h"
#include "memory/vector.h"
#include <string.h>

TEST(Vector, CreateAndInitZero) {
    Vector v1 = Vector_Create();
    ASSERT_EQ_INT(0, v1.size);
    ASSERT_EQ_INT(0, v1.capacity);
    ASSERT_TRUE(v1.memory == NULL);

    Vector v2;
    v2.size = 123;
    v2.capacity = 456;
    v2.memory = (void *)0xDEADBEEF;

    Vector_InitZero(&v2);
    ASSERT_EQ_INT(0, v2.size);
    ASSERT_EQ_INT(0, v2.capacity);
    ASSERT_TRUE(v2.memory == NULL);
}

TEST(Vector, InitAndFree) {
    Vector v;
    Vector_Init(&v);

    ASSERT_EQ_INT(0, v.size);
    ASSERT_EQ_INT(4096, v.capacity);
    ASSERT_NOT_NULL(v.memory);

    Vector_Free(&v);

    ASSERT_EQ_INT(0, v.size);
    ASSERT_EQ_INT(0, v.capacity);
    ASSERT_TRUE(v.memory == NULL);

    Vector_Free(&v);
}

TEST(Vector, InitWithCapacityAlignment) {
    Vector v;

    Vector_InitWithCapacity(&v, 100);
    ASSERT_EQ_INT(4096, v.capacity);
    Vector_Free(&v);

    Vector_InitWithCapacity(&v, 4096);
    ASSERT_EQ_INT(4096, v.capacity);
    Vector_Free(&v);

    Vector_InitWithCapacity(&v, 4097);
    ASSERT_EQ_INT(8192, v.capacity);
    Vector_Free(&v);
}

TEST(Vector, PushBytesBasic) {
    Vector v;
    Vector_InitZero(&v);

    const char *test_data = "Hello, Acu!";
    u32 len = strlen(test_data); // 11 байт

    Vector_PushBytes(&v, test_data, len);

    ASSERT_EQ_INT(len, v.size);
    ASSERT_EQ_INT(4096, v.capacity);
    ASSERT_NOT_NULL(v.memory);

    ASSERT_TRUE(memcmp(v.memory, test_data, len) == 0);

    Vector_PushBytes(&v, test_data, 0);
    ASSERT_EQ_INT(len, v.size);

    Vector_Free(&v);
}

TEST(Vector, PushBytesGrowAndRealloc) {
    Vector v;
    Vector_Init(&v);

    ASSERT_EQ_INT(4096, v.capacity);

    u8 buffer[5000];
    memset(buffer, 0xAA, sizeof(buffer));

    Vector_PushBytes(&v, buffer, 5000);

    ASSERT_EQ_INT(5000, v.size);
    ASSERT_EQ_INT(8192, v.capacity);
    ASSERT_NOT_NULL(v.memory);

    u8 *mem = (u8 *)v.memory;
    ASSERT_EQ_INT(0xAA, mem[0]);
    ASSERT_EQ_INT(0xAA, mem[2500]);
    ASSERT_EQ_INT(0xAA, mem[4999]);

    Vector_PushBytes(&v, "X", 1);
    ASSERT_EQ_INT(5001, v.size);
    ASSERT_EQ_INT(8192, v.capacity);

    u8 padding[3191];
    memset(padding, 0xBB, sizeof(padding));
    Vector_PushBytes(&v, padding, sizeof(padding));

    ASSERT_EQ_INT(8192, v.size);
    ASSERT_EQ_INT(8192, v.capacity);

    Vector_PushBytes(&v, "Y", 1);
    ASSERT_EQ_INT(8193, v.size);
    ASSERT_EQ_INT(16384, v.capacity);

    u8 *new_mem = (u8 *)v.memory;

    ASSERT_EQ_INT(0xAA, new_mem[0]);

    Vector_Free(&v);
}

TEST(Vector, DirectRealloc) {
    Vector v = Vector_Create();

    Vector_Realloc(&v, 1024);
    ASSERT_EQ_INT(0, v.size);
    ASSERT_EQ_INT(4096, v.capacity);

    Vector_Realloc(&v, 9000);
    ASSERT_EQ_INT(12288, v.capacity);

    Vector_Free(&v);
}
