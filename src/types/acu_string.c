#include "types/acu_string.h"
#include "common/panic.h"
#include <stdio.h>
#include <stdlib.h>

#define ACU_ASSERT_ALLOC(ptr)                                                                      \
    do {                                                                                           \
        if (unlikely(!(ptr))) {                                                                    \
            acu_panic("[FATAL] OOM in AcuString allocation.");                                     \
        }                                                                                          \
    } while (0)

static void AcuString_EnsureCapacity(AcuString *str, u32 required_length) {
    if (likely(str->capacity >= required_length)) {
        return;
    }

    u32 new_capacity = str->capacity == 0 ? 16 : str->capacity;
    while (new_capacity < required_length) {
        new_capacity = new_capacity * 3 / 2;
    }

    u8 *new_data = realloc(str->data, new_capacity);
    ACU_ASSERT_ALLOC(new_data);

    str->data = new_data;
    str->capacity = new_capacity;
}

AcuString AcuString_Create(u32 initial_capacity) {
    AcuString str = {0};
    if (initial_capacity > 0) {
        AcuString_EnsureCapacity(&str, initial_capacity);
    }
    return str;
}

void AcuString_Free(AcuString *str) {
    if (str->data) {
        free(str->data);
        str->data = NULL;
    }
    str->length = 0;
    str->capacity = 0;
}

void AcuString_Clear(AcuString *str) {
    str->length = 0;
    if (str->capacity > 0) {
        str->data[0] = '\0';
    }
}

AcuString AcuString_CloneFromView(StringView sv) {
    AcuString str = AcuString_Create(sv.length);
    if (sv.length > 0) {
        __builtin_memcpy(str.data, sv.data, sv.length);
        str.length = sv.length;
    }
    return str;
}

attribute_pure StringView AcuString_View(const AcuString *str) {
    return (StringView){.data = str->data, .length = str->length};
}

AcuString AcuString_CloneFromCStr(const char *c_str) {
    return AcuString_CloneFromView(StringView_CStr(c_str));
}

void AcuString_Append(AcuString *str, StringView sv) {
    if (unlikely(sv.length == 0))
        return;

    AcuString_EnsureCapacity(str, str->length + sv.length);

    __builtin_memcpy(str->data + str->length, sv.data, sv.length);
    str->length += sv.length;
}

void AcuString_AppendCStr(AcuString *str, const char *c_str) {
    AcuString_Append(str, StringView_CStr(c_str));
}

void AcuString_AppendChar(AcuString *str, u8 c) {
    AcuString_EnsureCapacity(str, str->length + 1);

    str->data[str->length++] = c;
}

void AcuString_AppendFormatVa(AcuString *str, const char *fmt, va_list args) {
    va_list args_copy;
    va_copy(args_copy, args);

    u32 space_left = str->capacity > str->length ? str->capacity - str->length : 0;

    int written = vsnprintf((char *)str->data + str->length, space_left, fmt, args);

    if (written < 0) {
        va_end(args_copy);
        return;
    }

    u32 required_len = (u32)written;

    if (required_len >= space_left) {
        AcuString_EnsureCapacity(str, str->length + required_len);
        vsnprintf((char *)str->data + str->length, str->capacity - str->length, fmt, args_copy);
    }

    str->length += required_len;
    va_end(args_copy);
}

void AcuString_AppendFormat(AcuString *str, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AcuString_AppendFormatVa(str, fmt, args);
    va_end(args);
}
