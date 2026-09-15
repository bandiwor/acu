#define _GNU_SOURCE

#include "memory/mmap.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void *mmap_allocate(u64 size) {
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return likely(ptr != MAP_FAILED) ? ptr : NULL;
}

void *mmap_reallocate(void *ptr, u64 old_size, u64 new_size) {
    void *new_ptr = mremap(ptr, old_size, new_size, MREMAP_MAYMOVE);
    return likely(new_ptr != MAP_FAILED) ? new_ptr : NULL;
}

void mmap_deallocate(void *ptr, u64 allocated_size) {
    munmap(ptr, allocated_size);
}

void *mmap_file_allocate(const char *path, u64 *out_size) {
    int fd = open(path, O_RDONLY);
    if (unlikely(fd < 0)) {
        return NULL;
    }

    struct stat st;
    if (unlikely(fstat(fd, &st) < 0 || st.st_size == 0)) {
        close(fd);
        return NULL;
    }

    u64 size = (u64)st.st_size;
    void *ptr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);

    if (unlikely(ptr == MAP_FAILED)) {
        return NULL;
    }

    *out_size = size;
    return ptr;
}

void mmap_file_deallocate(void *ptr, u64 allocated_size) {
    if (likely(ptr != NULL)) {
        munmap(ptr, allocated_size);
    }
}
