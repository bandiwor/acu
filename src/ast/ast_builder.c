#include "ast/ast_builder.h"
#include "ast/ast.h"
#include "common/panic.h"
#include "defines/defines.h"
#include "memory/arena.h"
#include "memory/vector.h"
#include "types/acu_position.h"
#include <string.h>

#define AST_DEFAULT_CAPACITY 1024
#define PAYLOAD_DEFAULT_CAPACITY 128

AcuAstBuilder AcuAstBuilder_Create(void) {
    AcuAstBuilder builder = {0};
    AcuAstBuilder_Init(&builder);
    return builder;
}

void AcuAstBuilder_Init(AcuAstBuilder *builder) {
    builder->arena = AcuArena_Create();
    V_Init(&builder->nodes);
    V_Init(&builder->positions);
    V_Init(&builder->type_nodes);
    V_Init(&builder->type_positions);
    V_Init(&builder->extra_pool);
    V_Init(&builder->fn_payloads);
}

void AcuAstBuilder_Free(AcuAstBuilder *builder) {
    AcuArena_Destroy(&builder->arena);

    V_Init(&builder->nodes);
    V_Init(&builder->positions);
    V_Init(&builder->type_nodes);
    V_Init(&builder->type_positions);
    V_Init(&builder->extra_pool);
    V_Init(&builder->fn_payloads);
}

AstNodeIdx AcuAstBuilder_PushNode(AcuAstBuilder *builder, AstNode node, AcuPosition pos) {
    if (unlikely(V_Count(&builder->nodes) != V_Count(&builder->positions))) {
        acu_unreachable_debug("count of nodes (%d) and positions (%d) are different!",
                              V_Count(&builder->nodes), V_Count(&builder->positions));
    }

    AstNodeIdx idx = V_Count(&builder->nodes);

    V_Push(&builder->nodes, node);
    V_Push(&builder->positions, pos);

    return idx;
}

TypeAstNodeIdx AcuAstBuilder_PushTypeNode(AcuAstBuilder *builder, TypeAstNode node,
                                          AcuPosition pos) {
    if (unlikely(V_Count(&builder->type_nodes) != V_Count(&builder->type_positions))) {
        acu_unreachable_debug("count of type-nodes (%d) and type-positions (%d) are different!",
                              V_Count(&builder->type_nodes), V_Count(&builder->type_positions));
    }

    TypeAstNodeIdx idx = V_Count(&builder->type_nodes);

    V_Push(&builder->type_nodes, node);
    V_Push(&builder->type_positions, pos);

    return idx;
}

static u32 PushToExtraPool(AcuAstBuilder *builder, const u32 *data, u32 count) {
    if (count == 0) {
        return ACU_NULL_IDX;
    }

    u32 start_idx = V_Count(&builder->extra_pool);

    V_EnsureCapacity(&builder->extra_pool, start_idx + count);

    memcpy(&V_Elements(&builder->extra_pool)[start_idx], data, count * sizeof(u32));

    V_AddCount(&builder->extra_pool, count);

    return start_idx;
}

ExtraIdx AcuAstBuilder_PushExtraNodes(AcuAstBuilder *builder, const AstNodeIdx *indices,
                                      u32 count) {
    return (ExtraIdx)PushToExtraPool(builder, (const u32 *)indices, count);
}

ExtraIdx AcuAstBuilder_PushExtraTypes(AcuAstBuilder *builder, const TypeAstNodeIdx *indices,
                                      u32 count) {
    return (ExtraIdx)PushToExtraPool(builder, (const u32 *)indices, count);
}

u32 AcuAstBuilder_PushFnDeclPayload(AcuAstBuilder *builder, AstFnDeclPayload payload) {
    u32 idx = V_Count(&builder->fn_payloads);
    V_Push(&builder->fn_payloads, payload);
    return idx;
}

AstFnDeclPayload *AcuAstBuilder_GetFnDeclPayload(AcuAstBuilder *builder, u32 payload_idx) {
    return &V_Elements(&builder->fn_payloads)[payload_idx];
}
