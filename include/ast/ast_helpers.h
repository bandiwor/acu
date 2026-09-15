#pragma once

#include "ast/ast.h"
#include "defines/defines.h"
#include <stdbool.h>

attribute_const static inline bool AstAssignOp_IsCompound(AstAssignKind op) {
    return op != AST_ASSIGN_EQ;
}

attribute_const static inline AstNode AstNode_Binary(AstBinaryKind op, AstNodeIdx left,
                                                     AstNodeIdx right) {
    return (AstNode){
        .type = AST_BINARY, .flags = 0, .as.binary = {.type = op, .left = left, .right = right}};
}

attribute_const static inline AstNode AstNode_Discard(AstNodeIdx expr) {
    return (AstNode){.type = AST_DISCARD, .flags = 0, .as.discard = {.expr = expr}};
}

attribute_const static inline AstNode AstNode_Unary(AstUnaryKind op, AstNodeIdx right) {
    return (AstNode){.type = AST_UNARY, .flags = 0, .as.unary = {.type = op, .right = right}};
}

attribute_const static inline AstNode AstNode_Assign(AstAssignKind op, AstNodeIdx target,
                                                     AstNodeIdx value) {
    return (AstNode){.type = AST_ASSIGN,
                     .flags = 0,
                     .as.assign = {.type = op, .target = target, .value = value}};
}

attribute_const static inline AstNode AstNode_If(AstNodeIdx condition, AstNodeIdx then_body,
                                                 AstNodeIdx else_body) {
    return (AstNode){
        .type = AST_IF,
        .flags = 0,
        .as.if_stmt = {.condition = condition, .then_body = then_body, .else_body = else_body}};
}

attribute_const static inline AstNode AstNode_Loop(AstNodeIdx body) {
    return (AstNode){.type = AST_LOOP, .flags = 0, .as.loop_stmt = {.body = body}};
}

attribute_const static inline AstNode AstNode_While(AstNodeIdx condition, AstNodeIdx body) {
    return (AstNode){
        .type = AST_WHILE, .flags = 0, .as.while_stmt = {.condition = condition, .body = body}};
}

attribute_const static inline AstNode AstNode_Call(AstNodeIdx object, ExtraIdx start_idx,
                                                   u32 arguments_count) {
    return (AstNode){
        .type = AST_CALL,
        .flags = 0,
        .as.call = {.object = object, .start_idx = start_idx, .arguments_count = arguments_count}};
}

attribute_const static inline AstNode AstNode_Return(AstNodeIdx expr) {
    return (AstNode){.type = AST_RETURN, .flags = 0, .as.ret_stmt = {.expr = expr}};
}

attribute_const static inline AstNode AstNode_VarDecl(StringId name, TypeAstNodeIdx type_hint,
                                                      AstNodeIdx init_expr, bool is_mutable,
                                                      bool is_exported) {
    u16 flags = 0;
    if (is_mutable)
        flags |= AST_FLAG_MUTABLE;
    if (is_exported)
        flags |= AST_FLAG_EXPORTED;

    return (AstNode){.type = AST_VAR_DECL,
                     .flags = flags,
                     .as.var_decl = {.name = name, .type_hint = type_hint, .init_expr = init_expr}};
}

attribute_const static inline AstNode AstNode_Identifier(StringId name) {
    return (AstNode){.type = AST_IDENTIFIER, .flags = 0, .as.identifier = {.name = name}};
}

attribute_const static inline AstNode AstNode_Literal(LiteralIdx literal) {
    return (AstNode){.type = AST_LITERAL, .flags = 0, .as.literal.idx = literal};
}

attribute_const static inline AstNode AstNode_Dot(AstNodeIdx object, AstNodeIdx member) {
    return (AstNode){.type = AST_DOT, .flags = 0, .as.dot = {.object = object, .member = member}};
}

attribute_const static inline AstNode AstNode_TypeCast(TypeAstNodeIdx target_type,
                                                       AstNodeIdx expr) {
    return (AstNode){.type = AST_TYPE_CAST,
                     .flags = 0,
                     .as.type_cast = {.target_type = target_type, .expr = expr}};
}

attribute_const static inline AstNode AstNode_Import(LiteralIdx module_name, StringId alias,
                                                     bool is_export) {
    return (AstNode){.type = AST_IMPORT,
                     .flags = (int)is_export ? AST_FLAG_EXPORTED : 0,
                     .as.import_stmt = {.module_name = module_name, .alias = alias}};
}

attribute_const static inline AstNode AstNode_Break(AstNodeIdx expr) {
    return (AstNode){.type = AST_BREAK, .flags = 0, .as.break_stmt = {.expr = expr}};
}

attribute_const static inline AstNode AstNode_Continue(void) {
    return (AstNode){.type = AST_CONTINUE, .flags = 0, .as = {0}};
}

attribute_const static inline AstNode AstNode_Block(ExtraIdx start_idx, u32 statements_count) {
    return (AstNode){.type = AST_BLOCK,
                     .flags = 0,
                     .as.block = {.start_idx = start_idx, .statements_count = statements_count}};
}

attribute_const static inline AstNode
AstNode_FunctionDeclaration(StringId name, AstNodeIdx body, bool is_exported, u32 payload_idx) {
    u16 flags = (int)is_exported ? AST_FLAG_EXPORTED : 0;
    return (AstNode){.type = AST_FN_DECL,
                     .flags = flags,
                     .as.fn_decl = {.name = name, .body = body, .payload_idx = payload_idx}};
}

attribute_const static inline AstNode AstNode_FunctionParamDeclaration(StringId name,
                                                                       TypeAstNodeIdx type,
                                                                       AstNodeIdx default_value,
                                                                       bool is_mutable) {
    u16 flags = (int)is_mutable ? AST_FLAG_MUTABLE : 0;
    return (AstNode){
        .type = AST_FN_PARAM_DECL,
        .flags = flags,
        .as.fn_param_decl = {.name = name, .type = type, .default_value = default_value}};
}

attribute_const static inline AstNode AstNode_Module(ModuleId id, ExtraIdx start_idx,
                                                     u32 statements_count) {
    return (AstNode){.type = AST_MODULE,
                     .flags = 0,
                     .as.module = {.module_id = id,
                                   .start_idx = start_idx,
                                   .statements_count = statements_count}};
}
