#include "fs/file_manager.h"
#include "common/error.h"
#include "memory/vector.h"
#include "types/string_view.h"
#include <stdio.h>
#include <unistd.h>

#ifdef _WIN32
#define ACU_MAX_PATH 4096
#else
#ifndef PATH_MAX
#define ACU_MAX_PATH 4096
#else
#define ACU_MAX_PATH PATH_MAX
#endif
#endif

#ifdef _WIN32
#include <windows.h>
// _fullpath корректно разрешает ../ и ./ относительно CWD, если передать относительный путь
#define RESOLVE_PATH(rel, abs) _fullpath(abs, rel, 4096)
#else
#include <limits.h>
#include <stdlib.h>
#define RESOLVE_PATH(rel, abs) realpath(rel, abs)
#endif

static bool IsAbsolutePath(StringView path) {
    if (path.length == 0)
        return false;
    if (path.data[0] == '/' || path.data[0] == '\\')
        return true;
#ifdef _WIN32
    if (path.length >= 3 && path.data[1] == ':' && (path.data[2] == '\\' || path.data[2] == '/')) {
        return true;
    }
#endif
    return false;
}

AcuFileManager AcuFileManager_Create(void) {
    AcuFileManager fm = {0};
    AcuFileManager_Init(&fm);
    return fm;
}

static StringView GetDirectoryName(StringView path) {
    for (i32 i = (i32)path.length - 1; i >= 0; i--) {
        if (path.data[i] == '/' || path.data[i] == '\\') {
            if (i == 0)
                return StringView_Create(path.data, 1);
            return StringView_Create(path.data, (u32)i);
        }
    }
    return StringView_CStr(".");
}

static bool HasAcuExtension(StringView path) {
    if (path.length < 4)
        return false;
    return (bool)(path.data[path.length - 4] == '.' && path.data[path.length - 3] == 'a' &&
                  path.data[path.length - 2] == 'c' && path.data[path.length - 1] == 'u');
}

void AcuFileManager_Init(AcuFileManager *fm) {
    fm->count = 0;
    V_Init(&fm->entries);
    V_Init(&fm->path_data);
    V_Init(&fm->hash_table);

    AcuFileSlot empty_slot = {.hash = 0, .file_id = ACU_NULL_IDX};
    for (u32 i = 0; i < V_Capacity(&fm->hash_table); i++) {
        V_Push(&fm->hash_table, empty_slot);
    }
}

void AcuFileManager_Free(AcuFileManager *fm) {
    for (u32 i = 0; i < V_Count(&fm->entries); i++) {
        Acu_FreeFile(&V_Elements(&fm->entries)[i].buffer);
    }
    V_Free(&fm->entries);
    V_Free(&fm->hash_table);
    V_Free(&fm->path_data);
}

static void AcuFileManager_Rehash(AcuFileManager *fm) {
    const u64 old_cap = V_Capacity(&fm->hash_table);
    const u64 new_cap = old_cap * 2;
    const u64 mask = new_cap - 1;

    TypedVector(AcuFileSlot) new_ht = {0};
    V_InitWithCapacity(&new_ht, new_cap);

    AcuFileSlot empty_slot = {.hash = 0, .file_id = ACU_NULL_IDX};
    for (u32 i = 0; i < new_cap; i++) {
        V_Push(&new_ht, empty_slot);
    }

    const AcuFileSlot *const old_elements = V_Elements(&fm->hash_table);
    AcuFileSlot *new_elements = V_Elements(&new_ht);

    for (u32 i = 0; i < old_cap; i++) {
        AcuFileSlot slot = old_elements[i];
        if (slot.file_id != ACU_NULL_IDX) {
            u32 idx = slot.hash & mask;
            while (new_elements[idx].file_id != ACU_NULL_IDX) {
                idx = (idx + 1) & mask;
            }
            new_elements[idx] = slot;
        }
    }
    V_Free(&fm->hash_table);
    fm->hash_table.base = new_ht.base;
}

