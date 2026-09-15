#pragma once

#include "ast/ast.h"
#include "ast/type_ast.h"
#include "defines/defines.h"
#include "memory/arena.h"
#include "memory/vector.h"
#include "types/acu_position.h"

typedef TypedVector(AstNode) AstNodeVector;
typedef TypedVector(TypeAstNode) TypeAstNodeVector;
typedef TypedVector(AcuPosition) AcuAstBuilderPositionsVector;
typedef TypedVector(AcuPosition) AcuAstBuilderTypePositionsVector;
typedef TypedVector(AstFnDeclPayload) AstFnDeclPayloadsVector;

typedef struct {
    AcuArena arena;

    AstNodeVector nodes;
    AcuAstBuilderPositionsVector positions;

    TypeAstNodeVector type_nodes;
    AcuAstBuilderTypePositionsVector type_positions;

    TypedVector(u32) extra_pool;

    AstFnDeclPayloadsVector fn_payloads;
} AcuAstBuilder;

AcuAstBuilder AcuAstBuilder_Create(void);

void AcuAstBuilder_Init(AcuAstBuilder *builder);
void AcuAstBuilder_Free(AcuAstBuilder *builder);

AstNodeIdx AcuAstBuilder_PushNode(AcuAstBuilder *builder, AstNode node, AcuPosition pos);
TypeAstNodeIdx AcuAstBuilder_PushTypeNode(AcuAstBuilder *builder, TypeAstNode node,
                                          AcuPosition pos);

ExtraIdx AcuAstBuilder_PushExtraNodes(AcuAstBuilder *builder, const AstNodeIdx *indices, u32 count);
ExtraIdx AcuAstBuilder_PushExtraTypes(AcuAstBuilder *builder, const TypeAstNodeIdx *indices,
                                      u32 count);

u32 AcuAstBuilder_PushFnDeclPayload(AcuAstBuilder *builder, AstFnDeclPayload payload);
AstFnDeclPayload *AcuAstBuilder_GetFnDeclPayload(AcuAstBuilder *builder, u32 payload_idx);

#define AcuAstBuilder_GetIdxByExtra(builder, extra_idx)                                            \
    (V_Elements(&(builder)->extra_pool)[extra_idx])
#define AcuAstBuilder_GetNode(builder, idx) (&V_Elements(&(builder)->nodes)[idx])
#define AcuAstBuilder_GetNodePos(builder, idx) (&V_Elements(&(builder)->positions)[idx])
#define AcuAstBuilder_GetTypeNode(builder, idx) (&V_Elements(&(builder)->type_nodes)[idx])
#define AcuAstBuilder_GetTypeNodePos(builder, idx) (&V_Elements(&(builder)->type_positions)[idx])
