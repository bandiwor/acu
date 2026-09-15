#pragma once
#include "ast/ast_builder.h"
#include "common/error.h"
#include "defines/defines.h"
#include "fs/file_manager.h"
#include "interner/string_interner.h"
#include "interner/type_interner.h"
#include "literal/literal_pool.h"
#include "memory/vector.h"
#include "module/module_manager.h"
#include "table/symbol_table.h"
#include "types/string_view.h"

typedef struct {
    u16 kind;
    bool has_break;
    TypeId expected_type;
} AcuLoopContext;

typedef union {
    struct {
        ScopeId inner_scope;
    } block;

    struct {
        SymbolId sym_id;
    } var_decl;

    struct {
        SymbolId sym_id;
        ScopeId body_scope;
    } fn_decl;

    struct {
        SymbolId resolved_sym;
    } reference;

    struct {
        SymbolId callee_sym;
    } call;

    struct {
        AstNodeIdx target_loop;
    } control_flow;

    u64 raw;
} AstNodeAnalysisData;

typedef struct {
    AcuAstBuilder *builder;
    AcuStringInterner *interner;
    AcuLiteralPool *literals;
    AcuModuleManager *modules;
    AcuFileManager *files;
    AcuSymbolTable *symbols;
    AcuTypeInterner *types;
    TypedVector(TypeId) node_types;
    TypedVector(AstNodeAnalysisData) node_analysis;
    TypedVector(AcuError) errors;
    TypedVector(AcuLoopContext) loop_ctx;
    TypedVector(SymbolId) global_init_order;
    TypeId expected_return_type;
    ModuleId main_module_id;
} AcuWorkspace;

AcuWorkspace AcuWorkspace_Create(AcuAstBuilder *builder, AcuStringInterner *interner,
                                 AcuLiteralPool *literals, AcuModuleManager *modules,
                                 AcuFileManager *files, AcuSymbolTable *symbols,
                                 AcuTypeInterner *types);

void AcuWorkspace_Free(AcuWorkspace *ws);

void AcuWorkspace_PushError(AcuWorkspace *ws, AstNodeIdx node_idx, AcuResult code,
                            AcuError payload);

bool AcuWorkspace_Pass1_ParseAndDiscover(AcuWorkspace *ws, StringView main_module_name);
bool AcuWorkspace_Pass2_BindSymbols(AcuWorkspace *ws);
bool AcuWorkspace_Pass3_TypeCheck(AcuWorkspace *ws);
void AcuWorkspace_Pass4_AnalyzePurity(AcuWorkspace *ws);
void AcuWorkspace_Pass5_Reachability(AcuWorkspace *ws);

bool AcuWorkspace_Analyze(AcuWorkspace *ws, StringView main_module_name);

TypeId AcuWorkspace_ResolveAstType(AcuWorkspace *ws, ScopeId scope, TypeAstNodeIdx ast_type_idx);
