#include "optimizer/optimizer.h"
#include "defines/bytecode.h"
#include "memory/vector.h"
#include "vm/syscall.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    u64 w[4];
} AcuRegBitset;

static inline void AcuRegSet_Zero(AcuRegBitset *s) {
    s->w[0] = 0;
    s->w[1] = 0;
    s->w[2] = 0;
    s->w[3] = 0;
}

static inline void AcuRegSet_Fill(AcuRegBitset *s) {
    s->w[0] = ~0ULL;
    s->w[1] = ~0ULL;
    s->w[2] = ~0ULL;
    s->w[3] = ~0ULL;
}

static inline void AcuRegSet_Set(AcuRegBitset *s, u8 reg) {
    s->w[reg >> 6] |= (1ULL << (reg & 63));
}

static inline void AcuRegSet_Clear(AcuRegBitset *s, u8 reg) {
    s->w[reg >> 6] &= ~(1ULL << (reg & 63));
}

static inline bool AcuRegSet_Test(const AcuRegBitset *s, u8 reg) {
    return (s->w[reg >> 6] & (1ULL << (reg & 63))) != 0;
}

static inline void AcuRegSet_SetRange(AcuRegBitset *s, u8 base, u8 count) {
    for (u32 i = 0; i < count; ++i) {
        AcuRegSet_Set(s, (u8)(base + i));
    }
}

static inline bool AcuOpt_IsBytecodeCall(AcuOpcode op) {
    return op == OP_CALL || op == OP_CALL_VOID || op == OP_TAILCALL;
}

static inline AcuSyscallId AcuOpt_GetSyscallIdAt(const AcuInstruction *code, u32 ip, u32 count) {
    AcuOpcode op = AcuInst_GetOp(code[ip]);
    assert(op == OP_SYSCALL || op == OP_SYSCALL_VOID);
    if (ip + 1 < count) {
        return (AcuSyscallId)code[ip + 1];
    }
    return (AcuSyscallId)UINT32_MAX;
}

static inline bool AcuOpt_IsInstructionTerminator(const AcuInstruction *code, u32 ip, u32 count) {
    const AcuInstruction inst = code[ip];
    const AcuOpcode op = AcuInst_GetOp(inst);
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);

    if (info->flags & ACU_FLAG_TERMINATOR) {
        return true;
    }

    if (op == OP_SYSCALL || op == OP_SYSCALL_VOID) {
        AcuSyscallId sys_id = AcuOpt_GetSyscallIdAt(code, ip, count);
        const AcuSyscallInfo *sinfo = AcuSyscall_GetInfo(sys_id);
        if (sinfo && sinfo->is_terminator) {
            return true;
        }
    }

    return false;
}

static inline bool AcuOpt_CanDCE(AcuInstruction inst) {
    AcuOpcode op = AcuInst_GetOp(inst);
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);

    if (info->default_purity <= ACU_PURITY_PURE) {
        return true;
    }

    if (op == OP_DIV_S_IMM || op == OP_REM_S_IMM) {
        i8 imm = AcuInst_GetsC(inst);
        return imm != 0 && imm != -1;
    }
    if (op == OP_DIV_U_IMM || op == OP_REM_U_IMM) {
        u8 imm = (u8)AcuInst_GetC(inst);
        return imm != 0;
    }

    return false;
}

static inline bool AcuOpt_IsInstructionPure(const AcuInstruction *code, u32 ip, u32 count) {
    const AcuInstruction inst = code[ip];
    const AcuOpcode op = AcuInst_GetOp(inst);

    if (op == OP_SYSCALL || op == OP_SYSCALL_VOID) {
        AcuSyscallId sys_id = AcuOpt_GetSyscallIdAt(code, ip, count);
        const AcuSyscallInfo *sinfo = AcuSyscall_GetInfo(sys_id);
        return sinfo && (sinfo->purity <= ACU_PURITY_PURE);
    }

    return AcuOpt_CanDCE(inst);
}

static inline void AcuOpt_AddInstructionUses(AcuInstruction inst, AcuRegBitset *live) {
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(AcuInst_GetOp(inst));

    switch (info->read_mode) {
        case OP_READ_NONE:
            break;
        case OP_READ_A:
            AcuRegSet_Set(live, AcuInst_GetA(inst));
            break;
        case OP_READ_B:
            AcuRegSet_Set(live, AcuInst_GetB(inst));
            break;
        case OP_READ_C:
            AcuRegSet_Set(live, AcuInst_GetC(inst));
            break;
        case OP_READ_AB:
            AcuRegSet_Set(live, AcuInst_GetA(inst));
            AcuRegSet_Set(live, AcuInst_GetB(inst));
            break;
        case OP_READ_BC:
            AcuRegSet_Set(live, AcuInst_GetB(inst));
            AcuRegSet_Set(live, AcuInst_GetC(inst));
            break;
        case OP_READ_ABC:
            AcuRegSet_Set(live, AcuInst_GetA(inst));
            AcuRegSet_Set(live, AcuInst_GetB(inst));
            AcuRegSet_Set(live, AcuInst_GetC(inst));
            break;
        case OP_READ_RANGE_CALL:
        case OP_READ_RANGE_SYSCALL: {
            AcuReg base = AcuInst_GetB(inst);
            u8 argc = AcuInst_GetC(inst);
            AcuRegSet_SetRange(live, base, argc);
            break;
        }
    }
}

