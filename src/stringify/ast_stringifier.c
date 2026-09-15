#include "stringify/ast_stringifier.h"
#include "ast/ast.h"
#include "ast/type_ast.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "interner/string_interner.h"
#include "interner/type_interner.h"
#include "literal/literal.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

AcuAstStringifier AcuAstStringifier_Create(AcuAstBuilder *builder, AcuStringInterner *interner,
                                           AcuLiteralPool *literal_pool, AcuTypeInterner *types,
                                           const TypeId *node_types, bool pretty_print,
                                           bool print_types) {
    AcuAstStringifier stringifier = {0};
    AcuAstStringifier_Init(&stringifier, builder, interner, literal_pool, types, node_types,
                           pretty_print, print_types);
    return stringifier;
}

void AcuAstStringifier_Init(AcuAstStringifier *str, AcuAstBuilder *builder,
                            AcuStringInterner *interner, AcuLiteralPool *literal_pool,
                            AcuTypeInterner *types, const TypeId *node_types, bool pretty_print,
                            bool print_types) {
    str->builder = builder;
    str->interner = interner;
    str->literal_pool = literal_pool;
    str->pretty_print = pretty_print;
    str->print_types = print_types;
    str->node_types = node_types;
    str->types = types;
    str->indent_level = 0;

    V_Init(&str->buffer);
}

void AcuAstStringifier_Destroy(AcuAstStringifier *str) {
    V_Free(&str->buffer);
    *str = (AcuAstStringifier){0};
}

static void AcuAstStringifier_AppendStr(AcuAstStringifier *str, const char *text, size_t len) {
    if (unlikely(len == 0)) {
        return;
    }

    u32 current_count = V_Count(&str->buffer);
    V_EnsureCapacity(&str->buffer, current_count + len);

    char *dest = (char *)V_Elements(&str->buffer) + current_count;

    __builtin_memcpy(dest, text, len);

    V_AddCount(&str->buffer, (u32)len);
}

static void AcuAstStringifier_Append(AcuAstStringifier *str, const char *fmt, ...) {
    char stack_buf[256];
    va_list args;
    va_start(args, fmt);

    int len = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    if (unlikely(len < 0))
        return;

    if (likely(len < (int)sizeof(stack_buf))) {
        AcuAstStringifier_AppendStr(str, stack_buf, (size_t)len);
        return;
    }

    char *heap_buf = (char *)__builtin_malloc(len + 1);
    if (unlikely(heap_buf == NULL)) {
        acu_unreachable_debug("malloc returns NULL-ptr.");
    }

    va_start(args, fmt);
    vsnprintf(heap_buf, len + 1, fmt, args);
    va_end(args);
    AcuAstStringifier_AppendStr(str, heap_buf, (size_t)len);
    __builtin_free(heap_buf);
}

static void AcuAstStringifier_PrintIndent(AcuAstStringifier *str) {
    if (!str->pretty_print)
        return;
    for (u32 i = 0; i < str->indent_level; i++) {
        AcuAstStringifier_AppendStr(str, " ", 1);
    }
}

static void AstFmt_Open(AcuAstStringifier *str) {
    AcuAstStringifier_AppendStr(str, "(", 1);
    if (str->pretty_print) {
        str->indent_level += 2; // Шаг отступа = 2 пробела
        AcuAstStringifier_AppendStr(str, "\n", 1);
        AcuAstStringifier_PrintIndent(str);
    }
}

static void AstFmt_Next(AcuAstStringifier *str) {
    if (str->pretty_print) {
        AcuAstStringifier_AppendStr(str, ",\n", 2);
        AcuAstStringifier_PrintIndent(str);
    } else {
        AcuAstStringifier_AppendStr(str, ", ", 2);
    }
}

static void AstFmt_Close(AcuAstStringifier *str) {
    if (str->pretty_print) {
        if (str->indent_level >= 2)
            str->indent_level -= 2;
        AcuAstStringifier_AppendStr(str, "\n", 1);
        AcuAstStringifier_PrintIndent(str);
    }
    AcuAstStringifier_AppendStr(str, ")", 1);
}

