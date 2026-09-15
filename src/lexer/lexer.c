#include "lexer/lexer.h"
#include "common/error.h"
#include "defines/defines.h"
#include "lexer/token.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <assert.h>
#include <immintrin.h>
#include <stdbool.h>
#include <stdio.h>

#define PACK_KW2(a, b) ((u16)((a) | ((b) << 8)))
#define PACK_KW3(a, b, c) ((u32)((a) | ((b) << 8) | ((c) << 16)))

AcuLexer AcuLexer_Create(StringView sv, AcuError *error, FileId file_id) {
    assert(sv.data != NULL);

    return (AcuLexer){
        .start = sv.data,
        .limit = sv.data + sv.length,
        .current = sv.data,
        .token_start = sv.data,
        .error = error,
        .file_id = file_id,
    };
}

void AcuLexer_Reset(AcuLexer *lexer) {
    lexer->current = lexer->start;
    lexer->token_start = lexer->start;
}

enum {
    LEX_CHAR_IDENT_START = 1 << 0,
    LEX_CHAR_IDENT_CONT = 1 << 1,
    LEX_CHAR_DIGIT = 1 << 2,
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-designator"

static const u8 ACU_CHAR_MAP[256] = {
    ['a' ... 'z'] = LEX_CHAR_IDENT_START | LEX_CHAR_IDENT_CONT,
    ['A' ... 'Z'] = LEX_CHAR_IDENT_START | LEX_CHAR_IDENT_CONT,
    ['_'] = LEX_CHAR_IDENT_START | LEX_CHAR_IDENT_CONT,
    ['0' ... '9'] = LEX_CHAR_DIGIT | LEX_CHAR_IDENT_CONT,
    [128 ... 255] = LEX_CHAR_IDENT_START | LEX_CHAR_IDENT_CONT,
};

#pragma clang diagnostic pop

static inline const u8 *skip_whitespace_avx2(const u8 *ptr);

static inline const u8 *skip_whitespace(const u8 *ptr) {
    if (*ptr > ' ') {
        return ptr;
    }

    if (*ptr == ' ') {
        do {
            ptr++;
        } while (*ptr == ' ');

        if (*ptr > ' ') {
            return ptr;
        }
    }

    return skip_whitespace_avx2(ptr);
}

static inline const u8 *skip_whitespace_avx2(const u8 *ptr) {
    __m256i v_space = _mm256_set1_epi8(' ');
    __m256i v_tab = _mm256_set1_epi8('\t');
    __m256i v_nl = _mm256_set1_epi8('\n');
    __m256i v_cr = _mm256_set1_epi8('\r');

    while (true) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)ptr);

        __m256i is_whitespace = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_space), _mm256_cmpeq_epi8(chunk, v_tab)),
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, v_nl), _mm256_cmpeq_epi8(chunk, v_cr)));

        u32 mask = _mm256_movemask_epi8(is_whitespace);

        if (unlikely(mask == 0xFFFFFFFF)) {
            ptr += 32;
            continue;
        }

        return ptr + __builtin_ctz(~mask);
    }
}

static inline const u8 *find_star_avx2(const u8 *ptr) {
    __m256i star = _mm256_set1_epi8('*');
    __m256i zero = _mm256_setzero_si256();

    while (true) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)ptr);

        __m256i is_star = _mm256_cmpeq_epi8(chunk, star);
        __m256i is_zero = _mm256_cmpeq_epi8(chunk, zero);

        __m256i match = _mm256_or_si256(is_star, is_zero);

        u32 mask = _mm256_movemask_epi8(match);
        if (mask != 0) {
            return ptr + __builtin_ctz(mask);
        }
        ptr += 32;
    }
}

static inline const u8 *find_nl_avx2(const u8 *ptr) {
    __m256i v_nl = _mm256_set1_epi8('\n');
    __m256i v_zero = _mm256_setzero_si256();

    while (true) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)ptr);

        __m256i is_nl = _mm256_cmpeq_epi8(chunk, v_nl);
        __m256i is_zero = _mm256_cmpeq_epi8(chunk, v_zero);

        __m256i match = _mm256_or_si256(is_nl, is_zero);

        u32 mask = _mm256_movemask_epi8(match);

        if (mask != 0) {
            return ptr + __builtin_ctz(mask);
        }
        ptr += 32;
    }
}

