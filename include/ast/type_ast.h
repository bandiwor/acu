#pragma once
#include "defines/defines.h"
#include "defines/types.h"

typedef enum {
    TYPE_AST_PRIMITIVE,
    TYPE_AST_ARRAY,
    TYPE_AST_VECTOR,
    TYPE_AST_TUPLE,
    TYPE_AST_FUNC,
    TYPE_AST_IDENTIFIER
} TypeAstNodeKind;

typedef struct {
    u16 kind;
    u16 flags;

    union {
        struct {
            TypePrimitiveKind kind;
        } primitive;

        struct {
            TypeAstNodeIdx inner_type;
            AstNodeIdx length_expr;
        } array;

        struct {
            TypeAstNodeIdx inner_type;
        } vector;

        struct {
            ExtraIdx fields_start;
            u32 count;
        } tuple;

        struct {
            TypeAstNodeIdx return_type;
            ExtraIdx params_start;
            u32 params_count;
        } func;

        struct {
            StringId name;
        } identifier;
    };
} TypeAstNode;
