#include "acu_test.h"
#include "interner/string_interner.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <stdio.h>

TEST(StringInterner, InitAndFree) {
    AcuStringInterner interner = AcuStringInterner_Create();

    ASSERT_EQ_INT(0, interner.count);
    ASSERT_EQ_INT(1, V_Count(&interner.strings));
    ASSERT_EQ_INT(1, V_Count(&interner.char_data));
    ASSERT_TRUE(V_Capacity(&interner.hash_table) > 0);

    AcuStringInterner_Free(&interner);
}

TEST(StringInterner, EmptyString) {
    AcuStringInterner interner = AcuStringInterner_Create();

    StringId id = AcuStringInterner_Intern(&interner, SV(""));
    ASSERT_EQ_INT(0, id);

    StringView sv = AcuStringInterner_GetString(&interner, 0);
    ASSERT_EQ_INT(0, sv.length);

    AcuStringInterner_Free(&interner);
}

TEST(StringInterner, InternAndDeduplicate) {
    AcuStringInterner interner = AcuStringInterner_Create();

    StringView str1 = SV("hello_world");
    StringView str2 = SV("acu_lang");

    StringId id1 = AcuStringInterner_Intern(&interner, str1);
    StringId id2 = AcuStringInterner_Intern(&interner, str2);

    ASSERT_TRUE(id1 > 0);
    ASSERT_TRUE(id2 > 0);
    ASSERT_TRUE(id1 != id2);

    StringId id1_dup = AcuStringInterner_Intern(&interner, SV("hello_world"));
    ASSERT_EQ_INT(id1, id1_dup);

    StringView res1 = AcuStringInterner_GetString(&interner, id1);
    ASSERT_TRUE(StringView_Equals(str1, res1));

    StringView res2 = AcuStringInterner_GetString(&interner, id2);
    ASSERT_TRUE(StringView_Equals(str2, res2));

    AcuStringInterner_Free(&interner);
}

TEST(StringInterner, InvalidId) {
    AcuStringInterner interner = AcuStringInterner_Create();

    StringView sv = AcuStringInterner_GetString(&interner, 99999);

    ASSERT_EQ_INT(0, sv.length);

    AcuStringInterner_Free(&interner);
}

TEST(StringInterner, Rehashing) {
    AcuStringInterner interner = AcuStringInterner_Create();

    u32 initial_cap = V_Capacity(&interner.hash_table);
    const int INSERT_COUNT = 6500;

    char buffer[64];
    for (int i = 0; i < INSERT_COUNT; i++) {
        snprintf(buffer, sizeof(buffer), "string_test_rehash_%d", i);
        AcuStringInterner_Intern(&interner, StringView_CStr(buffer));
    }

    u32 new_cap = V_Capacity(&interner.hash_table);
    ASSERT_TRUE(new_cap > initial_cap);
    ASSERT_EQ_INT(initial_cap * 2, new_cap);

    StringId test_id = AcuStringInterner_Intern(&interner, SV("string_test_rehash_5000"));
    StringView sv = AcuStringInterner_GetString(&interner, test_id);

    ASSERT_TRUE(StringView_Equals(SV("string_test_rehash_5000"), sv));

    AcuStringInterner_Free(&interner);
}

TEST(StringInterner, SameLengthDifferentContent) {
    AcuStringInterner interner = AcuStringInterner_Create();

    StringId id1 = AcuStringInterner_Intern(&interner, SV("abcd"));
    StringId id2 = AcuStringInterner_Intern(&interner, SV("dcba"));
    StringId id3 = AcuStringInterner_Intern(&interner, SV("aaaa"));

    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id2 != id3);
    ASSERT_TRUE(id1 != id3);

    StringView res1 = AcuStringInterner_GetString(&interner, id1);
    ASSERT_TRUE(StringView_Equals(SV("abcd"), res1));

    AcuStringInterner_Free(&interner);
}