static bool is_at_end(const AcuLexer *lexer) {
    return lexer->current >= lexer->limit;
}

static inline u8 advance(AcuLexer *lexer) {
    return *lexer->current++;
}

static inline bool match(AcuLexer *lexer, u8 expected) {
    if (*lexer->current != expected)
        return false;
    lexer->current++;
    return true;
}

static inline AcuToken emit_token(AcuLexer *lexer, AcuTokenType type) {
    return (AcuToken){
        .type = type,
        .offset = (u32)(lexer->token_start - lexer->start),
        .length = (u16)(lexer->current - lexer->token_start),
    };
}

static AcuToken emit_error(AcuLexer *lexer, AcuResult code) {
    u32 offset = (u32)(lexer->token_start - lexer->start);
    u16 length = (u16)(lexer->current - lexer->token_start);

    if (lexer->error) {
        lexer->error->code = code;
        lexer->error->file = lexer->file_id;
        lexer->error->offset = offset;
        lexer->error->length = length;
    }

    return (AcuToken){.type = ACU_TOKEN_ERROR, .offset = offset, .length = length};
}

static inline const u8 *find_string_end_avx2(const u8 *ptr) {
    __m256i quote = _mm256_set1_epi8('"');
    __m256i slash = _mm256_set1_epi8('\\');
    __m256i zero = _mm256_setzero_si256();
    __m256i nl = _mm256_set1_epi8('\n');

    while (true) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)ptr);

        __m256i match = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, quote), _mm256_cmpeq_epi8(chunk, slash)),
            _mm256_or_si256(_mm256_cmpeq_epi8(chunk, zero), _mm256_cmpeq_epi8(chunk, nl)));

        u32 mask = _mm256_movemask_epi8(match);
        if (mask != 0) {
            return ptr + __builtin_ctz(mask);
        }
        ptr += 32;
    }
}

static AcuToken scan_number(AcuLexer *lexer) {
    bool is_float = false;

    while (ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_DIGIT)
        lexer->current++;

    if (*lexer->current == '.' && (ACU_CHAR_MAP[lexer->current[1]] & LEX_CHAR_DIGIT)) {
        is_float = true;
        lexer->current += 2;
        while (ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_DIGIT)
            lexer->current++;
    }

    u8 c = *lexer->current;
    if (c == 'e' || c == 'E') {
        is_float = true;
        lexer->current++;
        c = *lexer->current;
        if (c == '+' || c == '-') {
            lexer->current++;
        }
        if (!(ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_DIGIT)) {
            return emit_error(lexer, ACU_ERR_LEXER_INVALID_NUMBER_FORMAT);
        }
        while (ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_DIGIT)
            lexer->current++;
    }

    while (ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_IDENT_CONT) {
        lexer->current++;
    }

    return emit_token(lexer, (int)is_float ? ACU_TOKEN_FLOAT_LITERAL : ACU_TOKEN_INT_LITERAL);
}

static AcuToken scan_string(AcuLexer *lexer) {
    while (true) {
        lexer->current = find_string_end_avx2(lexer->current);

        if (unlikely(is_at_end(lexer)))
            break;

        u8 c = *lexer->current;

        if (c == '"') {
            lexer->current++;
            return emit_token(lexer, ACU_TOKEN_STR_LITERAL);
        }

        if (c == '\\') {
            lexer->current++;
            u8 escape = *lexer->current++;
            switch (escape) {
                case 'n':
                case 'r':
                case 't':
                case '\\':
                case '"':
                case '0':
                    break;
                default:
                    if (lexer->error)
                        lexer->error->as.escape.bad_escape = escape;
                    return emit_error(lexer, ACU_ERR_LEXER_INVALID_ESCAPE_SEQUENCE);
            }
            continue;
        }

        break;
    }

    return emit_error(lexer, ACU_ERR_LEXER_UNTERMINATED_STRING);
}

