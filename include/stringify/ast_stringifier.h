#pragma once

#include "ast/ast_builder.h"
#include "defines/defines.h"
#include "interner/string_interner.h"
#include "interner/type_interner.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"

typedef TypedVector(u8) AcuAstStringifierBuffer;

typedef struct {
    AcuAstBuilder *builder;
    AcuStringInterner *interner;
    AcuTypeInterner *types;
    AcuLiteralPool *literal_pool;
    const TypeId *node_types;
    AcuAstStringifierBuffer buffer;
    u32 indent_level;
    bool pretty_print;
    bool print_types;
} AcuAstStringifier;

AcuAstStringifier AcuAstStringifier_Create(AcuAstBuilder *builder, AcuStringInterner *interner,
                                           AcuLiteralPool *literal_pool, AcuTypeInterner *types,
                                           const TypeId *node_types, bool pretty_print,
                                           bool print_types);

void AcuAstStringifier_Init(AcuAstStringifier *str, AcuAstBuilder *builder,
                            AcuStringInterner *interner, AcuLiteralPool *literal_pool,
                            AcuTypeInterner *types, const TypeId *node_types, bool pretty_print,
                            bool print_types);
void AcuAstStringifier_Destroy(AcuAstStringifier *str);

void AcuAstStringifier_StringifyNode(AcuAstStringifier *str, AstNodeIdx idx);
void AcuAstStringifier_StringifyTypeNode(AcuAstStringifier *str, TypeAstNodeIdx idx);

StringView AcuAstStringifier_GetSV(AcuAstStringifier *str);
