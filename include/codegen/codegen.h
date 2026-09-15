#pragma once

#include "analyzer/analyzer.h"
#include "defines/bytecode.h"
#include "defines/defines.h"
#include "memory/vector.h"
#include "pool/constant_pool.h"
#include "pool/string_pool.h"

typedef TypedVector(AcuInstruction) AcuInstructionVec;

typedef struct {
    AcuInstructionVec code;
    AcuConstantPool constants;
    AcuStringPool strings;
    u32 globals_count;
} AcuChunk;

AcuChunk AcuChunk_Create(void);
void AcuChunk_Free(AcuChunk *chunk);

bool AcuCodeGen_CompileWorkspace(AcuWorkspace *ws, AcuChunk *out_chunk);
