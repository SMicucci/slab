#include "slab.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(SLAB_WIN)
#include <memoryapi.h>
#include <windows.h>
#elif defined(SLAB_UNIX)
#include <sys/mman.h>
#include <unistd.h>
#else
#include <stdlib.h>
#endif

#define is_little_endian (*(unsigned short *)"\0\x1" == 1)

#define alignment (sizeof(size_t))
#define map_align(size) (((size) + alignment - 1) & ~(alignment - 1))
#define map_size(len) (((size_t)len + 7) / 8)

// this macro find first 0 in a little-endian word of 64 bit
#define modified_bruijin_clz(x)                                                \
        !(~(uint64_t)(x)) +                                                    \
            (const unsigned char                                               \
                 []){63, 0,  1,  52, 2,  6,  53, 26, 3,  37, 40, 7,  33,       \
                     54, 47, 27, 61, 4,  38, 45, 43, 41, 21, 8,  23, 34,       \
                     58, 55, 48, 17, 28, 10, 62, 51, 5,  25, 36, 39, 32,       \
                     46, 60, 44, 42, 20, 22, 57, 16, 9,  50, 24, 35, 31,       \
                     59, 19, 56, 15, 49, 30, 18, 14, 29, 13, 12, 11}           \
                [((~(uint64_t)(x) & -~(uint64_t)(x)) * 0x045FBAC7992A70DA) >>  \
                 58]

static inline slab_t *slab_init(size_t size, size_t len);
static inline void slab_destroy(slab_t *self, size_t size, size_t len);
static inline void *slab_alloc(slab_t *self, size_t size, size_t len);
static inline int slab_free(slab_t *self, void *mem, size_t size, size_t len);

void slab_pool_init(slab_pool_t *self, size_t slab_size, size_t slab_nelem)
{
        if (!self)
                return;
        self->len = slab_nelem;
        self->size = slab_size;
        self->head = slab_init(slab_size, slab_nelem);
}

void slab_pool_destroy(slab_pool_t *self)
{
        if (!self)
                return;
        slab_t *curr = self->head;
        while (curr) {
                slab_t *next = curr->next;
                slab_destroy(curr, self->size, self->len);
                curr = next;
        }
}

void *slab_pool_alloc(slab_pool_t *self)
{
        if (!self)
                return NULL;
        slab_t *curr = self->head;
        while (curr) {
                slab_t *next = curr->next;
                void *res = slab_alloc(curr, self->size, self->len);
                if (res)
                        return res;
                if (!next) {
                        curr->next = slab_init(self->size, self->len);
                        next = curr->next;
                }
                curr = next;
        }
}

void slab_pool_free(slab_pool_t *self, void *mem)
{
        if (!self)
                return;
        slab_t *curr = self->head;
        while (curr) {
                slab_t *next = curr->next;
                if (slab_free(curr, mem, self->size, self->len) == 1)
                        return;
                curr = next;
        }
}

static inline slab_t *slab_init(size_t slab_size, size_t slab_len)
{
        size_t map_len = map_size(slab_len);
        size_t map_size = map_align(map_len);
        size_t total_size = map_size + sizeof(slab_t) + (slab_size * slab_len);
        // allocation -> | bitmap | align | slab_t | FEM |
        void *mem;
#if defined(SLAB_WIN)
        mem = VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE,
                           PAGE_READWRITE);
        if (!mem)
                return NULL;
#elif defined(SLAB_UNIX)
        mem = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED)
                return NULL;
#else
        mem = malloc(total_size);
        if (!mem)
                return NULL;
#endif
        // Initialize slab metadata
        slab_t *slab = (slab_t *)((uint8_t *)mem + map_size);
        slab->next = NULL;
        slab->map = (uint8_t *)(mem);
        // Initialize bitmap - all blocks free
        memset(slab->map, 0, map_size);
        return slab;
}

static inline void slab_destroy(slab_t *self, size_t size, size_t len)
{
#if defined(SLAB_WIN)
        VirtualFree(self->map, 0, MEM_RELEASE);
#elif defined(SLAB_UNIX)
        size_t total_size =
            sizeof(slab_t) + map_align(map_size(len)) + (size * len);
        munmap(self->map, total_size);
#else
        free(self->map);
#endif
}

static inline void *slab_alloc(slab_t *self, size_t size, size_t len)
{
        size_t bites = map_size(len);
        for (size_t i = 0; i < bites; i++) {
                uint64_t word = 0;
                // work on big endian too
                for (size_t j = 0; j < 8 && (i + j) < bites; j++) {
                        word |= (uint64_t)self->map[i + j] << (j * 8);
                }
                int zero = modified_bruijin_clz(word);
                if (zero < 64) {
                        size_t pos = i * 8 + zero;
                        if (pos >= len)
                                return NULL;
                        self->map[pos / 8] |= (1 << (pos % 8));
                        return self->mem + (size * pos);
                }
        }
        return NULL;
}

static inline int slab_free(slab_t *self, void *mem, size_t size, size_t len)
{
        uintptr_t start = (uintptr_t)self->mem;
        uintptr_t end = start + (size * len);
        if ((uintptr_t)mem < start || (uintptr_t)mem >= end)
                return 0;
        size_t pos = ((uintptr_t)mem - start) / size;
        self->map[pos / 8] &= ~(1 << (pos % 8));
        return 1;
}
