#pragma once

#include "ast/ast_builder.h"
#include "common/error.h"
#include "defines/defines.h"
#include "interner/string_interner.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "literal/literal_pool.h"
#include "types/string_view.h"

typedef enum { LOOP_KIND_INFINITE, LOOP_KIND_CONDITIONAL } AcuParserLoopKind;

typedef struct {
    TypedVector(AstNodeIdx) scratch_nodes;
    TypedVector(u8) scratch_string;
    AcuErrorVector errors;
    AcuAstBuilder *builder;
    AcuStringInterner *interner;
    AcuLiteralPool *literal_pool;
    StringView source;
    AcuLexer lexer;
    AcuError lexer_error;
    AcuToken current_token;
    AcuToken previous_token;
    u32 errors_count;
    FileId file_id;
    ModuleId module_id;
    bool panic_mode;
} AcuParser;

void AcuParser_Init(AcuParser *restrict parser, AcuAstBuilder *restrict builder,
                    AcuStringInterner *restrict interner, AcuLiteralPool *restrict literal_pool,
                    StringView source, FileId file_id);

void AcuParser_Free(AcuParser *restrict parser);

AstNodeIdx AcuParser_ParseModule(AcuParser *restrict parser);