static inline bool AcuOpt_SubstituteReadReg(AcuInstruction *inst, AcuReg old_reg, AcuReg new_reg) {
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(AcuInst_GetOp(*inst));
    bool replaced = false;

    switch (info->read_mode) {
        case OP_READ_NONE:
            break;

        // Если вызов принимает ровно 1 аргумент, диапазон вырождается в единственный регистр base
        // (B)
        case OP_READ_RANGE_CALL:
        case OP_READ_RANGE_SYSCALL: {
            const u8 argc = (u8)AcuInst_GetC(*inst);
            if (argc == 1 && AcuInst_GetB(*inst) == old_reg) {
                AcuInst_SetB(inst, new_reg);
                replaced = true;
            }
            break;
        }

        case OP_READ_A:
            if (AcuInst_GetA(*inst) == old_reg) {
                AcuInst_SetA(inst, new_reg);
                replaced = true;
            }
            break;

        case OP_READ_B:
            if (AcuInst_GetB(*inst) == old_reg) {
                AcuInst_SetB(inst, new_reg);
                replaced = true;
            }
            break;

        case OP_READ_C:
            if (AcuInst_GetC(*inst) == old_reg) {
                AcuInst_SetC(inst, new_reg);
                replaced = true;
            }
            break;

        case OP_READ_AB:
            if (AcuInst_GetA(*inst) == old_reg) {
                AcuInst_SetA(inst, new_reg);
                replaced = true;
            }
            if (AcuInst_GetB(*inst) == old_reg) {
                AcuInst_SetB(inst, new_reg);
                replaced = true;
            }
            break;

        case OP_READ_BC:
            if (AcuInst_GetB(*inst) == old_reg) {
                AcuInst_SetB(inst, new_reg);
                replaced = true;
            }
            if (AcuInst_GetC(*inst) == old_reg) {
                AcuInst_SetC(inst, new_reg);
                replaced = true;
            }
            break;

        case OP_READ_ABC:
            if (AcuInst_GetA(*inst) == old_reg) {
                AcuInst_SetA(inst, new_reg);
                replaced = true;
            }
            if (AcuInst_GetB(*inst) == old_reg) {
                AcuInst_SetB(inst, new_reg);
                replaced = true;
            }
            if (AcuInst_GetC(*inst) == old_reg) {
                AcuInst_SetC(inst, new_reg);
                replaced = true;
            }
            break;
    }

    return replaced;
}

static inline attribute_always_inline bool AcuOpt_InstReadsReg(AcuInstruction inst, AcuReg reg) {
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(AcuInst_GetOp(inst));

    switch (info->read_mode) {
        case OP_READ_NONE:
            return false;
        case OP_READ_A:
            return AcuInst_GetA(inst) == reg;
        case OP_READ_B:
            return AcuInst_GetB(inst) == reg;
        case OP_READ_C:
            return AcuInst_GetC(inst) == reg;
        case OP_READ_AB:
            return AcuInst_GetA(inst) == reg || AcuInst_GetB(inst) == reg;
        case OP_READ_BC:
            return AcuInst_GetB(inst) == reg || AcuInst_GetC(inst) == reg;
        case OP_READ_ABC:
            return AcuInst_GetA(inst) == reg || AcuInst_GetB(inst) == reg ||
                   AcuInst_GetC(inst) == reg;
        case OP_READ_RANGE_CALL:
        case OP_READ_RANGE_SYSCALL: {
            const AcuReg base = AcuInst_GetB(inst);
            const u32 argc = (u32)AcuInst_GetC(inst);
            return ((u32)reg >= (u32)base) && ((u32)reg < (u32)base + argc);
        }
    }

    return false;
}

static inline bool AcuOpt_IsFunctionExit(AcuOpcode op) {
    return op == OP_RET || op == OP_RET_VOID || op == OP_HALT || op == OP_TAILCALL;
}

static bool AcuOpt_IsRegDeadAfter(const AcuChunk *chunk, u32 from_ip, AcuReg reg,
                                  const bool *is_target) {
    const u32 count = (u32)V_Count(&chunk->code);
    if (from_ip >= count) {
        return true;
    }

    const AcuInstruction *code = &V_At(&chunk->code, 0);

    for (u32 ip = from_ip; ip < count;) {
        if (is_target[ip]) {
            return false;
        }

        const AcuInstruction inst = code[ip];
        const AcuOpcode op = AcuInst_GetOp(inst);

        if (op == OP_NOP) {
            ip += 1;
            continue;
        }

        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 slots = info->slots;

        if (AcuOpt_InstReadsReg(inst, reg)) {
            return false;
        }

        if (info->write_mode == OP_WRITE_A && AcuInst_GetA(inst) == reg) {
            return true;
        }

        if (AcuOpt_IsFunctionExit(op)) {
            return true;
        }

        if (op == OP_SYSCALL || op == OP_SYSCALL_VOID) {
            AcuSyscallId sys_id = AcuOpt_GetSyscallIdAt(code, ip, count);
            const AcuSyscallInfo *sinfo = AcuSyscall_GetInfo(sys_id);
            if (sinfo && sinfo->is_terminator) {
                return true;
            }
        }

        if (info->flags & (ACU_FLAG_TERMINATOR | ACU_FLAG_BRANCH_COND)) {
            return false;
        }

        ip += slots;
    }

    return true;
}

