#pragma once

#include "ast/type_ast.h"
#include "defines/defines.h"
#include <stdbool.h>

static inline TypeAstNode TypeAstNode_Primitive(TypePrimitiveKind primitive_kind) {
    return (TypeAstNode){
        .kind = TYPE_AST_PRIMITIVE, .flags = 0, .primitive = {.kind = primitive_kind}};
}

static inline TypeAstNode TypeAstNode_Array(TypeAstNodeIdx inner, AstNodeIdx len_expr) {
    return (TypeAstNode){.kind = TYPE_AST_ARRAY,
                         .flags = 0,
                         .array = {.inner_type = inner, .length_expr = len_expr}};
}

static inline TypeAstNode TypeAstNode_Vector(TypeAstNodeIdx inner) {
    return (TypeAstNode){.kind = TYPE_AST_VECTOR, .flags = 0, .vector = {.inner_type = inner}};
}

static inline TypeAstNode TypeAstNode_Tuple(ExtraIdx start, u32 count) {
    return (TypeAstNode){
        .kind = TYPE_AST_TUPLE, .flags = 0, .tuple = {.fields_start = start, .count = count}};
}

static inline TypeAstNode TypeAstNode_Func(TypeAstNodeIdx ret, ExtraIdx params_start, u32 count) {
    return (TypeAstNode){
        .kind = TYPE_AST_FUNC,
        .flags = 0,
        .func = {.return_type = ret, .params_start = params_start, .params_count = count}};
}

static inline TypeAstNode TypeAstNode_Identifier(StringId name) {
    return (TypeAstNode){.kind = TYPE_AST_IDENTIFIER, .flags = 0, .identifier = {.name = name}};
}
