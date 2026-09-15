#pragma once
#include "defines/defines.h"

typedef enum {
    ACU_MODULE_STAGE_UNPROCESSED = 0,
    ACU_MODULE_STAGE_PARSED,
    ACU_MODULE_STAGE_SYMBOLS_BOUND,
    ACU_MODULE_STAGE_TYPE_CHECKED,
} AcuModuleStage;

typedef struct {
    ScopeId global_scope;

    union {
        struct {
            AstNodeIdx ast_root;
            AcuModuleStage stage;
            FileId file_id;
        } source;
    } as;
} AcuModule;

AcuModule AcuModule_Source(ScopeId global_scope, AstNodeIdx ast_root, FileId file_id);
AcuModule AcuModule_Virtual(ScopeId global_scope);
