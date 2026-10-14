/* Fixed-block memory pool: O(1) alloc/free from a static array via an embedded free list — the usual answer to "no malloc in firmware". */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BLOCK_SIZE  32u
#define BLOCK_COUNT 8u

typedef union block {
    union block *next;                                /* while free: link to the next free block */
    uint8_t      data[BLOCK_SIZE];                    /* while allocated: user data */
} block_t;

typedef struct {
    block_t  blocks[BLOCK_COUNT];
    block_t *free_list;
    uint32_t in_use, high_water, failures;
} pool_t;

static void pool_init(pool_t *p)
{
    for (size_t i = 0; i < BLOCK_COUNT - 1; i++)
        p->blocks[i].next = &p->blocks[i + 1];
    p->blocks[BLOCK_COUNT - 1].next = NULL;
    p->free_list = &p->blocks[0];
    p->in_use = p->high_water = p->failures = 0;
}

static void *pool_alloc(pool_t *p)
{
    block_t *b = p->free_list;
    if (b == NULL) {
        p->failures++;                               /* visible exhaustion, never undefined behavior */
        return NULL;
    }
    p->free_list = b->next;
    if (++p->in_use > p->high_water)
        p->high_water = p->in_use;
    return b->data;
}

static int pool_owns(const pool_t *p, const void *ptr)
{
    const uint8_t *u = ptr, *base = (const uint8_t *)p->blocks;
    return u >= base && u < base + sizeof p->blocks && (size_t)(u - base) % sizeof(block_t) == 0;
}

static int pool_free(pool_t *p, void *ptr)           /* 0 ok, -1 not from this pool (catches bad frees in tests) */
{
    if (ptr == NULL || !pool_owns(p, ptr))
        return -1;
    block_t *b = (block_t *)ptr;
    b->next = p->free_list;
    p->free_list = b;
    p->in_use--;
    return 0;
}

int main(void)
{
    static pool_t pool;
    void *ptrs[BLOCK_COUNT];
    pool_init(&pool);

    for (size_t i = 0; i < BLOCK_COUNT; i++)
        assert((ptrs[i] = pool_alloc(&pool)) != NULL);
    assert(pool_alloc(&pool) == NULL && pool.failures == 1);        /* exhausted */
    for (size_t i = 0; i < BLOCK_COUNT; i++)
        for (size_t j = i + 1; j < BLOCK_COUNT; j++)
            assert(ptrs[i] != ptrs[j]);                                /* no block handed out twice */

    assert(pool_free(&pool, ptrs[3]) == 0);
    assert(pool_alloc(&pool) == ptrs[3]);                              /* LIFO reuse */
    int x;
    assert(pool_free(&pool, &x) == -1);                                /* foreign pointer rejected */
    assert(pool_free(&pool, (uint8_t *)ptrs[0] + 1) == -1);            /* misaligned pointer rejected */
    for (size_t i = 0; i < BLOCK_COUNT; i++)
        assert(pool_free(&pool, ptrs[i]) == 0);
    assert(pool.in_use == 0 && pool.high_water == BLOCK_COUNT);

    puts("mem_pool: PASS");
    return 0;
}
