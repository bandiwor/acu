#include "acu_test.h"
#include "literal/literal.h"
#include "parser/parse_number.h"

static inline f64 abs_f64(f64 x) {
    return x < 0.0 ? -x : x;
}

TEST(ParseNumber, BasicIntegers) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("0"));
    ASSERT_FALSE(r.is_float);
    ASSERT_FALSE(r.is_overflow);
    ASSERT_FALSE(r.has_suffix);
    ASSERT_TRUE(r.as.u64 == 0);

    r = Acu_ParseNumericLiteral(SV("42"));
    ASSERT_FALSE(r.is_float);
    ASSERT_TRUE(r.as.u64 == 42);

    r = Acu_ParseNumericLiteral(SV("1234567890"));
    ASSERT_FALSE(r.is_float);
    ASSERT_TRUE(r.as.u64 == 1234567890ULL);
}

TEST(ParseNumber, IntegerOverflow) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("18446744073709551615"));
    ASSERT_FALSE(r.is_float);
    ASSERT_FALSE(r.is_overflow);
    ASSERT_TRUE(r.as.u64 == 18446744073709551615ULL);

    r = Acu_ParseNumericLiteral(SV("18446744073709551616"));
    ASSERT_TRUE(r.is_overflow);

    r = Acu_ParseNumericLiteral(SV("99999999999999999999"));
    ASSERT_TRUE(r.is_overflow);
}

TEST(ParseNumber, BasicFloats) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("3.1415"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FALSE(r.has_suffix);
    ASSERT_FLOAT_EQ(3.1415, r.as.f64);

    r = Acu_ParseNumericLiteral(SV("0.0"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FLOAT_EQ(0.0, r.as.f64);

    r = Acu_ParseNumericLiteral(SV(".5"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FLOAT_EQ(0.5, r.as.f64);
}

TEST(ParseNumber, ScientificFloats) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("1e3"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FLOAT_EQ(1000.0, r.as.f64);

    r = Acu_ParseNumericLiteral(SV("1.25e-2"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FLOAT_EQ(0.0125, r.as.f64);

    r = Acu_ParseNumericLiteral(SV("5E+2"));
    ASSERT_TRUE(r.is_float);
    ASSERT_FLOAT_EQ(500.0, r.as.f64);

    r = Acu_ParseNumericLiteral(SV("1e25"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.as.f64 >= 0.99e25 && r.as.f64 <= 1.01e25);

    r = Acu_ParseNumericLiteral(SV("1e-25"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.as.f64 >= 0.99e-25 && r.as.f64 <= 1.01e-25);
}

TEST(ParseNumber, IntegerSuffixes) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("42i8"));
    ASSERT_FALSE(r.is_float);
    ASSERT_TRUE(r.has_suffix);
    ASSERT_TRUE(r.is_suffix_valid);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_I8, r.suffix_kind);
    ASSERT_TRUE(r.as.u64 == 42);

    r = Acu_ParseNumericLiteral(SV("1000u32"));
    ASSERT_FALSE(r.is_float);
    ASSERT_TRUE(r.is_suffix_valid);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_U32, r.suffix_kind);
    ASSERT_TRUE(r.as.u64 == 1000);

    r = Acu_ParseNumericLiteral(SV("0i64"));
    ASSERT_TRUE(r.is_suffix_valid);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_I64, r.suffix_kind);
}

TEST(ParseNumber, FloatSuffixesAndPromotion) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("42f32"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.has_suffix);
    ASSERT_TRUE(r.is_suffix_valid);
    ASSERT_EQ_INT(ACU_FLOAT_SUFFIX_F32, r.suffix_kind);
    ASSERT_FLOAT_EQ(42.0, r.as.f64);

    r = Acu_ParseNumericLiteral(SV("3.14f64"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.is_suffix_valid);
    ASSERT_EQ_INT(ACU_FLOAT_SUFFIX_F64, r.suffix_kind);
    ASSERT_FLOAT_EQ(3.14, r.as.f64);
}

TEST(ParseNumber, InvalidSuffixes) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV("42foo"));
    ASSERT_FALSE(r.is_float);
    ASSERT_TRUE(r.has_suffix);
    ASSERT_FALSE(r.is_suffix_valid);

    r = Acu_ParseNumericLiteral(SV("3.14i32"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.has_suffix);
    ASSERT_FALSE(r.is_suffix_valid);

    r = Acu_ParseNumericLiteral(SV("1e3u64"));
    ASSERT_TRUE(r.is_float);
    ASSERT_TRUE(r.has_suffix);
    ASSERT_FALSE(r.is_suffix_valid);
}

TEST(ParseNumber, EmptyString) {
    AcuNumericResult r;

    r = Acu_ParseNumericLiteral(SV(""));
    ASSERT_FALSE(r.is_float);
    ASSERT_FALSE(r.has_suffix);
    ASSERT_TRUE(r.as.u64 == 0);
}