static void AcuAstStringifier_StringifySemanticType(AcuAstStringifier *str, TypeId type_id) {
    if (type_id <= TYPE_PRIMITIVE_MODULE) {
        const char *prim_str = (const char *)TypePrimitiveKind_String((TypePrimitiveKind)type_id);
        AcuAstStringifier_AppendStr(str, prim_str, __builtin_strlen(prim_str));
        return;
    }

    const AcuType *type = AcuTypeInterner_GetType(str->types, type_id);

    switch (type->kind) {
        case ACU_TYPE_PRIMITIVE: {
            const char *prim_str = (const char *)TypePrimitiveKind_String(type->as.primitive.kind);
            AcuAstStringifier_AppendStr(str, prim_str, __builtin_strlen(prim_str));
            break;
        }

        case ACU_TYPE_ARRAY:
            AcuAstStringifier_AppendStr(str, "[", 1);
            AcuAstStringifier_StringifySemanticType(str, type->as.array.inner);
            AcuAstStringifier_Append(str, "; %u]", type->as.array.size);
            break;

        case ACU_TYPE_VECTOR:
            AcuAstStringifier_AppendStr(str, "[", 1);
            AcuAstStringifier_StringifySemanticType(str, type->as.vector.inner);
            AcuAstStringifier_AppendStr(str, "]", 1);
            break;

        case ACU_TYPE_TUPLE:
            AcuAstStringifier_AppendStr(str, "(", 1);
            for (u32 i = 0; i < type->as.tuple.count; i++) {
                if (i > 0)
                    AcuAstStringifier_AppendStr(str, ", ", 2);
                TypeId field_type =
                    AcuTypeInterner_GetTypeIdByExtra(str->types, type->as.tuple.fields_start + i);
                AcuAstStringifier_StringifySemanticType(str, field_type);
            }
            AcuAstStringifier_AppendStr(str, ")", 1);
            break;

        case ACU_TYPE_FUNC:
            AcuAstStringifier_AppendStr(str, "fn(", 3);
            for (u32 i = 0; i < type->as.func.count; i++) {
                if (i > 0)
                    AcuAstStringifier_AppendStr(str, ", ", 2);
                TypeId param_type =
                    AcuTypeInterner_GetTypeIdByExtra(str->types, type->as.func.params_start + i);
                AcuAstStringifier_StringifySemanticType(str, param_type);
            }
            AcuAstStringifier_AppendStr(str, ") -> ", 5);
            AcuAstStringifier_StringifySemanticType(str, type->as.func.return_type);
            break;

        default:
            AcuAstStringifier_AppendStr(str, "unknown_type", 12);
            break;
    }
}

static void AcuAstStringifier_AppendU64(AcuAstStringifier *str, u64 val) {
    char buf[32];
    char *p = buf + sizeof(buf);

    do {
        *--p = (char)('0' + (char)(val % 10));
        val /= 10;
    } while (val > 0);

    AcuAstStringifier_AppendStr(str, p, (size_t)((buf + sizeof(buf)) - p));
}

static void AcuAstStringifier_AppendI64(AcuAstStringifier *str, i64 val) {
    if (val < 0) {
        AcuAstStringifier_AppendStr(str, "-", 1);
        AcuAstStringifier_AppendU64(str, (u64)(-(val + 1)) + 1);
    } else {
        AcuAstStringifier_AppendU64(str, (u64)val);
    }
}

static void AcuAstStringifier_AppendF64(AcuAstStringifier *str, f64 val) {
    if (val < 0.0) {
        AcuAstStringifier_AppendStr(str, "-", 1);
        val = -val;
    }

    f64 int_part;
    f64 frac_part = modf(val, &int_part);

    AcuAstStringifier_AppendU64(str, (u64)int_part);
    AcuAstStringifier_AppendStr(str, ".", 1);

    u32 precision = 6;
    char frac_buf[16];
    u32 frac_len = 0;

    for (u32 i = 0; i < precision; i++) {
        frac_part *= 10.0;
        u8 digit = (u8)frac_part;
        frac_buf[frac_len++] = (char)('0' + digit);
        frac_part -= digit;
    }

    while (frac_len > 1 && frac_buf[frac_len - 1] == '0') {
        frac_len--;
    }

    AcuAstStringifier_AppendStr(str, frac_buf, frac_len);
}

static void AcuAstStringifier_AppendEscapedStr(AcuAstStringifier *str, const char *data,
                                               u32 length) {
    AcuAstStringifier_AppendStr(str, "\"", 1);

    for (u32 i = 0; i < length; i++) {
        char c = data[i];
        switch (c) {
            case '\n':
                AcuAstStringifier_AppendStr(str, "\\n", 2);
                break;
            case '\r':
                AcuAstStringifier_AppendStr(str, "\\r", 2);
                break;
            case '\t':
                AcuAstStringifier_AppendStr(str, "\\t", 2);
                break;
            case '\\':
                AcuAstStringifier_AppendStr(str, "\\\\", 2);
                break;
            case '\"':
                AcuAstStringifier_AppendStr(str, "\\\"", 2);
                break;
            case '\0':
                AcuAstStringifier_AppendStr(str, "\\0", 2);
                break;
            default:
                AcuAstStringifier_AppendStr(str, &c, 1);
                break;
        }
    }

    AcuAstStringifier_AppendStr(str, "\"", 1);
}

