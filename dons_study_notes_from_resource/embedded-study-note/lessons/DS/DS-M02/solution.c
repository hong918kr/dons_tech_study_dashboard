/* DS-M02 고정 크기 메모리 풀: O(1) 할당/해제
 * 호출자가 준 버퍼를 같은 크기 블록으로 자르고, 빈 블록 안에 "다음 빈 블록" 포인터를 넣어
 * free list 를 만든다 (블록당 메타데이터 0 바이트). 디버그 모드: 범위 밖/내부 포인터·이중 해제 검출.
 * 통계: 사용 중 개수, 하이워터마크, 할당 실패 수. malloc 없음.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef POOL_DEBUG
#define POOL_DEBUG 1                         /* -DPOOL_DEBUG=0 이면 검사 코드가 사라진다 */
#endif
#define POOL_ALIGN      ((size_t)_Alignof(max_align_t))   /* 이 맥 arm64: 8, x86-64 Linux: 16 */
#define POOL_MAX_BLOCKS 1024u

typedef enum { POOL_OK = 0, POOL_E_RANGE, POOL_E_INTERIOR, POOL_E_DOUBLE } pool_err_t;  /* DOUBLE = 이미 free 상태 */

typedef struct {
    uint8_t  *base;                          /* 블록 0 의 주소 */
    size_t    stride;                        /* 블록 간격 (정렬·포인터 크기 반영) */
    uint32_t  nblocks;
    void     *free_head;                     /* free list 머리, NULL = 풀 비었음 */
    uint32_t  used, hwm, fails;              /* 사용 중 / 최대 사용 / 할당 실패 횟수 */
#if POOL_DEBUG
    uint32_t  live[POOL_MAX_BLOCKS / 32u];   /* 비트 i = 1: 블록 i 할당됨 (DS-B08) */
    uint32_t  bad_frees;
#endif
} pool_t;

/* 빈 블록의 첫 sizeof(void*) 바이트 = 다음 빈 블록 주소.
 * memcpy 로 읽고 쓴다: uint8_t 배열을 void* 로 직접 역참조하면 유효 타입 규칙(C11 6.5p7) 위반.
 * -O2 에서는 load/store 한 개로 컴파일된다. */
static void *blk_next(const void *blk) { void *n; memcpy(&n, blk, sizeof n); return n; }
static void blk_set_next(void *blk, void *n) { memcpy(blk, &n, sizeof n); }
static bool live_test(const pool_t *p, uint32_t i);
static void live_set(pool_t *p, uint32_t i, bool on);

/* ---------------- Part A: stride / init / alloc / push ---------------- */
/* 블록 간격 = max(block_size, sizeof(void*)) 를 POOL_ALIGN 의 배수로 올림. block_size==0 이나 넘침이면 0 */
static size_t pool_stride(size_t block_size) {
    if (block_size == 0 || block_size > SIZE_MAX - POOL_ALIGN) return 0;
    size_t s = block_size < sizeof(void *) ? sizeof(void *) : block_size;
    return (s + POOL_ALIGN - 1u) & ~(POOL_ALIGN - 1u);
}

/* buf 를 stride 단위로 잘라 블록 0 → 1 → … → n-1 → NULL 순서의 free list 를 만든다 */
static bool pool_init(pool_t *p, void *buf, size_t buf_bytes, size_t block_size) {
    size_t stride = pool_stride(block_size);
    if (p == NULL || buf == NULL || stride == 0) return false;
    if ((uintptr_t)buf % POOL_ALIGN != 0) return false;          /* 정렬 안 된 버퍼 거절 */
    size_t n = buf_bytes / stride;
    if (n == 0 || n > POOL_MAX_BLOCKS) return false;
    memset(p, 0, sizeof *p);
    p->base = buf;
    p->stride = stride;
    p->nblocks = (uint32_t)n;
    for (size_t i = n; i-- > 0;) {                               /* 뒤에서부터 머리에 push */
        uint8_t *b = p->base + i * stride;
        blk_set_next(b, p->free_head);
        p->free_head = b;
    }
    return true;
}

