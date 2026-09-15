#include "acu_test.h"
#include "table/symbol_table.h"

#define MOCK_MODULE_ID 1
#define MOCK_TYPE_ID 100
#define MOCK_AST_NODE 500
#define MOCK_LITERAL_ID 999

#define NAME_A 10
#define NAME_B 11
#define NAME_C 12
#define NAME_SYS 13

TEST(SymbolTable, InitAndScopes) {
    AcuSymbolTable table = AcuSymbolTable_Create();

    ASSERT_EQ_INT(0, table.count);
    ASSERT_EQ_INT(0, V_Count(&table.scopes));
    ASSERT_TRUE(V_Capacity(&table.hash_table) >= 4096);

    ScopeId root_scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);
    ASSERT_EQ_INT(0, root_scope);
    ASSERT_EQ_INT(1, V_Count(&table.scopes));

    AcuScope *rs = &V_Elements(&table.scopes)[root_scope];
    ASSERT_EQ_INT(ACU_NULL_IDX, rs->parent_id);
    ASSERT_EQ_INT(MOCK_MODULE_ID, rs->module_id);

    ScopeId child_scope = AcuSymbolTable_PushScope(&table, root_scope, MOCK_MODULE_ID);
    ASSERT_EQ_INT(1, child_scope);

    AcuScope *cs = &V_Elements(&table.scopes)[child_scope];
    ASSERT_EQ_INT(root_scope, cs->parent_id);

    AcuSymbolTable_Free(&table);
}

TEST(SymbolTable, AddAndLookupBasic) {
    AcuSymbolTable table = AcuSymbolTable_Create();
    ScopeId scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);

    SymbolId loc_id =
        AcuSymbolTable_AddLocalVar(&table, scope, NAME_A, MOCK_TYPE_ID, MOCK_AST_NODE, true);
    SymbolId glob_id = AcuSymbolTable_AddGlobalVar(&table, scope, NAME_B, MOCK_TYPE_ID,
                                                   MOCK_AST_NODE, true, false);
    SymbolId fn_id =
        AcuSymbolTable_AddFn(&table, scope, NAME_C, MOCK_TYPE_ID, MOCK_AST_NODE, false);

    ASSERT_TRUE(loc_id != ACU_NULL_IDX);
    ASSERT_TRUE(glob_id != ACU_NULL_IDX);
    ASSERT_TRUE(fn_id != ACU_NULL_IDX);

    AcuSymbol *loc_sym = AcuSymbolTable_Get(&table, loc_id);
    ASSERT_EQ_INT(ACU_SYMBOL_LOCAL_VAR, loc_sym->kind);
    ASSERT_TRUE((loc_sym->flags & ACU_SYMBOL_FLAG_MUTABLE) != 0);

    AcuSymbol *glob_sym = AcuSymbolTable_Get(&table, glob_id);
    ASSERT_EQ_INT(ACU_SYMBOL_GLOBAL_VAR, glob_sym->kind);
    ASSERT_TRUE((glob_sym->flags & ACU_SYMBOL_FLAG_EXPORTED) != 0);
    ASSERT_TRUE((glob_sym->flags & ACU_SYMBOL_FLAG_MUTABLE) == 0);

    AcuSymbol *fn_sym = AcuSymbolTable_Get(&table, fn_id);
    ASSERT_EQ_INT(ACU_SYMBOL_FUNCTION, fn_sym->kind);
    ASSERT_TRUE((fn_sym->flags & ACU_SYMBOL_FLAG_EXPORTED) == 0);

    ASSERT_EQ_INT(loc_id, AcuSymbolTable_Lookup(&table, scope, NAME_A));
    ASSERT_EQ_INT(glob_id, AcuSymbolTable_Lookup(&table, scope, NAME_B));

    AcuSymbolTable_Free(&table);
}

TEST(SymbolTable, AddSystemFnAndConstants) {
    AcuSymbolTable table = AcuSymbolTable_Create();
    ScopeId scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);

    u32 syscall_tag = 42;
    SymbolId sys_id =
        AcuSymbolTable_AddSystemFn(&table, scope, NAME_SYS, MOCK_TYPE_ID, syscall_tag, true);

    SymbolId const_id = AcuSymbolTable_AddConstant(&table, scope, NAME_A, MOCK_TYPE_ID,
                                                   MOCK_AST_NODE, MOCK_LITERAL_ID, false);

    AcuSymbol *sys_sym = AcuSymbolTable_Get(&table, sys_id);
    ASSERT_EQ_INT(ACU_SYMBOL_SYSTEM_FUNCTION, sys_sym->kind);
    ASSERT_EQ_INT(syscall_tag, sys_sym->as.syscall_tag);
    ASSERT_EQ_INT(ACU_NULL_IDX, sys_sym->decl_node);

    AcuSymbol *const_sym = AcuSymbolTable_Get(&table, const_id);
    ASSERT_EQ_INT(ACU_SYMBOL_CONSTANT, const_sym->kind);
    ASSERT_EQ_INT(MOCK_LITERAL_ID, const_sym->as.const_val);

    AcuSymbolTable_Free(&table);
}

