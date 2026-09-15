#include "acu_test.h"
#include "ast/ast_builder.h"
#include "defines/defines.h"
#include "interner/string_interner.h"
#include "literal/literal_pool.h"
#include "parser/parser.h"
#include "stringify/ast_stringifier.h"
#include "types/string_view.h"
#include <stdio.h>

static AcuResult GetAstString(const char *source, char *out_buf, size_t out_buf_size) {
    AcuStringInterner interner = AcuStringInterner_Create();
    AcuLiteralPool literal_pool = AcuLiteralPool_Create();
    AcuAstBuilder builder = AcuAstBuilder_Create();

    const u8 *padded_source = ACU_PADDED_STR(source);
    StringView sv_src = StringView_CStr((const char *)padded_source);

    AcuParser parser;
    AcuParser_Init(&parser, &builder, &interner, &literal_pool, sv_src, 0);

    AstNodeIdx root_idx = AcuParser_ParseModule(&parser);

    AcuResult result_code = ACU_ERR_OK;

    if (parser.errors_count == 0) {
        AcuAstStringifier stringifier =
            AcuAstStringifier_Create(&builder, &interner, &literal_pool, NULL, NULL, false, false);
        AcuAstStringifier_StringifyNode(&stringifier, root_idx);
        StringView result_sv = AcuAstStringifier_GetSV(&stringifier);

        u32 copy_len = result_sv.length < out_buf_size - 1 ? result_sv.length : out_buf_size - 1;
        __builtin_memcpy(out_buf, result_sv.data, copy_len);
        out_buf[copy_len] = '\0';

        AcuAstStringifier_Destroy(&stringifier);
    } else {
        AcuError *errors = (AcuError *)V_Elements(&parser.errors);
        result_code = errors[0].code;

        const u8 *err_msg = AcuResult_String(result_code);
        u32 msg_len = __builtin_strlen((const char *)err_msg);
        u32 copy_len = msg_len < out_buf_size - 1 ? msg_len : out_buf_size - 1;
        __builtin_memcpy(out_buf, err_msg, copy_len);
        out_buf[copy_len] = '\0';
    }

    AcuParser_Free(&parser);
    AcuAstBuilder_Free(&builder);
    AcuLiteralPool_Free(&literal_pool);
    AcuStringInterner_Free(&interner);

    return result_code;
}

#define CHECK_AST(source, expected_ast)                                                            \
    do {                                                                                           \
        char result_buf[4096] = {0};                                                               \
        const u32 errors = GetAstString((source), result_buf, sizeof(result_buf));                 \
        ASSERT_EQ_INT(0, errors);                                                                  \
        ASSERT_EQ_STR((expected_ast), result_buf);                                                 \
    } while (0)

#define CHECK_AST_ERROR(source, expected_error)                                                    \
    do {                                                                                           \
        char result_buf[4096] = {0};                                                               \
        const AcuResult actual_error = GetAstString((source), result_buf, sizeof(result_buf));     \
        ASSERT_TRUE(actual_error != ACU_ERR_OK);                                                   \
        ASSERT_EQ_INT(expected_error, actual_error);                                               \
    } while (0)

TEST(Parser, EmptyModule) {
    CHECK_AST("", "module[0]()");
}

TEST(Parser, BasicMathPrecedence) {
    CHECK_AST("1 + 2 * 3;",
              "module[0](discard(bin(+, u64(1, none), bin(*, u64(2, none), u64(3, none)))))");

    CHECK_AST("(1 + 2) * 3;",
              "module[0](discard(bin(*, bin(+, u64(1, none), u64(2, none)), u64(3, none))))");
}

TEST(Parser_Variables, ImmutableDeclaration) {
    CHECK_AST("$count = 10;", "module[0](var(count, null, u64(10, none)))");
    CHECK_AST("$count: u64 = 10;", "module[0](var(count, u64, u64(10, none)))");
}

TEST(Parser_Variables, MutableDeclaration) {
    CHECK_AST("$index! = 0;", "module[0](var(!:index, null, u64(0, none)))");
    CHECK_AST("$index!: i32 = 0;", "module[0](var(!:index, i32, u64(0, none)))");
}

TEST(Parser_Variables, CustomTypeIdentifier) {
    CHECK_AST("$user: User = null;", "module[0](var(user, id(User), ident(null)))");
}

TEST(Parser_Variables, ComplexInitialization) {
    CHECK_AST("$offset!: u64 = (10 + 5) * 2;", "module[0](var(!:offset, u64, bin(*, bin(+, "
                                               "u64(10, none), u64(5, none)), u64(2, none))))");
}

