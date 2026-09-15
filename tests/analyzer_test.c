#include "acu_test.h"
#include "analyzer/analyzer.h"
#include "ast/ast_builder.h"
#include "defines/defines.h"
#include "fs/file_manager.h"
#include "interner/string_interner.h"
#include "interner/type_interner.h"
#include "literal/literal_pool.h"
#include "module/module_manager.h"
#include "stringify/ast_stringifier.h"
#include "table/symbol_table.h"
#include "types/string_view.h"
#include <stdio.h>

#ifndef ACU_TESTS_DIR
#define ACU_TESTS_DIR "tests/cases/"
#endif

static AcuResult RunAnalyzerOnCase(const char *rel_case_path, char *out_buf, size_t out_buf_size) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", ACU_TESTS_DIR, rel_case_path);

    AcuAstBuilder builder = AcuAstBuilder_Create();
    AcuStringInterner interner = AcuStringInterner_Create();
    AcuLiteralPool literals = AcuLiteralPool_Create();
    AcuSymbolTable symbols = AcuSymbolTable_Create();
    AcuModuleManager modules = AcuModuleManager_Create(&symbols);
    AcuFileManager files = AcuFileManager_Create();
    AcuTypeInterner types = AcuTypeInterner_Create();

    AcuWorkspace ws =
        AcuWorkspace_Create(&builder, &interner, &literals, &modules, &files, &symbols, &types);

    StringView main_path_sv = StringView_CStr(full_path);
    bool ok = AcuWorkspace_Analyze(&ws, main_path_sv);
    AcuResult result_code = ACU_ERR_OK;

    if (ok && V_Count(&ws.errors) == 0) {
        const u32 modules_count = AcuModuleManager_GetCount(&modules);
        size_t current_len = 0;
        out_buf[0] = '\0';

        for (u32 m = 0; m < modules_count; ++m) {
            AstNodeIdx root_idx = AcuModuleManager_GetSourceModuleNode(&modules, m);

            AcuAstStringifier stringifier = AcuAstStringifier_Create(
                &builder, &interner, &literals, &types, V_Elements(&ws.node_types), false, true);

            AcuAstStringifier_StringifyNode(&stringifier, root_idx);
            StringView sv = AcuAstStringifier_GetSV(&stringifier);

            if (current_len + sv.length + 2 < out_buf_size) {
                __builtin_memcpy(out_buf + current_len, sv.data, sv.length);
                current_len += sv.length;
                out_buf[current_len++] = '%';
                out_buf[current_len] = '\0';
            }

            AcuAstStringifier_Destroy(&stringifier);
        }
    } else {
        AcuError *errors = (AcuError *)V_Elements(&ws.errors);
        result_code = errors[0].code;

        const char *err_msg = AcuResult_String(result_code);
        u32 msg_len = __builtin_strlen(err_msg);
        u32 copy_len = msg_len < out_buf_size - 1 ? msg_len : out_buf_size - 1;
        __builtin_memcpy(out_buf, err_msg, copy_len);
        out_buf[copy_len] = '\0';
    }

    V_Free(&ws.node_types);
    V_Free(&ws.loop_ctx);
    V_Free(&ws.errors);
    AcuFileManager_Free(&files);
    AcuModuleManager_Free(&modules);
    AcuSymbolTable_Free(&symbols);
    AcuTypeInterner_Free(&types);
    AcuAstBuilder_Free(&builder);
    AcuLiteralPool_Free(&literals);
    AcuStringInterner_Free(&interner);

    return result_code;
}

#define CHECK_FILE_AST(rel_path, expected_dump)                                                    \
    do {                                                                                           \
        char result_buf[8192] = {0};                                                               \
        AcuResult err = RunAnalyzerOnFile((rel_path), result_buf, sizeof(result_buf));             \
        ASSERT_EQ_INT(ACU_ERR_OK, err);                                                            \
        ASSERT_EQ_STR((expected_dump), result_buf);                                                \
    } while (0)