TEST(SymbolTable, ScopeResolutionAndShadowing) {
    AcuSymbolTable table = AcuSymbolTable_Create();

    ScopeId global_scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);
    ScopeId local_scope = AcuSymbolTable_PushScope(&table, global_scope, MOCK_MODULE_ID);

    SymbolId global_a = AcuSymbolTable_AddGlobalVar(&table, global_scope, NAME_A, MOCK_TYPE_ID,
                                                    ACU_NULL_IDX, false, false);
    SymbolId global_b = AcuSymbolTable_AddGlobalVar(&table, global_scope, NAME_B, MOCK_TYPE_ID,
                                                    ACU_NULL_IDX, false, false);

    SymbolId local_b =
        AcuSymbolTable_AddLocalVar(&table, local_scope, NAME_B, MOCK_TYPE_ID, ACU_NULL_IDX, false);
    SymbolId local_c =
        AcuSymbolTable_AddLocalVar(&table, local_scope, NAME_C, MOCK_TYPE_ID, ACU_NULL_IDX, false);

    ASSERT_EQ_INT(global_a, AcuSymbolTable_Lookup(&table, local_scope, NAME_A));
    ASSERT_EQ_INT(local_b, AcuSymbolTable_Lookup(&table, local_scope, NAME_B));
    ASSERT_EQ_INT(local_c, AcuSymbolTable_Lookup(&table, local_scope, NAME_C));

    ASSERT_EQ_INT(global_a, AcuSymbolTable_Lookup(&table, global_scope, NAME_A));
    ASSERT_EQ_INT(global_b, AcuSymbolTable_Lookup(&table, global_scope, NAME_B));
    ASSERT_EQ_INT(ACU_NULL_IDX, AcuSymbolTable_Lookup(&table, global_scope, NAME_C));

    AcuSymbolTable_Free(&table);
}

TEST(SymbolTable, RedefinitionInSameScope) {
    AcuSymbolTable table = AcuSymbolTable_Create();
    ScopeId scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);

    SymbolId first_id =
        AcuSymbolTable_AddLocalVar(&table, scope, NAME_A, MOCK_TYPE_ID, MOCK_AST_NODE, false);

    SymbolId second_id =
        AcuSymbolTable_AddLocalVar(&table, scope, NAME_A, MOCK_TYPE_ID, MOCK_AST_NODE, true);

    ASSERT_TRUE(first_id != second_id);

    SymbolId found_id = AcuSymbolTable_Lookup(&table, scope, NAME_A);
    ASSERT_EQ_INT(second_id, found_id);

    AcuSymbolTable_Free(&table);
}

TEST(SymbolTable, Rehashing) {
    AcuSymbolTable table = AcuSymbolTable_Create();
    ScopeId scope = AcuSymbolTable_PushScope(&table, ACU_NULL_IDX, MOCK_MODULE_ID);

    u32 initial_cap = V_Capacity(&table.hash_table);

    const u32 INSERT_COUNT = 3500;

    for (u32 i = 0; i < INSERT_COUNT; i++) {
        AcuSymbolTable_AddLocalVar(&table, scope, (StringId)i, MOCK_TYPE_ID, ACU_NULL_IDX, false);
    }

    u32 new_cap = V_Capacity(&table.hash_table);

    ASSERT_TRUE(new_cap > initial_cap);
    ASSERT_EQ_INT(initial_cap * 2, new_cap);
    ASSERT_EQ_INT(INSERT_COUNT, table.count);

    SymbolId test_id_0 = AcuSymbolTable_Lookup(&table, scope, 0);
    SymbolId test_id_mid = AcuSymbolTable_Lookup(&table, scope, INSERT_COUNT / 2);
    SymbolId test_id_last = AcuSymbolTable_Lookup(&table, scope, INSERT_COUNT - 1);

    ASSERT_TRUE(test_id_0 != ACU_NULL_IDX);
    ASSERT_TRUE(test_id_mid != ACU_NULL_IDX);
    ASSERT_TRUE(test_id_last != ACU_NULL_IDX);

    ASSERT_EQ_INT(0, AcuSymbolTable_Get(&table, test_id_0)->name);
    ASSERT_EQ_INT(INSERT_COUNT / 2, AcuSymbolTable_Get(&table, test_id_mid)->name);

    AcuSymbolTable_Free(&table);
}
