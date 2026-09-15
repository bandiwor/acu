#pragma once
#include "common/error.h"
#include "defines/defines.h"
#include "fs/file_buffer.h"
#include "memory/vector.h"

typedef struct {
    u32 path_offset;
    u32 path_length;
    AcuFileBuffer buffer;
} AcuFileEntry;

typedef struct {
    u32 hash;
    FileId file_id;
} AcuFileSlot;

typedef struct {
    TypedVector(AcuFileEntry) entries;
    TypedVector(AcuFileSlot) hash_table;
    TypedVector(u8) path_data;
    u32 count;
} AcuFileManager;

AcuFileManager AcuFileManager_Create(void);
void AcuFileManager_Init(AcuFileManager *fm);
void AcuFileManager_Free(AcuFileManager *fm);

FileId AcuFileManager_ResolveAndLoad(AcuFileManager *fm, FileId importer_id, StringView raw_path,
                                     AcuResult *out_status);

StringView AcuFileManager_GetSource(const AcuFileManager *fm, FileId id);
StringView AcuFileManager_GetPath(const AcuFileManager *fm, FileId id);
