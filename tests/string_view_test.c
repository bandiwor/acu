#include "acu_test.h"
#include "types/string_view.h"

TEST(StringView, CreateAndEquals) {
    StringView s1 = SV("hello");
    StringView s2 = StringView_CStr("hello");
    StringView s3 = SV("world");
    StringView s4 = SV("hell");

    ASSERT_TRUE(StringView_Equals(s1, s2));

    ASSERT_FALSE(StringView_Equals(s1, s3));

    ASSERT_FALSE(StringView_Equals(s1, s4));

    StringView empty1 = StringView_Empty();
    StringView empty2 = SV("");
    ASSERT_TRUE(StringView_Equals(empty1, empty2));
    ASSERT_FALSE(StringView_Equals(s1, empty1));
}

TEST(StringView, StartsAndEndsWith) {
    StringView sv = SV("acu_compiler");

    ASSERT_TRUE(StringView_StartsWith(sv, SV("acu_")));
    ASSERT_TRUE(StringView_StartsWith(sv, SV("acu_compiler")));
    ASSERT_TRUE(StringView_StartsWith(sv, SV("")));
    ASSERT_FALSE(StringView_StartsWith(sv, SV("compiler")));
    ASSERT_FALSE(StringView_StartsWith(sv, SV("acu_compiler_plus")));

    ASSERT_TRUE(StringView_EndsWith(sv, SV("compiler")));
    ASSERT_TRUE(StringView_EndsWith(sv, SV("acu_compiler")));
    ASSERT_TRUE(StringView_EndsWith(sv, SV("")));
    ASSERT_FALSE(StringView_EndsWith(sv, SV("acu_")));
    ASSERT_FALSE(StringView_EndsWith(sv, SV("super_acu_compiler")));
}

TEST(StringView, Substr) {
    StringView sv = SV("hello_world");

    StringView sub1 = StringView_Substr(sv, 6, 5);
    ASSERT_TRUE(StringView_Equals(sub1, SV("world")));

    StringView sub2 = StringView_Substr(sv, 6, 100);
    ASSERT_TRUE(StringView_Equals(sub2, SV("world")));

    StringView sub3 = StringView_Substr(sv, 50, 5);
    ASSERT_EQ_INT(0, sub3.length);
}

TEST(StringView, Chop) {
    StringView sv = SV("hello_world");

    StringView cl1 = StringView_ChopLeft(sv, 6);
    ASSERT_TRUE(StringView_Equals(cl1, SV("world")));

    StringView cl2 = StringView_ChopLeft(sv, 100);
    ASSERT_EQ_INT(0, cl2.length);

    StringView cr1 = StringView_ChopRight(sv, 6);
    ASSERT_TRUE(StringView_Equals(cr1, SV("hello")));

    StringView cr2 = StringView_ChopRight(sv, 100);
    ASSERT_EQ_INT(0, cr2.length);
}

TEST(StringView, Trim) {
    StringView t1 = StringView_Trim(SV("  hello \t\r\n "));
    ASSERT_TRUE(StringView_Equals(t1, SV("hello")));

    StringView t2 = StringView_Trim(SV("acu"));
    ASSERT_TRUE(StringView_Equals(t2, SV("acu")));

    StringView t3 = StringView_Trim(SV(" \t\n\r "));
    ASSERT_EQ_INT(0, t3.length);

    StringView t4 = StringView_Trim(SV("   left"));
    ASSERT_TRUE(StringView_Equals(t4, SV("left")));

    StringView t5 = StringView_Trim(SV("right   "));
    ASSERT_TRUE(StringView_Equals(t5, SV("right")));
}

TEST(StringView, Split) {
    StringView list = SV("a,b,c");
    StringView part;

    ASSERT_TRUE(StringView_Split(&list, ',', &part));
    ASSERT_TRUE(StringView_Equals(part, SV("a")));

    ASSERT_TRUE(StringView_Split(&list, ',', &part));
    ASSERT_TRUE(StringView_Equals(part, SV("b")));

    ASSERT_TRUE(StringView_Split(&list, ',', &part));
    ASSERT_TRUE(StringView_Equals(part, SV("c")));

    ASSERT_FALSE(StringView_Split(&list, ',', &part));
    ASSERT_EQ_INT(0, list.length);

    StringView empty_parts = SV("x,,y");
    ASSERT_TRUE(StringView_Split(&empty_parts, ',', &part));
    ASSERT_TRUE(StringView_Equals(part, SV("x")));

    ASSERT_TRUE(StringView_Split(&empty_parts, ',', &part));
    ASSERT_EQ_INT(0, part.length);

    ASSERT_TRUE(StringView_Split(&empty_parts, ',', &part));
    ASSERT_TRUE(StringView_Equals(part, SV("y")));
}

TEST(StringView, Hash) {
    StringView sv1 = SV("acu_compiler");
    StringView sv2 = SV("acu_compiler");
    StringView sv3 = SV("acu_compile!");

    u64 h1 = StringView_Hash(sv1);
    u64 h2 = StringView_Hash(sv2);
    u64 h3 = StringView_Hash(sv3);

    ASSERT_TRUE(h1 == h2);
    ASSERT_TRUE(h1 != h3);

    u64 h_empty = StringView_Hash(StringView_Empty());

    ASSERT_TRUE(h_empty != h1);
}
