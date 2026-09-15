#include "codegen/codegen_internal.h"
#include <assert.h>

AcuLocalBinding *AcuCodeGen_FindLocal(AcuCodeGen *cg, SymbolId sym_id) {
    i64 count = (i64)V_Count(&cg->locals);
    for (i64 i = count - 1; i >= 0; --i) {
        AcuLocalBinding *b = &V_At(&cg->locals, (u32)i);
        if (b->sym_id == sym_id) {
            return b;
        }
    }
    return NULL;
}

bool AcuCodeGen_IsLocalReg(AcuCodeGen *cg, AcuReg r) {
    u32 count = (u32)V_Count(&cg->locals);
    for (u32 i = 0; i < count; ++i) {
        if (V_At(&cg->locals, i).reg == r) {
            return true;
        }
    }
    return false;
}
