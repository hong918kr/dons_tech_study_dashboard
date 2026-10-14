/*
 * T2-9  Fixed-Block Memory Pool for Sensor Messages
 *
 * The telemetry task allocates one block per sensor message, fills it, and hands it to the
 * radio task, which frees it after transmission. malloc() is banned (fragmentation,
 * non-deterministic timing), so implement a fixed-block pool:
 *
 *   - Storage is static: NUM_BLOCKS blocks of BLOCK_SIZE bytes inside the Pool object.
 *   - pool_init()  : put every block on the free list (block 0 first).
 *   - pool_alloc() : O(1). Return a free block, or NULL when the pool is exhausted.
 *   - pool_free()  : O(1). Return the block to the pool. Must detect:
 *                      POOL_ERR_FOREIGN  pointer is not the start of a block in this pool
 *                      POOL_ERR_DOUBLE   block is already free
 *                    pool_free(p, NULL) is a no-op returning POOL_OK (like free(NULL)).
 *   - pool_free_count() : number of free blocks.
 *   - The free list is INTRUSIVE: the "next" pointer lives inside the free block itself.
 *     A used-block bitmap is allowed for the double-free check.
 *
 * Build & run:  ./run.sh T2-9
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define BLOCK_SIZE 32u        /* bytes per block (>= sizeof(void *)) */
#define NUM_BLOCKS 8u         /* <= 32 so one uint32_t bitmap is enough */

enum { POOL_OK = 0, POOL_ERR_FOREIGN = -1, POOL_ERR_DOUBLE = -2 };

typedef union Block {
    union Block *next;                  /* valid only while the block is free */
    uint8_t      bytes[BLOCK_SIZE];     /* payload while the block is in use */
    uint64_t     align_;                /* force 8-byte alignment */
} Block;

typedef struct {
    Block    blocks[NUM_BLOCKS];
    Block   *free_head;
    uint32_t used;                      /* bit i set = block i is allocated */
    uint32_t n_free;
} Pool;

void pool_init(Pool *p)
{
    /* TODO: your code here */
    (void)p;
}

void *pool_alloc(Pool *p)
{
    /* TODO: your code here */
    (void)p;
    return NULL;
}

int pool_free(Pool *p, void *ptr)
{
    /* TODO: your code here */
    (void)p; (void)ptr;
    return POOL_OK;
}

uint32_t pool_free_count(const Pool *p)
{
    /* TODO: your code here */
    (void)p;
    return 0;
}

int main(void)
{
    static Pool pool;                    /* static: lives in .bss, not on the stack */
    void *blk[NUM_BLOCKS + 1];

    /* 1. basic: alloc 3, free the middle one, the next alloc reuses it (LIFO) */
    pool_init(&pool);
    CHECK(pool_free_count(&pool) == 8u);
    blk[0] = pool_alloc(&pool); blk[1] = pool_alloc(&pool); blk[2] = pool_alloc(&pool);
    CHECK(blk[0] != NULL && blk[1] != NULL && blk[2] != NULL);
    CHECK(blk[0] != blk[1] && blk[1] != blk[2] && blk[0] != blk[2]);
    CHECK(pool_free_count(&pool) == 5u);
    CHECK(pool_free(&pool, blk[1]) == POOL_OK);
    CHECK(pool_free_count(&pool) == 6u);
    CHECK(blk[1] != NULL && pool_alloc(&pool) == blk[1]);

    /* 2. exhaustion: exactly NUM_BLOCKS distinct blocks, then NULL */
    pool_init(&pool);
    for (uint32_t i = 0; i < NUM_BLOCKS; i++) blk[i] = pool_alloc(&pool);
    bool distinct = true;
    for (uint32_t i = 0; i < NUM_BLOCKS; i++)
        for (uint32_t j = i + 1u; j < NUM_BLOCKS; j++)
            if (blk[i] == NULL || blk[i] == blk[j]) distinct = false;
    CHECK(distinct);
    CHECK(pool_alloc(&pool) == NULL);
    CHECK(pool_free_count(&pool) == 0u);

    /* 3. payload writes do not corrupt the pool (use the whole block) */
    for (uint32_t i = 0; i < NUM_BLOCKS; i++)
        if (blk[i]) memset(blk[i], 0xA5, BLOCK_SIZE);
    for (uint32_t i = 0; i < NUM_BLOCKS; i++) CHECK(pool_free(&pool, blk[i]) == POOL_OK);
    CHECK(pool_free_count(&pool) == NUM_BLOCKS);
    uint32_t got = 0;
    while (pool_alloc(&pool) != NULL && got <= NUM_BLOCKS) got++;
    CHECK(got == NUM_BLOCKS);

    /* 4. double free is detected and does not corrupt the free list */
    pool_init(&pool);
    void *x = pool_alloc(&pool);
    CHECK(pool_free(&pool, x) == POOL_OK);
    CHECK(pool_free(&pool, x) == POOL_ERR_DOUBLE);
    CHECK(pool_free_count(&pool) == NUM_BLOCKS);

    /* 5. foreign and misaligned pointers are rejected; NULL is a no-op */
    int on_stack = 0;
    x = pool_alloc(&pool);
    CHECK(pool_free(&pool, &on_stack) == POOL_ERR_FOREIGN);
    CHECK(x != NULL && pool_free(&pool, (uint8_t *)x + 1) == POOL_ERR_FOREIGN);
    CHECK(pool_free(&pool, (uint8_t *)&pool.blocks[NUM_BLOCKS - 1u] + BLOCK_SIZE) == POOL_ERR_FOREIGN);
    CHECK(pool_free(&pool, NULL) == POOL_OK);
    CHECK(pool_free_count(&pool) == NUM_BLOCKS - 1u);

    /* 6. never-allocated block inside the pool counts as a double free */
    pool_init(&pool);
    CHECK(pool_free(&pool, &pool.blocks[3]) == POOL_ERR_DOUBLE);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
