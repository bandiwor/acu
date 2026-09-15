#pragma once

#include "defines/defines.h"
#include "lexer/token.h"
#include "memory/vector.h"

#define X_ACU_ERR(X)                                                                               \
    X(ACU_ERR_OK, "ok", "No error")                                                                \
    X(ACU_ERR_LEXER_UNEXPECTED_CHAR, "unexpected-char", "Unexpected character encountered")        \
    X(ACU_ERR_LEXER_UNTERMINATED_COMMENT, "unterminated-comment",                                  \
      "Unterminated multiline comment")                                                            \
    X(ACU_ERR_LEXER_UNTERMINATED_STRING, "unterminated-string", "Unterminated string literal")     \
    X(ACU_ERR_LEXER_INVALID_ESCAPE_SEQUENCE, "invalid-escape",                                     \
      "Invalid escape sequence in string")                                                         \
    X(ACU_ERR_LEXER_INVALID_NUMBER_FORMAT, "invalid-number", "Invalid numeric literal format")     \
    X(ACU_ERR_LEXER_INVALID_UTF8, "invalid-utf8", "Invalid UTF-8 sequence in source file")         \
    X(ACU_ERR_LEXER_TOKEN_TOO_LONG, "token-too-long", "Token exceeds maximum allowed length")      \
    X(ACU_ERR_PARSER_EXPECTED_EXPRESSION, "expected-expr", "Expected an expression")               \
    X(ACU_ERR_PARSER_EXPECTED_IDENTIFIER, "expected-ident", "Expected an identifier")              \
    X(ACU_ERR_PARSER_EXPECTED_TYPE, "expected-type", "Expected a valid type specifier")            \
    X(ACU_ERR_PARSER_MISSING_SEMICOLON, "missing-semicolon", "Missing terminating semicolon ';'")  \
    X(ACU_ERR_PARSER_UNEXPECTED_TOKEN, "unexpected-token", "Unexpected token encountered")         \
    X(ACU_ERR_PARSER_BAD_EXPORT, "invalid-export", "Only variables or functions can be exported")  \
    X(ACU_ERR_PARSER_NUMBER_OVERFLOW, "number-overflow",                                           \
      "Numeric literal overflows integer bounds")                                                  \
    X(ACU_ERR_PARSER_INVALID_NUMBER_SUFFIX, "invalid-suffix", "Invalid numeric literal suffix")    \
    X(ACU_ERR_PARSER_TOO_MANY_PARAMETERS, "too-many-params",                                       \
      "Parameter count exceeds compiler limit")                                                    \
    X(ACU_ERR_PARSER_TOO_MANY_BRANCHES, "too-many-branches",                                       \
      "Branch count exceeds compiler limit")                                                       \
    X(ACU_ERR_PARSER_RECURSION_LIMIT, "recursion-limit", "Parser recursion limit exceeded (255)")  \
    X(ACU_ERR_LOADER_FILE_NOT_FOUND, "file-not-found", "File not found or OS access denied")       \
    X(ACU_ERR_LOADER_READ_FAILED, "read-failed", "Failed to read file contents")                   \
    X(ACU_ERR_LOADER_PATH_TOO_LONG, "path-too-long", "Resolved file path exceeds maximum length")  \
    X(ACU_ERR_LOADER_CYCLIC_DEPENDENCY, "cyclic-import",                                           \
      "Cyclic dependency detected during module import")                                           \
    X(ACU_ERR_EXPECTED_CONST_EXPR, "expected-const", "Expected compile-time constant expression")  \
    X(ACU_ERR_EXPECTED_UINT, "expected-uint", "Expected unsigned integer value")                   \
    X(ACU_ERR_TOO_MANY_FIELDS, "too-many-fields", "Field count limit exceeded")                    \
    X(ACU_ERR_TOO_MANY_PARAMS, "too-many-args", "Function argument limit exceeded")                \
    X(ACU_ERR_ANALYZER_UNDECLARED_IDENTIFIER, "undeclared-ident",                                  \
      "Cannot find identifier in this scope")                                                      \
    X(ACU_ERR_ANALYZER_TYPE_MISMATCH, "type-mismatch", "Mismatched types in expression")           \
    X(ACU_ERR_ANALYZER_UNSUPPORTED_OPERATION, "unsupported-op",                                    \
      "Operation not supported for this type")                                                     \
    X(ACU_ERR_ANALYZER_EXPECTED_UNIT, "expected-unit",                                             \
      "Expression evaluated to unused non-unit value")                                             \
    X(ACU_ERR_ANALYZER_CONDITION_NOT_BOOL, "condition-type", "Condition must be of type 'i1'")     \
    X(ACU_ERR_ANALYZER_BRANCH_TYPE_MISMATCH, "branch-mismatch",                                    \
      "'if' and 'else' branches have incompatible types")                                          \
    X(ACU_ERR_ANALYZER_IF_WITHOUT_ELSE_NOT_UNIT, "if-missing-else",                                \
      "'if' without 'else' must evaluate to '()'")                                                 \
    X(ACU_ERR_ANALYZER_CALL_NON_FUNCTION, "not-callable", "Attempt to call a non-function value")  \
    X(ACU_ERR_ANALYZER_WRONG_ARGUMENT_COUNT, "arg-count-mismatch",                                 \
      "Incorrect number of arguments in function call")                                            \
    X(ACU_ERR_ANALYZER_EXPECTED_MODULE_BEFORE_DOT, "expected-module",                              \
      "Expected a module identifier before '.'")                                                   \
    X(ACU_ERR_ANALYZER_EXPECTED_IDENTIFIER_AFTER_DOT, "expected-member",                           \
      "Expected an identifier after '.'")                                                          \
    X(ACU_ERR_ANALYZER_MODULE_MEMBER_NOT_FOUND, "no-such-member",                                  \
      "Module has no member with this name")                                                       \
    X(ACU_ERR_ANALYZER_PRIVATE_MEMBER_ACCESS, "private-member",                                    \
      "Cannot access private module member")                                                       \
    X(ACU_ERR_ANALYZER_TYPE_DOES_NOT_SUPPORT_MEMBER_ACCESS, "invalid-member-access",               \
      "Type does not support member access")                                                       \
    X(ACU_ERR_ANALYZER_BREAK_OUTSIDE_LOOP, "break-outside-loop",                                   \
      "'break' statement outside of a loop")                                                       \
    X(ACU_ERR_ANALYZER_BREAK_WITH_VALUE_IN_WHILE, "break-value-in-while",                          \
      "'break' with value is not allowed in 'while' loop")                                         \
    X(ACU_ERR_ANALYZER_BREAK_TYPE_MISMATCH, "break-mismatch", "Mismatched types in loop breaks")   \
    X(ACU_ERR_ANALYZER_CONTINUE_OUTSIDE_LOOP, "continue-outside-loop",                             \
      "'continue' statement outside of a loop")                                                    \
    X(ACU_ERR_ANALYZER_RETURN_MISSING_VALUE, "missing-return-val",                                 \
      "Non-unit function must return a value")                                                     \
    X(ACU_ERR_ANALYZER_INVALID_TYPE_CAST, "invalid-cast", "Invalid explicit type cast")            \
    X(ACU_ERR_ANALYZER_NEVER_AT_TOP_LEVEL, "never-at-top-level",                                   \
      "Cannot use 'never' type in top-level statements")                                           \
    X(ACU_ERR_ANALYZER_CANNOT_INFER_TYPE, "cannot-infer-type",                                     \
      "Cannot infer the type of variable")                                                         \
    X(ACU_ERR_ANALYZER_NOT_A_MODULE, "not-a-module",                                               \
      "Identifier does not refer to a valid module")                                               \
    X(ACU_ERR_ANALYZER_CYCLIC_DEPENDENCY, "cyclic-dependency",                                     \
      "Cyclic dependency detected among globals")                                                  \
    X(ACU_ERR_ANALYZER_INVALID_LVALUE, "invalid-lvalue", "Target of assignment is not assignable") \
    X(ACU_ERR_ANALYZER_ASSIGN_TO_IMMUTABLE, "cannot-assign-immutable",                             \
      "Cannot assign to immutable variable")                                                       \
    X(ACU_ERR_ANALYZER_REDEFINITION_OF_SYMBOL, "redefinition",                                     \
      "Symbol is already defined in module scope")                                                 \
    X(ACU_ERR_ANALYZER_RETURN_OUTSIDE_FUNCTION, "return-outside-func",                             \
      "'return' statement outside of a function")

