#include "parser/parser.h"
#include "ast/ast.h"
#include "ast/ast_builder.h"
#include "ast/ast_helpers.h"
#include "ast/type_ast_helpers.h"
#include "common/error.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "interner/string_interner.h"
#include "lexer/token.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "parser/parse_number.h"
#include "parser/parser_helpers.h"
#include "types/acu_position.h"
#include "types/string_view.h"
#include <string.h>

static AcuToken AcuParser_Advance(AcuParser *parser);

void AcuParser_Init(AcuParser *restrict parser, AcuAstBuilder *restrict builder,
                    AcuStringInterner *restrict interner, AcuLiteralPool *restrict literal_pool,
                    StringView source, FileId file_id) {
    parser->builder = builder;
    parser->interner = interner;
    parser->literal_pool = literal_pool;
    parser->source = source;
    parser->file_id = file_id;

    parser->errors_count = 0;

    parser->lexer_error = (AcuError){0};
    parser->lexer = AcuLexer_Create(source, &parser->lexer_error, file_id);
    parser->current_token = (AcuToken){.type = ACU_TOKEN_EOF};
    parser->previous_token = (AcuToken){.type = ACU_TOKEN_EOF};

    AcuParser_Advance(parser);

    parser->panic_mode = false;

    V_Init(&parser->scratch_nodes);
    V_Init(&parser->scratch_string);
}

void AcuParser_Free(AcuParser *parser) {
    V_Free(&parser->scratch_nodes);
    V_Free(&parser->scratch_string);

    parser->builder = NULL;
    parser->interner = NULL;
    parser->source = (StringView){0};
}

static inline AcuToken AcuParser_Current(AcuParser *parser) {
    return parser->current_token;
}

static inline AcuToken AcuParser_Previous(AcuParser *parser) {
    return parser->previous_token;
}

static inline bool AcuLexer_IsIgnored(AcuTokenType type) {
    return (bool)(type == ACU_TOKEN_SINGLE_LINE_COMMENT || type == ACU_TOKEN_MULTILINE_COMMENT ||
                  type == ACU_TOKEN_DOC_COMMENT);
}

static void Parser_ReportErrorEx(AcuParser *parser, AcuResult code, u32 offset, u32 length,
                                 AcuError payload) {
    if (parser->panic_mode)
        return;

    parser->panic_mode = true;
    ++parser->errors_count;

    payload.code = code;
    payload.offset = offset;
    payload.length = length;
    payload.file = parser->file_id;

    V_Push(&parser->errors, payload);
}

static AcuToken AcuParser_Advance(AcuParser *parser) {
    parser->previous_token = parser->current_token;

    while (true) {
        AcuToken token = AcuLexer_Next(&parser->lexer);

        if (unlikely(token.type == ACU_TOKEN_ERROR)) {
            Parser_ReportErrorEx(parser, parser->lexer_error.code, token.offset, token.length,
                                 parser->lexer_error);
            continue;
        }

        if (unlikely(token.type == ACU_TOKEN_EOF)) {
            parser->current_token = token;
            break;
        }

        if (likely(!AcuLexer_IsIgnored(token.type))) {
            parser->current_token = token;
            break;
        }
    }

    return parser->previous_token;
}

static inline bool Parser_Match(AcuParser *parser, u16 expected_type) {
    if (parser->current_token.type == expected_type) {
        AcuParser_Advance(parser);
        return true;
    }
    return false;
}

static inline void Parser_Consume(AcuParser *parser, AcuTokenType expected, AcuResult err_code) {
    if (AcuParser_Current(parser).type == expected) {
        AcuParser_Advance(parser);
    } else {
        AcuError err = {0};
        if (err_code == ACU_ERR_PARSER_UNEXPECTED_TOKEN) {
            err = (AcuError){
                .as.mismatch = {.expected = expected, .actual = AcuParser_Current(parser).type}};
        }
        Parser_ReportErrorEx(parser, err_code, AcuParser_Current(parser).offset,
                             AcuParser_Current(parser).length, err);
    }
}

static void Parser_Synchronize(AcuParser *parser) {
    parser->panic_mode = false;

    while (AcuParser_Current(parser).type != ACU_TOKEN_EOF) {
        if (AcuParser_Previous(parser).type == ACU_TOKEN_SEMICOLON)
            return;

        switch (AcuParser_Current(parser).type) {
            case ACU_TOKEN_NAME_DECLARATION:
            case ACU_TOKEN_NAMESPACE_DECLARATION:
            case ACU_TOKEN_STRUCTURE_DECLARATION:
            case ACU_TOKEN_IMPORT_STATEMENT:
            case ACU_TOKEN_EXPORT_STATEMENT:
            case ACU_TOKEN_CONDITION_STATEMENT:
            case ACU_TOKEN_WHILE_STATEMENT:
                return;

            default:
                break;
        }

        AcuParser_Advance(parser);
    }
}

static inline bool IsBlockLikeExpression(const AstNode *node) {
    return (bool)(node->type == AST_BLOCK || node->type == AST_IF || node->type == AST_LOOP ||
                  node->type == AST_WHILE);
}

static AcuPosition AcuParser_TokenPosition(AcuParser *parser, AcuToken tok) {
    return AcuPosition_FromToken(tok, parser->file_id);
}

static AstNodeIdx AcuParser_ParseExpression(AcuParser *parser, AcuPrecedence precedence, u32 depth,
                                            bool is_stmt_context);

static AstNodeIdx AcuParser_ParseStmtOrExpr(AcuParser *parser, bool *out_is_trailing, u32 depth);

