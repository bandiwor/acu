#include "fs/file_buffer.h"
#include "defines/defines.h"
#include "memory/mmap.h"
#include "types/string_view.h"
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ACU_GET_LEN(tagged) ((tagged) & ~((u64)1 << 63))
#define ACU_IS_MMAP(tagged) ((bool)((tagged) >> 63))
#define ACU_PACK_LEN(len, is_direct) ((len) | ((u64)(is_direct) << 63))

AcuFileBuffer Acu_LoadFile(const char *path) {
    AcuFileBuffer fb = {.data = NULL, .length_tagged = 0};
    u64 size = 0;

    void *mapped = mmap_file_allocate(path, &size);
    if (mapped == NULL) {
        return (AcuFileBuffer){
            .data = NULL,
            .length_tagged = ACU_PACK_LEN(0, true),
        };
    }

    u64 bytes_in_last_page = size % 4096;

    if (likely(bytes_in_last_page != 0 && bytes_in_last_page <= (4096 - 32))) {
        fb.data = (const u8 *)mapped;
        fb.length_tagged = ACU_PACK_LEN(size, true);
        return fb;
    }

    mmap_file_deallocate(mapped, size);

    int fd = open(path, O_RDONLY);
    if (unlikely(fd < 0)) {
        return fb;
    }

    struct stat st;
    if (likely(fstat(fd, &st) >= 0 && st.st_size > 0)) {
        size = (u64)st.st_size;

        u64 map_size = size + 32;

        void *anon_mapped =
            mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (likely(anon_mapped != MAP_FAILED)) {
            if (pread(fd, anon_mapped, size, 0) == (ssize_t)size) {
                __builtin_memset((u8 *)anon_mapped + size, 0, 32);

                fb.data = (const u8 *)anon_mapped;
                fb.length_tagged = ACU_PACK_LEN(size, false);
            } else {
                munmap(anon_mapped, map_size);
            }
        }
    }

    close(fd);
    return fb;
}

void Acu_FreeFile(AcuFileBuffer *fb) {
    if (fb->data) {
        void *real_ptr = (void *)fb->data;
        u64 real_size = ACU_GET_LEN(fb->length_tagged);

        if (real_size > 0) {
            if (ACU_IS_MMAP(fb->length_tagged)) {
                mmap_file_deallocate(real_ptr, real_size);
            } else {
                munmap(real_ptr, real_size + 32);
            }
        }
    }

    fb->data = NULL;
    fb->length_tagged = 0;
}

StringView AcuFileBuffer_ToStringView(AcuFileBuffer buffer) {
    return (StringView){
        .data = buffer.data,
        .length = ACU_GET_LEN(buffer.length_tagged),
    };
}
