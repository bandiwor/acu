#pragma once

#include "defines/defines.h"
#include "lexer/token.h"

typedef struct {
    u32 offset;
    u32 length;
    FileId file;
} AcuPosition;

attribute_const AcuPosition AcuPosition_FromToken(AcuToken token, FileId file);
