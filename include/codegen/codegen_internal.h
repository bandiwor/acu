#pragma once
#include "codegen/codegen.h"
#include "codegen/reg_alloc.h"
#include "common/panic.h"

typedef struct {
    SymbolId sym_id;
    AcuReg reg;
} AcuLocalBinding;

typedef struct {
    u32 inst_idx;
    SymbolId fn_sym_id;
} AcuFuncCallPatch;

typedef struct {
    u32 start_ip;
    TypedVector(u32) break_jumps;
    AcuTarget target_reg;
    AcuReg result_reg;
    bool is_tail;
} AcuLoopCtx;

typedef struct {
    AcuWorkspace *ws;
    AcuChunk *chunk;

    TypedVector(AcuLocalBinding) locals;
    TypedVector(AcuLoopCtx) loops;
    TypedVector(AcuFuncCallPatch) fn_patches;
    TypedVector(u32) fn_offsets;
    TypedVector(u32) sym_to_global;

    AcuRegSet reg_set;
    u32 max_reg;

    SymbolId last_stored_global;
    AcuReg last_stored_reg;

    SymbolId current_fn_sym;
    u32 current_fn_entry_ip;

    bool is_tail_pos;
    bool self_tail_emitted;
} AcuCodeGen;

static inline void AcuCodeGen_InvalidateGlobalCache(AcuCodeGen *cg) {
    cg->last_stored_global = ACU_NULL_IDX;
}

static inline u32 AcuCodeGen_Emit(AcuCodeGen *cg, AcuInstruction inst) {
    u32 ip = (u32)V_Count(&cg->chunk->code);
    V_Push(&cg->chunk->code, inst);
    return ip;
}

static inline u32 AcuCodeGen_EmitWide(AcuCodeGen *cg, AcuInstruction inst, u32 wide_payload) {
    u32 ip = AcuCodeGen_Emit(cg, inst);
    V_Push(&cg->chunk->code, (AcuInstruction)wide_payload);
    return ip;
}

static inline u32 AcuCodeGen_GetGlobalSlot(AcuCodeGen *cg, SymbolId sym_id) {
    assert(sym_id < V_Count(&cg->sym_to_global) && "SymbolId out of range!");
    u32 slot = V_At(&cg->sym_to_global, sym_id);
    assert(slot != ACU_NULL_IDX && "Symbol is not registered as a global variable!");
    return slot;
}

static inline AcuReg AcuCodeGen_AllocReg(AcuCodeGen *cg) {
    AcuReg r = AcuRegSet_Alloc(&cg->reg_set);
    if ((u32)r + 1 > cg->max_reg)
        cg->max_reg = (u32)r + 1;
    if (cg->last_stored_global != ACU_NULL_IDX && cg->last_stored_reg == r) {
        AcuCodeGen_InvalidateGlobalCache(cg);
    }
    return r;
}

static inline AcuReg AcuCodeGen_AllocContiguous(AcuCodeGen *cg, u32 count) {
    AcuReg base = 0;
    if (!AcuRegSet_AllocContiguous(&cg->reg_set, count, &base)) {
        acu_panic("Register file overflow: failed to allocate %u contiguous registers", count);
    }
    if ((u32)base + count > cg->max_reg)
        cg->max_reg = (u32)base + count;
    if (cg->last_stored_global != ACU_NULL_IDX) {
        if (cg->last_stored_reg >= base && cg->last_stored_reg < base + count) {
            AcuCodeGen_InvalidateGlobalCache(cg);
        }
    }
    return base;
}

static inline void AcuCodeGen_FreeReg(AcuCodeGen *cg, AcuReg r) {
    AcuRegSet_Free(&cg->reg_set, r);
}

static inline AcuReg AcuCodeGen_ResolveTarget(AcuCodeGen *cg, AcuTarget target) {
    assert(!AcuTarget_IsNone(target) &&
           "Attempted to resolve physical register for ACU_TARGET_NONE!");

    if (AcuTarget_IsRealReg(target)) {
        AcuReg reg = AcuTarget_ToRealReg(target);
        AcuRegSet_MarkUsed(&cg->reg_set, reg);

        if ((u32)reg + 1 > cg->max_reg) {
            cg->max_reg = (u32)reg + 1;
        }

        if (cg->last_stored_global != ACU_NULL_IDX && cg->last_stored_reg == reg) {
            AcuCodeGen_InvalidateGlobalCache(cg);
        }

        return reg;
    }

    return AcuCodeGen_AllocReg(cg);
}

static inline bool AcuCodeGen_IsNever(AcuCodeGen *cg, AstNodeIdx idx) {
    if (idx == ACU_NULL_IDX)
        return false;
    return V_At(&cg->ws->node_types, idx) == (TypeId)TYPE_PRIMITIVE_NEVER;
}

AcuLocalBinding *AcuCodeGen_FindLocal(AcuCodeGen *cg, SymbolId sym_id);
bool AcuCodeGen_IsLocalReg(AcuCodeGen *cg, AcuReg r);
AcuTarget AcuCodeGen_EmitExpr(AcuCodeGen *cg, AstNodeIdx expr_idx, AcuTarget target);
