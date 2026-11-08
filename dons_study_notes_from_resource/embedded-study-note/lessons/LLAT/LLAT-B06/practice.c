/* LLAT-B06 추가 연습 — 한 번에 검증
 *
 *   연습 1  lse_fetch_umax32: LSE ldumax 로 워터마크를 명령 하나에 (C11 엔 fetch_max 가 없다)
 *   연습 2  ID 블록 예약: fetch_add(64) 한 번으로 ID 64 개를 받아 로컬에서 나눠 준다
 *   연습 3  포화 카운터 sat_inc: CAS 가 꼭 필요한 경우 + 꽉 찼으면 RMW 하지 않고 바로 실패
 *   연습 4  lse_stadd: 결과를 돌려주지 않는 원자 덧셈 (STADD)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if !defined(__aarch64__)
#error "arm64 only"
#endif

#define LINE 128

/* 연습 1 — 옛 값을 돌려주고 *p = max(*p, v) (부호 없는 비교). 순서 보장 없음 (relaxed 에 해당) */
static uint32_t lse_fetch_umax32(uint32_t *p, uint32_t v) {
    uint32_t old;
    __asm__ volatile("ldumax %w2, %w0, [%1]" : "=r"(old) : "r"(p), "r"(v) : "memory");
    return old;
}

/* 연습 2 — 스레드마다 하나. [next, end) 가 비면 공유 카운터에서 BLK 개를 한 번에 예약 */
#define BLK 64
typedef struct { uint64_t next, end; uint32_t refills; } idblk_t;
static uint64_t id_next(idblk_t *b, _Atomic uint64_t *g) {
    if (b->next == b->end) {
        b->next = atomic_fetch_add_explicit(g, BLK, memory_order_relaxed);
        b->end = b->next + BLK;
        b->refills++;
    }
    return b->next++;
}

/* 연습 3 — *p < max 이면 1 올리고 true, 이미 max 면 false. 꽉 찬 뒤에는 읽기만 한다 */
static bool sat_inc(_Atomic uint32_t *p, uint32_t max) {
    uint32_t cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur < max) {
        if (atomic_compare_exchange_weak_explicit(p, &cur, cur + 1, memory_order_relaxed, memory_order_relaxed))
            return true;
    }
    return false;
}