static AstNodeIdx AcuParser_ParseNumber(AcuParser *parser, AcuToken tok) {
    AcuPosition pos = AcuParser_TokenPosition(parser, tok);
    StringView text = AcuToken_GetStringView(tok, parser->source);

    const AcuNumericResult num_res = Acu_ParseNumericLiteral(text);

    if (unlikely(num_res.is_overflow)) {
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_NUMBER_OVERFLOW, tok.offset, tok.length,
                             (AcuError){0});
    }

    if (unlikely(num_res.has_suffix && !num_res.is_suffix_valid)) {
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_INVALID_NUMBER_SUFFIX, tok.offset, tok.length,
                             (AcuError){0});
    }

    LiteralIdx lit_idx;
    if (num_res.is_float) {
        lit_idx = AcuLiteralPool_PushFloat(parser->literal_pool, num_res.as.f64,
                                           (int)(num_res.has_suffix && num_res.is_suffix_valid)
                                               ? num_res.suffix_kind
                                               : ACU_FLOAT_SUFFIX_NONE);
    } else {
        lit_idx = AcuLiteralPool_PushUint(parser->literal_pool, num_res.as.u64,
                                          (int)(num_res.has_suffix && num_res.is_suffix_valid)
                                              ? num_res.suffix_kind
                                              : ACU_INT_SUFFIX_NONE);
    }

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Literal(lit_idx), pos);
}

static LiteralIdx AcuParser_ParseStringLiteralValue(AcuParser *parser, AcuToken tok) {
    StringView raw_sv = AcuToken_GetStringText(tok, parser->source);
    void *slash_ptr = __builtin_memchr(raw_sv.data, '\\', raw_sv.length);

    if (likely(slash_ptr == NULL)) {
        return AcuLiteralPool_PushString(parser->literal_pool, raw_sv);
    }

    size_t slash_idx = (u8 *)slash_ptr - raw_sv.data;

    V_EnsureCapacity(&parser->scratch_string, raw_sv.length);
    u8 *dest = V_Elements(&parser->scratch_string);

    if (slash_idx > 0) {
        __builtin_memcpy(dest, raw_sv.data, slash_idx);
    }

    u32 current_len = (u32)slash_idx;

    for (u64 i = slash_idx; i < raw_sv.length; i++) {
        u8 c = raw_sv.data[i];

        if (c == '\\' && i + 1 < raw_sv.length) {
            i++;
            u8 next_c = raw_sv.data[i];
            switch (next_c) {
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '0':
                    c = '\0';
                    break;
                case '\\':
                    c = '\\';
                    break;
                case '"':
                    c = '"';
                    break;
                case '\'':
                    c = '\'';
                    break;
                default:
                    c = next_c;
                    AcuError err = {.as.escape = {.bad_escape = next_c}};
                    Parser_ReportErrorEx(parser, ACU_ERR_LEXER_INVALID_ESCAPE_SEQUENCE,
                                         tok.offset + 1 + i, 1, err);
                    break;
            }
        }

        dest[current_len++] = c;
    }

    V_SetCount(&parser->scratch_string, current_len);
    StringView unescaped_sv = {.data = dest, .length = current_len};

    return AcuLiteralPool_PushString(parser->literal_pool, unescaped_sv);
}

static AstNodeIdx AcuParser_ParseString(AcuParser *parser, AcuToken tok) {
    LiteralIdx lit_idx = AcuParser_ParseStringLiteralValue(parser, tok);
    AstNode node = AstNode_Literal(lit_idx);
    return AcuAstBuilder_PushNode(parser->builder, node, AcuParser_TokenPosition(parser, tok));
}

static AstNodeIdx AcuParser_ParseIdentifier(AcuParser *parser, AcuToken tok) {
    StringView sv = AcuToken_GetStringView(tok, parser->source);
    StringId name_id = AcuStringInterner_Intern(parser->interner, sv);
    AstNode node = AstNode_Identifier(name_id);

    AcuPosition pos = AcuParser_TokenPosition(parser, tok);

    return AcuAstBuilder_PushNode(parser->builder, node, pos);
}

static AstNodeIdx AcuParser_ParseBlock(AcuParser *parser, u32 depth) {
    AcuPosition start_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    u32 scratch_start = V_Count(&parser->scratch_nodes);

    while (AcuParser_Current(parser).type != ACU_TOKEN_RBRACE &&
           AcuParser_Current(parser).type != ACU_TOKEN_EOF) {
        bool is_trailing = false;
        AstNodeIdx stmt = AcuParser_ParseStmtOrExpr(parser, &is_trailing, depth + 1);

        if (stmt != ACU_NULL_IDX) {
            V_Push(&parser->scratch_nodes, stmt);
        }

        if (unlikely(is_trailing && AcuParser_Current(parser).type != ACU_TOKEN_RBRACE)) {
            AcuToken prev = AcuParser_Previous(parser);
            AcuError err = {.as.mismatch = {.expected = ACU_TOKEN_SEMICOLON, .actual = prev.type}};
            Parser_ReportErrorEx(parser, ACU_ERR_PARSER_MISSING_SEMICOLON,
                                 prev.offset + prev.length, 1, err);
        }

        if (unlikely(parser->panic_mode)) {
            Parser_Synchronize(parser);
        }
    }

    Parser_Consume(parser, ACU_TOKEN_RBRACE, ACU_ERR_PARSER_UNEXPECTED_TOKEN);

    u32 count = V_Count(&parser->scratch_nodes) - scratch_start;
    AstNodeIdx *elements = V_Elements(&parser->scratch_nodes) + scratch_start;

    ExtraIdx start_idx = AcuAstBuilder_PushExtraNodes(parser->builder, elements, count);

    V_SetCount(&parser->scratch_nodes, scratch_start);

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Block(start_idx, count), start_pos);
}

