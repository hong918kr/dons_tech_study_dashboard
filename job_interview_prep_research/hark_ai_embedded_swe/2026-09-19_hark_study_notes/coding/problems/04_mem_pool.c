/* 04_mem_pool.c — 고정 크기 블록 메모리 풀 할당자
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 04_mem_pool.c -o run_04
 * 또는: make prob N=04 && make run N=04
 *
 * 요구사항: 문제 파일(04_mem_pool.md)을 읽고, 아래 // TODO 섹션들을 채워라.
 *
 * 개요:
 * - 고정 크기 블록들로 구성된 메모리 풀
 * - malloc 없이 정적 배열만 사용 (임베디드 환경)
 * - intrusive free list: 빈 블록 안에 다음 빈 블록의 주소를 저장
 * - alloc/free 모두 O(1)
 * - ISR-safe: 짧은 critical section만 필요
 */

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================================================================== */
/* 설정                                                              */
/* ================================================================== */

#define POOL_BLOCK_SIZE 64u
#define POOL_MAX_BLOCKS 64u

#define POOL_OK          0
#define POOL_ERR_FOREIGN (-1)
#define POOL_ERR_DOUBLE  (-2)

/* ================================================================== */
/* 1. 블록 타입 — TODO: 구조체 정의                                    */
/* ================================================================== */

/* free일 때: next 링크
 * 사용 중일 때: payload 데이터
 * 둘 다 안에 들어갈 수 있도록 union으로 만든다.
 * max_align_t는 정렬을 최대로 강제하는 더미 멤버다.
 */
typedef union pool_block {
    // TODO: 다음 필드들을 추가하세요:
    // union pool_block *next;      /* free 상태: 다음 free 블록 */
    // max_align_t       align_dummy;
    // unsigned char     data[POOL_BLOCK_SIZE];  /* 사용 중: payload */
} pool_block_t;

/* 블록이 최소한 포인터 하나는 담을 수 있어야 한다 */
_Static_assert(POOL_BLOCK_SIZE >= sizeof(void *),
               "block too small to hold a free-list link");

_Static_assert(sizeof(pool_block_t) == POOL_BLOCK_SIZE,
               "union must not add padding");

/* ================================================================== */
/* 2. Pool 서술자 — TODO: 구조체 정의                                  */
/* ================================================================== */

#define POOL_BITMAP_WORDS ((POOL_MAX_BLOCKS + 31u) / 32u)

typedef struct {
    // TODO: 다음 필드들을 추가하세요:
    // pool_block_t *blocks;     /* caller가 준 저장소 시작 주소 */
    // pool_block_t *free_head;  /* free list 머리 (NULL = 고갈) */
    // uint32_t      capacity;   /* 블록 총 개수 */
    // uint32_t      used;       /* 나가 있는 블록 수 */
    // uint32_t      high_water; /* used의 최댓값 (pool 크기 근거용) */
    // uint32_t      in_use[POOL_BITMAP_WORDS];  /* 블록당 1bit (double-free 감지) */
} pool_t;

#define POOL_STORAGE(name, nblocks) static pool_block_t name[(nblocks)]

/* ================================================================== */
/* 3. Critical section (host 테스트용 no-op)                          */
/* ================================================================== */

#ifndef CRIT_ENTER
#define CRIT_ENTER() uint32_t _pm = 0u
#define CRIT_EXIT()  ((void)_pm)
#endif

/* ================================================================== */
/* 4. Bitmap 도우미 — TODO: 구현                                       */
/* ================================================================== */

static inline void pool_bitmap_set(pool_t *p, uint32_t idx)
{
    // TODO: p->in_use[idx >> 5] |= (uint32_t)1u << (idx & 31u);
    //       idx번 비트를 1로 설정
}

static inline void pool_bitmap_clear(pool_t *p, uint32_t idx)
{
    // TODO: p->in_use[idx >> 5] &= ~((uint32_t)1u << (idx & 31u));
    //       idx번 비트를 0으로 설정
}

static inline bool pool_bitmap_test(const pool_t *p, uint32_t idx)
{
    // TODO: return (p->in_use[idx >> 5] & ((uint32_t)1u << (idx & 31u))) != 0u;
    //       idx번 비트가 1인지 확인
}

/* ================================================================== */
/* 5. Pool 초기화 — TODO: 구현                                         */
/* ================================================================== */

void pool_init(pool_t *p, pool_block_t *storage, uint32_t nblocks)
{
    // TODO: 구현하세요.
    // 1. p->blocks = storage
    // 2. p->capacity = nblocks
    // 3. p->used = 0, p->high_water = 0
    // 4. in_use를 모두 0으로 초기화
    // 5. free list를 만든다: storage[0] -> storage[1] -> ... -> storage[nblocks-1] -> NULL
    //    for (uint32_t i = 0; i < nblocks - 1; i++) {
    //        storage[i].next = &storage[i + 1];
    //    }
    //    storage[nblocks - 1].next = NULL;
    // 6. free_head = &storage[0]
}

/* ================================================================== */
/* 6. Alloc — TODO: 구현                                               */
/* ================================================================== */

void *pool_alloc(pool_t *p)
{
    // TODO: 구현하세요.
    // 1. CRIT_ENTER() / CRIT_EXIT()로 critical section 감싼다.
    // 2. free_head == NULL이면 NULL 반환 (고갈)
    // 3. block = free_head에서 머리를 뺀다.
    // 4. 다음 free 블록으로 free_head 업데이트: free_head = block->next
    // 5. 블록 인덱스를 구한다: idx = block - p->blocks
    // 6. bitmap에 idx를 set (사용 중 표시)
    // 7. p->used++, high_water 업데이트
    // 8. 블록 포인터를 void*로 반환
}

