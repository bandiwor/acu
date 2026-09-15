#include "vm/vm.h"
#include "args/vm_options.h"
#include "defines/bytecode.h"
#include "serializer/serializer.h"
#include "types/acu_value.h"
#include "types/string_slice.h"
#include "vm/syscall.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ACU_REG_WINDOW 256

void AcuVM_Init(AcuVM *vm, const AcuVmOptions *options) {
    if (!vm) {
        return;
    }

    vm->chunk = NULL;
    vm->options = options;
    vm->globals = NULL;
    vm->globals_count = 0;
    vm->exit_code = 0;
    vm->last_status = ACU_VM_OK;

    vm->stack_capacity = ACU_VM_DEFAULT_STACK_CAPACITY;
    vm->stack_used = 0;
    vm->stack = (AcuValue *)calloc(vm->stack_capacity, sizeof(AcuValue));

    vm->frame_capacity = ACU_VM_DEFAULT_FRAME_CAPACITY;
    vm->frame_count = 0;
    vm->frames = (AcuCallFrame *)calloc(vm->frame_capacity, sizeof(AcuCallFrame));

    if (!vm->stack || !vm->frames) {
        AcuVM_Free(vm);
    }
}

void AcuVM_Free(AcuVM *vm) {
    if (!vm) {
        return;
    }

    if (vm->globals) {
        free(vm->globals);
        vm->globals = NULL;
    }

    if (vm->stack) {
        free(vm->stack);
        vm->stack = NULL;
    }

    if (vm->frames) {
        free(vm->frames);
        vm->frames = NULL;
    }

    vm->chunk = NULL;
    vm->options = NULL;
    vm->globals_count = 0;
    vm->stack_capacity = 0;
    vm->stack_used = 0;
    vm->frame_capacity = 0;
    vm->frame_count = 0;
    vm->exit_code = 0;
    vm->last_status = ACU_VM_OK;
}

bool AcuVM_Load(AcuVM *vm, const AcuRawChunk *chunk) {
    if (!vm || !chunk) {
        return false;
    }

    vm->chunk = chunk;
    vm->exit_code = 0;
    vm->last_status = ACU_VM_OK;
    vm->frame_count = 0;
    vm->stack_used = 0;

    if (vm->globals) {
        free(vm->globals);
        vm->globals = NULL;
        vm->globals_count = 0;
    }

    if (chunk->global_slots > 0) {
        vm->globals_count = chunk->global_slots;
        vm->globals = (AcuValue *)calloc(vm->globals_count, sizeof(AcuValue));
        if (!vm->globals) {
            return false;
        }
    }

    if (vm->stack && vm->stack_capacity > 0) {
        __builtin_memset(vm->stack, 0, vm->stack_capacity * sizeof(AcuValue));
    }

    return true;
}

#define DISPATCH()                                                                                 \
    do {                                                                                           \
        instr = *ip;                                                                               \
        goto *dispatch_table[AcuInst_GetOp(instr)];                                                \
    } while (0)

#define EXEC_JUMP_REG(opcode_label, type, op)                                                      \
    op_##opcode_label : {                                                                          \
        type va = (type)regs[AcuInst_GetA(instr)].u64;                                             \
        type vb = (type)regs[AcuInst_GetB(instr)].u64;                                             \
        i8 offset = AcuInst_GetsC(instr);                                                          \
        ip += (va op vb) ? (offset + 1) : 1;                                                       \
        DISPATCH();                                                                                \
    }

#define EXEC_JUMP_IMM(opcode_label, type, imm_type, op)                                            \
    op_##opcode_label : {                                                                          \
        type va = (type)regs[AcuInst_GetA(instr)].u64;                                             \
        type vb = (type)(imm_type)AcuInst_GetB(instr);                                             \
        i8 offset = AcuInst_GetsC(instr);                                                          \
        ip += (va op vb) ? (offset + 1) : 1;                                                       \
        DISPATCH();                                                                                \
    }

