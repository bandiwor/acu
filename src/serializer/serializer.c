#include "serializer/serializer.h"
#include "codegen/codegen.h"
#include "defines/bytecode.h"
#include "memory/vector.h"
#include "pool/constant_pool.h"
#include "pool/string_pool.h"
#include "vm/syscall.h"
#include <stdlib.h>

bool AcuChunk_Serialize(FILE *file, const AcuChunk *chunk) {
    if (unlikely(!file || !chunk)) {
        return false;
    }

    u32 code_count = (u32)V_Count(&chunk->code);
    u32 constant_count = (u32)(AcuConstantPool_RawDataLength(&chunk->constants) / sizeof(u64));
    u32 string_pool_bytes = AcuStringPool_RawDataLength(&chunk->strings);

    const void *constants_data = AcuConstantPool_RawData(&chunk->constants);
    const AcuInstruction *bytecode_data = V_Elements(&chunk->code);
    const void *strings_data = AcuStringPool_RawData(&chunk->strings);

    AcuBinaryHeader header;
    __builtin_memset(&header, 0, sizeof(AcuBinaryHeader));

    header.magic_number = ACU_MAGIC_NUMBER;
    header.version = ACU_CURRENT_VERSION;
    header.global_slots = chunk->globals_count;
    header.constant_pool_size = constant_count;
    header.bytecode_size = code_count;
    header.string_pool_size = string_pool_bytes;

    if (fwrite(&header, sizeof(AcuBinaryHeader), 1, file) != 1) {
        return false;
    }

    if (constant_count > 0) {
        if (fwrite(constants_data, sizeof(u64), constant_count, file) != constant_count) {
            return false;
        }
    }

    if (code_count > 0) {
        if (fwrite(bytecode_data, sizeof(AcuInstruction), code_count, file) != code_count) {
            return false;
        }
    }

    if (string_pool_bytes > 0) {
        if (fwrite(strings_data, 1, string_pool_bytes, file) != string_pool_bytes) {
            return false;
        }
    }

    return true;
}

bool AcuRawChunk_Deserialize(FILE *file, AcuRawChunk *out_chunk) {
    if (unlikely(!file || !out_chunk)) {
        return false;
    }

    __builtin_memset(out_chunk, 0, sizeof(AcuRawChunk));

    AcuBinaryHeader header;
    if (fread(&header, sizeof(AcuBinaryHeader), 1, file) != 1) {
        return false;
    }

    if (header.magic_number != ACU_MAGIC_NUMBER || header.version != ACU_CURRENT_VERSION) {
        return false;
    }

    if (header.constant_pool_size > UINT32_MAX / sizeof(u64)) {
        return false;
    }
    if (header.bytecode_size > UINT32_MAX / sizeof(AcuInstruction)) {
        return false;
    }

    out_chunk->global_slots = header.global_slots;
    out_chunk->constant_pool_size = header.constant_pool_size;
    out_chunk->code_size = header.bytecode_size;
    out_chunk->string_pool_size = header.string_pool_size;

    if (out_chunk->constant_pool_size > 0) {
        size_t bytes = (size_t)out_chunk->constant_pool_size * sizeof(u64);
        out_chunk->constant_pool = (u64 *)malloc(bytes);
        if (!out_chunk->constant_pool) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
        if (fread(out_chunk->constant_pool, sizeof(u64), out_chunk->constant_pool_size, file) !=
            out_chunk->constant_pool_size) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
    }

    if (out_chunk->code_size > 0) {
        size_t bytes = (size_t)out_chunk->code_size * sizeof(AcuInstruction);
        out_chunk->code = (AcuInstruction *)malloc(bytes);
        if (!out_chunk->code) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
        if (fread(out_chunk->code, sizeof(AcuInstruction), out_chunk->code_size, file) !=
            out_chunk->code_size) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
    }

    if (out_chunk->string_pool_size > 0) {
        out_chunk->string_pool = (u8 *)malloc(out_chunk->string_pool_size);
        if (!out_chunk->string_pool) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
        if (fread(out_chunk->string_pool, 1, out_chunk->string_pool_size, file) !=
            out_chunk->string_pool_size) {
            AcuRawChunk_Free(out_chunk);
            return false;
        }
    }

    if (!AcuRawChunk_Validate(out_chunk)) {
        AcuRawChunk_Free(out_chunk);
        return false;
    }

    return true;
}

void AcuRawChunk_Free(AcuRawChunk *chunk) {
    if (!chunk) {
        return;
    }

    if (chunk->constant_pool) {
        free((void *)chunk->constant_pool);
    }
    if (chunk->code) {
        free(chunk->code);
    }
    if (chunk->string_pool) {
        free((void *)chunk->string_pool);
    }

    __builtin_memset(chunk, 0, sizeof(AcuRawChunk));
}

