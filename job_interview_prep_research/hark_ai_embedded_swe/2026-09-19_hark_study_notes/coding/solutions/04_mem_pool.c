/* 04_mem_pool.c — 고정 크기 블록 memory pool allocator (모범답안)
 *
 *   cc -std=c11 -Wall -Wextra -O2 solutions/04_mem_pool.c -o sol_04 && ./sol_04
 *
 * 핵심 아이디어
 *   - 저장소는 caller가 준 정적 배열 하나. 내부에서 malloc을 절대 부르지 않는다.
 *   - 그 배열을 같은 크기 블록으로 자르고, "비어 있는 블록" 안에 다음 빈 블록의
 *     주소를 적어 둔다(intrusive free list). 블록당 메타데이터 오버헤드 0바이트.
 *   - alloc = 리스트 머리 pop, free = 머리 push. 둘 다 O(1), 분기 예측 가능,
 *     실행 시간이 pool 상태와 무관하다 → ISR에서 불러도 안전한 지연시간.
 *   - free list 머리(free_head)와 카운터는 ISR/task가 공유하므로 짧은 critical
 *     section으로 감싼다. host 테스트에서는 no-op이다(아래 CRIT_ENTER 참고).
 */

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 설정 상수                                                        */
/* ------------------------------------------------------------------ */

/* 블록 한 칸의 바이트 수. 예: BLE 패킷 한 개, 오디오 프레임 한 개, 이벤트 메시지. */
#define POOL_BLOCK_SIZE 64u

/* 이 구현이 지원하는 최대 블록 수. double-free 감지용 bitmap 크기를 컴파일
 * 타임에 고정하기 위한 상한이다. 늘리고 싶으면 이 값만 키우면 된다. */
#define POOL_MAX_BLOCKS 64u

/* pool_free() 반환값 */
#define POOL_OK 0          /* 정상 반납 (또는 ptr == NULL 인 no-op) */
#define POOL_ERR_FOREIGN (-1)  /* 이 pool의 블록 시작 주소가 아님 */
#define POOL_ERR_DOUBLE (-2)   /* 이미 free 상태인 블록을 또 반납 */

/* ------------------------------------------------------------------ */
/* 2. 블록 타입 — union으로 "두 얼굴"을 표현한다                        */
/* ------------------------------------------------------------------ */

/* free일 때는 next 링크, 사용 중일 때는 payload.
 * max_align_t 멤버를 넣어 union 전체의 alignment를 "어떤 타입이든 담을 수
 * 있는" 최대 정렬로 끌어올린다. 이게 있어야 caller가 이 블록을 double이나
 * uint64_t 배열로 캐스팅해도 정렬 위반이 없다. */
typedef union pool_block {
    union pool_block *next;              /* free 상태: 다음 free 블록 */
    max_align_t       align_dummy;       /* 정렬 강제용. 읽거나 쓰지 않는다 */
    unsigned char     data[POOL_BLOCK_SIZE]; /* 사용 중: caller의 payload */
} pool_block_t;

/* 블록 안에 최소한 포인터 하나는 들어가야 free list를 엮을 수 있다. */
_Static_assert(POOL_BLOCK_SIZE >= sizeof(void *),
               "block too small to hold a free-list link");
/* 블록 크기가 정렬의 배수가 아니면 blocks[1]부터 정렬이 깨진다. */
_Static_assert(POOL_BLOCK_SIZE % alignof(max_align_t) == 0u,
               "block size must be a multiple of max alignment");
/* union에 padding이 붙어 sizeof가 커지면 위 계산이 전부 틀어진다. */
_Static_assert(sizeof(pool_block_t) == POOL_BLOCK_SIZE,
               "union must not add padding");

/* ------------------------------------------------------------------ */
/* 3. pool 서술자                                                      */
/* ------------------------------------------------------------------ */

#define POOL_BITMAP_WORDS ((POOL_MAX_BLOCKS + 31u) / 32u)

typedef struct {
    pool_block_t *blocks;      /* caller가 준 저장소의 시작 주소 */
    pool_block_t *free_head;   /* free list의 머리. NULL이면 고갈 */
    uint32_t      capacity;    /* 블록 총 개수 */
    uint32_t      used;        /* 현재 나가 있는 블록 수 */
    uint32_t      high_water;  /* used의 역대 최댓값 — pool 크기 근거 */
    uint32_t      in_use[POOL_BITMAP_WORDS]; /* 블록당 1bit: 1=사용 중 */
} pool_t;

/* caller가 저장소를 선언할 때 쓰는 매크로. 정렬과 크기를 한 번에 맞춘다. */
#define POOL_STORAGE(name, nblocks) static pool_block_t name[(nblocks)]

/* ------------------------------------------------------------------ */
/* 4. critical section                                                 */
/* ------------------------------------------------------------------ */

/* 타깃(Cortex-M)에서는 다음처럼 정의한다 — CMSIS 이름.
 *     #define CRIT_ENTER() uint32_t _pm = __get_PRIMASK(); __disable_irq()
 *     #define CRIT_EXIT()  __set_PRIMASK(_pm)
 * 저장/복원 형태라 이미 인터럽트가 꺼진 문맥에서 불러도 안전하다.
 * host 테스트에서는 하드웨어 헤더를 쓸 수 없으므로 no-op으로 둔다. */