AcuVmStatus AcuVM_Run(AcuVM *vm) {
    if (!vm || !vm->chunk || !vm->chunk->code || vm->chunk->code_size == 0) {
        if (vm) {
            vm->last_status = ACU_VM_ERR_UNHANDLED_OPCODE;
        }
        return ACU_VM_ERR_UNHANDLED_OPCODE;
    }

    __extension__ static const void *const dispatch_table[ACU_OPCODE_COUNT] = {
#define X(name, ...) [name] = &&op_##name,
        X_ACU_OPCODES(X)
#undef X
    };

    const AcuInstruction *restrict ip = vm->chunk->code;
    AcuValue *const stack_base = vm->stack;
    AcuValue *restrict regs = stack_base;
    const AcuValue *restrict const constants = (const AcuValue *)vm->chunk->constant_pool;
    AcuValue *restrict const globals = vm->globals;
    AcuCallFrame *const frames_base = vm->frames;
    AcuCallFrame *restrict frame = frames_base;
    const u8 *restrict const string_pool = vm->chunk->string_pool;
    const AcuValue *const stack_limit = stack_base + vm->stack_capacity - ACU_REG_WINDOW;
    const AcuCallFrame *const frame_limit = frames_base + vm->frame_capacity;

    AcuInstruction instr;

    DISPATCH();

op_OP_NOP: {
    ip++;
    DISPATCH();
}

op_OP_HALT: {
    vm->exit_code = regs[0].u64;
    vm->last_status = ACU_VM_HALT;
    return ACU_VM_OK;
}

op_OP_RET: {
    AcuReg src = AcuInst_GetA(instr);
    AcuValue ret_val = regs[src];

    frame--;
    ip = frame->return_ip;
    regs = stack_base + frame->register_base;

    if (frame->return_reg >= 0) {
        regs[frame->return_reg] = ret_val;
    }

    DISPATCH();
}

op_OP_RET_VOID: {
    frame--;
    ip = frame->return_ip;
    regs = stack_base + frame->register_base;

    DISPATCH();
}

op_OP_JUMP: {
    i32 offset = AcuInst_GetsAx(instr);
    ip += 1 + offset;

    DISPATCH();
}

op_OP_JUMP_IF_TRUE: {
    if (regs[AcuInst_GetA(instr)].u64 != 0) {
        ip += 1 + AcuInst_GetsBx(instr);
    } else {
        ip++;
    }

    DISPATCH();
}

op_OP_JUMP_IF_FALSE: {
    if (regs[AcuInst_GetA(instr)].u64 == 0) {
        ip += 1 + AcuInst_GetsBx(instr);
    } else {
        ip++;
    }

    DISPATCH();
}

    EXEC_JUMP_REG(OP_JUMP_EQ, u64, ==)
    EXEC_JUMP_REG(OP_JUMP_NEQ, u64, !=)
    EXEC_JUMP_REG(OP_JUMP_LT_S, i64, <)
    EXEC_JUMP_REG(OP_JUMP_LT_U, u64, <)
    EXEC_JUMP_REG(OP_JUMP_LTE_S, i64, <=)
    EXEC_JUMP_REG(OP_JUMP_LTE_U, u64, <=)
    EXEC_JUMP_REG(OP_JUMP_GT_S, i64, >)
    EXEC_JUMP_REG(OP_JUMP_GT_U, u64, >)
    EXEC_JUMP_REG(OP_JUMP_GTE_S, i64, >=)
    EXEC_JUMP_REG(OP_JUMP_GTE_U, u64, >=)

    EXEC_JUMP_IMM(OP_JUMP_EQ_IMM, u64, u8, ==)
    EXEC_JUMP_IMM(OP_JUMP_NEQ_IMM, u64, u8, !=)
    EXEC_JUMP_IMM(OP_JUMP_LT_S_IMM, i64, i8, <)
    EXEC_JUMP_IMM(OP_JUMP_LT_U_IMM, u64, u8, <)
    EXEC_JUMP_IMM(OP_JUMP_LTE_S_IMM, i64, i8, <=)
    EXEC_JUMP_IMM(OP_JUMP_LTE_U_IMM, u64, u8, <=)
    EXEC_JUMP_IMM(OP_JUMP_GT_S_IMM, i64, i8, >)
    EXEC_JUMP_IMM(OP_JUMP_GT_U_IMM, u64, u8, >)
    EXEC_JUMP_IMM(OP_JUMP_GTE_S_IMM, i64, i8, >=)
    EXEC_JUMP_IMM(OP_JUMP_GTE_U_IMM, u64, u8, >=)

op_OP_MOVE: {
    regs[AcuInst_GetA(instr)] = regs[AcuInst_GetB(instr)];

    ip++;
    DISPATCH();
}

op_OP_LOAD_IMM: {
    regs[AcuInst_GetA(instr)] = AcuValue_I64((i64)AcuInst_GetsBx(instr));

    ip++;
    DISPATCH();
}

op_OP_LOAD_CONST: {
    regs[AcuInst_GetA(instr)] = constants[AcuInst_GetBx(instr)];

    ip++;
    DISPATCH();
}

op_OP_LOAD_STR: {
    u16 const_id = AcuInst_GetBx(instr);
    StringSlice slice = constants[const_id].string_slice;
    regs[AcuInst_GetA(instr)].u64 = (uintptr_t)(string_pool + slice.slice.offset);

    ip++;
    DISPATCH();
}

op_OP_LOAD_GLOBAL: {
    regs[AcuInst_GetA(instr)] = globals[AcuInst_GetBx(instr)];

    ip++;
    DISPATCH();
}

op_OP_STORE_GLOBAL: {
    globals[AcuInst_GetBx(instr)] = regs[AcuInst_GetA(instr)];

    ip++;
    DISPATCH();
}

op_OP_CALL: {
    AcuReg dst = AcuInst_GetA(instr);
    AcuReg base = AcuInst_GetB(instr);
    u8 argc = AcuInst_GetC(instr);
    u32 target_ip = (u32)ip[1];

    AcuValue *next_regs = regs + base;

    if (unlikely(next_regs > stack_limit)) {
        vm->last_status = ACU_VM_ERR_STACK_OVERFLOW;
        return ACU_VM_ERR_STACK_OVERFLOW;
    }
    if (unlikely(frame >= frame_limit)) {
        vm->last_status = ACU_VM_ERR_CALL_STACK_OVERFLOW;
        return ACU_VM_ERR_CALL_STACK_OVERFLOW;
    }

    frame->return_ip = ip + 2;
    frame->register_base = (u32)(regs - stack_base);
    frame->return_reg = dst;
    frame->argc = argc;
    frame++;

    regs = next_regs;
    ip = vm->chunk->code + target_ip;

    DISPATCH();
}

op_OP_CALL_VOID: {
    AcuReg base = AcuInst_GetB(instr);
    u8 argc = AcuInst_GetC(instr);
    u32 target_ip = (u32)ip[1];

    AcuValue *next_regs = regs + base;

    if (unlikely(next_regs > stack_limit)) {
        vm->last_status = ACU_VM_ERR_STACK_OVERFLOW;
        return ACU_VM_ERR_STACK_OVERFLOW;
    }
    if (unlikely(frame >= frame_limit)) {
        vm->last_status = ACU_VM_ERR_CALL_STACK_OVERFLOW;
        return ACU_VM_ERR_CALL_STACK_OVERFLOW;
    }

    frame->return_ip = ip + 2;
    frame->register_base = (u32)(regs - stack_base);
    frame->return_reg = ACU_FRAME_NO_RET;
    frame->argc = argc;
    frame->flags = 0;
    frame++;

    regs = next_regs;
    ip = vm->chunk->code + target_ip;
    DISPATCH();
}

op_OP_TAILCALL: {
    AcuReg base = AcuInst_GetB(instr);
    u8 argc = AcuInst_GetC(instr);
    u32 target_ip = (u32)ip[1];

    if (unlikely(base + argc > ACU_REG_WINDOW)) {
        vm->last_status = ACU_VM_ERR_STACK_OVERFLOW;
        return ACU_VM_ERR_STACK_OVERFLOW;
    }

    if (base != 0) {
        if (argc == 1) {
            regs[0] = regs[base];
        } else if (argc == 2) {
            AcuValue a0 = regs[base];
            AcuValue a1 = regs[base + 1];
            regs[0] = a0;
            regs[1] = a1;
        } else if (argc > 2) {
            for (u32 i = 0; i < argc; ++i) {
                regs[i] = regs[base + i];
            }
        }
    }

    ip = vm->chunk->code + target_ip;
    DISPATCH();
}

op_OP_SYSCALL: {
    AcuReg dst = AcuInst_GetA(instr);
    AcuReg base = AcuInst_GetB(instr);
    u8 argc = AcuInst_GetC(instr);
    u32 sys_id = (u32)ip[1];

    if (unlikely(base + argc > ACU_REG_WINDOW)) {
        vm->last_status = ACU_VM_ERR_STACK_OVERFLOW;
        return ACU_VM_ERR_STACK_OVERFLOW;
    }

    AcuValue ret = {0};
    AcuVmStatus status = g_acu_syscall_table[sys_id].handler(regs + base, argc, &ret);
    if (unlikely(status != ACU_VM_OK)) {
        vm->last_status = status;
        return status;
    }

    regs[dst] = ret;
    ip += 2;
    DISPATCH();
}

op_OP_ADD: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 + regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_SUB: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 - regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_MUL: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 * regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_DIV_S: {
    i64 b = regs[AcuInst_GetB(instr)].i64;
    i64 c = regs[AcuInst_GetC(instr)].i64;

    if (unlikely((u64)(c + 1) <= 1)) {
        if (c == 0) {
            vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
            return ACU_VM_ERR_DIVISION_BY_ZERO;
        }
        regs[AcuInst_GetA(instr)].i64 = (b == INT64_MIN) ? INT64_MIN : -b;
    } else {
        regs[AcuInst_GetA(instr)].i64 = b / c;
    }

    ip++;
    DISPATCH();
}

op_OP_DIV_U: {
    u64 c = regs[AcuInst_GetC(instr)].u64;
    if (unlikely(c == 0)) {
        vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
        return ACU_VM_ERR_DIVISION_BY_ZERO;
    }

    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 / c;

    ip++;
    DISPATCH();
}

op_OP_REM_S: {
    i64 b = regs[AcuInst_GetB(instr)].i64;
    i64 c = regs[AcuInst_GetC(instr)].i64;

    if (unlikely((u64)(c + 1) <= 1)) {
        if (unlikely(c == 0)) {
            vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
            return ACU_VM_ERR_DIVISION_BY_ZERO;
        }
        regs[AcuInst_GetA(instr)].i64 = 0;
    } else {
        regs[AcuInst_GetA(instr)].i64 = b % c;
    }

    ip++;
    DISPATCH();
}

op_OP_REM_U: {
    u64 c = regs[AcuInst_GetC(instr)].u64;
    if (unlikely(c == 0)) {
        vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
        return ACU_VM_ERR_DIVISION_BY_ZERO;
    }

    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 % c;

    ip++;
    DISPATCH();
}

op_OP_POW: {
    regs[AcuInst_GetA(instr)] =
        AcuValue_PowU64(regs[AcuInst_GetB(instr)], regs[AcuInst_GetC(instr)]);

    ip++;
    DISPATCH();
}

op_OP_AND: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 & regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_OR: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 | regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_XOR: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 ^ regs[AcuInst_GetC(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_SHL: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64
                                    << (regs[AcuInst_GetC(instr)].u64 & 63);

    ip++;
    DISPATCH();
}

op_OP_SHR_S: {
    regs[AcuInst_GetA(instr)].i64 =
        regs[AcuInst_GetB(instr)].i64 >> (regs[AcuInst_GetC(instr)].u64 & 63);

    ip++;
    DISPATCH();
}

op_OP_SHR_U: {
    regs[AcuInst_GetA(instr)].u64 =
        regs[AcuInst_GetB(instr)].u64 >> (regs[AcuInst_GetC(instr)].u64 & 63);

    ip++;
    DISPATCH();
}

op_OP_ADD_IMM: {
    regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 + (i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_SUB_IMM: {
    regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 - (i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_RSUB_IMM: {
    regs[AcuInst_GetA(instr)].i64 = (i64)AcuInst_GetsC(instr) - regs[AcuInst_GetB(instr)].i64;

    ip++;
    DISPATCH();
}

op_OP_MUL_IMM: {
    regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 * (i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_DIV_S_IMM: {
    i8 imm = AcuInst_GetsC(instr);

    if (unlikely((u8)(imm + 1) <= 1)) {
        if (imm == 0) {
            vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
            return ACU_VM_ERR_DIVISION_BY_ZERO;
        }
        i64 b = regs[AcuInst_GetB(instr)].i64;
        regs[AcuInst_GetA(instr)].i64 = (b == INT64_MIN) ? INT64_MIN : -b;
    } else {
        regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 / imm;
    }

    ip++;
    DISPATCH();
}

op_OP_DIV_U_IMM: {
    u8 imm = AcuInst_GetC(instr);
    if (unlikely(imm == 0)) {
        vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
        return ACU_VM_ERR_DIVISION_BY_ZERO;
    }

    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 / imm;

    ip++;
    DISPATCH();
}

op_OP_REM_S_IMM: {
    i8 imm = AcuInst_GetsC(instr);

    if (unlikely((u8)(imm + 1) <= 1)) {
        if (imm == 0) {
            vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
            return ACU_VM_ERR_DIVISION_BY_ZERO;
        }
        regs[AcuInst_GetA(instr)].i64 = 0;
    } else {
        regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 % imm;
    }

    ip++;
    DISPATCH();
}

op_OP_REM_U_IMM: {
    u8 imm = AcuInst_GetC(instr);
    if (unlikely(imm == 0)) {
        vm->last_status = ACU_VM_ERR_DIVISION_BY_ZERO;
        return ACU_VM_ERR_DIVISION_BY_ZERO;
    }

    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 % imm;

    ip++;
    DISPATCH();
}

op_OP_POW_IMM: {
    regs[AcuInst_GetA(instr)] =
        AcuValue_PowU64(regs[AcuInst_GetB(instr)], AcuValue_U64(AcuInst_GetC(instr)));

    ip++;
    DISPATCH();
}

op_OP_AND_IMM: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 & (u64)(i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_OR_IMM: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 | (u64)(i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_XOR_IMM: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 ^ (u64)(i64)AcuInst_GetsC(instr);

    ip++;
    DISPATCH();
}

op_OP_SHL_IMM: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 << (AcuInst_GetC(instr) & 63);

    ip++;
    DISPATCH();
}

op_OP_SHR_S_IMM: {
    regs[AcuInst_GetA(instr)].i64 = regs[AcuInst_GetB(instr)].i64 >> (AcuInst_GetC(instr) & 63);

    ip++;
    DISPATCH();
}

op_OP_SHR_U_IMM: {
    regs[AcuInst_GetA(instr)].u64 = regs[AcuInst_GetB(instr)].u64 >> (AcuInst_GetC(instr) & 63);

    ip++;
    DISPATCH();
}

op_OP_NEG: {
    regs[AcuInst_GetA(instr)].u64 = -regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_NOT: {
    regs[AcuInst_GetA(instr)].u64 = ~regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_LNOT: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 == 0);

    ip++;
    DISPATCH();
}

op_OP_EQ: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].u64 == regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_NEQ: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].u64 != regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_LT_S: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 < regs[AcuInst_GetC(instr)].i64);

    ip++;
    DISPATCH();
}

op_OP_LT_U: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 < regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_LTE_S: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].i64 <= regs[AcuInst_GetC(instr)].i64);

    ip++;
    DISPATCH();
}

op_OP_LTE_U: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].u64 <= regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_GT_S: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 > regs[AcuInst_GetC(instr)].i64);

    ip++;
    DISPATCH();
}