#define CHECK_FILE_ERR(rel_path, expected_err)                                                     \
    do {                                                                                           \
        char result_buf[8192] = {0};                                                               \
        AcuResult actual_err = RunAnalyzerOnFile((rel_path), result_buf, sizeof(result_buf));      \
        ASSERT_TRUE(actual_err != ACU_ERR_OK);                                                     \
        ASSERT_EQ_INT(expected_err, actual_err);                                                   \
    } while (0)

#define CHECK_CASE_TYPED_AST(rel_path, expected_dump)                                              \
    do {                                                                                           \
        char result_buf[8192] = {0};                                                               \
        AcuResult err = RunAnalyzerOnCase((rel_path), result_buf, sizeof(result_buf));             \
        ASSERT_EQ_INT(ACU_ERR_OK, err);                                                            \
        ASSERT_EQ_STR((expected_dump), result_buf);                                                \
    } while (0)

#define CHECK_CASE_ERR(rel_path, expected_err)                                                     \
    do {                                                                                           \
        char result_buf[8192] = {0};                                                               \
        AcuResult actual_err = RunAnalyzerOnCase((rel_path), result_buf, sizeof(result_buf));      \
        ASSERT_TRUE(actual_err != ACU_ERR_OK);                                                     \
        ASSERT_EQ_INT(expected_err, actual_err);                                                   \
    } while (0)

TEST(Analyzer, SimpleDAGInSameFile) {
    CHECK_CASE_TYPED_AST("01_simple_dag/main", "module[0]("
                                               "var(y, null, ident(x): i64): (), "
                                               "var(x, null, u64(15, none): i64): ()"
                                               "): ()%");
}

TEST(Analyzer, MultiModuleDAG) {
    CHECK_CASE_TYPED_AST("02_multi_module_dag/main",
                         "module[0]("
                         "import(\"mod1\", m1): (), "
                         "var(x, null, dot(ident(m1): mod, ident(pi): ()): i64): ()"
                         "): ()%"
                         "module[1]("
                         "import(\"mod2\", m2): (), "
                         "var(<:pi, null, dot(ident(m2): mod, ident(real_pi): ()): i64): ()"
                         "): ()%"
                         "module[2]("
                         "var(<:real_pi, null, u64(42, none): i64): ()"
                         "): ()%");
}

TEST(Analyzer, FunctionsAndCall) {
    CHECK_CASE_TYPED_AST("03_functions/main",
                         "module[0]("
                         "fn(add, params(param(a, i64, null): i64, param(b, i64, null): i64), i64, "
                         "block(bin(+, ident(a): i64, ident(b): i64): i64): i64): (), "
                         "var(res, null, call(ident(add): fn(i64, i64) -> i64, "
                         "u64(10, none): i64, u64(20, none): i64): i64): ()"
                         "): ()%");
}

TEST(Analyzer, ControlFlowExpressions) {
    CHECK_CASE_TYPED_AST("04_control_flow/main",
                         "module[0]("
                         "var(cond_val, null, if(u64(1, i1): i1, "
                         "block(u64(100, none): i64): i64, "
                         "block(u64(200, none): i64): i64): i64): (), "
                         "var(loop_val, null, loop("
                         "block(break(u64(42, none): i64): never): never): i64): ()"
                         "): ()%");
}

// ============================================================================
// Негативные тесты (Ожидаемые ошибки анализатора)
// ============================================================================

TEST(Analyzer_Errors, CrossModuleCyclicDependency) {
    CHECK_CASE_ERR("05_cyclic_error/main", ACU_ERR_ANALYZER_CYCLIC_DEPENDENCY);
}

TEST(Analyzer_Errors, PrivateMemberAccess) {
    CHECK_CASE_ERR("06_private_error/main", ACU_ERR_ANALYZER_PRIVATE_MEMBER_ACCESS);
}

TEST(Analyzer_Errors, MemberNotFound) {
    CHECK_CASE_ERR("07_member_not_found/main", ACU_ERR_ANALYZER_MODULE_MEMBER_NOT_FOUND);
}

TEST(Analyzer_Errors, ArgumentTypeMismatch) {
    CHECK_CASE_ERR("08_type_mismatch/main", ACU_ERR_ANALYZER_TYPE_MISMATCH);
}