static AstNodeIdx AcuParser_ParseIf(AcuParser *parser, u32 depth) {
    AcuPosition start_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    struct {
        AstNodeIdx cond;
        AstNodeIdx body;
        AcuPosition pos;
    } branches[128];
    u32 branch_count = 0;

    AstNodeIdx main_cond = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    AstNodeIdx main_body = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);

    branches[branch_count].cond = main_cond;
    branches[branch_count].body = main_body;
    branches[branch_count].pos = start_pos;
    branch_count++;

    while (Parser_Match(parser, ACU_TOKEN_CONDITION_ELSE_IF)) {
        AcuPosition elif_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

        AstNodeIdx cond = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
        AstNodeIdx body = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);

        if (likely(branch_count < 128)) {
            branches[branch_count].cond = cond;
            branches[branch_count].body = body;
            branches[branch_count].pos = elif_pos;
            branch_count++;
        } else {
            Parser_ReportErrorEx(parser, ACU_ERR_PARSER_TOO_MANY_BRANCHES, elif_pos.offset, 2,
                                 (AcuError){0});
        }
    }

    AstNodeIdx else_body = ACU_NULL_IDX;
    if (Parser_Match(parser, ACU_TOKEN_EXCLAMATION_MARK)) {
        else_body = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    }

    AstNodeIdx current_node = else_body;

    for (i32 i = (i32)branch_count - 1; i >= 0; i--) {
        AstNode if_node = {.type = AST_IF,
                           .flags = 0,
                           .as.if_stmt = {.condition = branches[i].cond,
                                          .then_body = branches[i].body,
                                          .else_body = current_node}};

        current_node = AcuAstBuilder_PushNode(parser->builder, if_node, branches[i].pos);
    }

    return current_node;
}

static AstNodeIdx AcuParser_ParseBreak(AcuParser *parser, AcuToken break_tok, u32 depth) {
    AcuPosition pos = AcuParser_TokenPosition(parser, break_tok);

    AstNodeIdx break_expr = ACU_NULL_IDX;

    AcuTokenType next_type = AcuParser_Current(parser).type;
    if (next_type != ACU_TOKEN_SEMICOLON && next_type != ACU_TOKEN_RBRACE &&
        next_type != ACU_TOKEN_RPAREN && next_type != ACU_TOKEN_RBRACKET &&
        next_type != ACU_TOKEN_COMMA) {
        break_expr = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    }

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Break(break_expr), pos);
}

static AstNodeIdx AcuParser_ParseContinue(AcuParser *parser, AcuToken cont_tok) {
    AcuPosition pos = AcuParser_TokenPosition(parser, cont_tok);

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Continue(), pos);
}

static AstNodeIdx AcuParser_ParseReturn(AcuParser *parser, u32 depth) {
    AcuPosition pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    AstNodeIdx ret_expr = ACU_NULL_IDX;

    AcuTokenType next_type = AcuParser_Current(parser).type;
    if (next_type != ACU_TOKEN_SEMICOLON && next_type != ACU_TOKEN_RBRACE) {

        ret_expr = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    }

    AstNode node = {.type = AST_RETURN, .flags = 0, .as.ret_stmt = {.expr = ret_expr}};

    return AcuAstBuilder_PushNode(parser->builder, node, pos);
}

static AstNodeIdx AcuParser_ParseLoop(AcuParser *parser, u32 depth) {
    AcuPosition start_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));
    AstNodeIdx condition = ACU_NULL_IDX;

    if (AcuParser_Current(parser).type != ACU_TOKEN_LBRACE) {
        condition = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    }

    AstNodeIdx body = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);

    AstNode loop_node =
        (IS_ACU_NULL_IDX(condition)) ? AstNode_Loop(body) : AstNode_While(condition, body);

    return AcuAstBuilder_PushNode(parser->builder, loop_node, start_pos);
}

static AstNodeIdx AcuParser_ParseUnary(AcuParser *parser, AcuToken tok, u32 depth) {
    AstNodeIdx operand = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_UNARY, depth + 1, false);
    if (operand == ACU_NULL_IDX) {
        return ACU_NULL_IDX;
    }

    AstUnaryKind op = AstUnaryKind_FromTokenType(tok.type);
    AstNode node = AstNode_Unary(op, operand);

    return AcuAstBuilder_PushNode(parser->builder, node, AcuParser_TokenPosition(parser, tok));
}