static void AcuAstStringifier_StringifyLiteralWithoutKindPrint(AcuAstStringifier *str,
                                                               LiteralIdx idx) {
    AcuLiteral literal = *AcuLiteralPool_Get(str->literal_pool, idx);

    switch (literal.kind) {
        case ACU_LITERAL_I64:
            AcuAstStringifier_AppendI64(str, literal.as.i64);
            break;

        case ACU_LITERAL_U64:
            AcuAstStringifier_AppendU64(str, literal.as.u64);
            break;

        case ACU_LITERAL_F64:
            AcuAstStringifier_AppendF64(str, literal.as.f64);
            break;

        case ACU_LITERAL_STR: {
            StringView literal_sv = AcuLiteralPool_GetString(str->literal_pool, idx);

            AcuAstStringifier_AppendEscapedStr(str, (const char *)literal_sv.data,
                                               literal_sv.length);
            return;
        }

        default:
            acu_unreachable_debug("Invalid literal kind (%d)", literal.kind);
    }

    AcuAstStringifier_AppendStr(str, ", ", 2);

    const char *suffix_str = NULL;
    if (literal.kind == ACU_LITERAL_F64) {
        suffix_str = (const char *)AcuFloatSuffix_String(literal.suffix);
    } else {
        suffix_str = (const char *)AcuIntSuffix_String(literal.suffix);
    }

    AcuAstStringifier_AppendStr(str, suffix_str, strlen(suffix_str));
}

static void AcuAstStringifier_StringifyLiteral(AcuAstStringifier *str, LiteralIdx idx) {
    AcuLiteral literal = *AcuLiteralPool_Get(str->literal_pool, idx);
    const char *kind_str = (const char *)AcuLiteralKind_String(literal.kind);

    AcuAstStringifier_AppendStr(str, kind_str, __builtin_strlen(kind_str));
    AcuAstStringifier_AppendStr(str, "(", 1);
    AcuAstStringifier_StringifyLiteralWithoutKindPrint(str, idx);
    AcuAstStringifier_AppendStr(str, ")", 1);
}

