#include "module/module.h"
#include "defines/defines.h"

AcuModule AcuModule_Source(ScopeId global_scope, AstNodeIdx ast_root, FileId file_id) {
    return (AcuModule){.global_scope = global_scope,
                       .as.source = {
                           .ast_root = ast_root,
                           .file_id = file_id,
                           .stage = ACU_MODULE_STAGE_UNPROCESSED,
                       }};
}

AcuModule AcuModule_Virtual(ScopeId global_scope) {
    return (AcuModule){
        .global_scope = global_scope,
    };
}