/**
 * Универсальный Target Forwarding (Def-to-Move Forwarding):
 * Ищет пару (def tmp_reg) + ... + (MOVE final_reg, tmp_reg) в пределах базового блока.
 * Перенаправляет запись сразу в final_reg и устраняет команду MOVE.
 */
static bool AcuOpt_TryForwardDefToMove(AcuChunk *chunk, u32 ip, const bool *is_target) {
    AcuInstruction *code = &V_At(&chunk->code, 0);
    const u32 count = (u32)V_Count(&chunk->code);
    AcuInstruction *inst = &code[ip];
    const AcuOpcode op = AcuInst_GetOp(*inst);
    const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);

    if (info->write_mode != OP_WRITE_A) {
        return false;
    }

    const AcuReg tmp_reg = AcuInst_GetA(*inst);
    const u8 sz = info->slots;

    u32 scan_ip = ip + sz;
    while (scan_ip < count) {
        if (is_target[scan_ip]) {
            break;
        }

        AcuInstruction *scan_inst = &code[scan_ip];
        const AcuOpcode scan_op = AcuInst_GetOp(*scan_inst);

        if (scan_op == OP_NOP) {
            scan_ip += 1;
            continue;
        }

        const AcuOpcodeInfo *scan_info = AcuOpcode_GetInfo(scan_op);

        if (scan_op == OP_MOVE && AcuInst_GetB(*scan_inst) == tmp_reg) {
            const AcuReg final_reg = AcuInst_GetA(*scan_inst);

            if (final_reg == tmp_reg) {
                scan_ip += scan_info->slots;
                continue;
            }

            // Защита вызовов: final_reg не должен перекрывать аргументы этого вызова
            if (AcuOpt_IsBytecodeCall(op)) {
                const AcuReg base = AcuInst_GetB(*inst);
                const u32 argc = (u32)AcuInst_GetC(*inst);
                if ((u32)final_reg >= (u32)base && (u32)final_reg < (u32)base + argc) {
                    break;
                }
            }

            // Проверяем промежуточные инструкции на чтение или перезапись final_reg
            bool conflict = false;
            for (u32 mid_ip = ip + sz; mid_ip < scan_ip;) {
                const AcuInstruction mid_inst = code[mid_ip];
                const AcuOpcode mid_op = AcuInst_GetOp(mid_inst);
                if (mid_op == OP_NOP) {
                    mid_ip += 1;
                    continue;
                }
                const AcuOpcodeInfo *mid_info = AcuOpcode_GetInfo(mid_op);

                if (AcuOpt_InstReadsReg(mid_inst, final_reg) ||
                    (mid_info->write_mode == OP_WRITE_A && AcuInst_GetA(mid_inst) == final_reg)) {
                    conflict = true;
                    break;
                }

                mid_ip += mid_info->slots;
            }

            if (conflict) {
                break;
            }

            // tmp_reg должен быть гарантированно мёртв после этого MOVE
            if (!AcuOpt_IsRegDeadAfter(chunk, scan_ip + scan_info->slots, tmp_reg, is_target)) {
                break;
            }

            // Перенаправляем запись в final_reg, затирая исходный MOVE
            AcuInst_SetA(inst, final_reg);
            *scan_inst = AcuInst_Encode_NONE(OP_NOP);
            return true;
        }

        // Если промежуточная инструкция читает или перезаписывает tmp_reg — останавливаемся
        if (AcuOpt_InstReadsReg(*scan_inst, tmp_reg)) {
            break;
        }
        if (scan_info->write_mode == OP_WRITE_A && AcuInst_GetA(*scan_inst) == tmp_reg) {
            break;
        }

        // Границы базового блока
        if ((scan_info->flags & (ACU_FLAG_TERMINATOR | ACU_FLAG_BRANCH_COND)) ||
            AcuOpt_IsInstructionTerminator(code, scan_ip, count)) {
            break;
        }

        scan_ip += scan_info->slots;
    }

    return false;
}

static void AcuOpt_CollectJumpTargets(const AcuChunk *chunk, bool *is_target) {
    const u32 count = (u32)V_Count(&chunk->code);
    memset(is_target, 0, sizeof(bool) * (count + 1));

    if (count == 0) {
        return;
    }

    is_target[0] = true;

    for (u32 ip = 0; ip < count;) {
        AcuInstruction inst = V_At(&chunk->code, ip);
        AcuOpcode op = AcuInst_GetOp(inst);
        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);

        if (info->flags & (ACU_FLAG_BRANCH_COND | ACU_FLAG_BRANCH_UNCOND)) {
            u32 target = AcuOpt_GetJumpTarget(ip, inst);
            if (target <= count) {
                is_target[target] = true;
            }
        } else if (AcuOpt_IsBytecodeCall(op)) {
            if (ip + 1 < count) {
                u32 fn_target = (u32)V_At(&chunk->code, ip + 1);
                if (fn_target <= count) {
                    is_target[fn_target] = true;
                }
            }
        }

        ip += info->slots;
    }
}