static AstNodeIdx AcuParser_ParsePrefix(AcuParser *parser, u32 depth) {
    AcuToken tok = AcuParser_Advance(parser);

    switch (tok.type) {
        case ACU_TOKEN_INT_LITERAL:
        case ACU_TOKEN_FLOAT_LITERAL:
            return AcuParser_ParseNumber(parser, tok);

        case ACU_TOKEN_STR_LITERAL:
            return AcuParser_ParseString(parser, tok);

        case ACU_TOKEN_IDENTIFIER:
            return AcuParser_ParseIdentifier(parser, tok);

        case ACU_TOKEN_BREAK_STATEMENT:
            return AcuParser_ParseBreak(parser, tok, depth);

        case ACU_TOKEN_CONTINUE_STATEMENT:
            return AcuParser_ParseContinue(parser, tok);

        case ACU_TOKEN_CIRCUMFLEX:
            return AcuParser_ParseReturn(parser, depth);

        case ACU_TOKEN_LPAREN: {
            AstNodeIdx expr =
                AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
            Parser_Consume(parser, ACU_TOKEN_RPAREN, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
            return expr;
        }

        case ACU_TOKEN_LBRACE:
            return AcuParser_ParseBlock(parser, depth);

        case ACU_TOKEN_CONDITION_STATEMENT:
            return AcuParser_ParseIf(parser, depth);

        case ACU_TOKEN_WHILE_STATEMENT:
            return AcuParser_ParseLoop(parser, depth);

        case ACU_TOKEN_PLUS:
        case ACU_TOKEN_MINUS:
        case ACU_TOKEN_EXCLAMATION_MARK:
        case ACU_TOKEN_TILDA:
            return AcuParser_ParseUnary(parser, tok, depth);

        default: {
            Parser_ReportErrorEx(parser, ACU_ERR_PARSER_EXPECTED_EXPRESSION, tok.offset, tok.length,
                                 (AcuError){0});
            return ACU_NULL_IDX;
        }
    }
}

static void AcuParser_ConsumeLeftAngle(AcuParser *parser) {
    AcuToken current = AcuParser_Current(parser);

    if (current.type == ACU_TOKEN_LANGLE) {
        AcuParser_Advance(parser);
    } else if (current.type == ACU_TOKEN_SHIFT_LEFT) {
        parser->previous_token = current;
        parser->previous_token.type = ACU_TOKEN_LANGLE;
        parser->previous_token.length = 1;

        parser->current_token.type = ACU_TOKEN_LANGLE;
        parser->current_token.offset += 1;
        parser->current_token.length = 1;
    } else {
        AcuError err = {.as.mismatch = {.expected = ACU_TOKEN_LANGLE, .actual = current.type}};
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_UNEXPECTED_TOKEN, current.offset,
                             current.length, err);
    }
}

static void AcuParser_ConsumeRightAngle(AcuParser *parser) {
    AcuToken current = AcuParser_Current(parser);

    if (current.type == ACU_TOKEN_RANGLE) {
        AcuParser_Advance(parser);
    } else if (current.type == ACU_TOKEN_SHIFT_RIGHT) {
        parser->previous_token = current;
        parser->previous_token.type = ACU_TOKEN_RANGLE;
        parser->previous_token.length = 1;

        parser->current_token.type = ACU_TOKEN_RANGLE;
        parser->current_token.offset += 1;
        parser->current_token.length = 1;
    } else {
        AcuError err = {.as.mismatch = {.expected = ACU_TOKEN_RANGLE, .actual = current.type}};
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_UNEXPECTED_TOKEN, current.offset,
                             current.length, err);
    }
}

static TypeAstNodeIdx AcuParser_ParseType(AcuParser *parser, u32 depth) {
    if (unlikely(depth > 255)) {
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_RECURSION_LIMIT,
                             AcuParser_Current(parser).offset, 1, (AcuError){0});
        return ACU_NULL_IDX;
    }

    AcuToken tok = AcuParser_Current(parser);
    AcuPosition pos = AcuParser_TokenPosition(parser, tok);

    if (tok.type >= ACU_TOKEN_I1_TYPE && tok.type <= ACU_TOKEN_STR_TYPE) {
        AcuParser_Advance(parser);
        TypePrimitiveKind prim_kind = TypePrimitiveKind_FromTokenType(tok.type);

        return AcuAstBuilder_PushTypeNode(parser->builder, TypeAstNode_Primitive(prim_kind), pos);
    }

    if (tok.type == ACU_TOKEN_IDENTIFIER) {
        AcuParser_Advance(parser);
        StringId name =
            AcuStringInterner_Intern(parser->interner, AcuToken_GetStringView(tok, parser->source));

        return AcuAstBuilder_PushTypeNode(parser->builder, TypeAstNode_Identifier(name), pos);
    }

    if (tok.type == ACU_TOKEN_LBRACKET) {
        AcuParser_Advance(parser);

        TypeAstNodeIdx inner = AcuParser_ParseType(parser, depth + 1);

        Parser_Consume(parser, ACU_TOKEN_SEMICOLON, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
        AstNodeIdx length =
            AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
        Parser_Consume(parser, ACU_TOKEN_RBRACKET, ACU_ERR_PARSER_UNEXPECTED_TOKEN);

        return AcuAstBuilder_PushTypeNode(parser->builder, TypeAstNode_Array(inner, length), pos);
    }

    if (tok.type == ACU_TOKEN_LANGLE || tok.type == ACU_TOKEN_SHIFT_LEFT) {
        AcuParser_ConsumeLeftAngle(parser);

        TypeAstNodeIdx inner = AcuParser_ParseType(parser, depth + 1);

        AcuParser_ConsumeRightAngle(parser);

        return AcuAstBuilder_PushTypeNode(parser->builder, TypeAstNode_Vector(inner), pos);
    }

    if (tok.type == ACU_TOKEN_LPAREN) {
        AcuParser_Advance(parser);

        TypeAstNodeIdx local_types[256];
        u32 count = 0;

        if (AcuParser_Current(parser).type != ACU_TOKEN_RPAREN) {
            do {
                if (count >= 256) {
                    Parser_ReportErrorEx(parser, ACU_ERR_PARSER_TOO_MANY_PARAMETERS,
                                         AcuParser_Current(parser).offset, 1, (AcuError){0});
                    break;
                }
                local_types[count++] = AcuParser_ParseType(parser, depth + 1);
            } while (Parser_Match(parser, ACU_TOKEN_COMMA));
        }

        Parser_Consume(parser, ACU_TOKEN_RPAREN, ACU_ERR_PARSER_UNEXPECTED_TOKEN);

        if (Parser_Match(parser, ACU_TOKEN_ARROW_RIGHT)) {
            TypeAstNodeIdx ret_type = AcuParser_ParseType(parser, depth + 1);
            ExtraIdx params_start =
                AcuAstBuilder_PushExtraTypes(parser->builder, local_types, count);

            return AcuAstBuilder_PushTypeNode(parser->builder,
                                              TypeAstNode_Func(ret_type, params_start, count), pos);
        }

        if (count == 0) {
            return AcuAstBuilder_PushTypeNode(parser->builder,
                                              TypeAstNode_Primitive(TYPE_PRIMITIVE_UNIT), pos);
        }

        ExtraIdx fields_start = AcuAstBuilder_PushExtraTypes(parser->builder, local_types, count);

        return AcuAstBuilder_PushTypeNode(parser->builder, TypeAstNode_Tuple(fields_start, count),
                                          pos);
    }

    AcuError err = {.as.mismatch = {.expected = ACU_TOKEN_IDENTIFIER, .actual = tok.type}};
    Parser_ReportErrorEx(parser, ACU_ERR_PARSER_EXPECTED_TYPE, tok.offset, tok.length, err);

    return ACU_NULL_IDX;
}