static inline AcuTokenType check_keyword(const u8 *start, u16 length) {
    if (length == 2) {
        u16 val;
        __builtin_memcpy(&val, start, 2);

        switch (val) {
            case PACK_KW2('i', '1'):
                return ACU_TOKEN_I1_TYPE;
            case PACK_KW2('i', '8'):
                return ACU_TOKEN_I8_TYPE;
            case PACK_KW2('u', '8'):
                return ACU_TOKEN_U8_TYPE;
            default:
                return ACU_TOKEN_IDENTIFIER;
        }
    }

    if (length == 3) {
        u32 val;
        __builtin_memcpy(&val, start, 4);
        val &= 0x00FFFFFF;

        switch (val) {
            case PACK_KW3('i', '1', '6'):
                return ACU_TOKEN_I16_TYPE;
            case PACK_KW3('i', '3', '2'):
                return ACU_TOKEN_I32_TYPE;
            case PACK_KW3('i', '6', '4'):
                return ACU_TOKEN_I64_TYPE;
            case PACK_KW3('u', '1', '6'):
                return ACU_TOKEN_U16_TYPE;
            case PACK_KW3('u', '3', '2'):
                return ACU_TOKEN_U32_TYPE;
            case PACK_KW3('u', '6', '4'):
                return ACU_TOKEN_U64_TYPE;
            case PACK_KW3('f', '3', '2'):
                return ACU_TOKEN_F32_TYPE;
            case PACK_KW3('f', '6', '4'):
                return ACU_TOKEN_F64_TYPE;
            case PACK_KW3('s', 't', 'r'):
                return ACU_TOKEN_STR_TYPE;
            default:
                return ACU_TOKEN_IDENTIFIER;
        }
    }

    return ACU_TOKEN_IDENTIFIER;
}

static inline AcuToken scan_ident(AcuLexer *lexer) {
    while (ACU_CHAR_MAP[*lexer->current] & LEX_CHAR_IDENT_CONT) {
        lexer->current++;
    }
    const u16 length = (u16)(lexer->current - lexer->token_start);
    const AcuTokenType type = check_keyword(lexer->token_start, length);
    return emit_token(lexer, type);
}

