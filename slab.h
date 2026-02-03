#ifndef SLAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct slab_pool_t slab_pool_t;
typedef struct slab_t slab_t;

struct slab_pool_t {
        slab_t *head;
        size_t size;
        size_t len;
};

struct slab_t {
        slab_t *next;
        uint8_t *map;
        uint8_t mem[];
};

void slab_pool_init(slab_pool_t *self, size_t slab_size, size_t slab_nelem);
void slab_pool_destroy(slab_pool_t *self);

void *slab_pool_alloc(slab_pool_t *self);
void slab_pool_free(slab_pool_t *self, void *mem);

#endif /* ifndef SLAB_H */
