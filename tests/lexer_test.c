#include "acu_test.h"
#include "lexer/lexer.h"
#include "lexer/token.h"
#include <string.h>

#define INIT_LEXER(source_str)                                                                     \
    AcuLexer_Create(StringView_Create(ACU_PADDED_STR(source_str), __builtin_strlen(source_str)),   \
                    NULL, 0)

TEST(Lexer, InitializeCorrectly) {
    AcuLexer lexer = INIT_LEXER("i32 $var = 10;");

    ASSERT_NOT_NULL(lexer.start);
    ASSERT_EQ_INT(lexer.start, lexer.current);
    ASSERT_TRUE(lexer.error == NULL);
}

TEST(LexerKeywords, ExactMatches) {
    AcuLexer lexer = INIT_LEXER("i1 i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 str");

    ASSERT_EQ_INT(ACU_TOKEN_I1_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_I8_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_I16_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_I32_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_I64_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_U8_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_U16_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_U32_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_U64_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_F32_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_F64_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_STR_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerKeywords, PrefixAndSuffixIdentifiers) {
    AcuLexer lexer = INIT_LEXER("i32_var my_str string u8Array f64_t");

    for (int i = 0; i < 5; i++) {
        AcuToken t = AcuLexer_Next(&lexer);
        ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, t.type);
    }

    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerKeywords, ExactLengthMismatches) {
    AcuLexer lexer = INIT_LEXER("in i9 ux st foo bar f33 u12");

    for (int i = 0; i < 8; i++) {
        AcuToken t = AcuLexer_Next(&lexer);
        ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, t.type);
    }
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerKeywords, CaseSensitivity) {
    AcuLexer lexer = INIT_LEXER("I32 i32 STR str U8 u8");

    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_I32_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_STR_TYPE, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_U8_TYPE, AcuLexer_Next(&lexer).type);
}

TEST(LexerKeywords, GluedToSymbols) {
    AcuLexer lexer = INIT_LEXER("i32$var=10;");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_I32_TYPE, t1.type);
    ASSERT_EQ_INT(3, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_NAME_DECLARATION, t2.type);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, t3.type);

    ASSERT_EQ_INT(ACU_TOKEN_EQUAL, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_SEMICOLON, AcuLexer_Next(&lexer).type);
}

TEST(Lexer, SingleTokenParsing) {
    AcuLexer lexer = INIT_LEXER("==");

    AcuToken token = AcuLexer_Next(&lexer);

    ASSERT_EQ_INT(ACU_TOKEN_EQUAL_EQUAL, token.type);
    ASSERT_EQ_INT(2, token.length);
    ASSERT_EQ_INT(0, token.offset);

    AcuToken eof = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, eof.type);
}

TEST(Lexer, MultipleTokensWithSpaces) {
    AcuLexer lexer = INIT_LEXER("=> !=");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_FAT_ARROW_RIGHT, t1.type);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_NOT_EQUAL, t2.type);
}

TEST(LexerNumbers, Integers) {
    AcuLexer lexer = INIT_LEXER("0 42 1234567890");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t1.type);
    ASSERT_EQ_INT(1, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t2.type);
    ASSERT_EQ_INT(2, t2.length);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t3.type);
    ASSERT_EQ_INT(10, t3.length);
}

TEST(LexerNumbers, FloatsAndScientific) {
    AcuLexer lexer = INIT_LEXER("3.14 0.001 1e10 2.5e-5 9E+2");

    AcuTokenType expected[] = {
        ACU_TOKEN_FLOAT_LITERAL, // 3.14
        ACU_TOKEN_FLOAT_LITERAL, // 0.001
        ACU_TOKEN_FLOAT_LITERAL, // 1e10
        ACU_TOKEN_FLOAT_LITERAL, // 2.5e-5
        ACU_TOKEN_FLOAT_LITERAL  // 9E+2
    };

    for (int i = 0; i < 5; i++) {
        AcuToken t = AcuLexer_Next(&lexer);
        ASSERT_EQ_INT(expected[i], t.type);
    }
}

TEST(LexerNumbers, NumberMethodCall) {
    AcuLexer lexer = INIT_LEXER("10.to_string()");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t1.type);
    ASSERT_EQ_INT(2, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_DOT, t2.type);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, t3.type);
    ASSERT_EQ_INT(9, t3.length);
}

TEST(LexerNumbers, NumberWithIdentCont) {
    AcuLexer lexer = INIT_LEXER("123foo");

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t.type);
    ASSERT_EQ_INT(6, t.length);
}