void AcuAstStringifier_StringifyNode(AcuAstStringifier *str, AstNodeIdx idx) {
    if (unlikely(IS_ACU_NULL_IDX(idx))) {
        AcuAstStringifier_Append(str, "null");
        return;
    }

    const AstNode *node = AcuAstBuilder_GetNode(str->builder, idx);
    const u32 *extras = V_Elements(&str->builder->extra_pool);

    switch ((AstNodeKind)node->type) {
        case AST_MODULE: {
            AcuAstStringifier_Append(str, "module[%d]", node->as.module.module_id);
            u32 start = node->as.module.start_idx;
            u32 count = node->as.module.statements_count;

            if (count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                AstFmt_Open(str);
                for (u32 i = 0; i < count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyNode(str, extras[start + i]);
                }
                AstFmt_Close(str);
            }
            break;
        }

        case AST_BLOCK: {
            AcuAstStringifier_AppendStr(str, "block", 5);
            u32 start = node->as.block.start_idx;
            u32 count = node->as.block.statements_count;

            if (count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                AstFmt_Open(str);
                for (u32 i = 0; i < count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyNode(str, extras[start + i]);
                }
                AstFmt_Close(str);
            }
            break;
        }

        case AST_DISCARD:
            AcuAstStringifier_AppendStr(str, "discard", 7);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.discard.expr);
            AstFmt_Close(str);
            break;

        case AST_LITERAL:
            AcuAstStringifier_StringifyLiteral(str, node->as.literal.idx);
            break;

        case AST_IDENTIFIER: {
            StringView sv = AcuStringInterner_GetString(str->interner, node->as.identifier.name);
            AcuAstStringifier_Append(str, "ident(%.*s)", (int)sv.length, sv.data);
            break;
        }

        case AST_BINARY:
            AcuAstStringifier_AppendStr(str, "bin", 3);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%s", AstBinaryKind_String(node->as.binary.type));
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.binary.left);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.binary.right);
            AstFmt_Close(str);
            break;

        case AST_UNARY:
            AcuAstStringifier_AppendStr(str, "unr", 3);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%s", AstUnaryKind_String(node->as.unary.type));
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.unary.right);
            AstFmt_Close(str);
            break;

        case AST_ASSIGN:
            AcuAstStringifier_AppendStr(str, "assign", 6);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%s", AstAssignKind_String(node->as.assign.type));
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.assign.target);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.assign.value);
            AstFmt_Close(str);
            break;

        case AST_VAR_DECL: {
            bool is_mut = (node->flags & AST_FLAG_MUTABLE) != 0;
            bool is_exp = (node->flags & AST_FLAG_EXPORTED) != 0;
            StringView sv = AcuStringInterner_GetString(str->interner, node->as.var_decl.name);

            AcuAstStringifier_AppendStr(str, "var", 3);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%s%s%.*s", (int)is_exp ? "<:" : "",
                                     (int)is_mut ? "!:" : "", (int)sv.length, sv.data);

            AstFmt_Next(str);
            AcuAstStringifier_StringifyTypeNode(str, node->as.var_decl.type_hint);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.var_decl.init_expr);
            AstFmt_Close(str);
            break;
        }

        case AST_FN_DECL: {
            const AstFnDeclPayload *payload =
                &V_Elements(&str->builder->fn_payloads)[node->as.fn_decl.payload_idx];
            bool is_exp = (node->flags & AST_FLAG_EXPORTED) != 0;
            StringView sv = AcuStringInterner_GetString(str->interner, node->as.fn_decl.name);

            AcuAstStringifier_AppendStr(str, "fn", 2);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%s%.*s", (int)is_exp ? "<:" : "", (int)sv.length,
                                     sv.data);

            AstFmt_Next(str);
            AcuAstStringifier_AppendStr(str, "params", 6);

            if (payload->params_count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                AstFmt_Open(str);
                for (u32 i = 0; i < payload->params_count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyNode(str, extras[payload->params_start_idx + i]);
                }
                AstFmt_Close(str);
            }

            AstFmt_Next(str);
            AcuAstStringifier_StringifyTypeNode(str, payload->return_type);

            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.fn_decl.body);

            AstFmt_Close(str);
            break;
        }

        case AST_FN_PARAM_DECL: {
            StringView sv = AcuStringInterner_GetString(str->interner, node->as.fn_param_decl.name);
            AcuAstStringifier_AppendStr(str, "param", 5);
            AstFmt_Open(str);
            AcuAstStringifier_Append(str, "%.*s", (int)sv.length, sv.data);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyTypeNode(str, node->as.fn_param_decl.type);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.fn_param_decl.default_value);
            AstFmt_Close(str);
            break;
        }

        case AST_TYPE_CAST:
            AcuAstStringifier_AppendStr(str, "cast", 4);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.type_cast.expr);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyTypeNode(str, node->as.type_cast.target_type);
            AstFmt_Close(str);
            break;

        case AST_IF:
            AcuAstStringifier_AppendStr(str, "if", 2);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.if_stmt.condition);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.if_stmt.then_body);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.if_stmt.else_body);
            AstFmt_Close(str);
            break;

        case AST_LOOP:
            AcuAstStringifier_AppendStr(str, "loop", 4);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.loop_stmt.body);
            AstFmt_Close(str);
            break;

        case AST_WHILE:
            AcuAstStringifier_AppendStr(str, "while", 5);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.while_stmt.condition);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.while_stmt.body);
            AstFmt_Close(str);
            break;

        case AST_RETURN:
            AcuAstStringifier_AppendStr(str, "ret", 3);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.ret_stmt.expr);
            AstFmt_Close(str);
            break;

        case AST_BREAK:
            AcuAstStringifier_AppendStr(str, "break", 5);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.break_stmt.expr);
            AstFmt_Close(str);
            break;

        case AST_CONTINUE:
            AcuAstStringifier_Append(str, "continue()");
            break;

        case AST_IMPORT: {
            StringView sv = AcuStringInterner_GetString(str->interner, node->as.import_stmt.alias);
            LiteralIdx path_literal_idx = node->as.import_stmt.module_name;

            AcuAstStringifier_AppendStr(str, "import", 6);

            AstFmt_Open(str);
            AcuAstStringifier_StringifyLiteralWithoutKindPrint(str, path_literal_idx);
            AstFmt_Next(str);
            AcuAstStringifier_Append(str, "%.*s", (int)sv.length, sv.data);
            AstFmt_Close(str);
            break;
        }

        case AST_CALL: {
            AcuAstStringifier_AppendStr(str, "call", 4);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.call.object);

            u32 start = node->as.call.start_idx;
            u32 count = node->as.call.arguments_count;

            for (u32 i = 0; i < count; i++) {
                AstFmt_Next(str);
                AcuAstStringifier_StringifyNode(str, extras[start + i]);
            }
            AstFmt_Close(str);
            break;
        }

        case AST_UNIT:
            AcuAstStringifier_Append(str, "unit");
            break;

        case AST_TUPLE: {
            AcuAstStringifier_AppendStr(str, "tuple", 5);
            u32 start = node->as.tuple.start_idx;
            u32 count = node->as.tuple.fields_count;

            if (count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                AstFmt_Open(str);
                for (u32 i = 0; i < count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyNode(str, extras[start + i]);
                }
                AstFmt_Close(str);
            }
            break;
        }

        case AST_DOT:
            AcuAstStringifier_AppendStr(str, "dot", 3);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyNode(str, node->as.dot.object);
            AstFmt_Next(str);
            AcuAstStringifier_StringifyNode(str, node->as.dot.member);
            AstFmt_Close(str);
            break;

        default:
            AcuAstStringifier_Append(str, "unknown");
            break;
    }

    if (str->print_types) {
        if (unlikely(str->types == NULL || str->node_types == NULL)) {
            acu_panic_debug(""); // TODO: change text
        }
        AcuAstStringifier_AppendStr(str, ": ", 2);
        AcuAstStringifier_StringifySemanticType(str, str->node_types[idx]);
    }
}

