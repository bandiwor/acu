#pragma once
#include "args/vm_options.h"
#include "defines/bytecode.h"
#include "serializer/serializer.h"
#include "types/acu_value.h"
#include "types/acu_vm_status.h"

#define ACU_FRAME_NO_RET ((i16) - 1)

typedef struct {
    const AcuInstruction *return_ip;
    u32 register_base;
    i16 return_reg;
    u8 argc;
    u8 flags;
} AcuCallFrame;

#define ACU_VM_DEFAULT_STACK_CAPACITY 65536
#define ACU_VM_DEFAULT_FRAME_CAPACITY 1024

typedef struct {
    const AcuRawChunk *chunk;
    const AcuVmOptions *options;
    AcuValue *globals;
    AcuValue *stack;
    AcuCallFrame *frames;
    u64 exit_code;

    u32 globals_count;
    u32 stack_capacity;
    u32 stack_used;
    u32 frame_capacity;
    u32 frame_count;
    AcuVmStatus last_status;
} AcuVM;

void AcuVM_Init(AcuVM *vm, const AcuVmOptions *options);
void AcuVM_Free(AcuVM *vm);
bool AcuVM_Load(AcuVM *vm, const AcuRawChunk *chunk);
AcuVmStatus AcuVM_Run(AcuVM *vm);
