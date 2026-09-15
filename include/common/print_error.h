#pragma once

#include "common/error.h"
#include "fs/file_manager.h"
#include "interner/type_interner.h"

void AcuError_Print(const AcuError *error, const AcuFileManager *fm, const AcuTypeInterner *types);
void AcuError_PrintAll(const AcuError *first, u64 count, const AcuFileManager *fm,
                       const AcuTypeInterner *types);