typedef enum {
#define X(name, tag, msg) name,
    X_ACU_ERR(X)
#undef X
        ACU_ERR_COUNT
} AcuResult;

typedef struct {
    AcuResult code;
    u32 offset;
    u32 length;
    FileId file;

    union {
        struct {
            u8 bad_char;
        } unexpected;

        struct {
            u8 bad_escape;
        } escape;

        struct {
            AcuTokenType expected;
            AcuTokenType actual;
        } mismatch;

        struct {
            TypeId expected;
            TypeId actual;
        } type_mismatch;

        struct {
            u32 expected;
            u32 actual;
        } arg_count;

        struct {
            TypeId target;
            TypeId actual;
        } type_cast;
    } as;
} AcuError;

typedef TypedVector(AcuError) AcuErrorVector;

static inline const char *AcuResult_Tag(AcuResult code) {
    switch (code) {
#define X(code, tag, msg)                                                                          \
    case code:                                                                                     \
        return tag;
        X_ACU_ERR(X)
#undef X
        default:
            return "unknown";
    }
}

static inline const char *AcuResult_Message(AcuResult code) {
    switch (code) {
#define X(code, tag, msg)                                                                          \
    case code:                                                                                     \
        return msg;
        X_ACU_ERR(X)
#undef X
        default:
            return "Unknown error";
    }
}

static inline const char *AcuResult_String(AcuResult code) {
    return AcuResult_Message(code);
}
