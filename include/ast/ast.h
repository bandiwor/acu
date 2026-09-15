#pragma once

#include "defines/defines.h"
#include "lexer/token.h"

#define X_AST_NODE_KIND(X)                                                                         \
    X(AST_MODULE, "module")                                                                        \
    X(AST_DISCARD, "discard")                                                                      \
    X(AST_BLOCK, "block")                                                                          \
    X(AST_IF, "if")                                                                                \
    X(AST_LOOP, "loop")                                                                            \
    X(AST_WHILE, "while")                                                                          \
    X(AST_LITERAL, "literal")                                                                      \
    X(AST_IDENTIFIER, "ident")                                                                     \
    X(AST_BINARY, "binary")                                                                        \
    X(AST_UNARY, "unary")                                                                          \
    X(AST_ASSIGN, "assign")                                                                        \
    X(AST_VAR_DECL, "var-decl")                                                                    \
    X(AST_FN_DECL, "fn-decl")                                                                      \
    X(AST_FN_PARAM_DECL, "fn-param")                                                               \
    X(AST_TYPE_CAST, "cast")                                                                       \
    X(AST_BREAK, "break")                                                                          \
    X(AST_CONTINUE, "continue")                                                                    \
    X(AST_CALL, "call")                                                                            \
    X(AST_UNIT, "unit")                                                                            \
    X(AST_TUPLE, "tuple")                                                                          \
    X(AST_RETURN, "return")                                                                        \
    X(AST_IMPORT, "import")                                                                        \
    X(AST_DOT, "dot")

#define X_AST_BINARY_NODE_KIND(X)                                                                  \
    X(AST_BINARY_ADD, "+", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_BINARY_SUB, "-", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_BINARY_MUL, "*", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_BINARY_POW, "**", AST_OPERATOR_CATEGORY_ARITHMETIC)                                      \
    X(AST_BINARY_DIV, "/", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_BINARY_REM, "%", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_BINARY_BITWISE_AND, "&", AST_OPERATOR_CATEGORY_BITWISE)                                  \
    X(AST_BINARY_BITWISE_OR, "|", AST_OPERATOR_CATEGORY_BITWISE)                                   \
    X(AST_BINARY_BITWISE_XOR, "^", AST_OPERATOR_CATEGORY_BITWISE)                                  \
    X(AST_BINARY_SHL, "<<", AST_OPERATOR_CATEGORY_BITWISE)                                         \
    X(AST_BINARY_SHR, ">>", AST_OPERATOR_CATEGORY_BITWISE)                                         \
    X(AST_BINARY_LT, "<", AST_OPERATOR_CATEGORY_COMPARE)                                           \
    X(AST_BINARY_LTE, "<=", AST_OPERATOR_CATEGORY_COMPARE)                                         \
    X(AST_BINARY_GT, ">", AST_OPERATOR_CATEGORY_COMPARE)                                           \
    X(AST_BINARY_GTE, ">=", AST_OPERATOR_CATEGORY_COMPARE)                                         \
    X(AST_BINARY_EQ, "==", AST_OPERATOR_CATEGORY_COMPARE)                                          \
    X(AST_BINARY_NEQ, "!=", AST_OPERATOR_CATEGORY_COMPARE)                                         \
    X(AST_BINARY_LOGICAL_AND, "&&", AST_OPERATOR_CATEGORY_LOGIC)                                   \
    X(AST_BINARY_LOGICAL_OR, "||", AST_OPERATOR_CATEGORY_LOGIC)

#define X_AST_UNARY_NODE_KIND(X)                                                                   \
    X(AST_UNARY_BITWISE_NOT, "~", AST_OPERATOR_CATEGORY_BITWISE)                                   \
    X(AST_UNARY_LOGICAL_NOT, "!", AST_OPERATOR_CATEGORY_LOGIC)                                     \
    X(AST_UNARY_PLUS, "+", AST_OPERATOR_CATEGORY_ARITHMETIC)                                       \
    X(AST_UNARY_MINUS, "-", AST_OPERATOR_CATEGORY_ARITHMETIC)

#define X_AST_ASSIGN_NODE_KIND(X)                                                                  \
    X(AST_ASSIGN_EQ, "=")                                                                          \
    X(AST_ASSIGN_ADD_EQ, "+=")                                                                     \
    X(AST_ASSIGN_SUB_EQ, "-=")                                                                     \
    X(AST_ASSIGN_MUL_EQ, "*=")                                                                     \
    X(AST_ASSIGN_POW_EQ, "**=")                                                                    \
    X(AST_ASSIGN_DIV_EQ, "/=")                                                                     \
    X(AST_ASSIGN_REM_EQ, "%=")                                                                     \
    X(AST_ASSIGN_BITWISE_AND_EQ, "&=")                                                             \
    X(AST_ASSIGN_BITWISE_OR_EQ, "|=")                                                              \
    X(AST_ASSIGN_BITWISE_XOR_EQ, "^=")                                                             \
    X(AST_ASSIGN_SHL_EQ, "<<=")                                                                    \
    X(AST_ASSIGN_SHR_EQ, ">>=")