static bool AcuOpt_EliminateDeadCode(AcuChunk *chunk, const bool *is_target) {
    const u32 count = (u32)V_Count(&chunk->code);
    if (count == 0) {
        return false;
    }

    AcuInstruction *code = &V_At(&chunk->code, 0);
    const AcuInstruction nop_inst = AcuInst_Encode_NONE(OP_NOP);
    bool changed = false;
    bool unreachable = false;

    for (u32 ip = 0; ip < count;) {
        if (is_target[ip]) {
            unreachable = false;
        }

        const AcuInstruction inst = code[ip];
        const AcuOpcode op = AcuInst_GetOp(inst);
        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 slots = info->slots;
        const u32 next_ip = (ip + slots <= count) ? (ip + slots) : count;

        if (unreachable) {
            if (op != OP_NOP) {
                code[ip] = nop_inst;
                changed = true;
            }
            if (slots > 1 && (ip + 1 < count) && code[ip + 1] != nop_inst) {
                code[ip + 1] = nop_inst;
                changed = true;
            }
        } else if (AcuOpt_IsInstructionTerminator(code, ip, count)) {
            unreachable = true;
        }

        ip = next_ip;
    }

    return changed;
}

static bool AcuOpt_EliminateDeadStores(AcuChunk *chunk, const bool *is_target, u32 *bb_ips) {
    const u32 count = (u32)V_Count(&chunk->code);
    if (count == 0) {
        return false;
    }

    AcuInstruction *code = &V_At(&chunk->code, 0);
    const AcuInstruction nop_inst = AcuInst_Encode_NONE(OP_NOP);
    bool changed = false;
    u32 ip = 0;

    while (ip < count) {
        u32 num_insts = 0;

        while (ip < count) {
            const u32 cur_inst_ip = ip;
            const AcuInstruction inst = code[cur_inst_ip];
            const AcuOpcode op = AcuInst_GetOp(inst);
            const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
            const u8 slots = info->slots;

            bb_ips[num_insts++] = cur_inst_ip;
            ip += slots;

            if ((info->flags & ACU_FLAG_BRANCH_COND) ||
                AcuOpt_IsInstructionTerminator(code, cur_inst_ip, count)) {
                break;
            }

            if (ip < count && is_target[ip]) {
                break;
            }
        }

        if (num_insts == 0) {
            continue;
        }

        AcuRegBitset live;
        const u32 last_ip = bb_ips[num_insts - 1];
        const AcuInstruction last_inst = code[last_ip];
        const AcuOpcode last_op = AcuInst_GetOp(last_inst);

        bool is_exit = AcuOpt_IsFunctionExit(last_op);
        if (!is_exit && last_op == OP_SYSCALL) {
            AcuSyscallId sys_id = AcuOpt_GetSyscallIdAt(code, last_ip, count);
            const AcuSyscallInfo *sinfo = AcuSyscall_GetInfo(sys_id);
            if (sinfo && sinfo->is_terminator) {
                is_exit = true;
            }
        }

        if (is_exit) {
            AcuRegSet_Zero(&live);
        } else {
            AcuRegSet_Fill(&live);
        }

        for (i32 idx = (i32)num_insts - 1; idx >= 0; --idx) {
            const u32 cur_ip = bb_ips[idx];
            AcuInstruction *inst = &code[cur_ip];
            const AcuOpcode op = AcuInst_GetOp(*inst);

            if (op == OP_NOP) {
                continue;
            }

            const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
            const u8 slots = info->slots;

            if (info->write_mode == OP_WRITE_A) {
                const AcuReg def_reg = AcuInst_GetA(*inst);

                if (!AcuRegSet_Test(&live, def_reg)) {
                    if (AcuOpt_IsInstructionPure(code, cur_ip, count)) {
                        code[cur_ip] = nop_inst;
                        if (slots > 1 && cur_ip + 1 < count) {
                            code[cur_ip + 1] = nop_inst;
                        }
                        changed = true;
                        continue;
                    }

                    if (op == OP_CALL) {
                        AcuInst_SetOp(inst, OP_CALL_VOID);
                        AcuInst_SetA(inst, 0);
                        changed = true;
                    } else if (op == OP_SYSCALL) {
                        AcuInst_SetOp(inst, OP_SYSCALL_VOID);
                        AcuInst_SetA(inst, 0);
                        changed = true;
                    }
                }

                AcuRegSet_Clear(&live, def_reg);
            }

            AcuOpt_AddInstructionUses(*inst, &live);
        }
    }

    return changed;
}