static inline AcuToken scan_comment(AcuLexer *lexer) {
    if (match(lexer, '/')) {
        const bool is_doc = match(lexer, '/');

        lexer->current = find_nl_avx2(lexer->current);

        return emit_token(lexer, is_doc ? ACU_TOKEN_DOC_COMMENT : ACU_TOKEN_SINGLE_LINE_COMMENT);
    }

    if (match(lexer, '*')) {
        while (true) {
            lexer->current = find_star_avx2(lexer->current);

            if (*lexer->current == '\0') {
                break;
            }

            while (*lexer->current == '*') {
                lexer->current++;
            }

            if (*lexer->current == '/') {
                lexer->current++;
                return emit_token(lexer, ACU_TOKEN_MULTILINE_COMMENT);
            }
        }

        return emit_error(lexer, ACU_ERR_LEXER_UNTERMINATED_COMMENT);
    }

    return emit_token(lexer, ACU_TOKEN_SLASH);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-designator"
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Winitializer-overrides"

AcuToken AcuLexer_Next(AcuLexer *lexer) {
    static const void *const dispatch_table[256] = {
        [0 ... 255] = &&case_error,

        ['a' ... 'z'] = &&case_ident,
        ['A' ... 'Z'] = &&case_ident,
        ['_'] = &&case_ident,
        [128 ... 255] = &&case_ident,

        ['0' ... '9'] = &&case_number,

        ['"'] = &&case_string,

        ['('] = &&case_lparen,
        [')'] = &&case_rparen,
        ['{'] = &&case_lbrace,
        ['}'] = &&case_rbrace,
        ['['] = &&case_lbracket,
        [']'] = &&case_rbracket,
        [','] = &&case_comma,
        [';'] = &&case_semicolon,
        [':'] = &&case_colon,
        ['.'] = &&case_dot,
        ['~'] = &&case_tilde,
        ['#'] = &&case_hash,

        ['+'] = &&case_plus,
        ['-'] = &&case_minus,
        ['*'] = &&case_star,
        ['/'] = &&case_slash,
        ['%'] = &&case_percent,
        ['&'] = &&case_ampersand,
        ['|'] = &&case_pipe,
        ['^'] = &&case_circumflex,
        ['='] = &&case_equal,
        ['!'] = &&case_bang,
        ['<'] = &&case_less,
        ['>'] = &&case_greater,
        ['?'] = &&case_question,
        ['$'] = &&case_dollar,
        ['@'] = &&case_at,

        [0] = &&case_eof,
    };

    lexer->current = skip_whitespace(lexer->current);
    lexer->token_start = lexer->current;

    if (unlikely(is_at_end(lexer))) {
        return emit_token(lexer, ACU_TOKEN_EOF);
    }

    const u8 c = advance(lexer);

    goto *dispatch_table[c];

case_ident:
    return scan_ident(lexer);
case_number:
    return scan_number(lexer);
case_string:
    return scan_string(lexer);

case_lparen:
    return emit_token(lexer, ACU_TOKEN_LPAREN);
case_rparen:
    return emit_token(lexer, ACU_TOKEN_RPAREN);
case_lbrace:
    return emit_token(lexer, ACU_TOKEN_LBRACE);
case_rbrace:
    return emit_token(lexer, ACU_TOKEN_RBRACE);
case_lbracket:
    return emit_token(lexer, ACU_TOKEN_LBRACKET);
case_rbracket:
    return emit_token(lexer, ACU_TOKEN_RBRACKET);
case_comma:
    return emit_token(lexer, ACU_TOKEN_COMMA);
case_semicolon:
    return emit_token(lexer, ACU_TOKEN_SEMICOLON);
case_colon:
    return emit_token(lexer, ACU_TOKEN_COLON);
case_dot:
    return emit_token(lexer, ACU_TOKEN_DOT);
case_tilde:
    return emit_token(lexer, ACU_TOKEN_TILDA);
case_hash:
    return emit_token(lexer, ACU_TOKEN_CONDITION_STATEMENT);

case_plus:
    if (match(lexer, '>'))
        return emit_token(lexer, ACU_TOKEN_IMPORT_STATEMENT);
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_PLUS_EQUAL);
    return emit_token(lexer, ACU_TOKEN_PLUS);

case_minus:
    if (match(lexer, '>'))
        return emit_token(lexer, ACU_TOKEN_ARROW_RIGHT);
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_MINUS_EQUAL);
    return emit_token(lexer, ACU_TOKEN_MINUS);

case_star:
    if (match(lexer, '*')) {
        if (match(lexer, '='))
            return emit_token(lexer, ACU_TOKEN_STAR_STAR_EQUAL);
        return emit_token(lexer, ACU_TOKEN_STAR_STAR);
    }
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_STAR_EQUAL);
    return emit_token(lexer, ACU_TOKEN_STAR);

case_slash:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_SLASH_EQUAL);
    return scan_comment(lexer);

case_percent:
    if (match(lexer, '%'))
        return emit_token(lexer, ACU_TOKEN_FOR_STATEMENT);
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_PERCENT_EQUAL);
    return emit_token(lexer, ACU_TOKEN_PERCENT);

case_ampersand:
    if (match(lexer, '&'))
        return emit_token(lexer, ACU_TOKEN_LOGICAL_AND);
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_BITWISE_AND_EQUAL);
    return emit_token(lexer, ACU_TOKEN_BITWISE_AND);

case_pipe:
    if (match(lexer, '|'))
        return emit_token(lexer, ACU_TOKEN_LOGICAL_OR);
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_BITWISE_OR_EQUAL);
    return emit_token(lexer, ACU_TOKEN_BITWISE_OR);

case_circumflex:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_CIRCUMFLEX_EQUAL);
    return emit_token(lexer, ACU_TOKEN_CIRCUMFLEX);

case_equal:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_EQUAL_EQUAL);
    if (match(lexer, '>'))
        return emit_token(lexer, ACU_TOKEN_FAT_ARROW_RIGHT);
    return emit_token(lexer, ACU_TOKEN_EQUAL);

