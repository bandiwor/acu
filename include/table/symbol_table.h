#pragma once
#include "defines/defines.h"
#include "memory/vector.h"

typedef enum {
    ACU_SYMBOL_LOCAL_VAR,
    ACU_SYMBOL_GLOBAL_VAR,
    ACU_SYMBOL_FUNCTION,
    ACU_SYMBOL_SYSTEM_FUNCTION,
    ACU_SYMBOL_CONSTANT,
    ACU_SYMBOL_MODULE,
} AcuSymbolKind;

enum {
    ACU_SYMBOL_FLAG_MUTABLE = 1 << 0,
    ACU_SYMBOL_FLAG_EXPORTED = 1 << 1,
    ACU_SYMBOL_FLAG_USED = 1 << 2,
    ACU_SYMBOL_FLAG_PURE = 1 << 3,
    ACU_SYMBOL_FLAG_GLOBAL_CHECKING = 1 << 4,
    ACU_SYMBOL_FLAG_GLOBAL_CHECKED = 1 << 5,
    ACU_SYMBOL_FLAG_REACHABLE = 1 << 6,
};

typedef struct {
    u16 kind;
    u16 flags;
    StringId name;
    TypeId type;
    ScopeId scope_id;
    AstNodeIdx decl_node;

    union {
        u32 syscall_tag;
        LiteralIdx const_val;
        ModuleId target_module_id;
    } as;
} AcuSymbol;

typedef struct {
    ScopeId parent_id;
    ModuleId module_id;
} AcuScope;

typedef struct {
    u32 hash;
    SymbolId sym_id;
} AcuSymbolSlot;

typedef struct {
    TypedVector(AcuSymbol) symbols;
    TypedVector(AcuScope) scopes;
    TypedVector(AcuSymbolSlot) hash_table;
    u32 count;
} AcuSymbolTable;

AcuSymbolTable AcuSymbolTable_Create(void);
void AcuSymbolTable_Init(AcuSymbolTable *table);
void AcuSymbolTable_Free(AcuSymbolTable *table);

ScopeId AcuSymbolTable_PushScope(AcuSymbolTable *table, ScopeId parent, ModuleId module);

SymbolId AcuSymbolTable_AddModule(AcuSymbolTable *table, ScopeId scope, StringId name,
                                  AstNodeIdx decl_node, ModuleId target_module, bool is_exported);

SymbolId AcuSymbolTable_AddLocalVar(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, AstNodeIdx decl_node, bool is_mutable);
SymbolId AcuSymbolTable_AddGlobalVar(AcuSymbolTable *table, ScopeId scope, StringId name,
                                     TypeId type, AstNodeIdx decl_node, bool is_exported,
                                     bool is_mutable);
SymbolId AcuSymbolTable_AddFn(AcuSymbolTable *table, ScopeId scope, StringId name, TypeId type,
                              AstNodeIdx decl_node, bool is_exported);
SymbolId AcuSymbolTable_AddSystemFn(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, u32 syscall_tag, bool is_exported);
SymbolId AcuSymbolTable_AddConstant(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, AstNodeIdx decl_node, LiteralIdx literal,
                                    bool is_exported);

SymbolId AcuSymbolTable_Lookup(AcuSymbolTable *table, ScopeId start_scope, StringId name);
SymbolId AcuSymbolTable_LookupExact(AcuSymbolTable *table, ScopeId scope, StringId name);

AcuScope *AcuSymbolTable_GetScope(AcuSymbolTable *table, ScopeId id);
ModuleId AcuSymbolTable_GetModule(AcuSymbolTable *table, ScopeId scope_id);

#define AcuSymbolTable_Get(table_ptr, id) (&V_Elements(&(table_ptr)->symbols)[id])