FileId AcuFileManager_ResolveAndLoad(AcuFileManager *fm, FileId importer_id, StringView raw_path,
                                     AcuResult *out_status) {
    char combined_path[4096];
    const char *extension = (int)HasAcuExtension(raw_path) ? "" : ".acu";

    if (IsAbsolutePath(raw_path) || importer_id == ACU_NULL_IDX) {
        int written = snprintf(combined_path, sizeof(combined_path), "%.*s%s", (int)raw_path.length,
                               raw_path.data, extension);

        if (written < 0 || written >= (int)sizeof(combined_path)) {
            *out_status = ACU_ERR_LOADER_PATH_TOO_LONG;
            return ACU_NULL_IDX;
        }
    } else {
        AcuFileEntry *importer = &V_Elements(&fm->entries)[importer_id];
        StringView importer_path_sv = StringView_Create(
            &V_Elements(&fm->path_data)[importer->path_offset], importer->path_length);
        StringView base_dir = GetDirectoryName(importer_path_sv);

        const char *separator =
            (base_dir.length == 1 && (base_dir.data[0] == '/' || base_dir.data[0] == '\\')) ? ""
                                                                                            : "/";

        int written =
            snprintf(combined_path, sizeof(combined_path), "%.*s%s%.*s%s", (int)base_dir.length,
                     base_dir.data, separator, (int)raw_path.length, raw_path.data, extension);

        if (written < 0 || written >= (int)sizeof(combined_path)) {
            *out_status = ACU_ERR_LOADER_PATH_TOO_LONG;
            return ACU_NULL_IDX;
        }
    }

    char absolute_path[4096];
    if (RESOLVE_PATH(combined_path, absolute_path) == NULL) {
        *out_status = ACU_ERR_LOADER_FILE_NOT_FOUND;
        return ACU_NULL_IDX;
    }

    StringView abs_sv = StringView_CStr(absolute_path);
    u32 hash = (u32)StringView_Hash(abs_sv);

    u32 cap = V_Count(&fm->hash_table);
    u32 mask = cap - 1;
    u32 idx = hash & mask;

    AcuFileSlot *ht = V_Elements(&fm->hash_table);
    AcuFileEntry *entries = V_Elements(&fm->entries);

    while (ht[idx].file_id != ACU_NULL_IDX) {
        if (ht[idx].hash == hash) {
            u32 existing_id = ht[idx].file_id;

            StringView existing_path_sv =
                StringView_Create(&V_Elements(&fm->path_data)[entries[existing_id].path_offset],
                                  entries[existing_id].path_length);

            if (StringView_Equals(existing_path_sv, abs_sv)) {
                *out_status = ACU_ERR_OK;
                return existing_id;
            }
        }
        idx = (idx + 1) & mask;
    }

    AcuFileBuffer buffer = Acu_LoadFile(absolute_path);
    if (!buffer.data) {
        *out_status = ACU_ERR_LOADER_READ_FAILED;
        return ACU_NULL_IDX;
    }

    u32 new_path_offset = V_Count(&fm->path_data);
    for (u32 i = 0; i < abs_sv.length; i++) {
        V_Push(&fm->path_data, abs_sv.data[i]);
    }

    if (unlikely(fm->count * 4 >= cap * 3)) {
        AcuFileManager_Rehash(fm);
        cap = V_Count(&fm->hash_table);
        mask = cap - 1;
        idx = hash & mask;
        ht = V_Elements(&fm->hash_table);

        while (ht[idx].file_id != ACU_NULL_IDX) {
            idx = (idx + 1) & mask;
        }
    }

    FileId new_file_id = V_Count(&fm->entries);
    AcuFileEntry new_entry = {
        .path_offset = new_path_offset, .path_length = abs_sv.length, .buffer = buffer};
    V_Push(&fm->entries, new_entry);

    ht[idx].hash = hash;
    ht[idx].file_id = new_file_id;
    fm->count++;

    *out_status = ACU_ERR_OK;
    return new_file_id;
}

StringView AcuFileManager_GetSource(const AcuFileManager *fm, FileId id) {
    if (unlikely(id >= V_Count(&fm->entries)))
        return StringView_Empty();

    return AcuFileBuffer_ToStringView(V_Elements(&fm->entries)[id].buffer);
}

StringView AcuFileManager_GetPath(const AcuFileManager *fm, FileId id) {
    if (unlikely(id >= V_Count(&fm->entries)))
        return StringView_Empty();

    const AcuFileEntry *entry = &V_Elements(&fm->entries)[id];
    return StringView_Create(&V_Elements(&fm->path_data)[entry->path_offset], entry->path_length);
}