static AstNodeIdx AcuParser_ParseBinary(AcuParser *parser, AstNodeIdx left, AcuToken tok,
                                        AcuPrecedence prec, u32 depth) {
    const AcuPrecedence parse_prec =
        tok.type == ACU_TOKEN_STAR_STAR ? (AcuPrecedence)(prec - 1) : prec;

    const AstNodeIdx right = AcuParser_ParseExpression(parser, parse_prec, depth + 1, false);
    if (unlikely(right == ACU_NULL_IDX)) {
        return ACU_NULL_IDX;
    }

    const AstBinaryKind op = AstBinaryKind_FromTokenType(tok.type);
    const AstNode node = AstNode_Binary(op, left, right);

    return AcuAstBuilder_PushNode(parser->builder, node, AcuParser_TokenPosition(parser, tok));
}

static AstNodeIdx AcuParser_ParseCast(AcuParser *parser, AstNodeIdx left, AcuToken tok, u32 depth) {
    TypeAstNodeIdx target_type = AcuParser_ParseType(parser, depth + 1);
    AstNode node = AstNode_TypeCast(target_type, left);

    return AcuAstBuilder_PushNode(parser->builder, node, AcuParser_TokenPosition(parser, tok));
}

static AstNodeIdx AcuParser_ParseAssign(AcuParser *parser, AstNodeIdx left, AcuToken tok,
                                        AcuPrecedence prec, u32 depth) {
    AstNodeIdx right =
        AcuParser_ParseExpression(parser, (AcuPrecedence)(prec - 1), depth + 1, false);
    AstAssignKind op = AstAssignKind_FromTokenType(tok.type);
    AstNode node = AstNode_Assign(op, left, right);
    return AcuAstBuilder_PushNode(parser->builder, node, AcuParser_TokenPosition(parser, tok));
}

static AstNodeIdx AcuParser_ParseCall(AcuParser *parser, AstNodeIdx left, AcuToken tok, u32 depth) {
    AcuPosition pos = AcuParser_TokenPosition(parser, tok);
    u32 scratch_start = V_Count(&parser->scratch_nodes);
    u32 args_count = 0;

    if (AcuParser_Current(parser).type != ACU_TOKEN_RPAREN) {
        do {
            if (AcuParser_Current(parser).type == ACU_TOKEN_RPAREN)
                break;

            AstNodeIdx arg =
                AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
            if (arg != ACU_NULL_IDX) {
                V_Push(&parser->scratch_nodes, arg);
                args_count++;
            }
        } while (Parser_Match(parser, ACU_TOKEN_COMMA));
    }

    Parser_Consume(parser, ACU_TOKEN_RPAREN, ACU_ERR_PARSER_UNEXPECTED_TOKEN);

    ExtraIdx args_start = 0;
    if (args_count > 0) {
        AstNodeIdx *args_ptr = V_Elements(&parser->scratch_nodes) + scratch_start;
        args_start = AcuAstBuilder_PushExtraNodes(parser->builder, args_ptr, args_count);
    }
    V_SetCount(&parser->scratch_nodes, scratch_start);

    AstNode call_node = AstNode_Call(left, args_start, args_count);
    return AcuAstBuilder_PushNode(parser->builder, call_node, pos);
}