/* ================================================================== */
/* 7. Free — TODO: 구현                                                */
/* ================================================================== */

int pool_free(pool_t *p, void *ptr)
{
    // TODO: 구현하세요.
    // 1. ptr == NULL이면 0 반환 (no-op)
    // 2. CRIT_ENTER() / CRIT_EXIT()로 critical section 감싼다.
    // 3. 블록 인덱스를 구한다: idx = (pool_block_t*)ptr - p->blocks
    // 4. 범위 체크: idx >= p->capacity이면 POOL_ERR_FOREIGN
    // 5. 블록이 실제로 pool storage 안에 있는지 확인:
    //    ((pool_block_t*)ptr - p->blocks) * sizeof(pool_block_t) 위치인지 검증
    // 6. bitmap 체크: idx가 set되지 않았으면 POOL_ERR_DOUBLE
    // 7. bitmap에서 idx를 clear (사용 중 아님)
    // 8. 블록을 free_head 앞에 push:
    //    ((pool_block_t*)ptr)->next = p->free_head;
    //    p->free_head = (pool_block_t*)ptr;
    // 9. p->used--
    // 10. 0 반환
}

/* ================================================================== */
/* 8. 관찰 함수 — TODO: 구현                                            */
/* ================================================================== */

uint32_t pool_used_count(const pool_t *p)
{
    // TODO: return p->used;
}

uint32_t pool_available(const pool_t *p)
{
    // TODO: return p->capacity - p->used;
}

uint32_t pool_high_water(const pool_t *p)
{
    // TODO: return p->high_water;
}

/* ================================================================== */
/* 9. 테스트                                                          */
/* ================================================================== */

#define OK(msg) printf("  ok  %s\n", (msg))

POOL_STORAGE(pool_storage, 8);  /* 8개 블록의 풀 */

static void test_init_empty(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    assert(pool_used_count(&pool) == 0u);
    assert(pool_available(&pool) == 8u);
    assert(pool_high_water(&pool) == 0u);

    OK("init: empty, 8 available");
}

static void test_alloc_free_single(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    void *p = pool_alloc(&pool);
    assert(p != NULL);
    assert(pool_used_count(&pool) == 1u);
    assert(pool_available(&pool) == 7u);

    int ret = pool_free(&pool, p);
    assert(ret == POOL_OK);
    assert(pool_used_count(&pool) == 0u);
    assert(pool_available(&pool) == 8u);

    OK("alloc 1 -> used=1 -> free -> used=0");
}

static void test_exhaust_pool(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    void *ptrs[8];
    for (int i = 0; i < 8; i++) {
        ptrs[i] = pool_alloc(&pool);
        assert(ptrs[i] != NULL);
    }
    assert(pool_used_count(&pool) == 8u);
    assert(pool_available(&pool) == 0u);

    void *p9 = pool_alloc(&pool);
    assert(p9 == NULL);  /* 고갈 */

    int ret = pool_free(&pool, ptrs[0]);
    assert(ret == POOL_OK);
    assert(pool_used_count(&pool) == 7u);

    void *p9_retry = pool_alloc(&pool);
    assert(p9_retry != NULL);

    OK("exhaustion: 8개 할당 후 고갈, 해제 후 다시 가능");
}

static void test_high_water(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    void *p1 = pool_alloc(&pool);
    void *p2 = pool_alloc(&pool);
    void *p3 = pool_alloc(&pool);

    assert(pool_high_water(&pool) == 3u);

    pool_free(&pool, p1);
    pool_free(&pool, p2);
    pool_free(&pool, p3);

    assert(pool_high_water(&pool) == 3u);  /* 역대 최댓값 유지 */

    OK("high_water mark: 3으로 최대, free 후에도 유지");
}

static void test_free_null(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    int ret = pool_free(&pool, NULL);
    assert(ret == POOL_OK);  /* null free는 no-op */

    OK("free(NULL) -> OK (no-op)");
}

static void test_free_foreign_pointer(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    int dummy_var = 42;
    int ret = pool_free(&pool, &dummy_var);
    assert(ret == POOL_ERR_FOREIGN);  /* 풀 밖의 포인터 */

    OK("free(foreign ptr) -> ERR_FOREIGN");
}

static void test_double_free(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    void *p = pool_alloc(&pool);
    assert(pool_free(&pool, p) == POOL_OK);
    int ret = pool_free(&pool, p);
    assert(ret == POOL_ERR_DOUBLE);  /* double-free 감지 */

    OK("double-free detection");
}

static void test_fifo_order(void)
{
    pool_t pool;
    pool_init(&pool, pool_storage, 8);

    void *p1 = pool_alloc(&pool);
    void *p2 = pool_alloc(&pool);
    void *p3 = pool_alloc(&pool);

    pool_free(&pool, p1);
    pool_free(&pool, p2);
    pool_free(&pool, p3);

    /* LIFO 순서로 돌아온다 (stack처럼 동작) */
    void *q3 = pool_alloc(&pool);
    void *q2 = pool_alloc(&pool);
    void *q1 = pool_alloc(&pool);

    assert(q3 == p3);
    assert(q2 == p2);
    assert(q1 == p1);

    OK("alloc/free LIFO 순서: 스택처럼 동작");
}

int main(void)
{
    printf("=== 04 메모리 풀 ===\n");
    test_init_empty();
    test_alloc_free_single();
    test_exhaust_pool();
    test_high_water();
    test_free_null();
    test_free_foreign_pointer();
    test_double_free();
    test_fifo_order();
    printf("ALL TESTS PASSED\n");
    return 0;
}
