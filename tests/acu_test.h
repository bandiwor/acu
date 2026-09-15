#pragma once

#include "defines/defines.h"
#include <setjmp.h>
#include <stdbool.h>
#include <string.h>

#define T_GRN "\x1b[32m"
#define T_RED "\x1b[31m"
#define T_YEL "\x1b[33m"
#define T_DIM "\x1b[90m"
#define T_RST "\x1b[0m"

typedef void (*AcuTestFn)(void);

typedef struct AcuTestCase {
    const char *suite;
    const char *name;
    AcuTestFn func;
    struct AcuTestCase *next;
} AcuTestCase;

extern AcuTestCase *acu_g_tests;
extern jmp_buf acu_g_test_buf;

void acu_test_register(AcuTestCase *tc);
attribute_noreturn void acu_test_fail(const char *file, int line, const char *fmt, ...);

#define TEST(suite_name, test_name)                                                                \
    static void test_body_##suite_name##_##test_name(void);                                        \
    static AcuTestCase tc_##suite_name##_##test_name = {.suite = #suite_name,                      \
                                                        .name = #test_name,                        \
                                                        .func =                                    \
                                                            test_body_##suite_name##_##test_name,  \
                                                        .next = NULL};                             \
    __attribute__((constructor)) static void register_##suite_name##_##test_name(void) {           \
        acu_test_register(&tc_##suite_name##_##test_name);                                         \
    }                                                                                              \
    static void test_body_##suite_name##_##test_name(void)

#define ASSERT_TRUE(cond)                                                                          \
    do {                                                                                           \
        if (unlikely(!(cond)))                                                                     \
            acu_test_fail(__FILE__, __LINE__, "Condition failed: %s", #cond);                      \
    } while (0)

#define ASSERT_FALSE(cond)                                                                         \
    do {                                                                                           \
        if (unlikely(cond))                                                                        \
            acu_test_fail(__FILE__, __LINE__, "Condition expected to be false: %s", #cond);        \
    } while (0)

#define ASSERT_NOT_NULL(ptr)                                                                       \
    do {                                                                                           \
        if (unlikely((ptr) == NULL))                                                               \
            acu_test_fail(__FILE__, __LINE__, "Pointer is NULL: %s", #ptr);                        \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                            \
    do {                                                                                           \
        i64 _e = (i64)(expected);                                                                  \
        i64 _a = (i64)(actual);                                                                    \
        if (unlikely(_e != _a))                                                                    \
            acu_test_fail(__FILE__, __LINE__,                                                      \
                          "Equality failed: %s == %s\n      Expected: %lld\n      Actual:   %lld", \
                          #expected, #actual, _e, _a);                                             \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual)                                                          \
    do {                                                                                           \
        f64 _e = (f64)(expected);                                                                  \
        f64 _a = (f64)(actual);                                                                    \
        if (unlikely(abs_f64(_e - _a) > 1e-7)) {                                                   \
            acu_test_fail(__FILE__, __LINE__,                                                      \
                          "Float equality failed:\n      Expected: %f\n      Actual:   %f", _e,    \
                          _a);                                                                     \
        }                                                                                          \
    } while (0)

#define ASSERT_EQ_STR(expected, actual)                                                            \
    do {                                                                                           \
        const char *_e = (const char *)(expected);                                                 \
        const char *_a = (const char *)(actual);                                                   \
        if (unlikely(strcmp(_e ? _e : "", _a ? _a : "") != 0))                                     \
            acu_test_fail(__FILE__, __LINE__,                                                      \
                          "String equality failed: %s == %s\n      Expected: \"%s\"\n      "       \
                          "Actual:   \"%s\"",                                                      \
                          #expected, #actual, _e, _a);                                             \
    } while (0)

#define ACU_PADDED_STR(str)                                                                        \
    __extension__({                                                                                \
        const char *_s = (str);                                                                    \
        size_t _len = strlen(_s);                                                                  \
        u8 *_buf = (u8 *)__builtin_alloca(_len + 32);                                              \
        __builtin_memcpy(_buf, _s, _len);                                                          \
        __builtin_memset(_buf + _len, 0, 32);                                                      \
        (const u8 *)_buf;                                                                          \
    })
