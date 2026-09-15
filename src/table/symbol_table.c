#include "table/symbol_table.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "memory/vector.h"

AcuSymbolTable AcuSymbolTable_Create(void) {
    AcuSymbolTable table = {0};
    AcuSymbolTable_Init(&table);
    return table;
}

void AcuSymbolTable_Init(AcuSymbolTable *table) {
    V_Init(&table->symbols);
    V_Init(&table->scopes);
    V_InitWithCapacity(&table->hash_table, 4096);

    AcuSymbolSlot empty_slot = {.hash = 0, .sym_id = ACU_NULL_IDX};

    for (u32 i = 0; i < V_Capacity(&table->hash_table); i++) {
        V_Push(&table->hash_table, empty_slot);
    }

    table->count = 0;
}

void AcuSymbolTable_Free(AcuSymbolTable *table) {
    V_Free(&table->symbols);
    V_Free(&table->scopes);
    V_Free(&table->hash_table);
}

ScopeId AcuSymbolTable_PushScope(AcuSymbolTable *table, ScopeId parent, ModuleId module) {
    ScopeId new_id = V_Count(&table->scopes);
    AcuScope scope = {.parent_id = parent, .module_id = module};

    V_Push(&table->scopes, scope);
    return new_id;
}

static inline u32 HashNameAndScope(StringId name, ScopeId scope) {
    u64 key = ((u64)scope << 32) | (u32)name;

    key ^= key >> 28;
    key *= 0x517cc1b727220a95ULL;
    key ^= key >> 28;

    return (u32)(key ^ (key >> 32));
}

static void AcuSymbolTable_Rehash(AcuSymbolTable *table) {
    const u32 old_cap = V_Capacity(&table->hash_table);
    const u32 new_cap = old_cap * 2;
    const u32 mask = new_cap - 1;

    TypedVector(AcuSymbolSlot) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);

    AcuSymbolSlot empty_slot = {.hash = 0, .sym_id = ACU_NULL_IDX};
    for (u32 i = 0; i < new_cap; i++) {
        V_Push(&new_ht, empty_slot);
    }

    const AcuSymbolSlot *const old_elements = V_Elements(&table->hash_table);
    AcuSymbolSlot *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        AcuSymbolSlot slot = old_elements[i];
        if (slot.sym_id != ACU_NULL_IDX) {
            u32 idx = slot.hash & mask;

            while (new_elements[idx].sym_id != ACU_NULL_IDX) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }

    V_Free(&table->hash_table);
    table->hash_table.base = new_ht.base;
}

static SymbolId AcuSymbolTable_Add(AcuSymbolTable *table, AcuSymbol symbol) {
    if (unlikely((u64)table->count * 4 >= V_Count(&table->hash_table) * 3)) {
        AcuSymbolTable_Rehash(table);
    }

    u64 hash = HashNameAndScope(symbol.name, symbol.scope_id);
    u32 cap = V_Count(&table->hash_table);
    u32 mask = cap - 1;
    u32 idx = hash & mask;

    AcuSymbolSlot *ht = V_Elements(&table->hash_table);
    AcuSymbol *symbols = V_Elements(&table->symbols);

    while (ht[idx].sym_id != ACU_NULL_IDX) {
        if (ht[idx].hash == hash) {
            u32 existing_sym_id = ht[idx].sym_id;
            AcuSymbol *sym = &symbols[existing_sym_id];

            if (sym->name == symbol.name && sym->scope_id == symbol.scope_id) {
                SymbolId new_sym_id = V_Count(&table->symbols);
                V_Push(&table->symbols, symbol);

                ht[idx].sym_id = new_sym_id;
                return new_sym_id;
            }
        }
        idx = (idx + 1) & mask;
    }

    SymbolId new_sym_id = V_Count(&table->symbols);
    V_Push(&table->symbols, symbol);

    ht[idx].hash = hash;
    ht[idx].sym_id = new_sym_id;
    table->count++;

    return new_sym_id;
}

SymbolId AcuSymbolTable_AddModule(AcuSymbolTable *table, ScopeId scope, StringId name,
                                  AstNodeIdx decl_node, ModuleId target_module, bool is_exported) {
    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_MODULE,
                                         .name = name,
                                         .type = TYPE_PRIMITIVE_MODULE,
                                         .scope_id = scope,
                                         .decl_node = decl_node,
                                         .flags = (int)is_exported ? ACU_SYMBOL_FLAG_EXPORTED : 0,
                                         .as.target_module_id = target_module,
                                     });
}

