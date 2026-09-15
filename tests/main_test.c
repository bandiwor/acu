#include "acu_test.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

AcuTestCase *acu_g_tests = NULL;
jmp_buf acu_g_test_buf;

static u32 pass_count = 0;
static u32 fail_count = 0;

void acu_test_register(AcuTestCase *tc) {
    if (!acu_g_tests) {
        acu_g_tests = tc;
    } else {
        AcuTestCase *curr = acu_g_tests;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = tc;
    }
}

void acu_test_fail(const char *file, int line, const char *fmt, ...) {
    printf("\n" T_RED "    [FAIL] " T_RST "%s:%d\n      " T_RED, file, line);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf(T_RST "\n");
    longjmp(acu_g_test_buf, 1);
}

int main(void) {
    printf(T_DIM "=================================================\n" T_RST);
    printf("🚀 " T_GRN "Starting Acu VM Test Suite" T_RST "\n");
    printf(T_DIM "=================================================\n" T_RST);

    struct timespec start_time;
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    AcuTestCase *curr = acu_g_tests;
    const char *current_suite = NULL;

    while (curr) {
        if (current_suite == NULL || strcmp(current_suite, curr->suite) != 0) {
            current_suite = curr->suite;
            printf("\n📦 " T_YEL "Suite: %s" T_RST "\n", current_suite);
        }

        printf("  " T_DIM "├─" T_RST " %-35s ", curr->name);
        fflush(stdout);

        if (setjmp(acu_g_test_buf) == 0) {
            curr->func();
            printf(T_GRN "OK" T_RST "\n");
            pass_count++;
        } else {
            fail_count++;
        }
        curr = curr->next;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    const double time_spent = ((double)(end_time.tv_sec - start_time.tv_sec) * 1000.0) +
                              ((double)(end_time.tv_nsec - start_time.tv_nsec) / 1e6);

    printf("\n" T_DIM "=================================================\n" T_RST);
    if (fail_count == 0) {
        printf("🎉 " T_GRN "All %u tests passed!" T_RST " (%.2f ms)\n", pass_count, time_spent);
        return 0;
    }

    printf("❌ " T_RED "%u tests failed" T_RST ", %u passed. (%.2f ms)\n", fail_count, pass_count,
           time_spent);
    return 1;
}