TEST(Parser_Functions, EmptyFunction) {
    CHECK_AST("$main() {}", "module[0](fn(main, params(), null, block()))");
}

TEST(Parser_Functions, SingleParameterNoReturn) {
    CHECK_AST("$print(msg: str) {}",
              "module[0](fn(print, params(param(msg, str, null)), null, block()))");
}

TEST(Parser_Functions, MultipleParametersWithReturn) {
    CHECK_AST("$add(a: i32, b: i32) -> i32 { ^a + b; }",
              "module[0](fn(add, params(param(a, i32, null), param(b, i32, null)), i32, "
              "block(ret(bin(+, ident(a), ident(b))))))");
}

TEST(Parser_Functions, CustomTypesInSignature) {
    CHECK_AST("$process(data: Data) -> Result {}",
              "module[0](fn(process, params(param(data, id(Data), null)), id(Result), block()))");
}

TEST(Parser_ControlFlow, SimpleIf) {
    CHECK_AST("# a > 5 { }", "module[0](if(bin(>, ident(a), u64(5, none)), block(), null))");
}

TEST(Parser_ControlFlow, IfElse) {
    CHECK_AST("# ok { } ! { }", "module[0](if(ident(ok), block(), block()))");
}

TEST(Parser_ControlFlow, IfElseIfElse) {
    CHECK_AST("# a == 1 { } !# a == 2 { } ! { }",
              "module[0](if(bin(==, ident(a), u64(1, none)), block(), if(bin(==, ident(a), u64(2, "
              "none)), block(), block())))");
}

TEST(Parser_Loops, InfiniteLoop) {
    CHECK_AST("@ { }", "module[0](loop(block()))");
}

TEST(Parser_Loops, WhileLoop) {
    CHECK_AST("@ i < 10 { }", "module[0](while(bin(<, ident(i), u64(10, none)), block()))");
}

TEST(Parser_Loops, LoopControlStatements) {
    CHECK_AST("@ { # a == 5 { @^; } @<; }",
              "module[0](loop(block(if(bin(==, ident(a), u64(5, none)), block(break(null)), "
              "null), continue())))");
}

TEST(Parser_Loops, FunctionCallAsCondition) {
    CHECK_AST("$main() { @ check() { } }",
              "module[0](fn(main, params(), null, block(while(call(ident(check)), block()))))");
}

TEST(Parser_Expressions, FunctionCallEmpty) {
    CHECK_AST("run();", "module[0](discard(call(ident(run))))");
}

TEST(Parser_Expressions, FunctionCallWithArgs) {
    CHECK_AST(
        "calc(1, a + 2);",
        "module[0](discard(call(ident(calc), u64(1, none), bin(+, ident(a), u64(2, none)))))");
}

TEST(Parser_Expressions, MemberAccessAndCall) {
    CHECK_AST("player.move(10);",
              "module[0](discard(call(dot(ident(player), ident(move)), u64(10, none))))");
}

TEST(Parser_Expressions, Assignment) {
    CHECK_AST("a = 15;", "module[0](discard(assign(=, ident(a), u64(15, none))))");
}

TEST(Parser_Modules, Imports) {
    CHECK_AST("+> \"std/io\" => io;", "module[0](import(\"std/io\", io))");
    CHECK_AST("+> \"math\" => m; +> \"net\" => net;",
              "module[0](import(\"math\", m), import(\"net\", net))");
}

TEST(Parser_Modules, Exports) {
    CHECK_AST("<+ $PI: f32 = 3.14;", "module[0](var(<:PI, f32, f64(3.14, none)))");
    CHECK_AST("<+ $E!: f32 = 2.7;", "module[0](var(<:!:E, f32, f64(2.7, none)))");
    CHECK_AST("<+ $init() {}", "module[0](fn(<:init, params(), null, block()))");
}

TEST(Parser_Types, ArraysAndVectors) {
    CHECK_AST("$buffer: [u8; 256] = null;",
              "module[0](var(buffer, array(u8; u64(256, none)), ident(null)))");
    CHECK_AST("$list: <i32> = null;", "module[0](var(list, vector(i32), ident(null)))");
    CHECK_AST("$matrix: <<f32>> = null;",
              "module[0](var(matrix, vector(vector(f32)), ident(null)))");
}

TEST(Parser_Types, Tuples) {
    CHECK_AST("$pair: (i32, str) = null;", "module[0](var(pair, tuple(i32, str), ident(null)))");
}

TEST(Parser_Types, Unit) {
    CHECK_AST("$pair: () = null;", "module[0](var(pair, (), ident(null)))");
}

TEST(Parser_Types, FunctionTypes) {
    CHECK_AST("$callback: (i32, i32) -> str = null;",
              "module[0](var(callback, fn((i32, i32), str), ident(null)))");
}

