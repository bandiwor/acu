#pragma once

#include "common/error.h"
#include "defines/defines.h"
#include "lexer/token.h"
#include "memory/vector.h"

typedef struct {
    const u8 *start;
    const u8 *limit;
    const u8 *current;
    const u8 *token_start;
    AcuError *error;
    FileId file_id;
} AcuLexer;

typedef TypedVector(AcuToken) AcuTokenVector;

AcuLexer AcuLexer_Create(StringView sv, AcuError *error, FileId file_id);
void AcuLexer_Reset(AcuLexer *lexer);

AcuToken AcuLexer_Next(AcuLexer *lexer);
AcuTokenVector AcuLexer_Tokenize(AcuLexer *restrict lexer, bool *restrict is_success);