static inline AcuOpcode AcuOpt_FuseCmpJump(AcuOpcode cmp_op, bool jump_on_true) {
    AcuOpcode fused = OP_NOP;

    switch (cmp_op) {
        case OP_EQ:
            fused = OP_JUMP_EQ;
            break;
        case OP_NEQ:
            fused = OP_JUMP_NEQ;
            break;
        case OP_LT_S:
            fused = OP_JUMP_LT_S;
            break;
        case OP_LT_U:
            fused = OP_JUMP_LT_U;
            break;
        case OP_LTE_S:
            fused = OP_JUMP_LTE_S;
            break;
        case OP_LTE_U:
            fused = OP_JUMP_LTE_U;
            break;
        case OP_GT_S:
            fused = OP_JUMP_GT_S;
            break;
        case OP_GT_U:
            fused = OP_JUMP_GT_U;
            break;
        case OP_GTE_S:
            fused = OP_JUMP_GTE_S;
            break;
        case OP_GTE_U:
            fused = OP_JUMP_GTE_U;
            break;

        case OP_EQ_IMM:
            fused = OP_JUMP_EQ_IMM;
            break;
        case OP_NEQ_IMM:
            fused = OP_JUMP_NEQ_IMM;
            break;
        case OP_LT_S_IMM:
            fused = OP_JUMP_LT_S_IMM;
            break;
        case OP_LT_U_IMM:
            fused = OP_JUMP_LT_U_IMM;
            break;
        case OP_LTE_S_IMM:
            fused = OP_JUMP_LTE_S_IMM;
            break;
        case OP_LTE_U_IMM:
            fused = OP_JUMP_LTE_U_IMM;
            break;
        case OP_GT_S_IMM:
            fused = OP_JUMP_GT_S_IMM;
            break;
        case OP_GT_U_IMM:
            fused = OP_JUMP_GT_U_IMM;
            break;
        case OP_GTE_S_IMM:
            fused = OP_JUMP_GTE_S_IMM;
            break;
        case OP_GTE_U_IMM:
            fused = OP_JUMP_GTE_U_IMM;
            break;

        default:
            return OP_NOP;
    }

    return jump_on_true ? fused : AcuOpcode_GetInverted(fused);
}

