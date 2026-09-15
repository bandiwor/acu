#pragma once

#include "defines/defines.h"
#include "types/string_view.h"

#define X_ACU_TOKEN_TYPE(X)                                                                        \
    X(ACU_TOKEN_EOF, "<eof>", ACU_PRECEDENCE_NONE)                                                 \
    X(ACU_TOKEN_ERROR, "<error>", ACU_PRECEDENCE_NONE)                                             \
    X(ACU_TOKEN_IDENTIFIER, "<ident>", ACU_PRECEDENCE_NONE)                                        \
    X(ACU_TOKEN_INT_LITERAL, "<int literal>", ACU_PRECEDENCE_NONE)                                 \
    X(ACU_TOKEN_FLOAT_LITERAL, "<float literal>", ACU_PRECEDENCE_NONE)                             \
    X(ACU_TOKEN_STR_LITERAL, "<const u8* literal>", ACU_PRECEDENCE_NONE)                           \
    X(ACU_TOKEN_SINGLE_LINE_COMMENT, "<single comment>", ACU_PRECEDENCE_NONE)                      \
    X(ACU_TOKEN_MULTILINE_COMMENT, "<multi comment>", ACU_PRECEDENCE_NONE)                         \
    X(ACU_TOKEN_DOC_COMMENT, "<doc comment>", ACU_PRECEDENCE_NONE)                                 \
    X(ACU_TOKEN_I1_TYPE, "i1", ACU_PRECEDENCE_NONE)                                                \
    X(ACU_TOKEN_I8_TYPE, "i8", ACU_PRECEDENCE_NONE)                                                \
    X(ACU_TOKEN_I16_TYPE, "i16", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_I32_TYPE, "i32", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_I64_TYPE, "i64", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_U8_TYPE, "u8", ACU_PRECEDENCE_NONE)                                                \
    X(ACU_TOKEN_U16_TYPE, "u16", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_U32_TYPE, "u32", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_U64_TYPE, "u64", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_F32_TYPE, "f32", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_F64_TYPE, "f64", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_STR_TYPE, "str", ACU_PRECEDENCE_NONE)                                              \
    X(ACU_TOKEN_NAME_DECLARATION, "$", ACU_PRECEDENCE_NONE)                                        \
    X(ACU_TOKEN_CONDITION_STATEMENT, "#", ACU_PRECEDENCE_NONE)                                     \
    X(ACU_TOKEN_CONDITION_ELSE_IF, "!#", ACU_PRECEDENCE_NONE)                                      \
    X(ACU_TOKEN_WHILE_STATEMENT, "@", ACU_PRECEDENCE_NONE)                                         \
    X(ACU_TOKEN_FOR_STATEMENT, "%%", ACU_PRECEDENCE_NONE)                                          \
    X(ACU_TOKEN_NAMESPACE_DECLARATION, "<>", ACU_PRECEDENCE_NONE)                                  \
    X(ACU_TOKEN_STRUCTURE_DECLARATION, "$>", ACU_PRECEDENCE_NONE)                                  \
    X(ACU_TOKEN_IMPORT_STATEMENT, "+>", ACU_PRECEDENCE_NONE)                                       \
    X(ACU_TOKEN_EXPORT_STATEMENT, "<+", ACU_PRECEDENCE_NONE)                                       \
    X(ACU_TOKEN_BREAK_STATEMENT, "@^", ACU_PRECEDENCE_NONE)                                        \
    X(ACU_TOKEN_CONTINUE_STATEMENT, "@<", ACU_PRECEDENCE_NONE)                                     \
    X(ACU_TOKEN_FAT_ARROW_RIGHT, "=>", ACU_PRECEDENCE_NONE)                                        \
    X(ACU_TOKEN_COMMA, ",", ACU_PRECEDENCE_NONE)                                                   \
    X(ACU_TOKEN_SEMICOLON, ";", ACU_PRECEDENCE_NONE)                                               \
    X(ACU_TOKEN_COLON, ":", ACU_PRECEDENCE_NONE)                                                   \
    X(ACU_TOKEN_LBRACE, "{", ACU_PRECEDENCE_NONE)                                                  \
    X(ACU_TOKEN_RBRACE, "}", ACU_PRECEDENCE_NONE)                                                  \
    X(ACU_TOKEN_RPAREN, ")", ACU_PRECEDENCE_NONE)                                                  \
    X(ACU_TOKEN_RBRACKET, "]", ACU_PRECEDENCE_NONE)                                                \
    X(ACU_TOKEN_QUESTION_COLON, "?:", ACU_PRECEDENCE_CONDITIONAL)                                  \
    X(ACU_TOKEN_LPAREN, "(", ACU_PRECEDENCE_POSTFIX)                                               \
    X(ACU_TOKEN_LBRACKET, "[", ACU_PRECEDENCE_POSTFIX)                                             \
    X(ACU_TOKEN_DOT, ".", ACU_PRECEDENCE_POSTFIX)                                                  \
    X(ACU_TOKEN_ARROW_RIGHT, "->", ACU_PRECEDENCE_POSTFIX)                                         \
    X(ACU_TOKEN_TILDA, "~", ACU_PRECEDENCE_NONE)                                                   \
    X(ACU_TOKEN_EXCLAMATION_MARK, "!", ACU_PRECEDENCE_NONE)                                        \
    X(ACU_TOKEN_PLUS, "+", ACU_PRECEDENCE_TERM)                                                    \
    X(ACU_TOKEN_MINUS, "-", ACU_PRECEDENCE_TERM)                                                   \
    X(ACU_TOKEN_STAR, "*", ACU_PRECEDENCE_FACTOR)                                                  \
    X(ACU_TOKEN_STAR_STAR, "**", ACU_PRECEDENCE_POWER)                                             \
    X(ACU_TOKEN_SLASH, "/", ACU_PRECEDENCE_FACTOR)                                                 \
    X(ACU_TOKEN_PERCENT, "%", ACU_PRECEDENCE_FACTOR)                                               \
    X(ACU_TOKEN_BITWISE_AND, "&", ACU_PRECEDENCE_BITWISE_AND)                                      \
    X(ACU_TOKEN_BITWISE_OR, "|", ACU_PRECEDENCE_BITWISE_OR)                                        \
    X(ACU_TOKEN_CIRCUMFLEX, "^", ACU_PRECEDENCE_BITWISE_XOR)                                       \
    X(ACU_TOKEN_SHIFT_LEFT, "<<", ACU_PRECEDENCE_SHIFT)                                            \
    X(ACU_TOKEN_SHIFT_RIGHT, ">>", ACU_PRECEDENCE_SHIFT)                                           \
    X(ACU_TOKEN_LANGLE, "<", ACU_PRECEDENCE_COMPARISON)                                            \
    X(ACU_TOKEN_LESS_THAN_OR_EQUALS, "<=", ACU_PRECEDENCE_COMPARISON)                              \
    X(ACU_TOKEN_RANGLE, ">", ACU_PRECEDENCE_COMPARISON)                                            \
    X(ACU_TOKEN_GREATER_THAN_OR_EQUALS, ">=", ACU_PRECEDENCE_COMPARISON)                           \
    X(ACU_TOKEN_EQUAL_EQUAL, "==", ACU_PRECEDENCE_EQUALITY)                                        \
    X(ACU_TOKEN_NOT_EQUAL, "!=", ACU_PRECEDENCE_EQUALITY)                                          \
    X(ACU_TOKEN_LOGICAL_AND, "&&", ACU_PRECEDENCE_LOGICAL_AND)                                     \
    X(ACU_TOKEN_LOGICAL_OR, "||", ACU_PRECEDENCE_LOGICAL_OR)                                       \
    X(ACU_TOKEN_EQUAL, "=", ACU_PRECEDENCE_ASSIGN)                                                 \
    X(ACU_TOKEN_PLUS_EQUAL, "+=", ACU_PRECEDENCE_ASSIGN)                                           \
    X(ACU_TOKEN_MINUS_EQUAL, "-=", ACU_PRECEDENCE_ASSIGN)                                          \
    X(ACU_TOKEN_STAR_EQUAL, "*=", ACU_PRECEDENCE_ASSIGN)                                           \
    X(ACU_TOKEN_STAR_STAR_EQUAL, "**=", ACU_PRECEDENCE_ASSIGN)                                     \
    X(ACU_TOKEN_SLASH_EQUAL, "/=", ACU_PRECEDENCE_ASSIGN)                                          \
    X(ACU_TOKEN_PERCENT_EQUAL, "%=", ACU_PRECEDENCE_ASSIGN)                                        \
    X(ACU_TOKEN_BITWISE_AND_EQUAL, "&=", ACU_PRECEDENCE_ASSIGN)                                    \
    X(ACU_TOKEN_BITWISE_OR_EQUAL, "|=", ACU_PRECEDENCE_ASSIGN)                                     \
    X(ACU_TOKEN_CIRCUMFLEX_EQUAL, "^=", ACU_PRECEDENCE_ASSIGN)                                     \
    X(ACU_TOKEN_SHIFT_LEFT_EQUAL, "<<=", ACU_PRECEDENCE_ASSIGN)                                    \
    X(ACU_TOKEN_SHIFT_RIGHT_EQUAL, ">>=", ACU_PRECEDENCE_ASSIGN)                                   \
    X(ACU_TOKEN_UNKNOWN, "<unknown>", ACU_PRECEDENCE_NONE)

typedef enum {
#define X(name, text, prec) name,
    X_ACU_TOKEN_TYPE(X)
#undef X
        ACU_TOKEN_COUNT
} AcuTokenType;

attribute_const const u8 *AcuTokenType_String(AcuTokenType);

typedef struct {
    u16 type;
    u16 length;
    u32 offset;
} AcuToken;

attribute_const StringView AcuToken_GetStringView(AcuToken token, StringView source);
attribute_const_release StringView AcuToken_GetStringText(AcuToken token, StringView source);
