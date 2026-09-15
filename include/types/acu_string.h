#pragma once

#include "defines/defines.h"
#include "types/string_view.h"
#include <stdarg.h>

typedef struct {
    u8 *data;
    u32 length;
    u32 capacity;
} AcuString;

AcuString AcuString_Create(u32 initial_capacity);
void AcuString_Free(AcuString *str);
void AcuString_Clear(AcuString *str);

attribute_pure StringView AcuString_View(const AcuString *str);

AcuString AcuString_CloneFromView(StringView sv);
AcuString AcuString_CloneFromCStr(const char *c_str);

void AcuString_Append(AcuString *str, StringView sv);
void AcuString_AppendCStr(AcuString *str, const char *c_str);
void AcuString_AppendChar(AcuString *str, u8 c);

attribute_format(printf, 2, 3) void AcuString_AppendFormat(AcuString *str, const char *fmt, ...);
attribute_format(printf, 2, 0) void AcuString_AppendFormatVa(AcuString *str, const char *fmt,
                                                             va_list args);