static bool AcuOpt_PeepholePass(AcuChunk *chunk, const bool *is_target) {
    const u32 count = (u32)V_Count(&chunk->code);
    if (count == 0) {
        return false;
    }

    AcuInstruction *code = &V_At(&chunk->code, 0);
    const AcuInstruction nop_inst = AcuInst_Encode_NONE(OP_NOP);
    bool changed = false;

    for (u32 ip = 0; ip < count;) {
        AcuInstruction *inst = &code[ip];
        const AcuOpcode op = AcuInst_GetOp(*inst);

        if (op == OP_NOP) {
            ip += 1;
            continue;
        }

        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 sz = info->slots;

        // ПРАВИЛО 1: Устранение прыжков на следующую инструкцию
        if (info->flags & (ACU_FLAG_BRANCH_COND | ACU_FLAG_BRANCH_UNCOND)) {
            const u32 target = AcuOpt_GetJumpTarget(ip, *inst);
            if (target == ip + sz) {
                *inst = nop_inst;
                changed = true;
                ip += sz;
                continue;
            }
        }

        // ПРАВИЛО 2: Jump Threading через LOAD_IMM 0 + JUMP_IF_TRUE
        if (op == OP_JUMP_IF_FALSE) {
            const u32 target = AcuOpt_GetJumpTarget(ip, *inst);
            if (target + 1 < count) {
                const AcuInstruction t0 = code[target];
                const AcuInstruction t1 = code[target + 1];

                if (AcuInst_GetOp(t0) == OP_LOAD_IMM && AcuInst_GetsBx(t0) == 0 &&
                    AcuInst_GetOp(t1) == OP_JUMP_IF_TRUE && AcuInst_GetA(t0) == AcuInst_GetA(t1)) {

                    const u32 bypassed_target = target + 2;
                    const i32 new_sbx = (i32)bypassed_target - (i32)ip - 1;
                    if (new_sbx >= -32768 && new_sbx <= 32767) {
                        AcuInst_SetsBx(inst, (i16)new_sbx);
                        changed = true;
                    }
                }
            }
        }

        // ПРАВИЛО 3: Цепочки условных переходов на JUMP
        if (info->flags & ACU_FLAG_BRANCH_COND) {
            const u32 target = AcuOpt_GetJumpTarget(ip, *inst);
            if (target < count) {
                const AcuInstruction t_inst = code[target];
                if (AcuInst_GetOp(t_inst) == OP_JUMP) {
                    const u32 final_target = AcuOpt_GetJumpTarget(target, t_inst);
                    if (final_target <= count && final_target != target) {
                        const i32 diff = (i32)final_target - (i32)ip - 1;

                        if (info->format == OP_FMT_AsBx) {
                            if (diff >= -32768 && diff <= 32767) {
                                AcuInst_SetsBx(inst, (i16)diff);
                                changed = true;
                            }
                        } else if (info->format == OP_FMT_ABsC || info->format == OP_FMT_AsBsC) {
                            if (diff >= -128 && diff <= 127) {
                                AcuInst_SetsC(inst, (i8)diff);
                                changed = true;
                            }
                        }
                    }
                }
            }
        }

        // ПРАВИЛО 4: Полноценный Target Forwarding (Def-to-Move) по базовому блоку
        if (info->write_mode == OP_WRITE_A) {
            if (AcuOpt_TryForwardDefToMove(chunk, ip, is_target)) {
                changed = true;
            }
        }

        // ПРАВИЛО 5: Инверсия ветвлений ("Прыжок через прыжок")
        if ((op == OP_JUMP_IF_TRUE || op == OP_JUMP_IF_FALSE) && (ip + 1 < count) &&
            !is_target[ip + 1]) {
            AcuInstruction *next = &code[ip + 1];
            if (AcuInst_GetOp(*next) == OP_JUMP) {
                const u32 cond_target = AcuOpt_GetJumpTarget(ip, *inst);
                if (cond_target == ip + 2) {
                    const u32 real_target = AcuOpt_GetJumpTarget(ip + 1, *next);
                    if (real_target <= count && real_target != ip + 1) {
                        const i32 diff = (i32)real_target - (i32)ip - 1;
                        if (diff >= -32768 && diff <= 32767) {
                            AcuInst_SetOp(inst, AcuOpcode_GetInverted(op));
                            AcuInst_SetsBx(inst, (i16)diff);
                            *next = nop_inst;
                            changed = true;
                            ip += sz;
                            continue;
                        }
                    }
                }
            }
        }

        // ПРАВИЛО 6: Цепочки безусловных переходов (JUMP -> JUMP / RET)
        if (op == OP_JUMP) {
            const u32 target = AcuOpt_GetJumpTarget(ip, *inst);
            if (target < count && target != ip) {
                const AcuInstruction target_inst = code[target];
                const AcuOpcode target_op = AcuInst_GetOp(target_inst);

                if (target_op == OP_JUMP) {
                    const u32 final_target = AcuOpt_GetJumpTarget(target, target_inst);
                    if (final_target <= count && final_target != target && final_target != ip) {
                        const i32 new_sax = (i32)final_target - (i32)ip - 1;
                        if (new_sax >= -8388608 && new_sax <= 8388607) {
                            AcuInst_SetsAx(inst, new_sax);
                            changed = true;
                        }
                    }
                } else if (target_op == OP_RET || target_op == OP_RET_VOID) {
                    *inst = target_inst;
                    changed = true;
                    ip += sz;
                    continue;
                }
            }
        }

        // ПРАВИЛО 7: Хвостовые вызовы (CALL / CALL_VOID + RET -> TAILCALL)
        if ((op == OP_CALL || op == OP_CALL_VOID) && (ip + 2 < count) && !is_target[ip + 2]) {
            AcuInstruction *ret_inst = &code[ip + 2];
            const AcuOpcode ret_op = AcuInst_GetOp(*ret_inst);

            bool is_tail = false;
            if (op == OP_CALL && ret_op == OP_RET &&
                AcuInst_GetA(*ret_inst) == AcuInst_GetA(*inst)) {
                is_tail = true;
            } else if (op == OP_CALL_VOID && ret_op == OP_RET_VOID) {
                is_tail = true;
            }

            if (is_tail) {
                AcuInst_SetOp(inst, OP_TAILCALL);
                AcuInst_SetA(inst, 0);
                *ret_inst = nop_inst;
                changed = true;
                ip += sz;
                continue;
            }
        }

        // ПРАВИЛО 8: Branch Fusion для сравнений с нулем
        if ((op == OP_EQ_IMM || op == OP_NEQ_IMM) && (ip + 1 < count) && !is_target[ip + 1]) {
            if (AcuInst_GetsC(*inst) == 0) {
                AcuInstruction *next = &code[ip + 1];
                const AcuOpcode next_op = AcuInst_GetOp(*next);

                if (next_op == OP_JUMP_IF_FALSE || next_op == OP_JUMP_IF_TRUE) {
                    const AcuReg cond_reg = AcuInst_GetA(*next);
                    const AcuReg dst = AcuInst_GetA(*inst);

                    if (cond_reg == dst) {
                        const bool is_eq = (op == OP_EQ_IMM);
                        const bool jump_on_true = (next_op == OP_JUMP_IF_TRUE);
                        const AcuOpcode fused_op =
                            (is_eq == jump_on_true) ? OP_JUMP_IF_FALSE : OP_JUMP_IF_TRUE;

                        const u32 target = AcuOpt_GetJumpTarget(ip + 1, *next);
                        const i32 diff = (i32)target - (i32)(ip + 1) - 1;

                        if (diff >= -32768 && diff <= 32767) {
                            *next = AcuInst_Encode_AsBx(fused_op, AcuInst_GetB(*inst), (i16)diff);
                            if (AcuOpt_IsRegDeadAfter(chunk, ip + 2, dst, is_target)) {
                                *inst = nop_inst;
                            }
                            changed = true;
                            ip += sz;
                            continue;
                        }
                    }
                }
            }
        }

        // ПРАВИЛО 9: Распространение копий (Copy Propagation) по базовому блоку
        if (op == OP_MOVE) {
            const AcuReg dst = AcuInst_GetA(*inst);
            const AcuReg src = AcuInst_GetB(*inst);

            if (dst == src) {
                *inst = nop_inst;
                changed = true;
                ip += sz;
                continue;
            }

            bool any_replaced = false;
            u32 scan_ip = ip + sz;
            while (scan_ip < count) {
                if (is_target[scan_ip]) {
                    break;
                }

                AcuInstruction *scan_inst = &code[scan_ip];
                const AcuOpcode scan_op = AcuInst_GetOp(*scan_inst);

                if (scan_op == OP_NOP) {
                    scan_ip += 1;
                    continue;
                }

                const AcuOpcodeInfo *scan_info = AcuOpcode_GetInfo(scan_op);

                // Подставляем src вместо dst (включая одиночные аргументы вызовов)
                if (AcuOpt_SubstituteReadReg(scan_inst, dst, src)) {
                    any_replaced = true;
                    changed = true;
                }

                // Если src перезаписывается — дальнейшая подстановка недопустима
                if (scan_info->write_mode == OP_WRITE_A && AcuInst_GetA(*scan_inst) == src) {
                    break;
                }

                // Если dst перезаписывается — переменная переопределена
                if (scan_info->write_mode == OP_WRITE_A && AcuInst_GetA(*scan_inst) == dst) {
                    break;
                }

                if ((scan_info->flags & (ACU_FLAG_TERMINATOR | ACU_FLAG_BRANCH_COND)) ||
                    AcuOpt_IsInstructionTerminator(code, scan_ip, count)) {
                    break;
                }

                scan_ip += scan_info->slots;
            }

            // Если dst гарантированно мёртв после этого MOVE — удаляем команду
            if (AcuOpt_IsRegDeadAfter(chunk, ip + sz, dst, is_target)) {
                *inst = nop_inst;
                changed = true;
                ip += sz;
                continue;
            }

            if (any_replaced) {
                ip += sz;
                continue;
            }
        }

        // ПРАВИЛО 10: Алгебраические тождества
        if (AcuOpcode_HasFlag(op, ACU_FLAG_SELF_ZERO) && info->format == OP_FMT_ABC) {
            if (AcuInst_GetB(*inst) == AcuInst_GetC(*inst)) {
                *inst = AcuInst_Encode_AsBx(OP_LOAD_IMM, AcuInst_GetA(*inst), 0);
                changed = true;
                ip += sz;
                continue;
            }
        }

        if (AcuOpcode_HasFlag(op, ACU_FLAG_SELF_ONE) && info->format == OP_FMT_ABC) {
            if (AcuInst_GetB(*inst) == AcuInst_GetC(*inst)) {
                *inst = AcuInst_Encode_AsBx(OP_LOAD_IMM, AcuInst_GetA(*inst), 1);
                changed = true;
                ip += sz;
                continue;
            }
        }

        if (info->format == OP_FMT_ABsC && AcuOpcode_HasFlag(op, ACU_FLAG_IS_IMM)) {
            const AcuReg dst = AcuInst_GetA(*inst);
            const AcuReg src = AcuInst_GetB(*inst);
            const i8 imm = AcuInst_GetsC(*inst);

            if ((op == OP_ADD_IMM || op == OP_SUB_IMM || op == OP_OR_IMM || op == OP_XOR_IMM) &&
                imm == 0) {
                *inst = (dst == src) ? nop_inst : AcuInst_Encode_AB(OP_MOVE, dst, src);
                changed = true;
                ip += sz;
                continue;
            }

            if (op == OP_MUL_IMM && imm == 1) {
                *inst = (dst == src) ? nop_inst : AcuInst_Encode_AB(OP_MOVE, dst, src);
                changed = true;
                ip += sz;
                continue;
            }

            if ((op == OP_MUL_IMM || op == OP_AND_IMM) && imm == 0) {
                *inst = AcuInst_Encode_AsBx(OP_LOAD_IMM, dst, 0);
                changed = true;
                ip += sz;
                continue;
            }

            if ((op == OP_SHL_IMM || op == OP_SHR_S_IMM || op == OP_SHR_U_IMM) &&
                ((u8)imm & 63) == 0) {
                *inst = (dst == src) ? nop_inst : AcuInst_Encode_AB(OP_MOVE, dst, src);
                changed = true;
                ip += sz;
                continue;
            }
        }

        // ПРАВИЛО 11: Branch Fusion
        if (sz == 1 && (ip + 1 < count) && !is_target[ip + 1]) {
            AcuInstruction *next = &code[ip + 1];
            const AcuOpcode next_op = AcuInst_GetOp(*next);

            if (next_op == OP_JUMP_IF_TRUE || next_op == OP_JUMP_IF_FALSE) {
                const AcuReg cond_reg = AcuInst_GetA(*next);
                const AcuReg dst = AcuInst_GetA(*inst);

                if (cond_reg == dst && AcuOpt_IsRegDeadAfter(chunk, ip + 2, dst, is_target)) {
                    const bool jump_on_true = (next_op == OP_JUMP_IF_TRUE);
                    const AcuOpcode fused_op = AcuOpt_FuseCmpJump(op, jump_on_true);

                    if (fused_op != OP_NOP) {
                        const u32 target = AcuOpt_GetJumpTarget(ip + 1, *next);
                        const i32 diff = (i32)target - (i32)ip - 1;

                        if (diff >= -128 && diff <= 127) {
                            if (AcuOpcode_HasFlag(fused_op, ACU_FLAG_IS_IMM)) {
                                *inst = AcuInst_Encode_AsBsC(fused_op, AcuInst_GetB(*inst),
                                                             (u8)AcuInst_GetC(*inst), (i8)diff);
                            } else {
                                *inst = AcuInst_Encode_ABsC(fused_op, AcuInst_GetB(*inst),
                                                            AcuInst_GetC(*inst), (i8)diff);
                            }

                            *next = nop_inst;
                            changed = true;
                            ip += sz;
                            continue;
                        }
                    }
                }
            }
        }

        ip += sz;
    }

    return changed;
}