TEST(Parser_Literals, StringsWithEscapes) {
    CHECK_AST("$msg = \"Hello\\n\\tWorld\\0\";",
              "module[0](var(msg, null, str(\"Hello\\n\\tWorld\\0\")))");
    CHECK_AST("$quote = \"He said \\\"Hi\\\"\";",
              "module[0](var(quote, null, str(\"He said \\\"Hi\\\"\")))");
}

TEST(Parser_Literals, NumericSuffixes) {
    CHECK_AST("$a = 100u32;", "module[0](var(a, null, u64(100, u32)))");
    CHECK_AST("$b = 3.14f32;", "module[0](var(b, null, f64(3.14, f32)))");
}

TEST(Parser_Expressions, CompoundAssignments) {
    CHECK_AST("a += 5;", "module[0](discard(assign(+=, ident(a), u64(5, none))))");
    CHECK_AST("b <<= 1;", "module[0](discard(assign(<<=, ident(b), u64(1, none))))");
}

TEST(Parser_Expressions, PrefixUnary) {
    CHECK_AST("-a;", "module[0](discard(unr(-, ident(a))))");
    CHECK_AST("!flag;", "module[0](discard(unr(!, ident(flag))))");
    CHECK_AST("~mask;", "module[0](discard(unr(~, ident(mask))))");
    CHECK_AST("!!flag;", "module[0](discard(unr(!, unr(!, ident(flag)))))");
}

TEST(Parser_Expressions, PostfixCast) {
    CHECK_AST("a ?: i32;", "module[0](discard(cast(ident(a), i32)))");
    CHECK_AST("a ?: i32 + 5;", "module[0](discard(bin(+, cast(ident(a), i32), u64(5, none))))");
}

TEST(Parser_Expressions, BitwiseAndShiftsPrecedence) {
    CHECK_AST("a & b ^ c | d;",
              "module[0](discard(bin(|, bin(^, bin(&, ident(a), ident(b)), ident(c)), ident(d))))");

    CHECK_AST("a << 1 & b >> 2;", "module[0](discard(bin(&, bin(<<, ident(a), u64(1, none)), "
                                  "bin(>>, ident(b), u64(2, none)))))");
}

TEST(Parser_Expressions, ComplexLogicalPrecedence) {
    CHECK_AST(
        "a && b || c && d;",
        "module[0](discard(bin(||, bin(&&, ident(a), ident(b)), bin(&&, ident(c), ident(d)))))");
}

TEST(Parser_Expressions, ChainedRelationalOperators) {
    CHECK_AST("$main() { a = 1 != 2 != 3 > 4; }",
              "module[0](fn(main, params(), null, block(discard(assign(=, ident(a), bin(!=, "
              "bin(!=, u64(1, none), u64(2, none)), bin(>, u64(3, none), u64(4, none))))))))");
}

TEST(Parser_ControlFlow, ReturnFromDeeplyNestedBlocks) {
    CHECK_AST(
        "$main() -> i32 { @ { # a == 1 { @ b { ^ 42; } } } ^ 0; }",
        "module[0](fn(main, params(), i32, block(loop(block(if(bin(==, ident(a), u64(1, none)), "
        "block(while(ident(b), block(ret(u64(42, none))))), null))), ret(u64(0, none)))))");
}

TEST(Parser_ControlFlow, DanglingElse) {
    CHECK_AST("# a { # b { } ! { } }",
              "module[0](if(ident(a), block(if(ident(b), block(), block())), null))");
}

TEST(Parser_ControlFlow, BlockSemicolonEllision) {
    CHECK_AST(
        "$test() { # ok { } a = 1; }",
        "module[0](fn(test, params(), null, block(if(ident(ok), block(), null), discard(assign(=, "
        "ident(a), u64(1, none))))))");
}

TEST(Parser_ControlFlow, NestedLoopsAndBreaks) {
    CHECK_AST("@ { @ cond { @^; } @<; }",
              "module[0](loop(block(while(ident(cond), block(break(null))), continue())))");
    CHECK_AST("@ { @ { @^ 42; } @<; }",
              "module[0](loop(block(loop(block(break(u64(42, none)))), continue())))");
}

TEST(Parser_Errors, VarDecl_MissingIdentifier) {
    CHECK_AST_ERROR("$ = 10;", ACU_ERR_PARSER_EXPECTED_IDENTIFIER);
}

