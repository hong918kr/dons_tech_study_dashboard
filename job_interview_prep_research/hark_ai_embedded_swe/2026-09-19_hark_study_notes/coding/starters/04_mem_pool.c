/* 04_mem_pool.c — 고정 크기 블록 memory pool allocator (starter)
 *
 *   make run N=04
 *
 * 아래 TODO 다섯 군데를 채우면 된다. 테스트(main 아래)는 건드리지 말 것.
 * 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 목표
 *   - caller가 준 정적 저장소를 같은 크기 블록으로 자른다. malloc 금지.
 *   - free 블록 안에 다음 free 블록 주소를 저장하는 intrusive free list를 엮는다.
 *   - pool_alloc / pool_free 는 둘 다 O(1)이어야 한다(반복문 금지).
 *   - 외부 포인터와 double free를 거부한다.
 */

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 설정 상수 (그대로 둔다)                                          */
/* ------------------------------------------------------------------ */

#define POOL_BLOCK_SIZE 64u
#define POOL_MAX_BLOCKS 64u

#define POOL_OK 0
#define POOL_ERR_FOREIGN (-1)
#define POOL_ERR_DOUBLE (-2)

/* ------------------------------------------------------------------ */
/* 2. 블록 타입                                                        */
/* ------------------------------------------------------------------ */

/* free일 때는 next 링크, 사용 중일 때는 payload.
 * max_align_t 멤버는 union의 정렬을 최대 정렬로 끌어올리기 위한 것이다. */
typedef union pool_block {
    union pool_block *next;
    max_align_t       align_dummy;
    unsigned char     data[POOL_BLOCK_SIZE];
} pool_block_t;

_Static_assert(POOL_BLOCK_SIZE >= sizeof(void *),
               "block too small to hold a free-list link");
_Static_assert(POOL_BLOCK_SIZE % alignof(max_align_t) == 0u,
               "block size must be a multiple of max alignment");
_Static_assert(sizeof(pool_block_t) == POOL_BLOCK_SIZE,
               "union must not add padding");

/* ------------------------------------------------------------------ */
/* 3. pool 서술자 (그대로 둔다)                                        */
/* ------------------------------------------------------------------ */

#define POOL_BITMAP_WORDS ((POOL_MAX_BLOCKS + 31u) / 32u)

typedef struct {
    pool_block_t *blocks;
    pool_block_t *free_head;
    uint32_t      capacity;
    uint32_t      used;
    uint32_t      high_water;
    uint32_t      in_use[POOL_BITMAP_WORDS]; /* 블록당 1bit: 1=사용 중 */
} pool_t;

#define POOL_STORAGE(name, nblocks) static pool_block_t name[(nblocks)]

/* ------------------------------------------------------------------ */
/* 4. critical section — 타깃에서는 PRIMASK 저장/복원, host에서는 no-op */
/* ------------------------------------------------------------------ */

#ifndef CRIT_ENTER
#define CRIT_ENTER() uint32_t _pm = 0u
#define CRIT_EXIT()  ((void)_pm)
#endif

/* ------------------------------------------------------------------ */
/* 5. bitmap 도우미 (그대로 둔다)                                      */
/* ------------------------------------------------------------------ */

void pool_bitmap_set(pool_t *p, uint32_t idx)
{
    p->in_use[idx >> 5] |= (uint32_t)1u << (idx & 31u);
}

void pool_bitmap_clear(pool_t *p, uint32_t idx)
{
    p->in_use[idx >> 5] &= ~((uint32_t)1u << (idx & 31u));
}

int pool_bitmap_test(const pool_t *p, uint32_t idx)
{
    return (p->in_use[idx >> 5] >> (idx & 31u)) & 1u;
}

/* ------------------------------------------------------------------ */
/* 6. 공개 API — 여기부터 채운다                                       */
/* ------------------------------------------------------------------ */

/* TODO 1: 저장소를 블록으로 자르고 전부 free list에 엮는다.
 *   - p / storage 가 NULL이면 -1
 *   - storage 시작 주소가 alignof(max_align_t) 배수가 아니면 -1
 *   - 만들 수 있는 블록 수 n = storage_bytes / sizeof(pool_block_t)
 *     n == 0 이거나 n > POOL_MAX_BLOCKS 이면 -1
 *   - blocks[i].next = &blocks[i+1], 마지막은 NULL
 *   - free_head / capacity / used / high_water / in_use 초기화
 * 반환: 0 성공, -1 인자 이상 */
int pool_init(pool_t *p, void *storage, size_t storage_bytes)
{
    (void)p;
    (void)storage;
    (void)storage_bytes;
    return -1;
}

