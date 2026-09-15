#pragma once

#include "defines/defines.h"
#include "types/string_view.h"

typedef struct {
    const u8 *data;
    u64 length_tagged;
} AcuFileBuffer;

AcuFileBuffer Acu_LoadFile(const char *path);

void Acu_FreeFile(AcuFileBuffer *fb);

StringView AcuFileBuffer_ToStringView(AcuFileBuffer);