TEST(Parser_Errors, VarDecl_MissingAssign) {
    // Ожидался знак '=' или ':' после имени переменной
    CHECK_AST_ERROR("$name 10;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, VarDecl_MissingTypeAfterColon) {
    CHECK_AST_ERROR("$value: = 42;", ACU_ERR_PARSER_EXPECTED_TYPE);
}

TEST(Parser_Errors, VarDecl_MissingSemicolon) {
    CHECK_AST_ERROR("$value = 42 \n $next = 1;", ACU_ERR_PARSER_MISSING_SEMICOLON);
}

TEST(Parser_Errors, FnDecl_MissingParamsParentheses) {
    // Ожидалась '(' после имени функции
    CHECK_AST_ERROR("$func { }", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, FnDecl_MissingParamType) {
    // Ожидалось двоеточие и тип для параметра 'a'
    CHECK_AST_ERROR("$func(a) { }", ACU_ERR_PARSER_EXPECTED_TYPE);
}

TEST(Parser_Errors, FnDecl_MissingReturnType) {
    CHECK_AST_ERROR("$func() -> { }", ACU_ERR_PARSER_EXPECTED_TYPE);
}

TEST(Parser_Errors, FnDecl_UnclosedParams) {
    CHECK_AST_ERROR("$func(a: i32 { }", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, ControlFlow_IfMissingCondition) {
    CHECK_AST_ERROR("# { }", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Errors, Types_UnclosedVector) {
    CHECK_AST_ERROR("$list: <i32 = null;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Types_MissingArrayLength) {
    CHECK_AST_ERROR("$arr: [i32;] = null;", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Errors, Types_InvalidTuple) {
    CHECK_AST_ERROR("$pair: (i32, str = null;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Expr_MissingOperand) {
    CHECK_AST_ERROR("$main() { a = 1 + ; }", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Errors, Expr_UnclosedParenthesis) {
    CHECK_AST_ERROR("$main() { a = (1 + 2 * 3; }", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Expr_InvalidPrefix) {
    // У тебя префиксы только !, ~ и -, так что '*' здесь вызовет ожидание выражения
    CHECK_AST_ERROR("$main() { a = * 5; }", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Errors, Module_ExportInvalidNode) {
    CHECK_AST_ERROR("<+ # a { }", ACU_ERR_PARSER_BAD_EXPORT);
}

TEST(Parser_Errors, Module_ImportMissingString) {
    CHECK_AST_ERROR("+> => alias;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Module_ImportMissingAlias) {
    CHECK_AST_ERROR("+> \"math\" => ;", ACU_ERR_PARSER_EXPECTED_IDENTIFIER);
}

TEST(Parser_Errors, PanicMode_Recovery) {
    CHECK_AST_ERROR("$a: i32 = 10 + * мусор ; $b: i32 = 20;", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Errors, RecoveryInsideFunctionDoesNotLoseScope) {
    CHECK_AST_ERROR("$main() { a = 10 + * error; ^ 42; }", ACU_ERR_PARSER_EXPECTED_EXPRESSION);
}

TEST(Parser_Expressions, TupleAccess) {
    CHECK_AST("a = tpl.0;",
              "module[0](discard(assign(=, ident(a), dot(ident(tpl), u64(0, none)))))");

    CHECK_AST("b = data.123;",
              "module[0](discard(assign(=, ident(b), dot(ident(data), u64(123, none)))))");
}

TEST(Parser_Expressions, ChainedDotAccess) {
    CHECK_AST("val = obj.field.0;", "module[0](discard(assign(=, ident(val), dot(dot(ident(obj), "
                                    "ident(field)), u64(0, none)))))");
    CHECK_AST("val = tpl.0.name;", "module[0](discard(assign(=, ident(val), dot(dot(ident(tpl), "
                                   "u64(0, none)), ident(name)))))");
}

TEST(Parser_Expressions, CallOnTupleElement) {
    CHECK_AST("tpl.1();", "module[0](discard(call(dot(ident(tpl), u64(1, none)))))");
    CHECK_AST(
        "tpl.0.run(1);",
        "module[0](discard(call(dot(dot(ident(tpl), u64(0, none)), ident(run)), u64(1, none))))");
}

TEST(Parser_Errors, Dot_MissingMember) {
    CHECK_AST_ERROR("$main() { a = obj.; }", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Dot_ExpressionNotAllowed) {
    CHECK_AST_ERROR("a = tpl.(0);", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
    CHECK_AST_ERROR("a = tpl.(1 + 2);", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Dot_InvalidOperatorAfterDot) {
    CHECK_AST_ERROR("a = obj.+;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
    CHECK_AST_ERROR("a = mod.*;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}

TEST(Parser_Errors, Dot_FloatNotAllowedAsIndex) {
    CHECK_AST_ERROR("a = tpl.1.5;", ACU_ERR_PARSER_UNEXPECTED_TOKEN);
}
