#pragma once

#include "analyzer/analyzer.h"
#include "defines/defines.h"

void AcuBuiltin_RegisterAll(AcuWorkspace *ws);

ModuleId AcuBuiltin_RegisterIO(AcuWorkspace *ws);
ModuleId AcuBuiltin_RegisterMath(AcuWorkspace *ws);
ModuleId AcuBuiltin_RegisterSys(AcuWorkspace *ws);