#define X_ACU_PRECEDENCE(X)                                                                        \
    X(ACU_PRECEDENCE_NONE, "None")                                                                 \
    X(ACU_PRECEDENCE_ASSIGN, "Assign")                                                             \
    X(ACU_PRECEDENCE_CONDITIONAL, "Conditional")                                                   \
    X(ACU_PRECEDENCE_LOGICAL_OR, "Logical Or")                                                     \
    X(ACU_PRECEDENCE_LOGICAL_AND, "Logical And")                                                   \
    X(ACU_PRECEDENCE_BITWISE_OR, "Bitwise Or")                                                     \
    X(ACU_PRECEDENCE_BITWISE_XOR, "Bitwise Xor")                                                   \
    X(ACU_PRECEDENCE_BITWISE_AND, "Bitwise And")                                                   \
    X(ACU_PRECEDENCE_EQUALITY, "Equality")                                                         \
    X(ACU_PRECEDENCE_COMPARISON, "Comparison")                                                     \
    X(ACU_PRECEDENCE_SHIFT, "Shift")                                                               \
    X(ACU_PRECEDENCE_TERM, "Term")                                                                 \
    X(ACU_PRECEDENCE_FACTOR, "Factor")                                                             \
    X(ACU_PRECEDENCE_POWER, "Power")                                                               \
    X(ACU_PRECEDENCE_UNARY, "Unary")                                                               \
    X(ACU_PRECEDENCE_POSTFIX, "Postfix")                                                           \
    X(ACU_PRECEDENCE_PRIMARY, "Primary")

typedef enum {
#define X(id, name) id,
    X_ACU_PRECEDENCE(X)
#undef X
} AcuPrecedence;

attribute_const const u8 *AcuPrecedence_String(AcuPrecedence);

attribute_const AcuPrecedence AcuPrecedence_FromTokenType(AcuTokenType);

typedef enum {
#define X(name, text) name,
    X_AST_NODE_KIND(X)
#undef X
} AstNodeKind;

const u8 *AstNodeKind_String(AstNodeKind);

typedef enum {
#define X(name, text, category) name,
    X_AST_BINARY_NODE_KIND(X)
#undef X
} AstBinaryKind;

const u8 *AstBinaryKind_String(AstBinaryKind);

typedef enum {
    AST_OPERATOR_CATEGORY_ARITHMETIC,
    AST_OPERATOR_CATEGORY_BITWISE,
    AST_OPERATOR_CATEGORY_LOGIC,
    AST_OPERATOR_CATEGORY_COMPARE,
} AstOperatorCategory;

typedef enum {
#define X(name, text, category) name,
    X_AST_UNARY_NODE_KIND(X)
#undef X
} AstUnaryKind;

const u8 *AstUnaryKind_String(AstUnaryKind);

attribute_const AstOperatorCategory AstBinaryKind_GetCategory(AstBinaryKind op);
attribute_const AstOperatorCategory AstUnaryKind_GetCategory(AstUnaryKind op);

typedef enum {
#define X(name, text) name,
    X_AST_ASSIGN_NODE_KIND(X)
#undef X
} AstAssignKind;

const u8 *AstAssignKind_String(AstAssignKind);

enum {
    AST_FLAG_EXPORTED = 1 << 0,
    AST_FLAG_MUTABLE = 1 << 1,
};

typedef struct {
    u16 type;
    u16 flags;

    union {
        struct {
            ModuleId module_id;
            ExtraIdx start_idx;
            u32 statements_count;
        } module;

        struct {
            AstBinaryKind type;
            AstNodeIdx right;
            AstNodeIdx left;
        } binary;

        struct {
            AstNodeIdx expr;
        } discard;

        struct {
            AstAssignKind type;
            AstNodeIdx target;
            AstNodeIdx value;
        } assign;

        struct {
            AstUnaryKind type;
            AstNodeIdx right;
        } unary;

        struct {
            StringId name;
            TypeAstNodeIdx type_hint;
            AstNodeIdx init_expr;
        } var_decl;

        struct {
            StringId name;
        } identifier;

        struct {
            LiteralIdx idx;
        } literal;

        struct {
            AstNodeIdx condition;
            AstNodeIdx then_body;
            AstNodeIdx else_body;
        } if_stmt;

        struct {
            AstNodeIdx body;
        } loop_stmt;

        struct {
            AstNodeIdx condition;
            AstNodeIdx body;
        } while_stmt;

        struct {
            AstNodeIdx expr;
        } break_stmt;

        struct {
            TypeAstNodeIdx target_type;
            AstNodeIdx expr;
        } type_cast;

        struct {
            AstNodeIdx object;
            AstNodeIdx member;
        } dot;

        struct {
            LiteralIdx module_name;
            StringId alias;
        } import_stmt;

        struct {
            AstNodeIdx expr;
        } ret_stmt;

        struct {
            AstNodeIdx object;
            ExtraIdx start_idx;
            u32 arguments_count;
        } call;

        struct {
            ExtraIdx start_idx;
            u32 statements_count;
        } block;

        struct {
            ExtraIdx start_idx;
            u32 fields_count;
        } tuple;

        struct {
            StringId name;
            TypeAstNodeIdx type;
            AstNodeIdx default_value;
        } fn_param_decl;

        struct {
            StringId name;
            AstNodeIdx body;
            u32 payload_idx;
        } fn_decl;
    } as;
} AstNode;

typedef struct {
    TypeAstNodeIdx return_type;
    ExtraIdx params_start_idx;
    u32 params_count;
} AstFnDeclPayload;