static void AcuOpt_StripNopsAndRelocate(AcuChunk *chunk, u32 *remap) {
    const u32 old_count = (u32)V_Count(&chunk->code);
    if (old_count == 0) {
        return;
    }

    AcuInstruction *code = &V_At(&chunk->code, 0);
    u32 new_count = 0;

    for (u32 old_ip = 0; old_ip < old_count;) {
        const AcuInstruction inst = code[old_ip];
        const AcuOpcode op = AcuInst_GetOp(inst);

        if (op == OP_NOP) {
            remap[old_ip] = new_count;
            old_ip += 1;
        } else {
            const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
            const u8 slots = info->slots;

            for (u8 i = 0; i < slots; ++i) {
                remap[old_ip + i] = new_count + i;
            }
            new_count += slots;
            old_ip += slots;
        }
    }
    remap[old_count] = new_count;

    if (new_count == old_count) {
        return;
    }

    for (u32 old_ip = 0; old_ip < old_count;) {
        AcuInstruction inst = code[old_ip];
        const AcuOpcode op = AcuInst_GetOp(inst);

        if (op == OP_NOP) {
            old_ip += 1;
            continue;
        }

        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 slots = info->slots;
        const u32 new_ip = remap[old_ip];

        if (info->flags & (ACU_FLAG_BRANCH_COND | ACU_FLAG_BRANCH_UNCOND)) {
            const u32 old_target = AcuOpt_GetJumpTarget(old_ip, inst);
            const u32 new_target = (old_target <= old_count) ? remap[old_target] : new_count;
            const i32 new_offset = (i32)new_target - (i32)new_ip - 1;

            switch (info->format) {
                case OP_FMT_sAx:
                    AcuInst_SetsAx(&inst, new_offset);
                    break;
                case OP_FMT_AsBx:
                    assert(new_offset >= -32768 && new_offset <= 32767 &&
                           "Jump offset out of sBx range!");
                    AcuInst_SetsBx(&inst, (i16)new_offset);
                    break;
                case OP_FMT_ABsC:
                case OP_FMT_AsBsC:
                    assert(new_offset >= -128 && new_offset <= 127 &&
                           "Fused jump offset out of sC range!");
                    AcuInst_SetsC(&inst, (i8)new_offset);
                    break;
                default:
                    break;
            }
        }

        AcuInstruction payload = 0;
        if (slots > 1 && (old_ip + 1 < old_count)) {
            payload = code[old_ip + 1];

            if (AcuOpt_IsBytecodeCall(op)) {
                const u32 old_fn_target = (u32)payload;
                const u32 new_fn_target =
                    (old_fn_target <= old_count) ? remap[old_fn_target] : old_fn_target;
                payload = (AcuInstruction)new_fn_target;
            }
        }

        code[new_ip] = inst;
        if (slots > 1) {
            code[new_ip + 1] = payload;
        }

        old_ip += slots;
    }

    V_SetCount(&chunk->code, new_count);
}

