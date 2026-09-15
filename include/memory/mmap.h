#pragma once

#include "defines/defines.h"

void *mmap_allocate(u64 size);
void *mmap_reallocate(void *ptr, u64 old_size, u64 new_size);
attribute_nonnull(1) void mmap_deallocate(void *ptr, u64 allocated_size);
attribute_nonnull(1, 2) void *mmap_file_allocate(const char *path, u64 *out_size);
attribute_nonnull(1) void mmap_file_deallocate(void *ptr, u64 allocated_size);