/* TODO 2: free list의 머리를 pop 한다. 반복문 없이 O(1).
 *   - free_head == NULL 이면 NULL 반환(고갈)
 *   - free_head를 b->next 로 옮기고, b의 bitmap 비트를 세우고,
 *     used++ 와 high_water 갱신
 *   - 공유 상태를 만지는 구간은 CRIT_ENTER/CRIT_EXIT 로 감싼다 */
void *pool_alloc(pool_t *p)
{
    (void)p;
    return NULL;
}

/* TODO 3: ptr이 이 pool의 유효한 블록 시작 주소인지 판정한다. 부수효과 없음.
 *   - [blocks, blocks + capacity*sizeof(block)) 범위 안인가
 *   - (ptr - base) 가 sizeof(pool_block_t)의 배수인가 (블록 중간 금지)
 * 반환: 1 맞음, 0 아님 */
int pool_owns(const pool_t *p, const void *ptr)
{
    (void)p;
    (void)ptr;
    return 0;
}

/* TODO 4: 블록을 free list에 push 한다. 반복문 없이 O(1).
 *   - ptr == NULL 이면 POOL_OK (free(NULL) 관례)
 *   - pool_owns 가 아니면 POOL_ERR_FOREIGN
 *   - bitmap 비트가 이미 0이면 POOL_ERR_DOUBLE 이고 상태를 바꾸지 않는다
 *   - 아니면 비트를 내리고 머리에 push, used-- */
int pool_free(pool_t *p, void *ptr)
{
    (void)p;
    (void)ptr;
    return POOL_ERR_FOREIGN;
}

/* TODO 5: 용량 회계 — 단순 읽기 */
uint32_t pool_capacity(const pool_t *p)   { (void)p; return 0u; }
uint32_t pool_used(const pool_t *p)       { (void)p; return 0u; }
uint32_t pool_available(const pool_t *p)  { (void)p; return 0u; }
uint32_t pool_high_water(const pool_t *p) { (void)p; return 0u; }

/* ================================================================== */
/* 테스트                                                              */
/* ================================================================== */

#define NBLOCKS 8u

POOL_STORAGE(g_storage, NBLOCKS);

/* 테스트 전용: free list 길이를 세어 자료구조 불변식을 확인한다.
 * O(n)이므로 제품 코드에는 넣지 않는다. cycle이 생기면 무한루프가 되지
 * 않도록 capacity+1에서 끊는다. */
static uint32_t freelist_len(const pool_t *p)
{
    uint32_t n = 0u;
    for (const pool_block_t *b = p->free_head; b != NULL; b = b->next) {
        if (++n > p->capacity) {
            return UINT32_MAX;           /* cycle 또는 손상 */
        }
    }
    return n;
}

static void check_invariant(const pool_t *p)
{
    assert(freelist_len(p) != UINT32_MAX);
    assert(p->used + freelist_len(p) == p->capacity);
    assert(p->used <= p->high_water);
}

static void test_init(void)
{
    pool_t pool;
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);
    assert(pool_capacity(&pool) == NBLOCKS);
    assert(pool_used(&pool) == 0u);
    assert(pool_available(&pool) == NBLOCKS);
    assert(pool_high_water(&pool) == 0u);
    assert(freelist_len(&pool) == NBLOCKS);
    check_invariant(&pool);
    puts("ok  1: init이 저장소를 블록으로 나누고 전부 free list에 엮는다");
}

static void test_init_rejects_bad_args(void)
{
    pool_t pool;
    unsigned char tiny[POOL_BLOCK_SIZE - 1u];
    /* 한 블록도 안 되는 저장소 */
    assert(pool_init(&pool, g_storage, sizeof tiny) == -1);
    /* NULL 저장소 */
    assert(pool_init(&pool, NULL, 128u) == -1);
    /* 정렬이 깨진 시작 주소 */
    unsigned char *misaligned = (unsigned char *)g_storage + 1;
    assert(pool_init(&pool, misaligned, sizeof g_storage - 1u) == -1);
    /* 상한 초과 */
    assert(pool_init(&pool, g_storage,
                     (size_t)(POOL_MAX_BLOCKS + 1u) * POOL_BLOCK_SIZE) == -1);
    puts("ok  2: init이 크기·정렬·상한 위반 인자를 거부한다");
}