case_bang:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_NOT_EQUAL);
    if (match(lexer, '#'))
        return emit_token(lexer, ACU_TOKEN_CONDITION_ELSE_IF);
    return emit_token(lexer, ACU_TOKEN_EXCLAMATION_MARK);

case_less:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_LESS_THAN_OR_EQUALS);
    if (match(lexer, '+'))
        return emit_token(lexer, ACU_TOKEN_EXPORT_STATEMENT);
    if (match(lexer, '<')) {
        if (match(lexer, '='))
            return emit_token(lexer, ACU_TOKEN_SHIFT_LEFT_EQUAL);
        return emit_token(lexer, ACU_TOKEN_SHIFT_LEFT);
    }
    if (match(lexer, '>'))
        return emit_token(lexer, ACU_TOKEN_NAMESPACE_DECLARATION);
    return emit_token(lexer, ACU_TOKEN_LANGLE);

case_greater:
    if (match(lexer, '='))
        return emit_token(lexer, ACU_TOKEN_GREATER_THAN_OR_EQUALS);
    if (match(lexer, '>')) {
        if (match(lexer, '='))
            return emit_token(lexer, ACU_TOKEN_SHIFT_RIGHT_EQUAL);
        return emit_token(lexer, ACU_TOKEN_SHIFT_RIGHT);
    }
    return emit_token(lexer, ACU_TOKEN_RANGLE);

case_question:
    if (match(lexer, ':'))
        return emit_token(lexer, ACU_TOKEN_QUESTION_COLON);
    // Проваливаемся в ошибку, если нет двоеточия
    goto handle_unexpected_char;

case_dollar:
    if (match(lexer, '>'))
        return emit_token(lexer, ACU_TOKEN_STRUCTURE_DECLARATION);
    return emit_token(lexer, ACU_TOKEN_NAME_DECLARATION);

case_at:
    if (match(lexer, '^'))
        return emit_token(lexer, ACU_TOKEN_BREAK_STATEMENT);
    if (match(lexer, '<'))
        return emit_token(lexer, ACU_TOKEN_CONTINUE_STATEMENT);
    return emit_token(lexer, ACU_TOKEN_WHILE_STATEMENT);

case_error:
handle_unexpected_char:
    if (lexer->error)
        lexer->error->as.unexpected.bad_char = c;

    return emit_error(lexer, ACU_ERR_LEXER_UNEXPECTED_CHAR);

case_eof:
    if (lexer->current < lexer->limit) {
        goto handle_unexpected_char;
    }
    return emit_token(lexer, ACU_TOKEN_EOF);
}

#pragma clang diagnostic pop

static inline bool AcuLexer_IsIgnored(AcuTokenType type) {
    return (bool)(type == ACU_TOKEN_SINGLE_LINE_COMMENT || type == ACU_TOKEN_MULTILINE_COMMENT ||
                  type == ACU_TOKEN_DOC_COMMENT);
}

AcuTokenVector AcuLexer_Tokenize(AcuLexer *restrict lexer, bool *restrict is_success) {
    AcuTokenVector tokens = {0};

    if (is_success != NULL) {
        *is_success = true;
    }

    const u32 file_size = (u32)(lexer->limit - lexer->start);
    const u32 max_possible_tokens = (file_size > 16) ? (file_size + 1) : 16;

    V_InitWithCapacity(&tokens, max_possible_tokens);

    u32 count = 0;
    AcuToken *restrict data = V_Elements(&tokens);

    while (true) {
        AcuToken token = AcuLexer_Next(lexer);

        if (unlikely(token.type == ACU_TOKEN_ERROR)) {
            if (is_success != NULL)
                *is_success = false;
            break;
        }

        if (unlikely(token.type == ACU_TOKEN_EOF)) {
            break;
        }

        if (!AcuLexer_IsIgnored(token.type)) {
            data[count++] = token;
        }
    }

    data[count++] = (AcuToken){
        .type = ACU_TOKEN_EOF, .length = 0, .offset = (u32)(lexer->limit - lexer->start)};

    V_SetCount(&tokens, count);

    return tokens;
}