/* free list 머리에서 하나 꺼낸다. 비었으면 fails++ 후 NULL */
static void *pool_alloc(pool_t *p) {
    void *b = p->free_head;
    if (b == NULL) { p->fails++; return NULL; }
    p->free_head = blk_next(b);
    p->used++;
    if (p->used > p->hwm) p->hwm = p->used;
#if POOL_DEBUG
    live_set(p, (uint32_t)(((uintptr_t)b - (uintptr_t)p->base) / p->stride), true);
#endif
    return b;
}

/* 검사가 끝난 블록을 free list 머리에 넣는다 (LIFO) */
static void pool_push(pool_t *p, void *blk) {
    blk_set_next(blk, p->free_head);
    p->free_head = blk;
    p->used--;
}

/* ---------------- Part B: 디버그 검사 + 통계 ---------------- */
/* ptr 이 이 풀의 "할당된 블록의 시작 주소" 인지 판정하고 블록 번호를 *idx 에 */
static pool_err_t pool_validate(const pool_t *p, const void *ptr, uint32_t *idx) {
    uintptr_t a = (uintptr_t)ptr, b = (uintptr_t)p->base;
    uintptr_t span = (uintptr_t)p->nblocks * p->stride;
    if (a < b || a - b >= span) return POOL_E_RANGE;             /* 다른 객체끼리 < 비교는 UB → 정수로 */
    uintptr_t off = a - b;
    if (off % p->stride != 0) return POOL_E_INTERIOR;            /* 블록 중간을 가리킴 */
    *idx = (uint32_t)(off / p->stride);
    if (!live_test(p, *idx)) return POOL_E_DOUBLE;               /* 이미 free 상태 */
    return POOL_OK;
}

/* 하이워터마크를 지금 사용량으로 되돌린다 (측정 구간 시작) */
static void pool_reset_hwm(pool_t *p) {
    p->hwm = p->used;
}

/* (주어짐) free(NULL) 은 무해. 디버그 모드에선 검사 실패 시 아무것도 바꾸지 않고 오류 코드 */
static pool_err_t pool_free(pool_t *p, void *ptr) {
    if (ptr == NULL) return POOL_OK;
#if POOL_DEBUG
    uint32_t idx = 0;
    pool_err_t e = pool_validate(p, ptr, &idx);
    if (e != POOL_OK) { p->bad_frees++; return e; }
    live_set(p, idx, false);
#endif
    pool_push(p, ptr);
    return POOL_OK;
}

/* ---------------- Part C: ISR → 링 → 태스크 프레임 파이프라인 ---------------- */
#define FRAME_PAYLOAD 60u
typedef struct { uint16_t len, seq; uint8_t data[FRAME_PAYLOAD]; } frame_t;   /* 64 B */

#define RING_N 64u                                    /* 2의 거듭제곱, 자유 증가 인덱스 (DS-B04) */
static frame_t *g_ring[RING_N];
static uint32_t g_rhead, g_rtail;                     /* head: ISR 만 씀, tail: 태스크만 씀 */
static uint32_t g_drops, g_done, g_corrupt, g_last_seq;
static uint32_t g_crit_depth, g_crit_entries;

/* 호스트에선 깊이만 센다. Cortex-M: pm = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm); */
static void crit_enter(void) { g_crit_depth++; g_crit_entries++; }
static void crit_exit(void)  { g_crit_depth--; }

static uint8_t frame_byte(uint16_t seq, uint32_t i) { return (uint8_t)(seq * 31u + i); }

/* 수신 ISR: 프레임 버퍼를 풀에서 받아 채우고 링에 넣는다. 풀이나 링이 차면 버리고 g_drops++ */
static bool rx_isr(pool_t *p, uint16_t seq, uint16_t len) {
    frame_t *f = pool_alloc(p);                       /* ISR 안: 태스크가 끼어들 수 없음 */
    if (f == NULL) { g_drops++; return false; }
    f->seq = seq;
    f->len = len;
    for (uint32_t i = 0; i < len; i++) f->data[i] = frame_byte(seq, i);
    if (g_rhead - g_rtail == RING_N) { pool_free(p, f); g_drops++; return false; }
    g_ring[g_rhead % RING_N] = f;
    g_rhead++;
    return true;
}