SymbolId AcuSymbolTable_AddLocalVar(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, AstNodeIdx decl_node, bool is_mutable) {
    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_LOCAL_VAR,
                                         .name = name,
                                         .type = type,
                                         .scope_id = scope,
                                         .decl_node = decl_node,
                                         .flags = (int)is_mutable ? ACU_SYMBOL_FLAG_MUTABLE : 0,
                                     });
}

SymbolId AcuSymbolTable_AddGlobalVar(AcuSymbolTable *table, ScopeId scope, StringId name,
                                     TypeId type, AstNodeIdx decl_node, bool is_exported,
                                     bool is_mutable) {
    u16 flags = 0;
    (int)is_exported ? flags |= ACU_SYMBOL_FLAG_EXPORTED : 0;
    (int)is_mutable ? flags |= ACU_SYMBOL_FLAG_MUTABLE : 0;

    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_GLOBAL_VAR,
                                         .name = name,
                                         .type = type,
                                         .scope_id = scope,
                                         .decl_node = decl_node,
                                         .flags = flags,
                                     });
}

SymbolId AcuSymbolTable_AddFn(AcuSymbolTable *table, ScopeId scope, StringId name, TypeId type,
                              AstNodeIdx decl_node, bool is_exported) {
    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_FUNCTION,
                                         .name = name,
                                         .type = type,
                                         .scope_id = scope,
                                         .decl_node = decl_node,
                                         .flags = (int)is_exported ? ACU_SYMBOL_FLAG_EXPORTED : 0,
                                     });
}

SymbolId AcuSymbolTable_AddSystemFn(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, u32 syscall_tag, bool is_exported) {
    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_SYSTEM_FUNCTION,
                                         .name = name,
                                         .type = type,
                                         .scope_id = scope,
                                         .decl_node = ACU_NULL_IDX,
                                         .flags = (int)is_exported ? ACU_SYMBOL_FLAG_EXPORTED : 0,
                                         .as.syscall_tag = syscall_tag,
                                     });
}

SymbolId AcuSymbolTable_AddConstant(AcuSymbolTable *table, ScopeId scope, StringId name,
                                    TypeId type, AstNodeIdx decl_node, LiteralIdx literal,
                                    bool is_exported) {
    return AcuSymbolTable_Add(table, (AcuSymbol){
                                         .kind = ACU_SYMBOL_CONSTANT,
                                         .name = name,
                                         .type = type,
                                         .scope_id = scope,
                                         .decl_node = decl_node,
                                         .flags = (int)is_exported ? ACU_SYMBOL_FLAG_EXPORTED : 0,
                                         .as.const_val = literal,
                                     });
}

SymbolId AcuSymbolTable_LookupExact(AcuSymbolTable *table, ScopeId scope, StringId name) {
    if (unlikely(scope == ACU_NULL_IDX))
        return ACU_NULL_IDX;

    u32 hash = HashNameAndScope(name, scope);
    u32 cap = V_Count(&table->hash_table);
    u32 mask = cap - 1;
    u32 idx = hash & mask;

    AcuSymbolSlot *ht = V_Elements(&table->hash_table);
    AcuSymbol *symbols = V_Elements(&table->symbols);

    while (ht[idx].sym_id != ACU_NULL_IDX) {
        if (ht[idx].hash == hash) {
            u32 sym_id = ht[idx].sym_id;
            AcuSymbol *sym = &symbols[sym_id];

            if (sym->name == name && sym->scope_id == scope) {
                return sym_id;
            }
        }
        idx = (idx + 1) & mask;
    }

    return ACU_NULL_IDX;
}

SymbolId AcuSymbolTable_Lookup(AcuSymbolTable *table, ScopeId start_scope, StringId name) {
    ScopeId current_scope = start_scope;

    while (current_scope != ACU_NULL_IDX) {
        SymbolId found = AcuSymbolTable_LookupExact(table, current_scope, name);
        if (found != ACU_NULL_IDX) {
            return found;
        }

        current_scope = V_Elements(&table->scopes)[current_scope].parent_id;
    }

    return ACU_NULL_IDX;
}

AcuScope *AcuSymbolTable_GetScope(AcuSymbolTable *table, ScopeId id) {
    return &V_Elements(&table->scopes)[id];
}

ModuleId AcuSymbolTable_GetModule(AcuSymbolTable *table, ScopeId scope_id) {
    return V_Elements(&table->scopes)[scope_id].module_id;
}
