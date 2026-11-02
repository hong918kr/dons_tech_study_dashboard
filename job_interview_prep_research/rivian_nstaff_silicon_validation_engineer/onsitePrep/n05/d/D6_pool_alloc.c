#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
typedef struct {
    void    *free_head;      /* singly linked list threaded through free blocks */
    uint8_t *base;
    size_t   block_size;
    size_t   nblocks;
    size_t   nfree;
} pool_t;

/* mem must be aligned for void*. block_size is rounded up to sizeof(void*). */
bool pool_init(pool_t *p, void *mem, size_t mem_size, size_t block_size)
{
    const size_t a = sizeof(void *);
    if (p == NULL || mem == NULL || ((uintptr_t)mem % a) != 0)
        return false;
    block_size = (block_size + a - 1) / a * a;
    if (block_size == 0 || mem_size < block_size)
        return false;
    p->base = mem;
    p->block_size = block_size;
    p->nblocks = mem_size / block_size;
    p->nfree = p->nblocks;
    p->free_head = NULL;
    for (size_t i = p->nblocks; i-- > 0;) {      /* push in reverse: block 0 first out */
        void *blk = p->base + i * block_size;
        *(void **)blk = p->free_head;            /* "next" lives inside the free block */
        p->free_head = blk;
    }
    return true;
}

void *pool_alloc(pool_t *p)
{
    void *blk = p->free_head;
    if (blk == NULL)
        return NULL;                             /* exhausted */
    p->free_head = *(void **)blk;
    p->nfree--;
    return blk;
}

bool pool_free(pool_t *p, void *blk)
{
    uintptr_t off = (uintptr_t)blk - (uintptr_t)p->base;
    if (blk == NULL || (uintptr_t)blk < (uintptr_t)p->base ||
        off >= p->nblocks * p->block_size || off % p->block_size != 0)
        return false;                            /* not one of ours */
    *(void **)blk = p->free_head;
    p->free_head = blk;
    p->nfree++;
    return true;
}
/* ---- end model answer ---- */

static void *mem[32];            /* 32 * 8 = 256 bytes, void*-aligned, holds void* objects */

int main(void)
{
    pool_t p;
    assert(!pool_init(&p, NULL, 256, 16));
    assert(!pool_init(&p, (uint8_t *)mem + 1, 255, 16));    /* misaligned */
    assert(!pool_init(&p, mem, 8, 16));                     /* smaller than 1 block */
    assert(!pool_init(&p, mem, 256, 0));

    assert(pool_init(&p, mem, sizeof mem, 20));             /* 20 -> 24 on LP64 */
    assert(p.block_size == 24 && p.nblocks == 10 && p.nfree == 10);

    void *b[10];
    for (int i = 0; i < 10; i++) {
        b[i] = pool_alloc(&p);
        assert(b[i] == (uint8_t *)mem + (size_t)i * 24);    /* in address order */
        memset(b[i], 0xA5, 24);                             /* user owns all bytes */
    }
    assert(pool_alloc(&p) == NULL && p.nfree == 0);

    assert(pool_free(&p, b[3]));
    assert(pool_free(&p, b[7]));
    assert(p.nfree == 2);
    assert(pool_alloc(&p) == b[7]);                         /* LIFO */
    assert(pool_alloc(&p) == b[3]);
    assert(pool_alloc(&p) == NULL);

    assert(!pool_free(&p, NULL));
    assert(!pool_free(&p, (uint8_t *)b[2] + 4));            /* not block start */
    assert(!pool_free(&p, (uint8_t *)mem + 240));           /* tail slack past last block */
    assert(!pool_free(&p, (uint8_t *)mem - 24));            /* below base */
    assert(p.nfree == 0);

    for (int i = 0; i < 10; i++)
        assert(pool_free(&p, b[i]));
    assert(p.nfree == 10);

    puts("D6 PASS");
    return 0;
}