TEST(LexerNumbers, InvalidFormatError) {
    AcuError err = {0};
    AcuLexer lexer = INIT_LEXER("1e 1.5e+");
    lexer.error = &err;

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_ERROR, t1.type);
    ASSERT_EQ_INT(ACU_ERR_LEXER_INVALID_NUMBER_FORMAT, err.code);

    err.code = 0;
    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_ERROR, t2.type);
    ASSERT_EQ_INT(ACU_ERR_LEXER_INVALID_NUMBER_FORMAT, err.code);
}

TEST(LexerStrings, BasicStrings) {
    AcuLexer lexer = INIT_LEXER("\"\" \"hello world\"");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_STR_LITERAL, t1.type);
    ASSERT_EQ_INT(2, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_STR_LITERAL, t2.type);
    ASSERT_EQ_INT(13, t2.length);
}

TEST(LexerStrings, ValidEscapes) {
    AcuLexer lexer = INIT_LEXER("\"line1\\nline2\" \"\\t\\r\\\\\\\"\\0\"");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_STR_LITERAL, t1.type);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_STR_LITERAL, t2.type);
}

TEST(LexerStrings, Avx2LongStringChunking) {
    const char *long_str = "\"This is a very long string that definitively exceeds thirty-two "
                           "bytes to trigger AVX2 looping.\"";
    AcuLexer lexer = INIT_LEXER(long_str);

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_STR_LITERAL, t.type);
    ASSERT_EQ_INT(strlen(long_str), t.length);
}

TEST(LexerStrings, UnterminatedError) {
    AcuError err = {0};
    AcuLexer lexer = INIT_LEXER("\"This string never ends");
    lexer.error = &err;

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_ERROR, t.type);
    ASSERT_EQ_INT(ACU_ERR_LEXER_UNTERMINATED_STRING, err.code);
}

TEST(LexerStrings, InvalidEscapeSequenceError) {
    AcuError err = {0};
    AcuLexer lexer = INIT_LEXER("\"bad \\p escape\"");
    lexer.error = &err;

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_ERROR, t.type);
    ASSERT_EQ_INT(ACU_ERR_LEXER_INVALID_ESCAPE_SEQUENCE, err.code);

    ASSERT_EQ_INT('p', err.as.escape.bad_escape);
}

TEST(LexerNumbers, FloatMultipleDecimals) {
    AcuLexer lexer = INIT_LEXER("1.2.3");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_FLOAT_LITERAL, t1.type);
    ASSERT_EQ_INT(3, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_DOT, t2.type);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, t3.type);
    ASSERT_EQ_INT(1, t3.length);
}

TEST(LexerNumbers, FloatZeroesAndExponents) {
    AcuLexer lexer = INIT_LEXER("0.0 0e0 1.000000001e-10");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_FLOAT_LITERAL, t1.type);
    ASSERT_EQ_INT(3, t1.length); // "0.0"

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_FLOAT_LITERAL, t2.type);
    ASSERT_EQ_INT(3, t2.length); // "0e0"

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_FLOAT_LITERAL, t3.type);
    ASSERT_EQ_INT(15, t3.length); // "1.000000001e-10"
}

TEST(LexerComments, BasicTypes) {
    AcuLexer lexer = INIT_LEXER("// usual comment\n/// doc comment\n/* block */");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_SINGLE_LINE_COMMENT, t1.type);
    ASSERT_EQ_INT(16, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_DOC_COMMENT, t2.type);
    ASSERT_EQ_INT(15, t2.length);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_MULTILINE_COMMENT, t3.type);
    ASSERT_EQ_INT(11, t3.length);
}

TEST(LexerComments, MultilineWithNewlines) {
    AcuLexer lexer = INIT_LEXER("/* line 1\nline 2\nline 3 */");

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_MULTILINE_COMMENT, t.type);
    ASSERT_EQ_INT(26, t.length);
}

TEST(LexerComments, EmptyComments) {
    AcuLexer lexer = INIT_LEXER("//\n///\n/**/");

    AcuToken t1 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_SINGLE_LINE_COMMENT, t1.type);
    ASSERT_EQ_INT(2, t1.length);

    AcuToken t2 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_DOC_COMMENT, t2.type);
    ASSERT_EQ_INT(3, t2.length);

    AcuToken t3 = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_MULTILINE_COMMENT, t3.type);
    ASSERT_EQ_INT(4, t3.length);
}

TEST(LexerComments, EOFWithoutNewline) {
    AcuLexer lexer = INIT_LEXER("// this file ends here");

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_SINGLE_LINE_COMMENT, t.type);
    ASSERT_EQ_INT(22, t.length);

    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerComments, UnterminatedMultilineError) {
    AcuError err = {0};
    AcuLexer lexer = INIT_LEXER("/* Oops, forgot to close it");
    lexer.error = &err;

    AcuToken t = AcuLexer_Next(&lexer);
    ASSERT_EQ_INT(ACU_TOKEN_ERROR, t.type);
    ASSERT_EQ_INT(ACU_ERR_LEXER_UNTERMINATED_COMMENT, err.code);
}

