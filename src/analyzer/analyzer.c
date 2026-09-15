#include "analyzer/analyzer.h"
#include "ast/ast_builder.h"
#include "builtin/builtin.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "interner/type_interner.h"
#include "memory/vector.h"
#include <assert.h>
#include <stdbool.h>

AcuWorkspace AcuWorkspace_Create(AcuAstBuilder *builder, AcuStringInterner *interner,
                                 AcuLiteralPool *literals, AcuModuleManager *modules,
                                 AcuFileManager *files, AcuSymbolTable *symbols,
                                 AcuTypeInterner *types) {
    AcuWorkspace ws = {
        .files = files,
        .builder = builder,
        .literals = literals,
        .modules = modules,
        .interner = interner,
        .symbols = symbols,
        .types = types,
        .loop_ctx = {0},
        .node_types = {0},
        .errors = {0},
        .expected_return_type = TYPE_PRIMITIVE_UNIT,
        .main_module_id = ACU_NULL_IDX,
    };

    V_Init(&ws.loop_ctx);
    V_Init(&ws.global_init_order);

    return ws;
}

void AcuWorkspace_Free(AcuWorkspace *ws) {
    V_Free(&ws->loop_ctx);
    V_Free(&ws->global_init_order);
}

void AcuWorkspace_PushError(AcuWorkspace *ws, AstNodeIdx node_idx, AcuResult code,
                            AcuError payload) {
    AcuPosition pos = *AcuAstBuilder_GetNodePos(ws->builder, node_idx);

    payload.code = code;
    payload.offset = pos.offset;
    payload.length = pos.length;
    payload.file = pos.file;

    V_Push(&ws->errors, payload);
}

bool AcuWorkspace_Analyze(AcuWorkspace *ws, StringView main_module_name) {
    AcuBuiltin_RegisterAll(ws);

    if (!AcuWorkspace_Pass1_ParseAndDiscover(ws, main_module_name)) {
        return false;
    }

    V_InitWithCapacity(&ws->node_types, V_Count(&ws->builder->nodes));
    V_InitWithCapacity(&ws->node_analysis, V_Count(&ws->builder->nodes));
    V_SetCount(&ws->node_types, V_Count(&ws->builder->nodes));
    V_SetCount(&ws->node_analysis, V_Count(&ws->builder->nodes));

    if (!AcuWorkspace_Pass2_BindSymbols(ws)) {
        return false;
    }

    if (!AcuWorkspace_Pass3_TypeCheck(ws)) {
        return false;
    }

    AcuWorkspace_Pass4_AnalyzePurity(ws);
    AcuWorkspace_Pass5_Reachability(ws);

    return true;
}

TypeId AcuWorkspace_ResolveAstType(AcuWorkspace *ws, ScopeId scope, TypeAstNodeIdx ast_type_idx) {
    if (unlikely(ast_type_idx == ACU_NULL_IDX)) {
        return TYPE_PRIMITIVE_UNIT;
    }

    TypeAstNode *node = AcuAstBuilder_GetTypeNode(ws->builder, ast_type_idx);

    switch (node->kind) {
        case TYPE_AST_PRIMITIVE: {
            return node->primitive.kind;
        }

        case TYPE_AST_IDENTIFIER: {
            return TYPE_PRIMITIVE_UNIT;
        }

        case TYPE_AST_VECTOR: {
            TypeId inner_type = AcuWorkspace_ResolveAstType(ws, scope, node->vector.inner_type);
            if (unlikely(inner_type == ACU_NULL_IDX)) {
                return ACU_NULL_IDX;
            }

            return AcuTypeInterner_GetVector(ws->types, inner_type);
        }

        case TYPE_AST_ARRAY: {
            TypeId inner_type = AcuWorkspace_ResolveAstType(ws, scope, node->array.inner_type);
            if (unlikely(inner_type == ACU_NULL_IDX)) {
                return ACU_NULL_IDX;
            }

            // ЗАГЛУШКА: Вычисление длины массива из константного выражения.
            // u32 length = AcuConstEvaluator_EvalU32(ws, scope, node->as.array.length_expr);
            u32 length = 1;

            return AcuTypeInterner_GetArray(ws->types, inner_type, length);
        }

        case TYPE_AST_FUNC: {
            TypeId ret_type = TYPE_PRIMITIVE_UNIT;
            if (node->func.return_type != ACU_NULL_IDX) {
                ret_type = AcuWorkspace_ResolveAstType(ws, scope, node->func.return_type);
            }

            TypedVector(TypeId) param_types = {0};
            V_Init(&param_types);

            ExtraIdx param_idx = node->func.params_start;
            for (u32 i = 0; i < node->func.params_count; ++i) {
                TypeAstNodeIdx p_ast = AcuAstBuilder_GetIdxByExtra(ws->builder, param_idx + i);
                TypeId p_type = AcuWorkspace_ResolveAstType(ws, scope, p_ast);
                V_Push(&param_types, p_type);
            }

            TypeId func_signature = AcuTypeInterner_GetFunc(
                ws->types, ret_type, V_Elements(&param_types), V_Count(&param_types));

            V_Free(&param_types);
            return func_signature;
        }

        case TYPE_AST_TUPLE: {
            TypedVector(TypeId) field_types = {0};
            V_Init(&field_types);

            ExtraIdx field_idx = node->tuple.fields_start;
            for (u32 i = 0; i < node->tuple.count; ++i) {
                TypeAstNodeIdx f_ast = AcuAstBuilder_GetIdxByExtra(ws->builder, field_idx + i);
                TypeId f_type = AcuWorkspace_ResolveAstType(ws, scope, f_ast);
                V_Push(&field_types, f_type);
            }

            TypeId tuple_type = AcuTypeInterner_GetTuple(ws->types, V_Elements(&field_types),
                                                         V_Count(&field_types));

            V_Free(&field_types);
            return tuple_type;
        }

        default:
            acu_unreachable_debug("Unknown TypeAstNodeKind in ResolveAstType");
            return ACU_NULL_IDX;
    }
}
