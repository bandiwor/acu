#include "disasm/disasm.h"
#include "defines/bytecode.h"
#include "pool/constant_pool.h"
#include "pool/string_pool.h"
#include "vm/syscall.h"
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISASM_TARGET_JUMP (1U << 0)
#define DISASM_TARGET_CALL (1U << 1)

static void PrintEscapedString(FILE *out, StringView sv) {
    if (!sv.data) {
        fputs("\"\"", out);
        return;
    }
    fputc('"', out);
    for (u64 i = 0; i < sv.length; ++i) {
        u8 ch = sv.data[i];
        switch (ch) {
            case '\n':
                fputs("\\n", out);
                break;
            case '\r':
                fputs("\\r", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            case '\"':
                fputs("\\\"", out);
                break;
            case '\\':
                fputs("\\\\", out);
                break;
            default:
                if (ch >= 32 && ch <= 126)
                    fputc((char)ch, out);
                else
                    fprintf(out, "\\x%02X", ch);
                break;
        }
    }
    fputc('"', out);
}

static void AcuDisasm_FormatConstant(char *buf, size_t size, u64 raw) {
    double fval;
    memcpy(&fval, &raw, sizeof(fval));
    u32 exp = (u32)((raw >> 52) & 0x7FF);

    bool is_likely_float = (exp >= 0x380 && exp <= 0x47F) && !isnan(fval) && !isinf(fval);
    if (is_likely_float) {
        char fbuf[40];
        snprintf(fbuf, sizeof(fbuf), "%.14g", fval);
        if (strchr(fbuf, '.') == NULL && strchr(fbuf, 'e') == NULL && strchr(fbuf, 'E') == NULL) {
            snprintf(buf, size, "%s.0", fbuf);
        } else {
            snprintf(buf, size, "%s", fbuf);
        }
        return;
    }

    if ((i64)raw < 0 && (i64)raw >= -1000000) {
        snprintf(buf, size, "%" PRId64, (i64)raw);
    } else if (raw <= 1000000) {
        snprintf(buf, size, "%" PRIu64, raw);
    } else {
        snprintf(buf, size, "%" PRId64 " (0x%" PRIX64 ")", (i64)raw, raw);
    }
}

static const char *AcuDisasm_GetBinaryOpSymbol(AcuOpcode op) {
    switch (op) {
        case OP_ADD:
        case OP_ADD_IMM:
        case OP_FADD:
            return "+";
        case OP_SUB:
        case OP_SUB_IMM:
        case OP_FSUB:
            return "-";
        case OP_MUL:
        case OP_MUL_IMM:
        case OP_FMUL:
            return "*";
        case OP_DIV_S:
        case OP_DIV_U:
        case OP_FDIV:
            return "/";
        case OP_REM_S:
        case OP_REM_U:
        case OP_FREM:
            return "%";
        case OP_POW:
        case OP_POW_IMM:
        case OP_FPOW:
            return "**";
        case OP_AND:
        case OP_AND_IMM:
            return "&";
        case OP_OR:
        case OP_OR_IMM:
            return "|";
        case OP_XOR:
        case OP_XOR_IMM:
            return "^";
        case OP_SHL:
        case OP_SHL_IMM:
            return "<<";
        case OP_SHR_S:
        case OP_SHR_S_IMM:
            return ">>";
        case OP_SHR_U:
        case OP_SHR_U_IMM:
            return ">>>";
        case OP_EQ:
        case OP_EQ_IMM:
        case OP_FEQ:
            return "==";
        case OP_NEQ:
        case OP_NEQ_IMM:
        case OP_FNEQ:
            return "!=";
        case OP_LT_S:
        case OP_LT_U:
        case OP_LT_S_IMM:
        case OP_LT_U_IMM:
        case OP_FLT:
            return "<";
        case OP_LTE_S:
        case OP_LTE_U:
        case OP_LTE_S_IMM:
        case OP_LTE_U_IMM:
        case OP_FLTE:
            return "<=";
        case OP_GT_S:
        case OP_GT_U:
        case OP_GT_S_IMM:
        case OP_GT_U_IMM:
        case OP_FGT:
            return ">";
        case OP_GTE_S:
        case OP_GTE_U:
        case OP_GTE_S_IMM:
        case OP_GTE_U_IMM:
        case OP_FGTE:
            return ">=";
        default:
            return NULL;
    }
}

static inline const char *AcuDisasm_GetJumpConditionSymbol(AcuOpcode op) {
    switch (op) {
        case OP_JUMP_EQ:
        case OP_JUMP_EQ_IMM:
            return "==";
        case OP_JUMP_NEQ:
        case OP_JUMP_NEQ_IMM:
            return "!=";
        case OP_JUMP_LT_S:
        case OP_JUMP_LT_S_IMM:
        case OP_JUMP_LT_U:
        case OP_JUMP_LT_U_IMM:
            return "<";
        case OP_JUMP_LTE_S:
        case OP_JUMP_LTE_S_IMM:
        case OP_JUMP_LTE_U:
        case OP_JUMP_LTE_U_IMM:
            return "<=";
        case OP_JUMP_GT_S:
        case OP_JUMP_GT_S_IMM:
        case OP_JUMP_GT_U:
        case OP_JUMP_GT_U_IMM:
            return ">";
        case OP_JUMP_GTE_S:
        case OP_JUMP_GTE_S_IMM:
        case OP_JUMP_GTE_U:
        case OP_JUMP_GTE_U_IMM:
            return ">=";
        default:
            return "?";
    }
}

static inline bool AcuDisasm_IsUnsignedJump(AcuOpcode op) {
    switch (op) {
        case OP_JUMP_LT_U:
        case OP_JUMP_LT_U_IMM:
        case OP_JUMP_LTE_U:
        case OP_JUMP_LTE_U_IMM:
        case OP_JUMP_GT_U:
        case OP_JUMP_GT_U_IMM:
        case OP_JUMP_GTE_U:
        case OP_JUMP_GTE_U_IMM:
            return true;
        default:
            return false;
    }
}

u32 AcuDisasm_PrintInstruction(FILE *out, const AcuChunk *chunk, u32 ip) {
    if (!chunk || ip >= V_Count(&chunk->code))
        return 0;
    if (!out)
        out = stdout;

    AcuInstruction inst = V_At(&chunk->code, ip);
    AcuOpcode op = (AcuOpcode)(inst & 0xFF);

    if (op >= ACU_OPCODE_COUNT) {
        fprintf(out, "%04u  OP_UNKNOWN(%u)\n", ip, op);
        return 1;
    }

    const AcuOpcodeInfo *info = &g_acu_opcode_info[op];

    AcuReg a = (AcuReg)((inst >> 8) & 0xFF);
    AcuReg b = (AcuReg)((inst >> 16) & 0xFF);
    i8 sb = (i8)((inst >> 16) & 0xFF);
    AcuReg c = (AcuReg)((inst >> 24) & 0xFF);
    i8 sc = (i8)(inst >> 24);
    u16 bx = (u16)(inst >> 16);
    i16 sbx = (i16)(inst >> 16);
    i32 sax = (i32)inst >> 8;

    char operands[48] = {0};
    char comment[96] = {0};
    u32 words_consumed = 1;

    switch (info->format) {
        case OP_FMT_NONE:
            if (op == OP_HALT)
                snprintf(comment, sizeof(comment), "stop execution");
            else if (op == OP_RET_VOID)
                snprintf(comment, sizeof(comment), "return void");
            else if (op == OP_NOP)
                snprintf(comment, sizeof(comment), "no-op");
            break;

        case OP_FMT_A:
            snprintf(operands, sizeof(operands), "R%u", a);
            if (op == OP_RET)
                snprintf(comment, sizeof(comment), "return R%u", a);
            break;

        case OP_FMT_AB:
            snprintf(operands, sizeof(operands), "R%u, R%u", a, b);
            if (op == OP_MOVE)
                snprintf(comment, sizeof(comment), "R%u = R%u", a, b);
            else if (op == OP_NEG)
                snprintf(comment, sizeof(comment), "R%u = -R%u", a, b);
            else if (op == OP_NOT)
                snprintf(comment, sizeof(comment), "R%u = ~R%u", a, b);
            else if (op == OP_LNOT)
                snprintf(comment, sizeof(comment), "R%u = !R%u", a, b);
            else if (op == OP_FNEG)
                snprintf(comment, sizeof(comment), "R%u = -R%u (f64)", a, b);
            else if (op == OP_SEXT8 || op == OP_SEXT16 || op == OP_SEXT32)
                snprintf(comment, sizeof(comment), "R%u = sext(R%u)", a, b);
            else if (op == OP_ZEXT8 || op == OP_ZEXT16 || op == OP_ZEXT32)
                snprintf(comment, sizeof(comment), "R%u = zext(R%u)", a, b);
            else if (op == OP_TRUNC8 || op == OP_TRUNC16 || op == OP_TRUNC32)
                snprintf(comment, sizeof(comment), "R%u = trunc(R%u)", a, b);
            else if (op == OP_I64_TO_F64 || op == OP_U64_TO_F64)
                snprintf(comment, sizeof(comment), "R%u = (f64)R%u", a, b);
            else if (op == OP_F64_TO_I64 || op == OP_F64_TO_U64)
                snprintf(comment, sizeof(comment), "R%u = (int)R%u", a, b);
            break;

        case OP_FMT_ABC: {
            snprintf(operands, sizeof(operands), "R%u, R%u, R%u", a, b, c);
            const char *sym = AcuDisasm_GetBinaryOpSymbol(op);
            if (sym) {
                snprintf(comment, sizeof(comment), "R%u = R%u %s R%u", a, b, sym, c);
            }
            break;
        }

        case OP_FMT_ABC_WIDE: {
            words_consumed = (ip + 1 < V_Count(&chunk->code)) ? 2 : 1;
            u32 payload = (words_consumed == 2) ? (u32)V_At(&chunk->code, ip + 1) : 0;

            if (op == OP_SYSCALL) {
                char target[128];
                if (payload < ACU_SYSCALL_COUNT && g_acu_syscall_table[payload].name) {
                    snprintf(target, sizeof(target), "%s.%s", g_acu_syscall_table[payload].module,
                             g_acu_syscall_table[payload].name);
                } else {
                    snprintf(target, sizeof(target), "unknown_0x%X", payload);
                }

                if (c > 1) {
                    snprintf(operands, sizeof(operands), "R%u, R%u..R%u, %s", a, b, b + c - 1,
                             target);
                } else if (c == 1) {
                    snprintf(operands, sizeof(operands), "R%u, R%u, %s", a, b, target);
                } else {
                    snprintf(operands, sizeof(operands), "R%u, %s", a, target);
                }

                snprintf(comment, sizeof(comment), "R%u = %s(argc=%u)", a, target, c);
            } else if (op == OP_CALL || op == OP_CALL_VOID || op == OP_TAILCALL) {
                if (c > 1) {
                    snprintf(operands, sizeof(operands), "R%u, R%u..R%u, fn_%04u", a, b, b + c - 1,
                             payload);
                } else if (c == 1) {
                    snprintf(operands, sizeof(operands), "R%u, R%u, fn_%04u", a, b, payload);
                } else {
                    snprintf(operands, sizeof(operands), "R%u, fn_%04u", a, payload);
                }

                if (op == OP_CALL_VOID) {
                    snprintf(comment, sizeof(comment), "call fn_%04u(argc=%u)", payload, c);
                } else if (op == OP_TAILCALL) {
                    snprintf(comment, sizeof(comment), "tailcall fn_%04u(argc=%u)", payload, c);
                } else {
                    snprintf(comment, sizeof(comment), "R%u = fn_%04u(argc=%u)", a, payload, c);
                }
            }
            break;
        }

        case OP_FMT_ABsC: {
            if (info->side_effect) {
                u32 target = (u32)((i32)ip + 1 + sc);
                snprintf(operands, sizeof(operands), "R%u, R%u, .L%04u", a, b, target);
                const char *sym = AcuDisasm_GetJumpConditionSymbol(op);
                snprintf(comment, sizeof(comment), "if R%u %s R%u jump -> .L%04u", a, sym, b,
                         target);
            } else {
                snprintf(operands, sizeof(operands), "R%u, R%u, %d", a, b, sc);
                if (op == OP_RSUB_IMM) {
                    snprintf(comment, sizeof(comment), "R%u = %d - R%u", a, sc, b);
                } else {
                    const char *sym = AcuDisasm_GetBinaryOpSymbol(op);
                    if (sym) {
                        snprintf(comment, sizeof(comment), "R%u = R%u %s %d", a, b, sym, sc);
                    }
                }
            }
            break;
        }

        case OP_FMT_AsBsC: {
            u32 target = (u32)((i32)ip + 1 + sc);
            const char *sym = AcuDisasm_GetJumpConditionSymbol(op);
            bool is_u = AcuDisasm_IsUnsignedJump(op);

            if (is_u) {
                snprintf(operands, sizeof(operands), "R%u, %u, .L%04u", a, (u8)b, target);
                snprintf(comment, sizeof(comment), "if R%u %s %u jump -> .L%04u", a, sym, (u8)b,
                         target);
            } else {
                snprintf(operands, sizeof(operands), "R%u, %d, .L%04u", a, sb, target);
                snprintf(comment, sizeof(comment), "if R%u %s %d jump -> .L%04u", a, sym, sb,
                         target);
            }
            break;
        }

        case OP_FMT_ABx:
            if (op == OP_LOAD_CONST) {
                snprintf(operands, sizeof(operands), "R%u, K#%u", a, bx);
                if (bx < V_Count(&chunk->constants.constants)) {
                    u64 raw = AcuConstantPool_Get(&chunk->constants, bx);
                    char cval[48];
                    AcuDisasm_FormatConstant(cval, sizeof(cval), raw);
                    snprintf(comment, sizeof(comment), "R%u = %s", a, cval);
                }
            } else if (op == OP_LOAD_STR) {
                snprintf(operands, sizeof(operands), "R%u, K#%u", a, bx);
            } else if (op == OP_LOAD_GLOBAL) {
                snprintf(operands, sizeof(operands), "R%u, G#%u", a, bx);
                snprintf(comment, sizeof(comment), "R%u = globals[%u]", a, bx);
            } else if (op == OP_STORE_GLOBAL) {
                snprintf(operands, sizeof(operands), "R%u, G#%u", a, bx);
                snprintf(comment, sizeof(comment), "globals[%u] = R%u", bx, a);
            }
            break;

        case OP_FMT_AsBx:
            if (op == OP_LOAD_IMM) {
                snprintf(operands, sizeof(operands), "R%u, %d", a, sbx);
                snprintf(comment, sizeof(comment), "R%u = %d", a, sbx);
            } else if (op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) {
                u32 target = (u32)((i32)ip + 1 + sbx);
                snprintf(operands, sizeof(operands), "R%u, .L%04u", a, target);
                const char *cond_name = (op == OP_JUMP_IF_TRUE) ? "true" : "false";
                snprintf(comment, sizeof(comment), "jump-%s R%u -> .L%04u", cond_name, a, target);
            }
            break;

        case OP_FMT_sAx:
            if (op == OP_JUMP) {
                u32 target = (u32)((i32)ip + 1 + sax);
                snprintf(operands, sizeof(operands), ".L%04u", target);
                snprintf(comment, sizeof(comment), "jump -> .L%04u", target);
            }
            break;
    }

    fprintf(out, "%04u  %-16s %-22s", ip, info->name, operands);

    if (op == OP_LOAD_STR && bx < V_Count(&chunk->constants.constants)) {
        u64 raw = AcuConstantPool_Get(&chunk->constants, bx);
        StringSlice slice = {.raw = raw};
        StringView sv = AcuStringPool_Get(&chunk->strings, slice);
        fprintf(out, " # R%u = ", a);
        PrintEscapedString(out, sv);
        fputc('\n', out);
    } else if (comment[0] != '\0') {
        fprintf(out, " # %s\n", comment);
    } else {
        fputc('\n', out);
    }

    return words_consumed;
}

void AcuDisasm_DumpChunk(FILE *out, const AcuChunk *chunk) {
    if (!out)
        out = stdout;
    if (!chunk) {
        fprintf(out, "; [ERROR: Chunk is NULL]\n");
        return;
    }

    u32 total_words = (u32)V_Count(&chunk->code);
    u32 const_count = (u32)V_Count(&chunk->constants.constants);
    u32 str_count = (u32)V_Count(&chunk->strings.entries);

    fprintf(out, "; DISASSEMBLY (code: %u, globals: %u, consts: %u, strings: %u)\n", total_words,
            chunk->globals_count, const_count, str_count);

    if (const_count > 0) {
        for (u32 i = 0; i < const_count; ++i) {
            u64 raw = AcuConstantPool_Get(&chunk->constants, i);
            char cval[48];
            AcuDisasm_FormatConstant(cval, sizeof(cval), raw);
            fprintf(out, ";   K#%-2u = %-12s (raw: 0x%016" PRIX64 ")\n", i, cval, raw);
        }
    }

    if (str_count > 0) {
        for (u32 i = 0; i < str_count; ++i) {
            StringView sv = AcuStringPool_GetById(&chunk->strings, i);
            fprintf(out, ";   S#%-2u = ", i);
            PrintEscapedString(out, sv);
            fputc('\n', out);
        }
    }

    if (const_count > 0 || str_count > 0) {
        fprintf(out, "; ----------------------------------------------------\n");
    }

    if (total_words == 0)
        return;

    uint8_t *targets = (uint8_t *)calloc(total_words + 1, sizeof(uint8_t));
    if (targets) {
        u32 scan_ip = 0;
        while (scan_ip < total_words) {
            AcuInstruction inst = V_At(&chunk->code, scan_ip);
            AcuOpcode op = (AcuOpcode)(inst & 0xFF);
            if (op >= ACU_OPCODE_COUNT) {
                scan_ip++;
                continue;
            }

            const AcuOpcodeInfo *info = &g_acu_opcode_info[op];

            if (info->format == OP_FMT_AsBx) {
                i16 sbx = (i16)(inst >> 16);
                if (op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) {
                    u32 target = (u32)((i32)scan_ip + 1 + sbx);
                    if (target < total_words)
                        targets[target] |= DISASM_TARGET_JUMP;
                }
            } else if (info->format == OP_FMT_sAx) {
                i32 sax = (i32)inst >> 8;
                if (op == OP_JUMP) {
                    u32 target = (u32)((i32)scan_ip + 1 + sax);
                    if (target < total_words)
                        targets[target] |= DISASM_TARGET_JUMP;
                }
            } else if (info->format == OP_FMT_ABC_WIDE && scan_ip + 1 < total_words) {
                if (op != OP_SYSCALL) {
                    u32 target = (u32)V_At(&chunk->code, scan_ip + 1);
                    if (target < total_words)
                        targets[target] |= DISASM_TARGET_CALL;
                }
                scan_ip += 2;
                continue;
            }
            scan_ip++;
        }
    }

    u32 ip = 0;
    while (ip < total_words) {
        if (targets) {
            if (targets[ip] & DISASM_TARGET_CALL) {
                fprintf(out, "\nfn_%04u:\n", ip);
            } else if (targets[ip] & DISASM_TARGET_JUMP) {
                fprintf(out, "\n.L%04u:\n", ip);
            }
        }

        AcuInstruction inst = V_At(&chunk->code, ip);
        AcuOpcode op = (AcuOpcode)(inst & 0xFF);

        u32 step = AcuDisasm_PrintInstruction(out, chunk, ip);
        if (step == 0)
            break;

        if (op == OP_HALT) {
            fputc('\n', out);
        }

        ip += step;
    }

    free(targets);
}