static void test_alloc_all_then_exhaust(void)
{
    pool_t pool;
    void  *got[NBLOCKS];
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    for (uint32_t i = 0u; i < NBLOCKS; i++) {
        got[i] = pool_alloc(&pool);
        assert(got[i] != NULL);
        assert(pool_used(&pool) == i + 1u);
        assert(pool_available(&pool) == NBLOCKS - (i + 1u));
        /* 정렬: 모든 블록이 max_align_t 경계 위에 있어야 한다 */
        assert(((uintptr_t)got[i] % alignof(max_align_t)) == 0u);
        assert(pool_owns(&pool, got[i]));
    }
    /* 서로 다른 블록인가 */
    for (uint32_t i = 0u; i < NBLOCKS; i++) {
        for (uint32_t j = i + 1u; j < NBLOCKS; j++) {
            assert(got[i] != got[j]);
        }
    }
    /* 고갈 */
    assert(pool_alloc(&pool) == NULL);
    assert(pool_used(&pool) == NBLOCKS);
    assert(pool_high_water(&pool) == NBLOCKS);
    assert(pool_free(&pool, got[0]) == POOL_OK);
    check_invariant(&pool);
    puts("ok  3: 전부 alloc하면 정렬된 서로 다른 블록, 그 뒤엔 NULL을 준다");
}

static void test_blocks_do_not_overlap(void)
{
    pool_t         pool;
    unsigned char *got[NBLOCKS];
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    for (uint32_t i = 0u; i < NBLOCKS; i++) {
        got[i] = pool_alloc(&pool);
        assert(got[i] != NULL);
        /* 블록 전체를 자기 번호로 칠한다 */
        memset(got[i], (int)(0xA0u + i), POOL_BLOCK_SIZE);
    }
    /* 모두 칠한 뒤에도 각자 칠한 값이 온전히 남아 있어야 겹치지 않는 것이다 */
    for (uint32_t i = 0u; i < NBLOCKS; i++) {
        for (uint32_t k = 0u; k < POOL_BLOCK_SIZE; k++) {
            assert(got[i][k] == (unsigned char)(0xA0u + i));
        }
    }
    puts("ok  4: 블록 전체를 써도 이웃 블록을 침범하지 않는다");
}

static void test_free_is_lifo_and_reuses(void)
{
    pool_t pool;
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    void *a = pool_alloc(&pool);
    void *b = pool_alloc(&pool);
    void *c = pool_alloc(&pool);
    assert(a && b && c);
    assert(pool_used(&pool) == 3u);

    assert(pool_free(&pool, b) == POOL_OK);
    assert(pool_used(&pool) == 2u);
    /* 가장 최근에 반납한 블록이 다음 alloc에서 바로 나온다 (스택) */
    assert(pool_alloc(&pool) == b);
    assert(pool_used(&pool) == 3u);

    assert(pool_free(&pool, a) == POOL_OK);
    assert(pool_free(&pool, c) == POOL_OK);
    assert(pool_alloc(&pool) == c);      /* c가 나중에 들어갔으니 먼저 나온다 */
    assert(pool_alloc(&pool) == a);
    check_invariant(&pool);
    puts("ok  5: free는 LIFO로 쌓이고 방금 반납한 블록을 즉시 재사용한다");
}

static void test_double_free_detected(void)
{
    pool_t pool;
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    void *a = pool_alloc(&pool);
    assert(a != NULL);
    assert(pool_free(&pool, a) == POOL_OK);
    assert(pool_used(&pool) == 0u);

    /* 두 번째 반납은 거부되고 상태가 전혀 바뀌지 않아야 한다 */
    assert(pool_free(&pool, a) == POOL_ERR_DOUBLE);
    assert(pool_used(&pool) == 0u);
    assert(freelist_len(&pool) == NBLOCKS);   /* 사슬에 중복이 안 들어갔다 */
    assert(pool_free(&pool, a) == POOL_ERR_DOUBLE);
    check_invariant(&pool);

    /* 한 번도 alloc된 적 없는 블록을 free하는 것도 double free로 잡힌다 */
    assert(pool_free(&pool, &g_storage[NBLOCKS - 1u]) == POOL_ERR_DOUBLE);
    check_invariant(&pool);
    puts("ok  6: double free를 잡아내고 free list를 오염시키지 않는다");
}

static void test_foreign_pointer_rejected(void)
{
    pool_t pool;
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    unsigned char on_stack[POOL_BLOCK_SIZE];
    static pool_block_t other_storage[4];
    pool_t other;
    assert(pool_init(&other, other_storage, sizeof other_storage) == 0);
    void *from_other = pool_alloc(&other);
    assert(from_other != NULL);

    void *a = pool_alloc(&pool);
    assert(a != NULL);

    assert(pool_free(&pool, on_stack) == POOL_ERR_FOREIGN);      /* 스택 주소 */
    assert(pool_free(&pool, from_other) == POOL_ERR_FOREIGN);    /* 다른 pool */
    assert(pool_free(&pool, (unsigned char *)a + 1) == POOL_ERR_FOREIGN);
    assert(pool_free(&pool, (unsigned char *)a + 8) == POOL_ERR_FOREIGN);
    assert(pool_free(&pool, &g_storage[NBLOCKS]) == POOL_ERR_FOREIGN); /* one past end */
    assert(pool_free(&pool, NULL) == POOL_OK);                   /* no-op */

    assert(pool_used(&pool) == 1u);      /* 어떤 거부도 회계를 바꾸지 않았다 */
    check_invariant(&pool);
    puts("ok  7: 외부·중간·경계 밖 포인터를 거부하고 NULL은 no-op이다");
}