#ifndef CRIT_ENTER
#define CRIT_ENTER() uint32_t _pm = 0u
#define CRIT_EXIT()  ((void)_pm)
#endif

/* ------------------------------------------------------------------ */
/* 5. bitmap 도우미                                                    */
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
/* 6. 공개 API                                                         */
/* ------------------------------------------------------------------ */

/* 저장소를 블록으로 자르고 전부 free list에 엮는다.
 * 반환: 0 성공, -1 인자 이상(NULL / 정렬 위반 / 블록 0개 / 상한 초과) */
int pool_init(pool_t *p, void *storage, size_t storage_bytes)
{
    if (p == NULL || storage == NULL) {
        return -1;
    }
    /* caller가 char 배열을 그냥 넘기면 정렬이 안 맞을 수 있다. 거부한다. */
    if (((uintptr_t)storage % alignof(max_align_t)) != 0u) {
        return -1;
    }

    size_t n = storage_bytes / sizeof(pool_block_t);
    if (n == 0u || n > POOL_MAX_BLOCKS) {
        return -1;                       /* 한 블록도 못 만들거나 상한 초과 */
    }

    pool_block_t *b = (pool_block_t *)storage;
    /* i번 블록이 i+1번을 가리키도록 사슬을 엮고, 마지막은 NULL로 끝낸다. */
    for (size_t i = 0u; i + 1u < n; i++) {
        b[i].next = &b[i + 1u];
    }
    b[n - 1u].next = NULL;

    p->blocks     = b;
    p->free_head  = &b[0];
    p->capacity   = (uint32_t)n;
    p->used       = 0u;
    p->high_water = 0u;
    memset(p->in_use, 0, sizeof p->in_use);
    return 0;
}

/* 블록 하나를 꺼낸다. O(1). 고갈이면 NULL. */
void *pool_alloc(pool_t *p)
{
    if (p == NULL) {
        return NULL;
    }

    CRIT_ENTER();
    pool_block_t *b = p->free_head;
    if (b != NULL) {
        p->free_head = b->next;          /* 머리를 다음 칸으로 옮긴다 */
        uint32_t idx = (uint32_t)(b - p->blocks);
        pool_bitmap_set(p, idx);
        p->used++;
        if (p->used > p->high_water) {
            p->high_water = p->used;
        }
    }
    CRIT_EXIT();

    /* b->next는 caller의 payload 영역이므로 지워 줄 필요는 없다.
     * 다만 이전 내용이 남아 있으니 caller가 초기화 없이 읽으면 안 된다. */
    return b;
}

/* 이 포인터가 이 pool의 유효한 블록 시작 주소인가? 부수효과 없음. */
int pool_owns(const pool_t *p, const void *ptr)
{
    if (p == NULL || ptr == NULL || p->blocks == NULL) {
        return 0;
    }
    uintptr_t base = (uintptr_t)p->blocks;
    uintptr_t end  = base + (uintptr_t)p->capacity * sizeof(pool_block_t);
    uintptr_t a    = (uintptr_t)ptr;

    if (a < base || a >= end) {
        return 0;                        /* 범위 밖 = 남의 메모리 */
    }
    if (((a - base) % sizeof(pool_block_t)) != 0u) {
        return 0;                        /* 블록 중간을 가리킴 */
    }
    return 1;
}

/* 블록 하나를 되돌린다. O(1).
 * 반환: 0 성공(ptr==NULL은 no-op 성공), -1 외부 포인터, -2 double free */
int pool_free(pool_t *p, void *ptr)
{
    if (p == NULL) {
        return POOL_ERR_FOREIGN;
    }
    if (ptr == NULL) {
        return POOL_OK;                  /* free(NULL) 관례와 동일 */
    }
    if (!pool_owns(p, ptr)) {
        return POOL_ERR_FOREIGN;
    }

    pool_block_t *b   = (pool_block_t *)ptr;
    uint32_t      idx = (uint32_t)(b - p->blocks);
    int           rc;

    CRIT_ENTER();
    if (!pool_bitmap_test(p, idx)) {
        /* 이미 free list에 들어 있는 블록. 여기서 막지 않으면 free list에
         * 같은 블록이 두 번 실려 cycle이 생기고, 나중에 서로 다른 두 caller가
         * 같은 블록을 받아 쓴다 — 추적 불가능한 데이터 손상. */
        rc = POOL_ERR_DOUBLE;
    } else {
        pool_bitmap_clear(p, idx);
        b->next      = p->free_head;     /* 지금 머리를 내 next로 */
        p->free_head = b;                /* 내가 새 머리 */
        p->used--;
        rc = POOL_OK;
    }
    CRIT_EXIT();
    return rc;
}

/* 용량 회계 — ISR에서도 부를 수 있도록 단순 읽기만 한다. */
uint32_t pool_capacity(const pool_t *p)   { return p->capacity; }
uint32_t pool_used(const pool_t *p)       { return p->used; }
uint32_t pool_available(const pool_t *p)  { return p->capacity - p->used; }
uint32_t pool_high_water(const pool_t *p) { return p->high_water; }

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
