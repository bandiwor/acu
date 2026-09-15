#pragma once
#include "defines/defines.h"
#include "memory/vector.h"
#include "module/module.h"
#include "table/symbol_table.h"
#include "types/string_view.h"

typedef struct {
    StringView name;
    ModuleId module_id;
} AcuVirtualModule;

typedef struct {
    TypedVector(AcuModule) modules;
    TypedVector(ModuleId) file_to_module;
    TypedVector(AcuVirtualModule) virtual_modules;
    AcuSymbolTable *symbols;
} AcuModuleManager;

AcuModuleManager AcuModuleManager_Create(AcuSymbolTable *symbols);
void AcuModuleManager_Init(AcuModuleManager *mm, AcuSymbolTable *symbols);
void AcuModuleManager_Free(AcuModuleManager *mm);

ModuleId AcuModuleManager_CreateVirtual(AcuModuleManager *mm, StringView import_name);
ModuleId AcuModuleManager_FindVirtual(AcuModuleManager *mm, StringView import_name);
ModuleId AcuModuleManager_GetOrCreate(AcuModuleManager *mm, StringView import_name, FileId file_id,
                                      AstNodeIdx root_idx);

u32 AcuModuleManager_GetCount(AcuModuleManager *mm);
AstNodeIdx AcuModuleManager_GetSourceModuleNode(AcuModuleManager *mm, u32 m);
FileId AcuModuleManager_GetSourceModuleFile(AcuModuleManager *mm, ModuleId m);
ScopeId AcuModuleManager_GetModuleScope(AcuModuleManager *mm, u32 m);

AcuModuleStage AcuModuleManager_GetSourceModuleStage(AcuModuleManager *mm, ModuleId mod_id);
void AcuModuleManager_SetSourceModuleStage(AcuModuleManager *mm, ModuleId mod_id,
                                           AcuModuleStage stage);

ModuleId AcuModuleManager_GetSourceModuleByFile(AcuModuleManager *mm, FileId file_id);
