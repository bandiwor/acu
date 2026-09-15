#include "common/panic.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

attribute_cold noreturn void acu_panic_impl(const char *file, int line, const char *func,
                                            const char *fmt, ...) {
#ifdef DEBUG
    fprintf(stderr, "\n\033[1;31m[ACU PANIC]\033[0m at %s:%d (in %s):\n  ", file, line, func);
#else
    (void)file;
    (void)line;
    (void)func;
    fprintf(stderr, "\n\033[1;31m[ACU PANIC]\033[0m\n  ");
#endif
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n\n");

    abort();
}
