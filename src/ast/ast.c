#include "ast/ast.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "lexer/token.h"
#include <assert.h>

const u8 *AstNodeKind_String(AstNodeKind kind) {
#define X(name, msg)                                                                               \
    case name:                                                                                     \
        return (const u8 *)(msg);

    switch (kind) {
        X_AST_NODE_KIND(X)
        default:
            acu_unreachable_debug("Unknown kind: %d\n", kind);
    }
#undef X
}

const u8 *AstBinaryKind_String(AstBinaryKind kind) {
#define X(name, msg, category)                                                                     \
    case name:                                                                                     \
        return (const u8 *)(msg);

    switch (kind) {
        X_AST_BINARY_NODE_KIND(X)
        default:
            acu_unreachable_debug("Unknown kind: %d\n", kind);
    }
#undef X
}

const u8 *AstUnaryKind_String(AstUnaryKind kind) {
#define X(name, msg, category)                                                                     \
    case name:                                                                                     \
        return (const u8 *)(msg);

    switch (kind) {
        X_AST_UNARY_NODE_KIND(X)
        default:
            acu_unreachable_debug("Unknown kind: %d\n", kind);
    }
#undef X
}

const u8 *AstAssignKind_String(AstAssignKind kind) {
    switch (kind) {
#define X(name, text, binary_kind)                                                                 \
    case name:                                                                                     \
        return (const u8 *)(text);
        X_AST_ASSIGN_NODE_KIND(X)
#undef X
        default:
            acu_unreachable_debug("Unknown assign kind (%d)", kind);
    }
}

attribute_const const u8 *AcuPrecedence_String(AcuPrecedence prec) {
    switch (prec) {
#define X(name, text)                                                                              \
    case name:                                                                                     \
        return (const u8 *)(text);
        X_ACU_PRECEDENCE(X)
#undef X
        default:
            acu_unreachable_debug("Unknown prec: %d\n", prec);
    }
}

attribute_const AcuPrecedence AcuPrecedence_FromTokenType(AcuTokenType type) {
    static const u8 precedence_table[] = {
#define X(id, str, prec) [id] = (prec),
        X_ACU_TOKEN_TYPE(X)
#undef X
    };

    if (unlikely(type >= COUNTOF(precedence_table))) {
        acu_unreachable_debug("Invalid token type");
    }

    return (AcuPrecedence)precedence_table[type];
}

attribute_const AstOperatorCategory AstBinaryKind_GetCategory(AstBinaryKind op) {
    static const u8 category_table[] = {
#define X(id, str, cat) [id] = (cat),
        X_AST_BINARY_NODE_KIND(X)
#undef X
    };

    if (unlikely(op >= COUNTOF(category_table))) {
        acu_unreachable_debug("Invalid binary op");
    }

    return (AstOperatorCategory)category_table[op];
}

attribute_const AstOperatorCategory AstUnaryKind_GetCategory(AstUnaryKind op) {
    static const u8 category_table[] = {
#define X(id, str, cat) [id] = (cat),
        X_AST_UNARY_NODE_KIND(X)
#undef X
    };

    if (unlikely(op >= COUNTOF(category_table))) {
        acu_unreachable_debug("Invalid binary op");
    }

    return (AstOperatorCategory)category_table[op];
}

attribute_const AstBinaryKind AstAssignKind_ToBinaryKind(AstAssignKind op) {
    switch (op) {
#define X(name, text, binary_kind)                                                                 \
    case name:                                                                                     \
        return binary_kind;
        X_AST_COMPOUND_ASSIGN_NODE_KIND(X)
#undef X
        default:
            acu_unreachable_debug("Unexpected or non-compound assign operator kind (%d)", op);
    }
}
