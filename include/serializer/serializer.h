#pragma once

#include "codegen/codegen.h"
#include "defines/bytecode.h"
#include "defines/version.h"
#include <stdbool.h>
#include <stdio.h>

#define ACU_MAGIC_NUMBER 0x41435500 // "ACU\0"

#define ACU_VERSION_MAKE(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ACU_CURRENT_VERSION                                                                        \
    ACU_VERSION_MAKE(ACU_CURRENT_VERSION_MAJOR, ACU_CURRENT_VERSION_MINOR,                         \
                     ACU_CURRENT_VERSION_PATCH)

typedef struct __attribute__((packed)) {
    u32 magic_number;
    u32 version;
    u32 global_slots;
    u32 constant_pool_size;
    u32 bytecode_size;
    u32 string_pool_size;
} AcuBinaryHeader;

typedef struct {
    u64 *constant_pool;
    AcuInstruction *code;
    u8 *string_pool;
    u32 global_slots;
    u32 constant_pool_size;
    u32 code_size;
    u32 string_pool_size;
} AcuRawChunk;

bool AcuChunk_Serialize(FILE *file, const AcuChunk *chunk);
bool AcuRawChunk_Deserialize(FILE *file, AcuRawChunk *out_chunk);
void AcuRawChunk_Free(AcuRawChunk *chunk);

bool AcuChunk_SerializePath(const char *path, const AcuChunk *chunk);
bool AcuRawChunk_DeserializePath(const char *path, AcuRawChunk *out_chunk);

bool AcuRawChunk_Validate(const AcuRawChunk *chunk);