op_OP_GT_U: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 > regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_GTE_S: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].i64 >= regs[AcuInst_GetC(instr)].i64);

    ip++;
    DISPATCH();
}

op_OP_GTE_U: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].u64 >= regs[AcuInst_GetC(instr)].u64);

    ip++;
    DISPATCH();
}

op_OP_EQ_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 == (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_NEQ_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 != (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_LT_S_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 < (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_LT_U_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 < (u64)AcuInst_GetC(instr));

    ip++;
    DISPATCH();
}

op_OP_LTE_S_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 <= (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_LTE_U_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 <= (u64)AcuInst_GetC(instr));

    ip++;
    DISPATCH();
}

op_OP_GT_S_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 > (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_GT_U_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 > (u64)AcuInst_GetC(instr));

    ip++;
    DISPATCH();
}

op_OP_GTE_S_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].i64 >= (i64)AcuInst_GetsC(instr));

    ip++;
    DISPATCH();
}

op_OP_GTE_U_IMM: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].u64 >= (u64)AcuInst_GetC(instr));

    ip++;
    DISPATCH();
}

op_OP_FADD: {
    regs[AcuInst_GetA(instr)].f64 = regs[AcuInst_GetB(instr)].f64 + regs[AcuInst_GetC(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_FSUB: {
    regs[AcuInst_GetA(instr)].f64 = regs[AcuInst_GetB(instr)].f64 - regs[AcuInst_GetC(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_FMUL: {
    regs[AcuInst_GetA(instr)].f64 = regs[AcuInst_GetB(instr)].f64 * regs[AcuInst_GetC(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_FDIV: {
    regs[AcuInst_GetA(instr)].f64 = regs[AcuInst_GetB(instr)].f64 / regs[AcuInst_GetC(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_FREM: {
    regs[AcuInst_GetA(instr)].f64 =
        __builtin_fmod(regs[AcuInst_GetB(instr)].f64, regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FPOW: {
    regs[AcuInst_GetA(instr)].f64 =
        __builtin_pow(regs[AcuInst_GetB(instr)].f64, regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FNEG: {
    regs[AcuInst_GetA(instr)].f64 = -regs[AcuInst_GetB(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_FEQ: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].f64 == regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FNEQ: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].f64 != regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FLT: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].f64 < regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FLTE: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].f64 <= regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FGT: {
    regs[AcuInst_GetA(instr)].u64 = (regs[AcuInst_GetB(instr)].f64 > regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_FGTE: {
    regs[AcuInst_GetA(instr)].u64 =
        (regs[AcuInst_GetB(instr)].f64 >= regs[AcuInst_GetC(instr)].f64);

    ip++;
    DISPATCH();
}

op_OP_SEXT8: {
    regs[AcuInst_GetA(instr)].i64 = (i8)regs[AcuInst_GetB(instr)].i64;

    ip++;
    DISPATCH();
}

op_OP_SEXT16: {
    regs[AcuInst_GetA(instr)].i64 = (i16)regs[AcuInst_GetB(instr)].i64;

    ip++;
    DISPATCH();
}

op_OP_SEXT32: {
    regs[AcuInst_GetA(instr)].i64 = (i32)regs[AcuInst_GetB(instr)].i64;

    ip++;
    DISPATCH();
}

op_OP_ZEXT8: {
    regs[AcuInst_GetA(instr)].u64 = (u8)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_ZEXT16: {
    regs[AcuInst_GetA(instr)].u64 = (u16)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_ZEXT32: {
    regs[AcuInst_GetA(instr)].u64 = (u32)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_TRUNC8: {
    regs[AcuInst_GetA(instr)].u64 = (u8)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_TRUNC16: {
    regs[AcuInst_GetA(instr)].u64 = (u16)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_TRUNC32: {
    regs[AcuInst_GetA(instr)].u64 = (u32)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_I64_TO_F64: {
    regs[AcuInst_GetA(instr)].f64 = (f64)regs[AcuInst_GetB(instr)].i64;

    ip++;
    DISPATCH();
}

op_OP_U64_TO_F64: {
    regs[AcuInst_GetA(instr)].f64 = (f64)regs[AcuInst_GetB(instr)].u64;

    ip++;
    DISPATCH();
}

op_OP_F64_TO_I64: {
    regs[AcuInst_GetA(instr)].i64 = (i64)regs[AcuInst_GetB(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_F64_TO_U64: {
    regs[AcuInst_GetA(instr)].u64 = (u64)regs[AcuInst_GetB(instr)].f64;

    ip++;
    DISPATCH();
}

op_OP_F32_TO_F64: {
    regs[AcuInst_GetA(instr)].f64 = (f64)regs[AcuInst_GetB(instr)].f32;

    ip++;
    DISPATCH();
}

op_OP_F64_TO_F32: {
    regs[AcuInst_GetA(instr)] = AcuValue_F32((f32)regs[AcuInst_GetB(instr)].f64);

    ip++;
    DISPATCH();
}
}