bool AcuRawChunk_Validate(const AcuRawChunk *chunk) {
    if (unlikely(!chunk)) {
        return false;
    }
    if (chunk->code_size == 0) {
        return true;
    }

    bool *is_boundary = (bool *)calloc(chunk->code_size + 1, sizeof(bool));
    if (!is_boundary) {
        return false;
    }

    // -------------------------------------------------------------------------
    // Проход 1: Проверка валидности опкодов и разметка границ инструкций
    // -------------------------------------------------------------------------
    u32 ip = 0;
    while (ip < chunk->code_size) {
        const AcuInstruction inst = chunk->code[ip];
        const AcuOpcode op = AcuInst_GetOp(inst);

        if (op >= ACU_OPCODE_COUNT) {
            free(is_boundary);
            return false;
        }

        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 slots = info->slots;

        if (ip + slots > chunk->code_size) {
            free(is_boundary);
            return false;
        }

        is_boundary[ip] = true;
        ip += slots;
    }

    if (ip != chunk->code_size) {
        free(is_boundary);
        return false;
    }
    is_boundary[chunk->code_size] = true; // Валидная цель для прыжка в конец кода

    // -------------------------------------------------------------------------
    // Проход 2: Проверка операндов, ссылок на пулы и целей переходов
    // -------------------------------------------------------------------------
    for (ip = 0; ip < chunk->code_size;) {
        const AcuInstruction inst = chunk->code[ip];
        const AcuOpcode op = AcuInst_GetOp(inst);
        const AcuOpcodeInfo *info = AcuOpcode_GetInfo(op);
        const u8 slots = info->slots;

        // 1. Проверка любых прыжков (безусловных, условных, сплавленных)
        if (info->flags & (ACU_FLAG_BRANCH_COND | ACU_FLAG_BRANCH_UNCOND)) {
            u32 target = AcuOpt_GetJumpTarget(ip, inst);
            if (target > chunk->code_size || !is_boundary[target]) {
                free(is_boundary);
                return false;
            }
        }

        // 2. Проверка обращений к пулам и специфичных опкодов
        switch (op) {
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL: {
                u16 slot = AcuInst_GetBx(inst);
                if (slot >= chunk->global_slots) {
                    free(is_boundary);
                    return false;
                }
                break;
            }

            case OP_LOAD_CONST: {
                u16 const_id = AcuInst_GetBx(inst);
                if (const_id >= chunk->constant_pool_size) {
                    free(is_boundary);
                    return false;
                }
                break;
            }

            case OP_LOAD_STR: {
                u16 const_id = AcuInst_GetBx(inst);
                if (const_id >= chunk->constant_pool_size) {
                    free(is_boundary);
                    return false;
                }

                u64 slice_raw = chunk->constant_pool[const_id];
                u32 str_offset = (u32)(slice_raw & 0xFFFFFFFFULL);
                u32 str_len = (u32)(slice_raw >> 32);

                if ((u64)str_offset + (u64)str_len > (u64)chunk->string_pool_size) {
                    free(is_boundary);
                    return false;
                }
                break;
            }

            case OP_CALL:
            case OP_CALL_VOID:
            case OP_TAILCALL: {
                u32 target_ip = (u32)chunk->code[ip + 1];

                if (target_ip >= chunk->code_size || !is_boundary[target_ip]) {
                    free(is_boundary);
                    return false;
                }

                const AcuReg base_reg = AcuInst_GetB(inst);
                const u8 argc = AcuInst_GetC(inst);
                if ((u32)base_reg + argc > 256) {
                    free(is_boundary);
                    return false;
                }
                break;
            }

            case OP_SYSCALL: {
                const AcuSyscallId sys_id = (AcuSyscallId)chunk->code[ip + 1];
                if (sys_id >= ACU_SYSCALL_COUNT) {
                    free(is_boundary);
                    return false;
                }

                const AcuReg base_reg = AcuInst_GetB(inst);
                const u8 argc = AcuInst_GetC(inst);
                if ((u32)base_reg + argc > 256) {
                    free(is_boundary);
                    return false;
                }
                break;
            }

            default:
                break;
        }

        ip += slots;
    }

    free(is_boundary);
    return true;
}

bool AcuChunk_SerializePath(const char *path, const AcuChunk *chunk) {
    if (unlikely(!path || !chunk)) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }

    bool ok = AcuChunk_Serialize(file, chunk);

    if (fclose(file) != 0) {
        return false;
    }

    return ok;
}

bool AcuRawChunk_DeserializePath(const char *path, AcuRawChunk *out_chunk) {
    if (unlikely(!path || !out_chunk)) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    bool ok = AcuRawChunk_Deserialize(file, out_chunk);

    fclose(file);
    return ok;
}