void AcuAstStringifier_StringifyTypeNode(AcuAstStringifier *str, TypeAstNodeIdx idx) {
    if (unlikely(IS_ACU_NULL_IDX(idx))) {
        AcuAstStringifier_Append(str, "null");
        return;
    }

    const TypeAstNode *node = AcuAstBuilder_GetTypeNode(str->builder, idx);

    switch (node->kind) {
        case TYPE_AST_PRIMITIVE:
            AcuAstStringifier_Append(str,
                                     (const char *)TypePrimitiveKind_String(node->primitive.kind));
            break;

        case TYPE_AST_ARRAY:
            AcuAstStringifier_AppendStr(str, "array", 5);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyTypeNode(str, node->array.inner_type);

            if (str->pretty_print) {
                AcuAstStringifier_AppendStr(str, ";\n", 2);
                AcuAstStringifier_PrintIndent(str);
            } else {
                AcuAstStringifier_AppendStr(str, "; ", 2);
            }

            AcuAstStringifier_StringifyNode(str, node->array.length_expr);
            AstFmt_Close(str);
            break;

        case TYPE_AST_VECTOR:
            AcuAstStringifier_AppendStr(str, "vector", 6);
            AstFmt_Open(str);
            AcuAstStringifier_StringifyTypeNode(str, node->vector.inner_type);
            AstFmt_Close(str);
            break;

        case TYPE_AST_IDENTIFIER: {
            StringView sv = AcuStringInterner_GetString(str->interner, node->identifier.name);
            AcuAstStringifier_Append(str, "id(%.*s)", (int)sv.length, sv.data);
            break;
        }

        case TYPE_AST_TUPLE: {
            AcuAstStringifier_AppendStr(str, "tuple", 5);
            u32 start = node->tuple.fields_start;
            u32 count = node->tuple.count;

            if (count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                const TypeAstNodeIdx *extra_types = V_Elements(&str->builder->extra_pool);
                AstFmt_Open(str);
                for (u32 i = 0; i < count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyTypeNode(str, extra_types[start + i]);
                }
                AstFmt_Close(str);
            }
            break;
        }

        case TYPE_AST_FUNC: {
            AcuAstStringifier_AppendStr(str, "fn", 2);
            AstFmt_Open(str);

            u32 start = node->func.params_start;
            u32 count = node->func.params_count;

            if (count == 0) {
                AcuAstStringifier_AppendStr(str, "()", 2);
            } else {
                const TypeAstNodeIdx *extra_types = V_Elements(&str->builder->extra_pool);
                AstFmt_Open(str);
                for (u32 i = 0; i < count; i++) {
                    if (i > 0)
                        AstFmt_Next(str);
                    AcuAstStringifier_StringifyTypeNode(str, extra_types[start + i]);
                }
                AstFmt_Close(str);
            }

            AstFmt_Next(str);
            AcuAstStringifier_StringifyTypeNode(str, node->func.return_type);
            AstFmt_Close(str);
            break;
        }

        default:
            AcuAstStringifier_Append(str, "unknown");
            break;
    }
}

StringView AcuAstStringifier_GetSV(AcuAstStringifier *str) {
    return (StringView){
        .data = V_Elements(&str->buffer),
        .length = V_Count(&str->buffer),
    };
}