/* 처리 태스크: 최대 budget 개를 꺼내 내용 검증 후 반납. ISR 과 풀을 공유하므로 free 는 임계 구역에서 */
static uint32_t rx_task(pool_t *p, uint32_t budget) {
    uint32_t n = 0;
    while (n < budget && g_rtail != g_rhead) {
        frame_t *f = g_ring[g_rtail % RING_N];
        g_rtail++;
        bool ok = f->len <= FRAME_PAYLOAD && (g_done == 0 || f->seq > g_last_seq);
        for (uint32_t i = 0; ok && i < f->len; i++) ok = f->data[i] == frame_byte(f->seq, i);
        if (!ok) g_corrupt++;
        g_last_seq = f->seq;
        g_done++;
        crit_enter();
        pool_err_t e = pool_free(p, f);
        crit_exit();
        if (e != POOL_OK) g_corrupt++;
        n++;
    }
    return n;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
static bool live_test(const pool_t *p, uint32_t i) {
#if POOL_DEBUG
    return (p->live[i / 32u] >> (i % 32u)) & 1u;
#else
    (void)p; (void)i; return true;
#endif
}
static void live_set(pool_t *p, uint32_t i, bool on) {
#if POOL_DEBUG
    if (on) p->live[i / 32u] |= 1u << (i % 32u);
    else    p->live[i / 32u] &= ~(1u << (i % 32u));
#else
    (void)p; (void)i; (void)on;
#endif
}

/* 불변식: free list 의 모든 노드는 범위 안·블록 시작·중복 없음·free 상태,
 * (free 개수) + used == nblocks, used <= hwm <= nblocks, 디버그 비트 수 == used.
 * 고장 난 free list(사이클, 쓰레기 포인터)에서도 범위 밖 주소는 읽지 않고 끝난다. */
static bool pool_check(const pool_t *p) {
    if (p->base == NULL) return p->nblocks == 0 && p->free_head == NULL;
    uint32_t seen[POOL_MAX_BLOCKS / 32u] = { 0 };
    uint32_t nfree = 0;
    uintptr_t b0 = (uintptr_t)p->base, span = (uintptr_t)p->nblocks * p->stride;
    for (const void *b = p->free_head; b != NULL; b = blk_next(b)) {
        uintptr_t a = (uintptr_t)b;
        if (a < b0 || a - b0 >= span || (a - b0) % p->stride != 0) return false;
        uint32_t i = (uint32_t)((a - b0) / p->stride);
        if ((seen[i / 32u] >> (i % 32u)) & 1u) return false;      /* 사이클 / 중복 */
        seen[i / 32u] |= 1u << (i % 32u);
#if POOL_DEBUG
        if (live_test(p, i)) return false;                         /* free 인데 live 표시 */
#endif
        if (++nfree > p->nblocks) return false;
    }
    if (nfree + p->used != p->nblocks) return false;
    if (p->hwm < p->used || p->hwm > p->nblocks) return false;
#if POOL_DEBUG
    uint32_t bits = 0;
    for (uint32_t w = 0; w < POOL_MAX_BLOCKS / 32u; w++) bits += (uint32_t)__builtin_popcount(p->live[w]);
    if (bits != p->used) return false;
#endif
    return true;
}

/* 참조용: 느리지만 뻔한 stride 계산 */
static size_t ref_stride(size_t s) {
    size_t x = s < sizeof(void *) ? sizeof(void *) : s;
    while (x % POOL_ALIGN != 0) x++;
    return x;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* Part C 시뮬레이션: 1 tick = ISR 도착 0~n개 + 태스크 1회. 500 tick 마다 DMA 배치 18개,
 * 1000 tick 마다 태스크가 플래시 erase 로 12 tick 멈춘다. */
typedef struct { uint32_t gen, drops, done, hwm, corrupt, left; } sim_t;
static sim_t run_pipeline(pool_t *p, void *buf, size_t buf_bytes, uint32_t ticks) {
    sim_t s = { 0, 0, 0, 0, 0, 0 };
    g_rhead = g_rtail = 0; g_drops = g_done = g_corrupt = g_last_seq = 0;
    if (!pool_init(p, buf, buf_bytes, sizeof(frame_t))) return s;
    rng_state = 2024u;
    uint16_t seq = 0;
    for (uint32_t t = 0; t < ticks; t++) {
        uint32_t r = xorshift32();
        uint32_t arrivals = (r & 7u) == 0 ? 2u : (r & 7u) < 4u ? 1u : 0u;   /* 평균 0.625 개/tick */
        if (t % 500u == 250u) arrivals += 18u;                         /* 버스트 */
        for (uint32_t k = 0; k < arrivals; k++) {
            s.gen++;
            rx_isr(p, ++seq, (uint16_t)(8u + (xorshift32() % (FRAME_PAYLOAD - 7u))));
        }
        bool stalled = (t % 1000u) >= 600u && (t % 1000u) < 612u;    /* erase 중 */
        if (!stalled) rx_task(p, 1u);
    }
    while (rx_task(p, 1000u) != 0) { }                                 /* 남은 것 처리 */
    s.drops = g_drops; s.done = g_done; s.hwm = p->hwm; s.corrupt = g_corrupt; s.left = p->used;
    return s;
}

static _Alignas(max_align_t) uint8_t g_buf[16 + 1024];   /* 앞 16 바이트는 "풀 앞 주소" 테스트용 */
static _Alignas(max_align_t) uint8_t g_big[POOL_MAX_BLOCKS * 64u];
static void   *g_held[POOL_MAX_BLOCKS];
static uint8_t g_tag[POOL_MAX_BLOCKS];
static bool    g_shadow[POOL_MAX_BLOCKS];
static pool_t  g_p, g_q;

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    pool_t *p = &g_p;
    uint8_t *buf = g_buf + 16;                       /* POOL_ALIGN 정렬 유지 */
    const size_t BS = 20, BUFN = 1024;               /* 20 B 블록 → stride 24 (이 맥) */

    /* Part A: stride 규칙 — 1..200 바이트 전부 참조 계산과 일치 */
    {
        bool ok = pool_stride(0) == 0 && pool_stride(SIZE_MAX) == 0;
        for (size_t s = 1; s <= 200; s++) {
            size_t st = pool_stride(s);
            ok = ok && st == ref_stride(s) && st % POOL_ALIGN == 0 && st >= s && st >= sizeof(void *);
        }
        CHECK(ok);
        printf("[A] POOL_ALIGN=%zu sizeof(void*)=%zu | size->stride:", POOL_ALIGN, sizeof(void *));
        static const size_t sz[] = { 1, 7, 8, 9, 20, 24, 33, 60, 64 };
        for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) printf(" %zu->%zu", sz[i], pool_stride(sz[i]));
        printf("\n");
    }
    /* Part A: init 거절 조건 */
    {
        CHECK(!pool_init(p, NULL, BUFN, BS));
        CHECK(!pool_init(p, buf, BUFN, 0));
        CHECK(!pool_init(p, buf + 1, BUFN - 1, BS));                     /* 정렬 안 됨 */
        CHECK(!pool_init(p, buf, pool_stride(BS) - 1u, BS));             /* 블록 0개 */
        CHECK(!pool_init(&g_q, g_big, sizeof g_big, 1));                 /* 8192 블록 > 최대 */
    }
    /* Part A: 전부 할당 — 정렬·범위·순서·중복 없음, 매번 불변식 */
    {
        CHECK(pool_init(p, buf, BUFN, BS) && p->nblocks == BUFN / 24u && p->stride == 24u);
        CHECK(pool_check(p) && p->used == 0);
        bool ok = true;
        for (uint32_t i = 0; i < p->nblocks; i++) {
            uint8_t *b = pool_alloc(p);
            ok = ok && b == buf + (size_t)i * p->stride                         /* 0,1,2… 순서 */
                    && (uintptr_t)b % _Alignof(max_align_t) == 0 && pool_check(p);
            g_held[i] = b;
        }
        CHECK(ok && p->nblocks > 0 && p->used == p->nblocks);
        CHECK(pool_alloc(p) == NULL && p->fails == 1 && pool_check(p));   /* 43번째 실패 */
        /* 각 블록에 BS 바이트를 꽉 채워도 이웃을 덮지 않는다 */
        for (uint32_t i = 0; i < p->nblocks; i++) if (g_held[i]) memset(g_held[i], (int)(i + 1u), BS);
        ok = p->nblocks > 0;
        for (uint32_t i = 0; i < p->nblocks; i++)
            for (size_t k = 0; k < BS; k++) ok = ok && g_held[i] && ((uint8_t *)g_held[i])[k] == (uint8_t)(i + 1u);
        CHECK(ok);
        /* LIFO: 5, 9 반납 → 9, 5 순으로 다시 나옴 */
        CHECK(pool_free(p, g_held[5]) == POOL_OK && pool_free(p, g_held[9]) == POOL_OK && p->used == p->nblocks - 2u);
        CHECK(pool_check(p) && g_held[9] != NULL && pool_alloc(p) == g_held[9] && pool_alloc(p) == g_held[5] && pool_check(p));
    }

    /* Part B: 잘못된 free — 상태를 바꾸지 않고 오류 코드 */
    {
        pool_init(p, buf, BUFN, BS);
        uint8_t *a = pool_alloc(p), *b = pool_alloc(p);
        if (a == NULL || b == NULL) { a = buf; b = buf + 24; }          /* 미완성 starter 에서 NULL 연산 방지 */
        int local = 0;
        CHECK(pool_free(p, NULL) == POOL_OK && p->used == 2);
        CHECK(pool_free(p, a) == POOL_OK && p->used == 1);
        CHECK(pool_free(p, a) == POOL_E_DOUBLE && p->used == 1 && pool_check(p));     /* 이중 해제 */
        CHECK(pool_free(p, b + 4) == POOL_E_INTERIOR && p->used == 1);               /* 블록 중간 */
        CHECK(pool_free(p, buf - 1) == POOL_E_RANGE);                                /* 풀 바로 앞 */
        CHECK(pool_free(p, buf + (size_t)p->nblocks * p->stride) == POOL_E_RANGE);   /* 블록 끝 너머 (꼬리 16 B) */
        CHECK(pool_free(p, &local) == POOL_E_RANGE);                                 /* 스택 변수 */
        CHECK(pool_free(p, buf + 2u * p->stride) == POOL_E_DOUBLE);                  /* 한 번도 할당 안 됨 */
        CHECK(p->bad_frees == 6 && p->used == 1 && pool_check(p));
        CHECK(p->nblocks > 0 && pool_free(p, b) == POOL_OK && p->used == 0 && pool_check(p));
    }
    /* Part B: 하이워터마크 — 10 할당, 7 반납, 2 할당 → used 5, hwm 10 → reset → 5 */
    {
        pool_init(p, buf, BUFN, BS);
        for (int i = 0; i < 10; i++) g_held[i] = pool_alloc(p);
        for (int i = 0; i < 7; i++) pool_free(p, g_held[i]);
        pool_alloc(p); pool_alloc(p);
        CHECK(p->used == 5 && p->hwm == 10 && pool_check(p));
        pool_reset_hwm(p);
        CHECK(p->hwm == 5 && pool_check(p));
        pool_alloc(p);
        CHECK(p->hwm == 6);
    }
    /* Part B: 랜덤 스트레스 200000 연산 — 참조 모델(블록별 shadow) 대조 + 내용 무결성 + 잘못된 free 주입 */
    {
        bool ok = pool_init(p, buf, BUFN, BS);
        memset(g_shadow, 0, sizeof g_shadow);
        uint32_t nheld = 0, ref_hwm = 0, bad_injected = 0, ref_fails = 0;
        void *last_freed = NULL;
        rng_state = 0xC0FFEEu;
        for (int op = 0; op < 200000 && ok; op++) {
            uint32_t r = xorshift32();
            if (r % 64u == 0 && last_freed != NULL && !g_shadow[((uint8_t *)last_freed - buf) / 24]) {
                ok = pool_free(p, last_freed) == POOL_E_DOUBLE;                      /* 이중 해제 주입 */
                bad_injected++;
            } else if (r % 64u == 1 && nheld > 0) {
                ok = pool_free(p, (uint8_t *)g_held[(r >> 8) % nheld] + 1 + (r >> 20) % 23u) == POOL_E_INTERIOR;
                bad_injected++;
            } else if ((r >> 6) % 100u < 52u) {                                       /* 52% 할당 */
                uint8_t *b = pool_alloc(p);
                if (nheld == p->nblocks) { ok = b == NULL; ref_fails++; }           /* 가득 참 → NULL */
                else if (b == NULL || (size_t)(b - buf) % 24u != 0) ok = false;
                else {
                    uint32_t i = (uint32_t)((size_t)(b - buf) / 24u);
                    ok = !g_shadow[i];                                                /* 살아있는 블록 재배포 X */
                    g_shadow[i] = true;
                    g_tag[i] = (uint8_t)(r >> 24);
                    memset(b, g_tag[i], BS);
                    g_held[nheld++] = b;
                    if (nheld > ref_hwm) ref_hwm = nheld;
                }
            } else if (nheld > 0) {
                uint32_t k = (r >> 8) % nheld;
                uint8_t *b = g_held[k];
                uint32_t i = (uint32_t)((size_t)(b - buf) / 24u);
                for (size_t j = 0; j < BS; j++) ok = ok && b[j] == g_tag[i];         /* 남이 덮어쓰지 않았나 */
                ok = ok && pool_free(p, b) == POOL_OK;
                g_shadow[i] = false;
                g_held[k] = g_held[--nheld];
                last_freed = b;
            }
            ok = ok && p->used == nheld && p->hwm == ref_hwm && pool_check(p);
        }
        while (ok && nheld > 0) ok = pool_free(p, g_held[--nheld]) == POOL_OK;
        CHECK(ok && p->used == 0 && pool_check(p) && p->bad_frees == bad_injected && p->fails == ref_fails);
        printf("[B] stress 200000 ops: %s | blocks=%u hwm=%u fails=%u bad_frees caught=%u\n",
               ok ? "match" : "MISMATCH", p->nblocks, p->hwm, p->fails, p->bad_frees);
    }

    /* Part C: 프레임 파이프라인 — 풀 크기별 drop / hwm */
    {
        static const uint32_t nb[] = { 8, 16, 24, 32 };
        sim_t r[4];
        printf("[C] pool blocks | generated | dropped | processed | hwm | corrupt | leak\n");
        for (int i = 0; i < 4; i++) {
            r[i] = run_pipeline(&g_q, g_big, nb[i] * sizeof(frame_t), 20000u);
            printf("[C] %11u | %9u | %7u | %9u | %3u | %7u | %4u\n",
                   nb[i], r[i].gen, r[i].drops, r[i].done, r[i].hwm, r[i].corrupt, r[i].left);
        }
        CHECK(r[0].gen > 0 && r[0].drops > 0 && r[0].hwm == 8 && r[0].drops + r[0].done == r[0].gen);
        CHECK(r[3].gen > 0 && r[3].drops == 0 && r[3].done == r[3].gen && r[3].hwm < 32);
        bool ok = true;
        for (int i = 0; i < 4; i++) ok = ok && r[i].corrupt == 0 && r[i].left == 0 && r[i].drops + r[i].done == r[i].gen;
        CHECK(ok && pool_check(&g_q) && g_crit_depth == 0 && g_crit_entries > 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
