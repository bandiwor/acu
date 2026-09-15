#include "defines/bytecode.h"

const AcuOpcodeInfo g_acu_opcode_info[ACU_OPCODE_COUNT] = {
#define X(_name, str, slots_count, fmt, rmode, wmode, effect, purity, opcode_flags)                \
    [_name] = {                                                                                    \
        .name = (str),                                                                             \
        .format = (fmt),                                                                           \
        .read_mode = (rmode),                                                                      \
        .write_mode = (wmode),                                                                     \
        .side_effect = (effect),                                                                   \
        .default_purity = (purity),                                                                \
        .flags = (opcode_flags),                                                                   \
        .slots = (slots_count),                                                                    \
    },
    X_ACU_OPCODES(X)
#undef X
};
