#include "module/module_manager.h"
#include "defines/defines.h"
#include "memory/vector.h"
#include <stdio.h>
#include <stdlib.h>

AcuModuleManager AcuModuleManager_Create(AcuSymbolTable *symbols) {
    AcuModuleManager mm = {0};
    AcuModuleManager_Init(&mm, symbols);
    return mm;
}

void AcuModuleManager_Init(AcuModuleManager *mm, AcuSymbolTable *symbols) {
    mm->symbols = symbols;

    V_Init(&mm->modules);
    V_Init(&mm->file_to_module);
    V_Init(&mm->virtual_modules);
}

void AcuModuleManager_Free(AcuModuleManager *mm) {
    mm->symbols = NULL;

    V_Free(&mm->modules);
    V_Free(&mm->file_to_module);
    V_Free(&mm->virtual_modules);
}

ModuleId AcuModuleManager_CreateVirtual(AcuModuleManager *mm, StringView import_name) {
    for (u32 i = 0; i < V_Count(&mm->virtual_modules); i++) {
        if (StringView_Equals(V_Elements(&mm->virtual_modules)[i].name, import_name)) {
            return V_Elements(&mm->virtual_modules)[i].module_id;
        }
    }

    ModuleId new_id = V_Count(&mm->modules);
    ScopeId root_scope = AcuSymbolTable_PushScope(mm->symbols, ACU_NULL_IDX, new_id);

    AcuModule mod = AcuModule_Virtual(root_scope);
    V_Push(&mm->modules, mod);

    AcuVirtualModule v_mod = {.name = import_name, .module_id = new_id};
    V_Push(&mm->virtual_modules, v_mod);

    return new_id;
}

ModuleId AcuModuleManager_FindVirtual(AcuModuleManager *mm, StringView import_name) {
    if (import_name.length == 0 || import_name.data[0] == '.' || import_name.data[0] == '/') {
        return ACU_NULL_IDX;
    }

    u32 count = V_Count(&mm->virtual_modules);
    AcuVirtualModule *vmods = V_Elements(&mm->virtual_modules);

    for (u32 i = 0; i < count; i++) {
        if (StringView_Equals(vmods[i].name, import_name)) {
            return vmods[i].module_id;
        }
    }

    return ACU_NULL_IDX;
}

ModuleId AcuModuleManager_GetOrCreate(AcuModuleManager *mm, StringView import_name, FileId file_id,
                                      AstNodeIdx root_idx) {
    for (u32 i = 0; i < V_Count(&mm->virtual_modules); i++) {
        if (StringView_Equals(V_Elements(&mm->virtual_modules)[i].name, import_name)) {
            return V_Elements(&mm->virtual_modules)[i].module_id;
        }
    }

    if (unlikely(file_id == ACU_NULL_IDX))
        return ACU_NULL_IDX;

    while (V_Count(&mm->file_to_module) <= file_id) {
        V_Push(&mm->file_to_module, ACU_NULL_IDX);
    }

    ModuleId existing_mod = V_Elements(&mm->file_to_module)[file_id];
    if (existing_mod != ACU_NULL_IDX) {
        return existing_mod;
    }

    ModuleId new_id = V_Count(&mm->modules);
    ScopeId root_scope = AcuSymbolTable_PushScope(mm->symbols, ACU_NULL_IDX, new_id);

    AcuModule mod = AcuModule_Source(root_scope, root_idx, file_id);
    V_Push(&mm->modules, mod);

    V_Elements(&mm->file_to_module)[file_id] = new_id;
    return new_id;
}

u32 AcuModuleManager_GetCount(AcuModuleManager *mm) {
    return V_Count(&mm->modules);
}

AstNodeIdx AcuModuleManager_GetSourceModuleNode(AcuModuleManager *mm, u32 m) {
    return V_At(&mm->modules, m).as.source.ast_root;
}

ScopeId AcuModuleManager_GetModuleScope(AcuModuleManager *mm, u32 m) {
    return V_At(&mm->modules, m).global_scope;
}

ModuleId AcuModuleManager_GetSourceModuleByFile(AcuModuleManager *mm, FileId file_id) {
    if (file_id == ACU_NULL_IDX || file_id >= V_Count(&mm->file_to_module)) {
        return ACU_NULL_IDX;
    }
    return V_Elements(&mm->file_to_module)[file_id];
}

AcuModuleStage AcuModuleManager_GetSourceModuleStage(AcuModuleManager *mm, ModuleId mod_id) {
    if (mod_id == ACU_NULL_IDX || mod_id >= V_Count(&mm->modules)) {
        return ACU_MODULE_STAGE_UNPROCESSED;
    }
    return V_Elements(&mm->modules)[mod_id].as.source.stage;
}

void AcuModuleManager_SetSourceModuleStage(AcuModuleManager *mm, ModuleId mod_id,
                                           AcuModuleStage stage) {
    if (mod_id != ACU_NULL_IDX && mod_id < V_Count(&mm->modules)) {
        V_Elements(&mm->modules)[mod_id].as.source.stage = stage;
    }
}

FileId AcuModuleManager_GetSourceModuleFile(AcuModuleManager *mm, ModuleId m) {
    if (m >= V_Count(&mm->modules))
        return ACU_NULL_IDX;
    return V_Elements(&mm->modules)[m].as.source.file_id;
}
