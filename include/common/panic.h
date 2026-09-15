#pragma once

#include "defines/defines.h"
#include <stdnoreturn.h>

attribute_cold noreturn void acu_panic_impl(const char *file, int line, const char *func,
                                            const char *fmt, ...);

#define acu_panic(...) acu_panic_impl(__FILE__, __LINE__, __func__, __VA_ARGS__)

#define acu_unreachable(...)                                                                       \
    do {                                                                                           \
        acu_panic("UNREACHABLE: " __VA_ARGS__);                                                    \
        __builtin_unreachable();                                                                   \
    } while (0)

#ifdef DEBUG
#define acu_panic_debug acu_panic
#define acu_unreachable_debug acu_unreachable
#else
#define acu_panic_debug(...) WRAPPER_START WRAPPER_END
#define acu_unreachable_debug(...) __builtin_unreachable()
#endif
