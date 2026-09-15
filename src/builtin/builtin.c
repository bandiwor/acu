#include "builtin/builtin.h"
#include "defines/types.h"
#include "vm/syscall.h"

#define REG_SYS_FN(id, ret, ...)                                                                   \
    do {                                                                                           \
        const TypeId _params[] = {__VA_ARGS__};                                                    \
        AcuBuiltin_RegisterSysFn(interner, types, symbols, scope, (id), (ret), _params,            \
                                 (u32)(sizeof(_params) / sizeof(_params[0])));                     \
    } while (0)

#define REG_SYS_FN0(id, ret)                                                                       \
    AcuBuiltin_RegisterSysFn(interner, types, symbols, scope, (id), (ret), NULL, 0)

static inline StringId intern_cstr(AcuStringInterner *interner, const char *str) {
    return AcuStringInterner_Intern(interner, StringView_CStr(str));
}

static inline void AcuBuiltin_RegisterSysFn(AcuStringInterner *interner, AcuTypeInterner *types,
                                            AcuSymbolTable *symbols, ScopeId scope,
                                            AcuSyscallId syscall_id, TypeId ret_type,
                                            const TypeId *params, u32 param_count) {
    TypeId fn_type = AcuTypeInterner_GetFunc(types, ret_type, params, param_count);
    AcuSymbolTable_AddSystemFn(symbols, scope,
                               intern_cstr(interner, g_acu_syscall_table[syscall_id].name), fn_type,
                               syscall_id, true);
}

ModuleId AcuBuiltin_RegisterIO(AcuWorkspace *ws) {
    AcuModuleManager *mm = ws->modules;
    AcuStringInterner *interner = ws->interner;
    AcuTypeInterner *types = ws->types;
    AcuSymbolTable *symbols = ws->symbols;

    ModuleId mod_id = AcuModuleManager_CreateVirtual(mm, StringView_CStr("io"));
    ScopeId scope = AcuModuleManager_GetModuleScope(mm, mod_id);

    StringId name_id = intern_cstr(interner, "io");
    AcuSymbolTable_AddModule(symbols, 0, name_id, 0, mod_id, true);

    REG_SYS_FN(ACU_SYSCALL_IO_PRINT_I64, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_I64);
    REG_SYS_FN(ACU_SYSCALL_IO_PRINT_U64, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_U64);
    REG_SYS_FN(ACU_SYSCALL_IO_PRINT_F64, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_F64);
    REG_SYS_FN0(ACU_SYSCALL_IO_PRINTLN, TYPE_PRIMITIVE_UNIT);

    REG_SYS_FN0(ACU_SYSCALL_IO_READ_I64, TYPE_PRIMITIVE_I64);
    REG_SYS_FN0(ACU_SYSCALL_IO_READ_U64, TYPE_PRIMITIVE_U64);
    REG_SYS_FN0(ACU_SYSCALL_IO_READ_F64, TYPE_PRIMITIVE_F64);

    return mod_id;
}

ModuleId AcuBuiltin_RegisterSys(AcuWorkspace *ws) {
    AcuModuleManager *mm = ws->modules;
    AcuStringInterner *interner = ws->interner;
    AcuTypeInterner *types = ws->types;
    AcuSymbolTable *symbols = ws->symbols;

    ModuleId mod_id = AcuModuleManager_CreateVirtual(mm, StringView_CStr("sys"));
    ScopeId scope = AcuModuleManager_GetModuleScope(mm, mod_id);

    StringId name_id = intern_cstr(interner, "sys");
    AcuSymbolTable_AddModule(symbols, 0, name_id, 0, mod_id, true);

    REG_SYS_FN0(ACU_SYSCALL_SYS_CLOCK_NS, TYPE_PRIMITIVE_U64);
    REG_SYS_FN0(ACU_SYSCALL_SYS_CLOCK_MS, TYPE_PRIMITIVE_U64);
    REG_SYS_FN0(ACU_SYSCALL_SYS_RDTSC, TYPE_PRIMITIVE_U64);

    REG_SYS_FN0(ACU_SYSCALL_SYS_TIME_SEC, TYPE_PRIMITIVE_U64);
    REG_SYS_FN0(ACU_SYSCALL_SYS_TIME_MS, TYPE_PRIMITIVE_U64);

    REG_SYS_FN(ACU_SYSCALL_SYS_SLEEP_MS, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_U64);
    REG_SYS_FN(ACU_SYSCALL_SYS_SLEEP_NS, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_U64);
    REG_SYS_FN(ACU_SYSCALL_SYS_EXIT, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_I64);
    REG_SYS_FN(ACU_SYSCALL_SYS_PANIC, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_I64);

    REG_SYS_FN0(ACU_SYSCALL_SYS_RANDOM_U64, TYPE_PRIMITIVE_U64);
    REG_SYS_FN(ACU_SYSCALL_SYS_RANDOM_SEED, TYPE_PRIMITIVE_UNIT, TYPE_PRIMITIVE_U64);

    REG_SYS_FN0(ACU_SYSCALL_SYS_PID, TYPE_PRIMITIVE_I64);
    REG_SYS_FN0(ACU_SYSCALL_SYS_CPU_COUNT, TYPE_PRIMITIVE_U64);

    return mod_id;
}

ModuleId AcuBuiltin_RegisterMath(AcuWorkspace *ws) {
    AcuModuleManager *mm = ws->modules;
    AcuStringInterner *interner = ws->interner;
    AcuTypeInterner *types = ws->types;
    AcuSymbolTable *symbols = ws->symbols;

    ModuleId mod_id = AcuModuleManager_CreateVirtual(mm, StringView_CStr("math"));
    ScopeId scope = AcuModuleManager_GetModuleScope(mm, mod_id);

    StringId name_id = intern_cstr(interner, "math");
    AcuSymbolTable_AddModule(symbols, 0, name_id, 0, mod_id, true);

    /* --------------------------------------------------------------------- */
    /* MATH F64                                                              */
    /* --------------------------------------------------------------------- */
    REG_SYS_FN(ACU_SYSCALL_MATH_SQRT_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_CBRT_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_POW_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64,
               TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_SIN_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_COS_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_TAN_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ASIN_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ACOS_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ATAN_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ATAN2_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64,
               TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_EXP_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG2_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG10_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_FLOOR_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_CEIL_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ROUND_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_ABS_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64);
    REG_SYS_FN(ACU_SYSCALL_MATH_HYPOT_F64, TYPE_PRIMITIVE_F64, TYPE_PRIMITIVE_F64,
               TYPE_PRIMITIVE_F64);

    /* --------------------------------------------------------------------- */
    /* MATH F32                                                              */
    /* --------------------------------------------------------------------- */
    REG_SYS_FN(ACU_SYSCALL_MATH_SQRT_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_CBRT_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_POW_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32,
               TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_SIN_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_COS_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_TAN_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ASIN_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ACOS_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ATAN_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ATAN2_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32,
               TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_EXP_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG2_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_LOG10_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_FLOOR_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_CEIL_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ROUND_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_ABS_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32);
    REG_SYS_FN(ACU_SYSCALL_MATH_HYPOT_F32, TYPE_PRIMITIVE_F32, TYPE_PRIMITIVE_F32,
               TYPE_PRIMITIVE_F32);

    return mod_id;
}

void AcuBuiltin_RegisterAll(AcuWorkspace *ws) {
    AcuBuiltin_RegisterIO(ws);
    AcuBuiltin_RegisterMath(ws);
    AcuBuiltin_RegisterSys(ws);
}
