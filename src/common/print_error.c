#include "common/print_error.h"
#include "common/error.h"
#include "defines/defines.h"
#include "defines/types.h"
#include "interner/type_interner.h"
#include <stdio.h>
#include <string.h>

static void print_type(const AcuTypeInterner *types, const TypeId type_id) {
    if (type_id <= TYPE_PRIMITIVE_MODULE) {
        fprintf(stderr, "%s", (const char *)TypePrimitiveKind_String((TypePrimitiveKind)type_id));
        return;
    }

    const AcuType *type = AcuTypeInterner_GetType(types, type_id);
    switch ((AcuTypeKind)type->kind) {
        case ACU_TYPE_ARRAY:
            fprintf(stderr, "[");
            print_type(types, type->as.array.inner);
            fprintf(stderr, "; %d]", type->as.array.size);
            break;
        case ACU_TYPE_VECTOR:
            fprintf(stderr, "<");
            print_type(types, type->as.vector.inner);
            fprintf(stderr, ">");
            break;
        case ACU_TYPE_TUPLE: {
            fprintf(stderr, "(");
            ExtraIdx current_extra_idx = type->as.tuple.fields_start;
            const ExtraIdx end_extra_idx = current_extra_idx + type->as.tuple.count;
            while (current_extra_idx < end_extra_idx) {
                print_type(types, AcuTypeInterner_GetTypeIdByExtra(types, current_extra_idx));
                if (current_extra_idx + 1 < end_extra_idx) {
                    fprintf(stderr, ", ");
                }

                ++current_extra_idx;
            }
            fprintf(stderr, ")");
            break;
        }
        case ACU_TYPE_FUNC: {
            fprintf(stderr, "(");
            ExtraIdx current_extra_idx = type->as.func.params_start;
            const ExtraIdx end_extra_idx = current_extra_idx + type->as.func.count;
            while (current_extra_idx < end_extra_idx) {
                print_type(types, AcuTypeInterner_GetTypeIdByExtra(types, current_extra_idx));
                if (current_extra_idx + 1 < end_extra_idx) {
                    fprintf(stderr, ", ");
                }

                ++current_extra_idx;
            }

            fprintf(stderr, ") -> ");
            print_type(types, type->as.func.return_type);
            break;
        }
        default:
            break;
    }
}