TEST(LexerComments, MixedWithCode) {
    AcuLexer lexer = INIT_LEXER("i32 /* skip */ $x = 10; // end");

    ASSERT_EQ_INT(ACU_TOKEN_I32_TYPE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_MULTILINE_COMMENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_NAME_DECLARATION, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_IDENTIFIER, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_INT_LITERAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SEMICOLON, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SINGLE_LINE_COMMENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, SingleCharPunctuation) {
    AcuLexer lexer = INIT_LEXER("( ) { } [ ] , ; : . ~ @ # $");

    ASSERT_EQ_INT(ACU_TOKEN_LPAREN, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_RPAREN, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_LBRACE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_RBRACE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_LBRACKET, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_RBRACKET, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_COMMA, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SEMICOLON, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_COLON, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_DOT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_TILDA, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_WHILE_STATEMENT, AcuLexer_Next(&lexer).type);     // @
    ASSERT_EQ_INT(ACU_TOKEN_CONDITION_STATEMENT, AcuLexer_Next(&lexer).type); // #
    ASSERT_EQ_INT(ACU_TOKEN_NAME_DECLARATION, AcuLexer_Next(&lexer).type);    // $

    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, SingleCharOperators) {
    AcuLexer lexer = INIT_LEXER("+ - * / % = < > ! & | ^");

    ASSERT_EQ_INT(ACU_TOKEN_PLUS, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_MINUS, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_STAR, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SLASH, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_PERCENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_LANGLE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_RANGLE, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_EXCLAMATION_MARK, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_BITWISE_AND, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_BITWISE_OR, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_CIRCUMFLEX, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, CompoundAssignment) {
    AcuLexer lexer = INIT_LEXER("+= -= *= /= %= &= |= ^= ** **=");

    ASSERT_EQ_INT(ACU_TOKEN_PLUS_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_MINUS_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_STAR_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SLASH_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_PERCENT_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_BITWISE_AND_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_BITWISE_OR_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_CIRCUMFLEX_EQUAL, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_STAR_STAR, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_STAR_STAR_EQUAL, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, LogicAndShifts) {
    AcuLexer lexer = INIT_LEXER("== != <= >= && || << >> <<= >>=");

    ASSERT_EQ_INT(ACU_TOKEN_EQUAL_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_NOT_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_LESS_THAN_OR_EQUALS, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_GREATER_THAN_OR_EQUALS, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_LOGICAL_AND, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_LOGICAL_OR, AcuLexer_Next(&lexer).type);

    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_LEFT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_RIGHT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_LEFT_EQUAL, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_RIGHT_EQUAL, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, AcuSpecificTokens) {
    AcuLexer lexer = INIT_LEXER("-> => +> <+ <> $> !# %% ?:");

    ASSERT_EQ_INT(ACU_TOKEN_ARROW_RIGHT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_FAT_ARROW_RIGHT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_IMPORT_STATEMENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_EXPORT_STATEMENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_NAMESPACE_DECLARATION, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_STRUCTURE_DECLARATION, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_CONDITION_ELSE_IF, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_FOR_STATEMENT, AcuLexer_Next(&lexer).type);
    ASSERT_EQ_INT(ACU_TOKEN_QUESTION_COLON, AcuLexer_Next(&lexer).type);
}

TEST(LexerSymbols, EdgeCaseMaximalMunch) {
    AcuLexer lexer1 = INIT_LEXER(">>=");
    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_RIGHT_EQUAL, AcuLexer_Next(&lexer1).type);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer1).type);

    AcuLexer lexer2 = INIT_LEXER("<<<=");
    ASSERT_EQ_INT(ACU_TOKEN_SHIFT_LEFT, AcuLexer_Next(&lexer2).type);
    ASSERT_EQ_INT(ACU_TOKEN_LESS_THAN_OR_EQUALS, AcuLexer_Next(&lexer2).type);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer2).type);

    AcuLexer lexer3 = INIT_LEXER("***=");
    ASSERT_EQ_INT(ACU_TOKEN_STAR_STAR, AcuLexer_Next(&lexer3).type);
    ASSERT_EQ_INT(ACU_TOKEN_STAR_EQUAL, AcuLexer_Next(&lexer3).type);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer3).type);

    AcuLexer lexer4 = INIT_LEXER("=>=>");
    ASSERT_EQ_INT(ACU_TOKEN_FAT_ARROW_RIGHT, AcuLexer_Next(&lexer4).type);
    ASSERT_EQ_INT(ACU_TOKEN_FAT_ARROW_RIGHT, AcuLexer_Next(&lexer4).type);
    ASSERT_EQ_INT(ACU_TOKEN_EOF, AcuLexer_Next(&lexer4).type);
}