void AcuChunk_Optimize(AcuChunk *chunk) {
    const u32 MAX_ITERATIONS = 32;
    u32 iter = 0;
    bool changed = true;

    u32 initial_count = (u32)V_Count(&chunk->code);
    if (initial_count == 0) {
        return;
    }

    bool *is_target = (bool *)malloc(sizeof(bool) * (initial_count + 1));
    u32 *remap = (u32 *)malloc(sizeof(u32) * (initial_count + 1));
    u32 *bb_ips = (u32 *)malloc(sizeof(u32) * (initial_count + 1));

    if (!is_target || !remap || !bb_ips) {
        free(is_target);
        free(remap);
        free(bb_ips);
        return;
    }

    while (changed && iter < MAX_ITERATIONS) {
        changed = false;
        u32 count = (u32)V_Count(&chunk->code);
        if (count == 0) {
            break;
        }

        AcuOpt_CollectJumpTargets(chunk, is_target);
        changed |= AcuOpt_EliminateDeadCode(chunk, is_target);
        changed |= AcuOpt_EliminateDeadStores(chunk, is_target, bb_ips);
        changed |= AcuOpt_PeepholePass(chunk, is_target);

        if (changed) {
            AcuOpt_StripNopsAndRelocate(chunk, remap);
        }
        iter++;
    }

    free(is_target);
    free(remap);
    free(bb_ips);
}