static void test_high_water_is_sticky(void)
{
    pool_t pool;
    void  *got[NBLOCKS];
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    for (uint32_t i = 0u; i < 5u; i++) {
        got[i] = pool_alloc(&pool);
        assert(got[i] != NULL);
    }
    assert(pool_high_water(&pool) == 5u);
    for (uint32_t i = 0u; i < 5u; i++) {
        assert(pool_free(&pool, got[i]) == POOL_OK);
    }
    assert(pool_used(&pool) == 0u);
    assert(pool_high_water(&pool) == 5u);    /* 내려가지 않는다 */

    for (uint32_t i = 0u; i < 3u; i++) {
        assert(pool_alloc(&pool) != NULL);
    }
    assert(pool_high_water(&pool) == 5u);    /* 최댓값보다 작으면 그대로 */
    puts("ok  8: high_water는 최댓값을 유지해 pool 크기 근거가 된다");
}

static void test_full_cycle_repeatable(void)
{
    pool_t pool;
    void  *got[NBLOCKS];
    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    for (uint32_t round = 0u; round < 100u; round++) {
        for (uint32_t i = 0u; i < NBLOCKS; i++) {
            got[i] = pool_alloc(&pool);
            assert(got[i] != NULL);
        }
        assert(pool_alloc(&pool) == NULL);
        for (uint32_t i = 0u; i < NBLOCKS; i++) {
            assert(pool_free(&pool, got[i]) == POOL_OK);
        }
        assert(pool_used(&pool) == 0u);
        assert(freelist_len(&pool) == NBLOCKS);
    }
    /* 100바퀴를 돌아도 블록이 새거나 사라지지 않는다 = 단편화 없음 */
    check_invariant(&pool);
    puts("ok  9: 100바퀴 전부 alloc/free를 반복해도 용량이 그대로다");
}

static void test_random_stress(void)
{
    pool_t   pool;
    void    *live[NBLOCKS];
    uint32_t nlive = 0u;
    uint32_t seed  = 12345u;

    assert(pool_init(&pool, g_storage, sizeof g_storage) == 0);

    for (uint32_t step = 0u; step < 20000u; step++) {
        seed = seed * 1103515245u + 12345u;     /* 재현 가능한 LCG */
        uint32_t r = (seed >> 16) & 0x7FFFu;

        if ((r & 1u) == 0u && nlive < NBLOCKS) {
            void *b = pool_alloc(&pool);
            assert(b != NULL);                  /* 여유가 있으면 반드시 성공 */
            for (uint32_t i = 0u; i < nlive; i++) {
                assert(live[i] != b);           /* 같은 블록 두 번 배급 금지 */
            }
            memset(b, (int)(step & 0xFFu), POOL_BLOCK_SIZE);
            live[nlive++] = b;
        } else if (nlive > 0u) {
            uint32_t k = r % nlive;
            assert(pool_free(&pool, live[k]) == POOL_OK);
            assert(pool_free(&pool, live[k]) == POOL_ERR_DOUBLE);
            live[k] = live[--nlive];
        }
        assert(pool_used(&pool) == nlive);
        assert(pool_used(&pool) + freelist_len(&pool) == NBLOCKS);
    }
    assert(pool_high_water(&pool) == NBLOCKS);
    puts("ok 10: 무작위 alloc/free 20000회에도 불변식이 깨지지 않는다");
}

int main(void)
{
    printf("pool: block=%u bytes, align=%zu, blocks=%u, pool_t=%zu bytes\n",
           POOL_BLOCK_SIZE, alignof(max_align_t), NBLOCKS, sizeof(pool_t));
    puts("(구현 전에는 아래 첫 assert에서 멈추는 것이 정상이다)");

    test_init();
    test_init_rejects_bad_args();
    test_alloc_all_then_exhaust();
    test_blocks_do_not_overlap();
    test_free_is_lifo_and_reuses();
    test_double_free_detected();
    test_foreign_pointer_rejected();
    test_high_water_is_sticky();
    test_full_cycle_repeatable();
    test_random_stress();

    puts("ALL TESTS PASSED");
    return 0;
}
