#include "acu_test.h"
#include "interner/type_interner.h"

TEST(TypeInterner, InitAndPrimitives) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    ASSERT_EQ_INT(15, interner.count);
    ASSERT_TRUE(V_Capacity(&interner.hash_table) >= 4096);

    TypeId p_i32 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_I32);
    TypeId p_i32_dup = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_I32);
    TypeId p_str = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_STR);

    ASSERT_EQ_INT(p_i32, p_i32_dup);

    ASSERT_TRUE(p_i32 != p_str);

    ASSERT_EQ_INT(15, interner.count);

    AcuType *t1 = AcuTypeInterner_GetType(&interner, p_i32);
    ASSERT_EQ_INT(ACU_TYPE_PRIMITIVE, t1->kind);
    ASSERT_EQ_INT(TYPE_PRIMITIVE_I32, t1->as.primitive.kind);

    AcuTypeInterner_Free(&interner);
}

TEST(TypeInterner, Vectors) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    TypeId base_i32 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_I32);
    TypeId base_f32 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_F32);

    TypeId v1 = AcuTypeInterner_GetVector(&interner, base_i32);
    TypeId v1_dup = AcuTypeInterner_GetVector(&interner, base_i32);
    TypeId v2 = AcuTypeInterner_GetVector(&interner, base_f32);

    ASSERT_EQ_INT(v1, v1_dup);
    ASSERT_TRUE(v1 != v2);

    AcuType *t_v1 = AcuTypeInterner_GetType(&interner, v1);
    ASSERT_EQ_INT(ACU_TYPE_VECTOR, t_v1->kind);
    ASSERT_EQ_INT(base_i32, t_v1->as.vector.inner);

    AcuTypeInterner_Free(&interner);
}

TEST(TypeInterner, Arrays) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    TypeId base_u8 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_U8);

    TypeId a1 = AcuTypeInterner_GetArray(&interner, base_u8, 10);
    TypeId a1_dup = AcuTypeInterner_GetArray(&interner, base_u8, 10);
    TypeId a2 = AcuTypeInterner_GetArray(&interner, base_u8, 20); // другой размер

    ASSERT_EQ_INT(a1, a1_dup);
    ASSERT_TRUE(a1 != a2);

    AcuType *t_a1 = AcuTypeInterner_GetType(&interner, a1);
    ASSERT_EQ_INT(ACU_TYPE_ARRAY, t_a1->kind);
    ASSERT_EQ_INT(base_u8, t_a1->as.array.inner);
    ASSERT_EQ_INT(10, t_a1->as.array.size);

    AcuTypeInterner_Free(&interner);
}

TEST(TypeInterner, Tuples) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    TypeId p_i64 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_I64);
    TypeId p_f64 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_F64);

    TypeId fields_a[] = {p_i64, p_f64};
    TypeId fields_b[] = {p_i64, p_f64, p_i64};
    TypeId fields_c[] = {p_f64, p_i64};

    TypeId t1 = AcuTypeInterner_GetTuple(&interner, fields_a, 2);
    TypeId t1_dup = AcuTypeInterner_GetTuple(&interner, fields_a, 2);
    TypeId t2 = AcuTypeInterner_GetTuple(&interner, fields_b, 3);
    TypeId t3 = AcuTypeInterner_GetTuple(&interner, fields_c, 2);

    TypeId t_empty1 = AcuTypeInterner_GetTuple(&interner, NULL, 0);
    TypeId t_empty2 = AcuTypeInterner_GetTuple(&interner, NULL, 0);

    ASSERT_EQ_INT(t1, t1_dup);
    ASSERT_TRUE(t1 != t2);
    ASSERT_TRUE(t1 != t3);
    ASSERT_EQ_INT(t_empty1, t_empty2);

    AcuType *type_t1 = AcuTypeInterner_GetType(&interner, t1);
    ASSERT_EQ_INT(ACU_TYPE_TUPLE, type_t1->kind);
    ASSERT_EQ_INT(2, type_t1->as.tuple.count);

    AcuTypeInterner_Free(&interner);
}

TEST(TypeInterner, Functions) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    TypeId ret_unit = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_UNIT);
    TypeId param_i32 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_I32);
    TypeId param_str = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_STR);

    TypeId params[] = {param_i32, param_str};

    TypeId f1 = AcuTypeInterner_GetFunc(&interner, ret_unit, params, 2);
    TypeId f1_dup = AcuTypeInterner_GetFunc(&interner, ret_unit, params, 2);
    TypeId f_diff_ret = AcuTypeInterner_GetFunc(&interner, param_i32, params, 2);
    TypeId f_no_params1 = AcuTypeInterner_GetFunc(&interner, ret_unit, NULL, 0);
    TypeId f_no_params2 = AcuTypeInterner_GetFunc(&interner, ret_unit, NULL, 0);

    ASSERT_EQ_INT(f1, f1_dup);
    ASSERT_TRUE(f1 != f_diff_ret);
    ASSERT_EQ_INT(f_no_params1, f_no_params2);

    AcuType *type_f1 = AcuTypeInterner_GetType(&interner, f1);
    ASSERT_EQ_INT(ACU_TYPE_FUNC, type_f1->kind);
    ASSERT_EQ_INT(ret_unit, type_f1->as.func.return_type);
    ASSERT_EQ_INT(2, type_f1->as.func.count);

    AcuTypeInterner_Free(&interner);
}

TEST(TypeInterner, Rehashing) {
    AcuTypeInterner interner = AcuTypeInterner_Create();

    u32 initial_cap = V_Capacity(&interner.hash_table);
    TypeId p_u16 = AcuTypeInterner_GetPrimitive(&interner, TYPE_PRIMITIVE_U16);

    const int INSERT_COUNT = 3500;
    for (int i = 0; i < INSERT_COUNT; i++) {
        AcuTypeInterner_GetArray(&interner, p_u16, i);
    }

    u32 new_cap = V_Capacity(&interner.hash_table);

    ASSERT_TRUE(new_cap > initial_cap);
    ASSERT_EQ_INT(initial_cap * 2, new_cap);

    u32 count_before_check = interner.count;

    TypeId existing_arr = AcuTypeInterner_GetArray(&interner, p_u16, 1000);

    ASSERT_EQ_INT(count_before_check, interner.count);

    AcuType *t = AcuTypeInterner_GetType(&interner, existing_arr);
    ASSERT_EQ_INT(ACU_TYPE_ARRAY, t->kind);
    ASSERT_EQ_INT(1000, t->as.array.size);

    AcuTypeInterner_Free(&interner);
}
