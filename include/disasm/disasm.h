#pragma once

#include "codegen/codegen.h"
#include <stdio.h>

u32 AcuDisasm_PrintInstruction(FILE *out, const AcuChunk *chunk, u32 ip);

void AcuDisasm_DumpChunk(FILE *out, const AcuChunk *chunk);