static AstNodeIdx AcuParser_ParseDot(AcuParser *parser, AstNodeIdx left, AcuToken tok) {
    AcuPosition pos = AcuParser_TokenPosition(parser, tok);

    AcuToken member_tok = AcuParser_Current(parser);
    AstNodeIdx member_node = ACU_NULL_IDX;

    if (member_tok.type == ACU_TOKEN_IDENTIFIER) {
        Parser_Consume(parser, ACU_TOKEN_IDENTIFIER, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
        member_node = AcuParser_ParseIdentifier(parser, member_tok);
    } else if (member_tok.type == ACU_TOKEN_INT_LITERAL) {
        Parser_Consume(parser, ACU_TOKEN_INT_LITERAL, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
        member_node = AcuParser_ParseNumber(parser, member_tok);
    } else {
        Parser_Consume(parser, ACU_TOKEN_IDENTIFIER, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
        return ACU_NULL_IDX;
    }

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Dot(left, member_node), pos);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-designator"
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Winitializer-overrides"

static AstNodeIdx AcuParser_ParseInfix(AcuParser *parser, AstNodeIdx left, u32 depth) {
    const AcuToken tok = AcuParser_Advance(parser);
    const AcuPrecedence prec = AcuPrecedence_FromTokenType(tok.type);

    static const void *const dispatch_table[256] = {
        [0 ... 255] = &&case_default,

        [ACU_TOKEN_PLUS] = &&case_binary,
        [ACU_TOKEN_MINUS] = &&case_binary,
        [ACU_TOKEN_STAR] = &&case_binary,
        [ACU_TOKEN_SLASH] = &&case_binary,
        [ACU_TOKEN_PERCENT] = &&case_binary,
        [ACU_TOKEN_LOGICAL_OR] = &&case_binary,
        [ACU_TOKEN_LOGICAL_AND] = &&case_binary,
        [ACU_TOKEN_BITWISE_OR] = &&case_binary,
        [ACU_TOKEN_BITWISE_AND] = &&case_binary,
        [ACU_TOKEN_CIRCUMFLEX] = &&case_binary,
        [ACU_TOKEN_EQUAL_EQUAL] = &&case_binary,
        [ACU_TOKEN_NOT_EQUAL] = &&case_binary,
        [ACU_TOKEN_LANGLE] = &&case_binary,
        [ACU_TOKEN_RANGLE] = &&case_binary,
        [ACU_TOKEN_LESS_THAN_OR_EQUALS] = &&case_binary,
        [ACU_TOKEN_GREATER_THAN_OR_EQUALS] = &&case_binary,
        [ACU_TOKEN_SHIFT_LEFT] = &&case_binary,
        [ACU_TOKEN_SHIFT_RIGHT] = &&case_binary,
        [ACU_TOKEN_STAR_STAR] = &&case_binary,

        // Присваивания
        [ACU_TOKEN_EQUAL] = &&case_assign,
        [ACU_TOKEN_PLUS_EQUAL] = &&case_assign,
        [ACU_TOKEN_MINUS_EQUAL] = &&case_assign,
        [ACU_TOKEN_STAR_EQUAL] = &&case_assign,
        [ACU_TOKEN_SLASH_EQUAL] = &&case_assign,
        [ACU_TOKEN_PERCENT_EQUAL] = &&case_assign,
        [ACU_TOKEN_STAR_STAR_EQUAL] = &&case_assign,
        [ACU_TOKEN_BITWISE_AND_EQUAL] = &&case_assign,
        [ACU_TOKEN_BITWISE_OR_EQUAL] = &&case_assign,
        [ACU_TOKEN_CIRCUMFLEX_EQUAL] = &&case_assign,
        [ACU_TOKEN_SHIFT_LEFT_EQUAL] = &&case_assign,
        [ACU_TOKEN_SHIFT_RIGHT_EQUAL] = &&case_assign,

        // Специфичные инфиксы
        [ACU_TOKEN_LPAREN] = &&case_call,
        [ACU_TOKEN_DOT] = &&case_dot,
        [ACU_TOKEN_QUESTION_COLON] = &&case_cast,
    };

    goto *dispatch_table[tok.type];

case_binary:
    return AcuParser_ParseBinary(parser, left, tok, prec, depth);
case_assign:
    return AcuParser_ParseAssign(parser, left, tok, prec, depth);
case_call:
    return AcuParser_ParseCall(parser, left, tok, depth);
case_dot:
    return AcuParser_ParseDot(parser, left, tok);
case_cast:
    return AcuParser_ParseCast(parser, left, tok, depth);
case_default:
    return left;
}

#pragma clang diagnostic pop

static AstNodeIdx AcuParser_ParseExpression(AcuParser *parser, AcuPrecedence precedence, u32 depth,
                                            bool is_stmt_context) {
    if (unlikely(depth > 255)) {
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_RECURSION_LIMIT,
                             AcuParser_Current(parser).offset, 1, (AcuError){0});
        return ACU_NULL_IDX;
    }

    AstNodeIdx left = AcuParser_ParsePrefix(parser, depth);
    if (unlikely(left == ACU_NULL_IDX)) {
        return ACU_NULL_IDX;
    }

    while (true) {
        AcuTokenType next_type = AcuParser_Current(parser).type;
        u8 next_prec = AcuPrecedence_FromTokenType(next_type);

        if (precedence >= next_prec) {
            break;
        }

        const AstNode *left_node = AcuAstBuilder_GetNode(parser->builder, left);
        if (unlikely(IsBlockLikeExpression(left_node))) {
            break;
        }

        left = AcuParser_ParseInfix(parser, left, depth);

        if (unlikely(left == ACU_NULL_IDX)) {
            return ACU_NULL_IDX;
        }
    }

    return left;
}

static ExtraIdx AcuParser_ParseFnParams(AcuParser *restrict parser, u32 *restrict out_param_count,
                                        u32 depth) {
    u32 scratch_start = V_Count(&parser->scratch_nodes);

    if (AcuParser_Current(parser).type != ACU_TOKEN_RPAREN) {
        do {
            if (AcuParser_Current(parser).type == ACU_TOKEN_RPAREN) {
                break;
            }

            AcuToken p_name = AcuParser_Current(parser);
            Parser_Consume(parser, ACU_TOKEN_IDENTIFIER, ACU_ERR_PARSER_EXPECTED_IDENTIFIER);

            if (unlikely(parser->panic_mode)) {
                break;
            }

            bool is_mutable = Parser_Match(parser, ACU_TOKEN_EXCLAMATION_MARK);

            StringView sv = AcuToken_GetStringView(p_name, parser->source);
            StringId p_id = AcuStringInterner_Intern(parser->interner, sv);

            Parser_Consume(parser, ACU_TOKEN_COLON, ACU_ERR_PARSER_EXPECTED_TYPE);

            if (unlikely(parser->panic_mode)) {
                break;
            }

            TypeAstNodeIdx p_type = AcuParser_ParseType(parser, depth + 1);
            if (unlikely(parser->panic_mode)) {
                break;
            }

            AstNodeIdx def_val = ACU_NULL_IDX;
            if (Parser_Match(parser, ACU_TOKEN_EQUAL)) {
                def_val = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
                if (unlikely(parser->panic_mode)) {
                    break;
                }
            }

            AstNode param_node =
                AstNode_FunctionParamDeclaration(p_id, p_type, def_val, is_mutable);
            AstNodeIdx p_node_idx = AcuAstBuilder_PushNode(parser->builder, param_node,
                                                           AcuParser_TokenPosition(parser, p_name));

            V_Push(&parser->scratch_nodes, p_node_idx);

        } while (Parser_Match(parser, ACU_TOKEN_COMMA));
    }

    if (likely(!parser->panic_mode)) {
        Parser_Consume(parser, ACU_TOKEN_RPAREN, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
    }

    if (unlikely(parser->panic_mode)) {
        V_SetCount(&parser->scratch_nodes, scratch_start);
        *out_param_count = 0;
        return ACU_NULL_IDX;
    }

    *out_param_count = V_Count(&parser->scratch_nodes) - scratch_start;
    AstNodeIdx *param_ptr = V_Elements(&parser->scratch_nodes) + scratch_start;

    ExtraIdx params_idx =
        AcuAstBuilder_PushExtraNodes(parser->builder, param_ptr, *out_param_count);

    V_SetCount(&parser->scratch_nodes, scratch_start);

    return params_idx;
}

static AstNodeIdx AcuParser_ParseVarDecl(AcuParser *parser, StringId name, AcuPosition pos,
                                         bool is_export, u32 depth) {
    bool is_mutable = Parser_Match(parser, ACU_TOKEN_EXCLAMATION_MARK);

    TypeAstNodeIdx type_hint = ACU_NULL_IDX;
    if (Parser_Match(parser, ACU_TOKEN_COLON)) {
        type_hint = AcuParser_ParseType(parser, depth + 1);

        if (unlikely(parser->panic_mode)) {
            return ACU_NULL_IDX;
        }
    }

    Parser_Consume(parser, ACU_TOKEN_EQUAL, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    AstNodeIdx init_expr = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    Parser_Consume(parser, ACU_TOKEN_SEMICOLON, ACU_ERR_PARSER_MISSING_SEMICOLON);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    AstNode var_node = AstNode_VarDecl(name, type_hint, init_expr, is_mutable, is_export);
    return AcuAstBuilder_PushNode(parser->builder, var_node, pos);
}

static AstNodeIdx AcuParser_ParseFnDecl(AcuParser *parser, StringId name_id, AcuPosition pos,
                                        bool is_export, u32 depth) {
    u32 param_count = 0;

    ExtraIdx params_idx = AcuParser_ParseFnParams(parser, &param_count, depth + 1);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    TypeAstNodeIdx ret_type = ACU_NULL_IDX;

    if (Parser_Match(parser, ACU_TOKEN_ARROW_RIGHT)) {
        ret_type = AcuParser_ParseType(parser, depth + 1);
        if (unlikely(parser->panic_mode)) {
            return ACU_NULL_IDX;
        }
    }

    AstNodeIdx body = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, false);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    AstFnDeclPayload payload = {
        .return_type = ret_type, .params_start_idx = params_idx, .params_count = param_count};
    u32 payload_idx = AcuAstBuilder_PushFnDeclPayload(parser->builder, payload);

    AstNode fn_node = AstNode_FunctionDeclaration(name_id, body, is_export, payload_idx);
    return AcuAstBuilder_PushNode(parser->builder, fn_node, pos);
}

static AstNodeIdx AcuParser_ParseNameDecl(AcuParser *parser, bool is_export, u32 depth) {
    AcuPosition start_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    AcuToken name_tok = AcuParser_Current(parser);

    Parser_Consume(parser, ACU_TOKEN_IDENTIFIER, ACU_ERR_PARSER_EXPECTED_IDENTIFIER);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    StringView sv = AcuToken_GetStringView(name_tok, parser->source);
    StringId name_id = AcuStringInterner_Intern(parser->interner, sv);

    if (Parser_Match(parser, ACU_TOKEN_LPAREN)) {
        return AcuParser_ParseFnDecl(parser, name_id, start_pos, is_export, depth);
    }

    return AcuParser_ParseVarDecl(parser, name_id, start_pos, is_export, depth);
}

static AstNodeIdx AcuParser_ParseImport(AcuParser *parser, bool is_export) {
    AcuPosition start_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    AcuToken path_tok = AcuParser_Current(parser);
    Parser_Consume(parser, ACU_TOKEN_STR_LITERAL, ACU_ERR_PARSER_UNEXPECTED_TOKEN);

    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    LiteralIdx module_name = AcuParser_ParseStringLiteralValue(parser, path_tok);

    Parser_Consume(parser, ACU_TOKEN_FAT_ARROW_RIGHT, ACU_ERR_PARSER_UNEXPECTED_TOKEN);
    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    AcuToken alias_tok = AcuParser_Current(parser);
    Parser_Consume(parser, ACU_TOKEN_IDENTIFIER, ACU_ERR_PARSER_EXPECTED_IDENTIFIER);

    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    StringView alias_sv = AcuToken_GetStringView(alias_tok, parser->source);
    StringId alias_id = AcuStringInterner_Intern(parser->interner, alias_sv);

    Parser_Consume(parser, ACU_TOKEN_SEMICOLON, ACU_ERR_PARSER_MISSING_SEMICOLON);

    if (unlikely(parser->panic_mode)) {
        return ACU_NULL_IDX;
    }

    return AcuAstBuilder_PushNode(parser->builder, AstNode_Import(module_name, alias_id, is_export),
                                  start_pos);
}

static AstNodeIdx AcuParser_ParseExport(AcuParser *parser, u32 depth) {
    AcuPosition export_pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));

    AstNodeIdx decl_idx = ACU_NULL_IDX;
    if (Parser_Match(parser, ACU_TOKEN_NAME_DECLARATION)) {
        decl_idx = AcuParser_ParseNameDecl(parser, true, depth);
    } else if (Parser_Match(parser, ACU_TOKEN_IMPORT_STATEMENT)) {
        decl_idx = AcuParser_ParseImport(parser, true);
    } else {
        AcuError err = {0};
        Parser_ReportErrorEx(parser, ACU_ERR_PARSER_BAD_EXPORT, export_pos.offset,
                             export_pos.length, err);
        return ACU_NULL_IDX;
    }

    return decl_idx;
}

static AstNodeIdx AcuParser_ParseStmtOrExpr(AcuParser *parser, bool *out_is_trailing, u32 depth) {
    *out_is_trailing = false;

    if (Parser_Match(parser, ACU_TOKEN_NAME_DECLARATION)) {
        return AcuParser_ParseNameDecl(parser, false, depth);
    }

    AstNodeIdx expr_idx = AcuParser_ParseExpression(parser, ACU_PRECEDENCE_NONE, depth + 1, true);
    if (unlikely(expr_idx == ACU_NULL_IDX)) {
        return ACU_NULL_IDX;
    }

    if (Parser_Match(parser, ACU_TOKEN_SEMICOLON)) {
        AcuPosition pos = AcuParser_TokenPosition(parser, AcuParser_Previous(parser));
        const AstNode *expr_node = AcuAstBuilder_GetNode(parser->builder, expr_idx);

        if (expr_node->type == AST_RETURN || expr_node->type == AST_BREAK ||
            expr_node->type == AST_CONTINUE) {
            return expr_idx;
        }

        AstNode discard_node = AstNode_Discard(expr_idx);
        return AcuAstBuilder_PushNode(parser->builder, discard_node, pos);
    }

    if (AcuParser_Current(parser).type == ACU_TOKEN_RBRACE) {
        *out_is_trailing = true;
        return expr_idx;
    }

    const AstNode *expr_node = &V_Elements(&parser->builder->nodes)[expr_idx];
    if (IsBlockLikeExpression(expr_node)) {
        return expr_idx;
    }

    AcuError err = {
        .as.mismatch = {.expected = ACU_TOKEN_SEMICOLON, .actual = AcuParser_Current(parser).type}};
    Parser_ReportErrorEx(parser, ACU_ERR_PARSER_UNEXPECTED_TOKEN, AcuParser_Current(parser).offset,
                         AcuParser_Current(parser).length, err);

    return expr_idx;
}

static AstNodeIdx AcuParser_ParseTopLevelStmtOrExpr(AcuParser *parser, bool *out_is_trailing) {
    if (Parser_Match(parser, ACU_TOKEN_IMPORT_STATEMENT)) {
        *out_is_trailing = false;
        return AcuParser_ParseImport(parser, false);
    }

    if (Parser_Match(parser, ACU_TOKEN_EXPORT_STATEMENT)) {
        *out_is_trailing = false;
        return AcuParser_ParseExport(parser, 0);
    }

    return AcuParser_ParseStmtOrExpr(parser, out_is_trailing, 0);
}

AstNodeIdx AcuParser_ParseModule(AcuParser *restrict parser) {
    const AcuPosition start_pos = (AcuPosition){0}; // TODO: CHANGE IT

    const u32 scratch_start_idx = V_Count(&parser->scratch_nodes);

    while (AcuParser_Current(parser).type != ACU_TOKEN_EOF) {
        bool is_trailing = false;
        const AstNodeIdx stmt = AcuParser_ParseTopLevelStmtOrExpr(parser, &is_trailing);

        if (stmt != ACU_NULL_IDX) {
            V_Push(&parser->scratch_nodes, stmt);
        }

        if (unlikely(is_trailing)) {
            const AcuToken prev = AcuParser_Previous(parser);
            const AcuError err = {
                .as.mismatch = {.expected = ACU_TOKEN_SEMICOLON, .actual = prev.type}};

            Parser_ReportErrorEx(parser, ACU_ERR_PARSER_MISSING_SEMICOLON,
                                 prev.offset + prev.length, 1, err);
        }

        if (unlikely(parser->panic_mode)) {
            Parser_Synchronize(parser);
        }
    }

    const u32 module_nodes_count = V_Count(&parser->scratch_nodes) - scratch_start_idx;
    const AstNodeIdx *module_elements = V_Elements(&parser->scratch_nodes) + scratch_start_idx;

    const ExtraIdx extra_idx_start =
        AcuAstBuilder_PushExtraNodes(parser->builder, module_elements, module_nodes_count);

    V_SetCount(&parser->scratch_nodes, scratch_start_idx);

    AstNode module_node = AstNode_Module(parser->file_id, extra_idx_start, module_nodes_count);

    return AcuAstBuilder_PushNode(parser->builder, module_node, start_pos);
}