void AcuError_Print(const AcuError *err, const AcuFileManager *fm, const AcuTypeInterner *types) {
    StringView file_path = {0};
    StringView source = {0};

    if (err->file != ACU_NULL_IDX && fm != NULL) {
        file_path = AcuFileManager_GetPath(fm, err->file);
        source = AcuFileManager_GetSource(fm, err->file);
    }

    if (source.data == NULL || source.length == 0) {
        fprintf(stderr, "\033[1;31merror\033[0m[\033[1m%s\033[0m]: %s\n", AcuResult_Tag(err->code),
                AcuResult_Message(err->code));

        if (file_path.data != NULL && file_path.length > 0) {
            fprintf(stderr, "  \033[1;34m-->\033[0m %.*s\n", SV_Arg(file_path));
        } else if (err->file != ACU_NULL_IDX) {
            fprintf(stderr, "  \033[1;34m-->\033[0m <file_id:%d>\n", err->file);
        }

        fprintf(stderr, "\n");
        return;
    }

    if (unlikely(err->offset >= source.length)) {
        fprintf(stderr, "\033[1;31merror\033[0m[\033[1m%s\033[0m]: %s (at end of file)\n",
                AcuResult_Tag(err->code), AcuResult_Message(err->code));
        return;
    }

    u32 line_num = 1;
    u32 col_num = 1;
    u64 line_start_offset = 0;

    for (u64 i = 0; i < err->offset; i++) {
        if (source.data[i] == '\n') {
            line_num++;
            line_start_offset = i + 1;
        }
    }

    col_num = (u32)(err->offset - line_start_offset) + 1;

    u64 line_end_offset = line_start_offset;
    while (line_end_offset < source.length && source.data[line_end_offset] != '\n' &&
           source.data[line_end_offset] != '\r') {
        line_end_offset++;
    }

    u64 line_len = line_end_offset - line_start_offset;

    u64 underline_len = err->length;
    if (err->offset + underline_len > line_end_offset) {
        underline_len = line_end_offset - err->offset;
    }
    if (underline_len == 0)
        underline_len = 1;

    // Заголовок ошибки в едином стиле
    fprintf(stderr, "\033[1;31merror\033[0m[\033[1;37m%s\033[0m]: \033[1m%s\033[0m\n",
            AcuResult_Tag(err->code), AcuResult_Message(err->code));

    if (file_path.data != NULL && file_path.length > 0) {
        fprintf(stderr, "  \033[1;34m-->\033[0m %.*s:%d:%d\n", SV_Arg(file_path), line_num,
                col_num);
    } else {
        fprintf(stderr, "  \033[1;34m-->\033[0m <file_id:%d>:%d:%d\n", err->file, line_num,
                col_num);
    }

    fprintf(stderr, "   \033[1;34m|\033[0m\n");

    fprintf(stderr, "\033[1;34m%4d |\033[0m ", line_num);
    if (line_len > 0) {
        fwrite(source.data + line_start_offset, 1, line_len, stderr);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "     \033[1;34m|\033[0m ");
    for (u32 i = 1; i < col_num; i++) {
        if (source.data[line_start_offset + i - 1] == '\t') {
            fprintf(stderr, "\t");
        } else {
            fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\033[1;31m^");
    for (u64 i = 1; i < underline_len; i++) {
        fprintf(stderr, "~");
    }
    fprintf(stderr, "\033[0m\n");

    switch (err->code) {
        case ACU_ERR_ANALYZER_REDEFINITION_OF_SYMBOL: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Top-level symbols cannot be shadowed. "
                            "Shadowing is only allowed inside blocks & functions.\n");
            break;
        }

        case ACU_ERR_ANALYZER_ASSIGN_TO_IMMUTABLE: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Variables are immutable by default; "
                            "declare with 'mut' to allow assignment.\n");
            break;
        }

        case ACU_ERR_ANALYZER_CYCLIC_DEPENDENCY: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Global variable initializers cannot "
                            "cyclically depend on one another.\n");
            break;
        }

        case ACU_ERR_ANALYZER_INVALID_LVALUE: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Left-hand side of an assignment must be "
                            "an identifier variable.\n");
            break;
        }

        case ACU_ERR_LEXER_UNEXPECTED_CHAR: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            u8 c = err->as.unexpected.bad_char;
            if (c >= 32 && c <= 126) {
                fprintf(stderr, "     \033[1;36mnote\033[0m: Character literal: '%c'\n", c);
            } else {
                fprintf(stderr,
                        "     \033[1;36mnote\033[0m: Non-printable character hex code: 0x%02X\n",
                        c);
            }
            break;
        }

        case ACU_ERR_LEXER_INVALID_ESCAPE_SEQUENCE: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr,
                    "     \033[1;36mnote\033[0m: '\\%c' is not a valid escape sequence. Supported: "
                    "\\n, \\t, \\r, \\0, \\\\, \\\", \\'\n",
                    err->as.escape.bad_escape);
            break;
        }

        case ACU_ERR_PARSER_UNEXPECTED_TOKEN:
        case ACU_ERR_PARSER_MISSING_SEMICOLON:
        case ACU_ERR_PARSER_EXPECTED_TYPE:
        case ACU_ERR_PARSER_EXPECTED_IDENTIFIER:
        case ACU_ERR_PARSER_EXPECTED_EXPRESSION: {
            if (err->as.mismatch.expected != 0 || err->as.mismatch.actual != 0) {
                fprintf(stderr, "     \033[1;34m|\033[0m\n");
                fprintf(stderr,
                        "     \033[1;36mnote\033[0m: Syntax mismatch: expected %s, but "
                        "found %s\n",
                        AcuTokenType_String(err->as.mismatch.expected),
                        AcuTokenType_String(err->as.mismatch.actual));
            }
            break;
        }

        case ACU_ERR_ANALYZER_TYPE_MISMATCH:
        case ACU_ERR_ANALYZER_CONDITION_NOT_BOOL:
        case ACU_ERR_ANALYZER_BRANCH_TYPE_MISMATCH:
        case ACU_ERR_ANALYZER_IF_WITHOUT_ELSE_NOT_UNIT:
        case ACU_ERR_ANALYZER_EXPECTED_UNIT:
        case ACU_ERR_ANALYZER_BREAK_TYPE_MISMATCH:
        case ACU_ERR_ANALYZER_RETURN_MISSING_VALUE: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Expected ");
            print_type(types, err->as.type_mismatch.expected);
            fprintf(stderr, ", but found ");
            print_type(types, err->as.type_mismatch.actual);
            fprintf(stderr, ".\n");
            break;
        }

        case ACU_ERR_ANALYZER_WRONG_ARGUMENT_COUNT: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Expected %u argument(s), but got %u.\n",
                    err->as.arg_count.expected, err->as.arg_count.actual);
            break;
        }

        case ACU_ERR_ANALYZER_INVALID_TYPE_CAST: {
            fprintf(stderr, "     \033[1;34m|\033[0m\n");
            fprintf(stderr, "     \033[1;36mnote\033[0m: Cannot explicitly cast from ");
            print_type(types, err->as.type_cast.actual);
            fprintf(stderr, " to ");
            print_type(types, err->as.type_cast.target);
            fprintf(stderr, ".\n");
            break;
        }

        default:
            break;
    }

    fprintf(stderr, "\n");
}

void AcuError_PrintAll(const AcuError *const first, u64 count, const AcuFileManager *fm,
                       const AcuTypeInterner *types) {
    const AcuError *current = first;
    const AcuError *const limit = first + count;

    while (current < limit) {
        AcuError_Print(current, fm, types);
        ++current;
    }
}
