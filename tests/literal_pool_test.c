#include "acu_test.h"
#include "literal/literal_pool.h"
#include "types/string_view.h"

// 1. Инициализация и освобождение памяти
TEST(LiteralPool, InitAndFree) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    // При создании пула массивы должны быть пустыми
    ASSERT_EQ_INT(0, V_Count(&pool.items));
    ASSERT_EQ_INT(0, V_Count(&pool.string_blob));

    AcuLiteralPool_Free(&pool);
}

// 2. Тестирование целочисленных литералов (знаковых)
TEST(LiteralPool, PushInt) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    LiteralIdx idx1 = AcuLiteralPool_PushInt(&pool, 42, ACU_INT_SUFFIX_NONE);
    LiteralIdx idx2 = AcuLiteralPool_PushInt(&pool, -123456, ACU_INT_SUFFIX_I32);

    ASSERT_EQ_INT(0, idx1);
    ASSERT_EQ_INT(1, idx2);
    ASSERT_EQ_INT(2, V_Count(&pool.items));

    AcuLiteral *lit1 = AcuLiteralPool_Get(&pool, idx1);
    ASSERT_EQ_INT(ACU_LITERAL_I64, lit1->kind);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_NONE, lit1->suffix);
    ASSERT_EQ_INT(42, lit1->as.i64);

    AcuLiteral *lit2 = AcuLiteralPool_Get(&pool, idx2);
    ASSERT_EQ_INT(ACU_LITERAL_I64, lit2->kind);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_I32, lit2->suffix);
    ASSERT_EQ_INT(-123456, lit2->as.i64);

    AcuLiteralPool_Free(&pool);
}

// 3. Тестирование беззнаковых литералов (u64)
TEST(LiteralPool, PushUint) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    u64 huge_val = 0xFFFFFFFFFFFFFFFFULL; // Максимальное значение u64
    LiteralIdx idx = AcuLiteralPool_PushUint(&pool, huge_val, ACU_INT_SUFFIX_U64);

    AcuLiteral *lit = AcuLiteralPool_Get(&pool, idx);
    ASSERT_EQ_INT(ACU_LITERAL_U64, lit->kind);
    ASSERT_EQ_INT(ACU_INT_SUFFIX_U64, lit->suffix);

    // Используем ASSERT_TRUE, так как ASSERT_EQ_INT приводит к i64,
    // что может исказить вывод ошибки для u64_MAX.
    ASSERT_TRUE(lit->as.u64 == huge_val);

    AcuLiteralPool_Free(&pool);
}

// 4. Тестирование чисел с плавающей точкой
TEST(LiteralPool, PushFloat) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    LiteralIdx idx = AcuLiteralPool_PushFloat(&pool, 3.1415926535, ACU_FLOAT_SUFFIX_F64);

    AcuLiteral *lit = AcuLiteralPool_Get(&pool, idx);
    ASSERT_EQ_INT(ACU_LITERAL_F64, lit->kind);
    ASSERT_EQ_INT(ACU_FLOAT_SUFFIX_F64, lit->suffix);

    // Прямое сравнение на == для float корректно в тестах,
    // если мы сами задали константу без арифметики.
    ASSERT_TRUE(lit->as.f64 == 3.1415926535);

    AcuLiteralPool_Free(&pool);
}

// 5. Тестирование строковых литералов и корректности блоба
TEST(LiteralPool, PushString) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    StringView s1 = SV("hello");
    StringView s2 = SV("acu_lang");
    StringView s3 = SV(""); // Пустая строка

    LiteralIdx idx1 = AcuLiteralPool_PushString(&pool, s1);
    LiteralIdx idx2 = AcuLiteralPool_PushString(&pool, s2);
    LiteralIdx idx3 = AcuLiteralPool_PushString(&pool, s3);

    // Проверяем первый литерал
    AcuLiteral *lit1 = AcuLiteralPool_Get(&pool, idx1);
    ASSERT_EQ_INT(ACU_LITERAL_STR, lit1->kind);
    ASSERT_EQ_INT(0, lit1->as.str.offset); // Начинается с 0
    ASSERT_EQ_INT(5, lit1->as.str.length);

    // Проверяем второй литерал (смещение должно быть = длине первой строки)
    AcuLiteral *lit2 = AcuLiteralPool_Get(&pool, idx2);
    ASSERT_EQ_INT(5, lit2->as.str.offset);
    ASSERT_EQ_INT(8, lit2->as.str.length);

    // Проверяем чтение строк обратно через AcuLiteralPool_GetString
    StringView res1 = AcuLiteralPool_GetString(&pool, idx1);
    ASSERT_TRUE(StringView_Equals(s1, res1));

    StringView res2 = AcuLiteralPool_GetString(&pool, idx2);
    ASSERT_TRUE(StringView_Equals(s2, res2));

    StringView res3 = AcuLiteralPool_GetString(&pool, idx3);
    ASSERT_TRUE(StringView_Equals(s3, res3));

    // Общий размер string_blob должен быть равен сумме длин строк (5 + 8 + 0 = 13)
    ASSERT_EQ_INT(13, V_Count(&pool.string_blob));

    AcuLiteralPool_Free(&pool);
}

// 6. Смешанное добавление
TEST(LiteralPool, MixedPush) {
    AcuLiteralPool pool = AcuLiteralPool_Create();

    LiteralIdx id_i = AcuLiteralPool_PushInt(&pool, 10, ACU_INT_SUFFIX_NONE);
    LiteralIdx id_s = AcuLiteralPool_PushString(&pool, SV("test"));
    LiteralIdx id_f = AcuLiteralPool_PushFloat(&pool, 2.5, ACU_FLOAT_SUFFIX_F32);

    ASSERT_EQ_INT(0, id_i);
    ASSERT_EQ_INT(1, id_s);
    ASSERT_EQ_INT(2, id_f);

    ASSERT_EQ_INT(ACU_LITERAL_I64, AcuLiteralPool_Get(&pool, id_i)->kind);
    ASSERT_EQ_INT(ACU_LITERAL_STR, AcuLiteralPool_Get(&pool, id_s)->kind);
    ASSERT_EQ_INT(ACU_LITERAL_F64, AcuLiteralPool_Get(&pool, id_f)->kind);

    AcuLiteralPool_Free(&pool);
}