/* 연습 4 — *p += v, 값은 돌려주지 않는다 */
static void lse_stadd(uint64_t *p, uint64_t v) {
    __asm__ volatile("stadd %x1, [%0]" : : "r"(p), "r"(v) : "memory");
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static bool run_threads(void *(*fn)(void *), void *args, size_t argsz, int n) {   /* n <= 16 */
    pthread_t th[16];
    for (int i = 0; i < n; i++)
        if (pthread_create(&th[i], NULL, fn, (char *)args + (size_t)i * argsz) != 0) return false;
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    return true;
}

/* 연습 1 */
static _Alignas(LINE) uint32_t p1_max;
typedef struct { uint32_t seed, local_max; } p1_t;
static void *p1_worker(void *vp) {
    p1_t *a = vp;
    uint32_t s = a->seed;
    for (int i = 0; i < 100000; i++) {
        uint32_t v = xs32(&s);
        if (v > a->local_max) a->local_max = v;
        lse_fetch_umax32(&p1_max, v);
    }
    return NULL;
}

/* 연습 2 */
#define P2T 4
#define P2N 10000u
static _Alignas(LINE) _Atomic uint64_t p2_gen;
static atomic_uchar p2_seen[P2T * (P2N + BLK)];
typedef struct { idblk_t b; uint32_t dup, out_of_range; } p2_t;
static void *p2_worker(void *vp) {
    p2_t *a = vp;
    for (uint32_t i = 0; i < P2N; i++) {
        uint64_t id = id_next(&a->b, &p2_gen);
        if (id >= sizeof p2_seen) { a->out_of_range++; continue; }
        if (atomic_exchange_explicit(&p2_seen[id], 1, memory_order_relaxed)) a->dup++;
    }
    return NULL;
}

/* 연습 3 */
#define P3T 8
#define P3TRY 10000u
#define P3MAX 30000u
static _Alignas(LINE) _Atomic uint32_t p3_credit;
static void *p3_worker(void *vp) {
    uint32_t *ok = vp;
    for (uint32_t i = 0; i < P3TRY; i++) if (sat_inc(&p3_credit, P3MAX)) (*ok)++;
    return NULL;
}

/* 연습 4 */
static _Alignas(LINE) uint64_t p4_sum;
static void *p4_worker(void *vp) {
    uint64_t k = *(uint64_t *)vp;
    for (int i = 0; i < 200000; i++) lse_stadd(&p4_sum, k);
    return NULL;
}

int main(void) {
    printf("-- 연습 1: ldumax\n");
    uint32_t m = 10;
    CHECK(lse_fetch_umax32(&m, 7) == 10 && m == 10);
    CHECK(lse_fetch_umax32(&m, 11) == 10 && m == 11);
    CHECK(lse_fetch_umax32(&m, 0x80000000u) == 11 && m == 0x80000000u);   /* 부호 없는 비교 */
    p1_max = 0;
    p1_t a1[4] = { { 1u, 0 }, { 77u, 0 }, { 0xdeadbeefu, 0 }, { 0x1234567u, 0 } };
    CHECK(run_threads(p1_worker, a1, sizeof a1[0], 4));
    uint32_t want = 0;
    for (int i = 0; i < 4; i++) if (a1[i].local_max > want) want = a1[i].local_max;
    CHECK(p1_max == want);

    printf("-- 연습 2: ID 블록 예약\n");
    _Atomic uint64_t g = 0;
    idblk_t b = { 0, 0, 0 };
    CHECK(id_next(&b, &g) == 0 && b.refills == 1 && atomic_load(&g) == BLK);
    for (int i = 1; i < BLK; i++) (void)id_next(&b, &g);
    CHECK(b.refills == 1);                                       /* 64 개까지는 공유 카운터를 안 건드린다 */
    CHECK(id_next(&b, &g) == BLK && b.refills == 2 && atomic_load(&g) == 2 * BLK);
    p2_t a2[P2T] = { 0 };
    CHECK(run_threads(p2_worker, a2, sizeof a2[0], P2T));
    uint32_t dup = 0, oor = 0, refills_ok = 1;
    for (int t = 0; t < P2T; t++) {
        dup += a2[t].dup; oor += a2[t].out_of_range;
        if (a2[t].b.refills != (P2N + BLK - 1) / BLK) refills_ok = 0;   /* 157 번 */
    }
    CHECK(dup == 0 && oor == 0);                                  /* 모든 ID 가 한 번씩만 */
    CHECK(refills_ok);
    CHECK(atomic_load(&p2_gen) == (uint64_t)P2T * ((P2N + BLK - 1) / BLK) * BLK);

    printf("-- 연습 3: 포화 카운터\n");
    _Atomic uint32_t c = 0;
    int okc = 0;
    for (int i = 0; i < 5; i++) okc += sat_inc(&c, 3);
    CHECK(okc == 3 && atomic_load(&c) == 3);
    CHECK(sat_inc(&c, 0) == false && atomic_load(&c) == 3);
    atomic_store(&p3_credit, 0);
    uint32_t ok3[P3T] = { 0 };
    CHECK(run_threads(p3_worker, ok3, sizeof ok3[0], P3T));
    uint32_t tot = 0;
    for (int t = 0; t < P3T; t++) tot += ok3[t];
    CHECK(tot == P3MAX && atomic_load(&p3_credit) == P3MAX);     /* 80000 번 시도 중 정확히 30000 번 성공 */

    printf("-- 연습 4: stadd\n");
    uint64_t s = 5;
    lse_stadd(&s, 10);
    CHECK(s == 15);
    lse_stadd(&s, UINT64_MAX);
    CHECK(s == 14);
    p4_sum = 0;
    uint64_t k4[4] = { 1, 2, 3, 4 };
    CHECK(run_threads(p4_worker, k4, sizeof k4[0], 4));
    CHECK(p4_sum == 200000u * (1 + 2 + 3 + 4));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
