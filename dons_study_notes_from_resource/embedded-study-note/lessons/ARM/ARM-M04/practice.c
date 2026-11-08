/* ARM-M04 practice.c — 추가 연습 4개를 한 번에 검증
 *
 *   연습 1  원자 max: LL/SC (LDAXR/CMP/CSEL/STLXR) 와 LSE LDUMAXAL — 둘 다 "옛 값" 반환
 *   연습 2  LSE 만으로 버퍼 풀 alloc: LDR 로 후보 비트 고르기 → LDCLRA 로 지우기 → 옛 값에 그 비트가 있었으면 내 것
 *   연습 3  128비트 CAS (값+세대 태그): LDAXP/STLXP 루프와 CASPAL
 *   연습 4  이벤트 플래그: post = STSETL, take = SWPA WZR (모두 가져가며 비우기)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__aarch64__)
#error "AArch64 only"
#endif
#define LSE ".arch_extension lse\n\t"

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#include <sanitizer/tsan_interface.h>
#define HB_ACQUIRE(p) __tsan_acquire((void *)(p))
#define HB_RELEASE(p) __tsan_release((void *)(p))
#endif
#endif
#ifndef HB_ACQUIRE
#define HB_ACQUIRE(p) ((void)(p))
#define HB_RELEASE(p) ((void)(p))
#endif

static inline void cpu_relax(void) { __asm__ volatile("yield" ::: "memory"); }

/* ---------- 연습 1: 원자 max ---------- */
static uint32_t llsc_fetch_umax(uint32_t *p, uint32_t v) {
    uint32_t old, nv, st;
    __asm__ volatile(
        "1: ldaxr  %w[old], %[mem]\n"
        "   cmp    %w[old], %w[v]\n"
        "   csel   %w[nv], %w[old], %w[v], hs\n"     /* 부호 없는 max */
        "   stlxr  %w[st], %w[nv], %[mem]\n"
        "   cbnz   %w[st], 1b\n"
        : [old] "=&r"(old), [nv] "=&r"(nv), [st] "=&r"(st), [mem] "+Q"(*p)
        : [v] "r"(v)
        : "cc", "memory");
    return old;
}
static uint32_t lse_fetch_umax(uint32_t *p, uint32_t v) {
    uint32_t old;
    __asm__ volatile(LSE "ldumaxal %w[v], %w[old], %[mem]\n" : [old] "=r"(old), [mem] "+Q"(*p) : [v] "r"(v) : "memory");
    return old;
}

/* ---------- 연습 2: LSE 버퍼 풀 ---------- */
#define POOL_N 3u
static uint32_t ld32(const uint32_t *p) {        /* 정렬된 LDR = single-copy atomic (힌트용 읽기) */
    uint32_t v;
    __asm__ volatile("ldr %w[v], %[mem]\n" : [v] "=r"(v) : [mem] "Q"(*p) : "memory");
    return v;
}
static int pool_alloc_lse(uint32_t *mask) {
    for (;;) {
        uint32_t m = ld32(mask);
        if (m == 0u) return -1;
        uint32_t bit = m & (0u - m);                  /* 가장 낮은 1비트 */
        uint32_t old;
        __asm__ volatile(LSE "ldclra %w[bit], %w[old], %[mem]\n"
                         : [old] "=r"(old), [mem] "+Q"(*mask) : [bit] "r"(bit) : "memory");
        if (old & bit) return __builtin_ctz(bit);     /* 내가 지웠다 → 내 것 */
        /* 다른 코어가 먼저 가져갔다: 내 LDCLR 은 이미 0 인 비트를 지웠으니 아무것도 안 바뀜 → 재시도 */
    }
}
static void pool_free_lse(uint32_t *mask, int idx) {
    uint32_t bit = 1u << (unsigned)idx;
    __asm__ volatile(LSE "stsetl %w[bit], %[mem]\n" : [mem] "+Q"(*mask) : [bit] "r"(bit) : "memory");
}

/* ---------- 연습 3: 128비트 CAS ---------- */
typedef struct { _Alignas(16) uint64_t lo; uint64_t hi; } u128_t;   /* lo = 값(인덱스/포인터), hi = 세대 태그. _Alignas 는 첫 멤버에만 (둘 다 주면 hi 가 +16 으로 밀림) */
_Static_assert(sizeof(u128_t) == 16, "u128_t must be one 16-byte quadword");
static bool cas128_llsc(u128_t *p, u128_t *exp, u128_t des) {   /* strong, 실패 시 *exp = 본 값 */
    uint64_t lo, hi;
    uint32_t st;
    __asm__ volatile(
        "1: ldaxp  %[lo], %[hi], %[mem]\n"
        "   cmp    %[lo], %[elo]\n"
        "   ccmp   %[hi], %[ehi], #0, eq\n"
        "   b.ne   2f\n"
        "   stlxp  %w[st], %[dlo], %[dhi], %[mem]\n"
        "   cbnz   %w[st], 1b\n"
        "   b      3f\n"
        "2: stlxp  %w[st], %[lo], %[hi], %[mem]\n"   /* 본 값을 그대로 다시 써서 128비트 읽기를 원자로 확정 */
        "   cbnz   %w[st], 1b\n"
        "   mov    %w[st], #1\n"
        "3:\n"
        : [lo] "=&r"(lo), [hi] "=&r"(hi), [st] "=&r"(st), [mem] "+Q"(*p)
        : [elo] "r"(exp->lo), [ehi] "r"(exp->hi), [dlo] "r"(des.lo), [dhi] "r"(des.hi)
        : "cc", "memory");
    if (st == 0u) return true;
    exp->lo = lo; exp->hi = hi;
    return false;
}
static bool cas128_lse(u128_t *p, u128_t *exp, u128_t des) {
    register uint64_t s0 __asm__("x4") = exp->lo;    /* CASP 는 짝수 번호부터 연속한 레지스터 쌍이 필요 */
    register uint64_t s1 __asm__("x5") = exp->hi;
    register uint64_t t0 __asm__("x6") = des.lo;
    register uint64_t t1 __asm__("x7") = des.hi;
    uint64_t elo = exp->lo, ehi = exp->hi;
    __asm__ volatile(LSE "caspal %[s0], %[s1], %[t0], %[t1], %[mem]\n"
                     : [s0] "+r"(s0), [s1] "+r"(s1), [mem] "+Q"(*p)
                     : [t0] "r"(t0), [t1] "r"(t1)
                     : "memory");
    if (s0 == elo && s1 == ehi) return true;
    exp->lo = s0; exp->hi = s1;
    return false;
}

/* ---------- 연습 4: 이벤트 플래그 ---------- */
static void ev_post(uint32_t *ev, uint32_t bits) {
    __asm__ volatile(LSE "stsetl %w[b], %[mem]\n" : [mem] "+Q"(*ev) : [b] "r"(bits) : "memory");
}
static uint32_t ev_take(uint32_t *ev) {
    uint32_t old;
    __asm__ volatile(LSE "swpa   wzr, %w[old], %[mem]\n" : [old] "=r"(old), [mem] "+Q"(*ev) : : "memory");
    return old;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NT 4
static atomic_int g_go;
static void wait_go(void) { while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax(); }
typedef struct { int id; int kind; } warg_t;
static void run_threads(int n, void *(*fn)(void *), int kind) {
    pthread_t th[8];
    warg_t a[8];
    atomic_store(&g_go, 0);
    for (int i = 0; i < n; i++) { a[i].id = i; a[i].kind = kind; pthread_create(&th[i], NULL, fn, &a[i]); }
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
}
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 연습 1 */
#define MAX_ITERS 100000
static uint32_t g_max;
static void *max_worker(void *arg) {
    const warg_t *a = arg;
    uint32_t s = 0x9E3779B9u + (uint32_t)a->id * 7919u;
    wait_go();
    for (int k = 0; k < MAX_ITERS; k++) {
        uint32_t v = xorshift32(&s) >> 1;
        if (a->kind) (void)lse_fetch_umax(&g_max, v); else (void)llsc_fetch_umax(&g_max, v);
    }
    return NULL;
}
/* 연습 2 */
#define POOL_ITERS 100000u
static uint32_t g_mask;
static struct { uint32_t owner, data; } g_buf[POOL_N];
static uint32_t g_bad[NT], g_allocs[NT];
static void *pool_worker(void *arg) {
    const warg_t *a = arg;
    uint32_t me = (uint32_t)a->id + 1u;
    wait_go();
    for (uint32_t i = 0; i < POOL_ITERS; i++) {
        int idx;
        while ((idx = pool_alloc_lse(&g_mask)) < 0) cpu_relax();
        HB_ACQUIRE(&g_mask);
        if (g_buf[idx].owner != 0u) g_bad[a->id]++;
        g_buf[idx].owner = me; g_buf[idx].data = me ^ i;
        if (g_buf[idx].owner != me || g_buf[idx].data != (me ^ i)) g_bad[a->id]++;
        g_buf[idx].owner = 0u;
        g_allocs[a->id]++;
        HB_RELEASE(&g_mask);
        pool_free_lse(&g_mask, idx);
    }
    return NULL;
}
/* 연습 3 */
#define C128_ITERS 100000u
static u128_t g_pair;
static void *c128_worker(void *arg) {
    const warg_t *a = arg;
    wait_go();
    for (uint32_t i = 0; i < C128_ITERS; i++) {
        u128_t e = { 0u, 0u }, d;
        do { d.lo = e.lo + 1u; d.hi = e.hi + 3u; }
        while (!(a->kind ? cas128_lse(&g_pair, &e, d) : cas128_llsc(&g_pair, &e, d)));
    }
    return NULL;
}
/* 연습 4 */
#define EV_PROD 8
#define EV_ROUNDS 300
static uint32_t g_ev, g_slot[EV_PROD], g_round;
static void *ev_producer(void *arg) {
    const warg_t *a = arg;
    wait_go();
    g_slot[a->id] = g_round * 100u + (uint32_t)a->id;      /* 1) 데이터 */
    HB_RELEASE(&g_ev);
    ev_post(&g_ev, 1u << a->id);                            /* 2) 비트 (release) */
    return NULL;
}

int main(void) {
    /* 연습 1 */
    uint32_t m = 5u;
    CHECK(llsc_fetch_umax(&m, 3u) == 5u && m == 5u);
    CHECK(llsc_fetch_umax(&m, 0x80000000u) == 5u && m == 0x80000000u);   /* 부호 없는 비교 */
    CHECK(lse_fetch_umax(&m, 0xFFFFFFFFu) == 0x80000000u && m == 0xFFFFFFFFu);
    uint32_t want = 0;
    for (int i = 0; i < NT; i++) {
        uint32_t s = 0x9E3779B9u + (uint32_t)i * 7919u;
        for (int k = 0; k < MAX_ITERS; k++) { uint32_t v = xorshift32(&s) >> 1; if (v > want) want = v; }
    }
    g_max = 0u; run_threads(NT, max_worker, 0);
    CHECK(g_max == want);
    g_max = 0u; run_threads(NT, max_worker, 1);
    CHECK(g_max == want);

    /* 연습 2 */
    uint32_t pm = 0x5u;                                     /* 버퍼 0, 2 비어 있음 */
    int i0 = pool_alloc_lse(&pm), i1 = pool_alloc_lse(&pm), i2 = pool_alloc_lse(&pm);
    CHECK(i0 == 0 && i1 == 2 && i2 == -1 && pm == 0u);
    pool_free_lse(&pm, 2);
    CHECK(pm == 4u);
    g_mask = (1u << POOL_N) - 1u;
    run_threads(NT, pool_worker, 0);
    uint32_t bad = 0, allocs = 0;
    for (int i = 0; i < NT; i++) { bad += g_bad[i]; allocs += g_allocs[i]; }
    CHECK(bad == 0u && allocs == NT * POOL_ITERS && g_mask == (1u << POOL_N) - 1u);

    /* 연습 3 */
    u128_t v = { 1u, 2u }, e = { 1u, 9u }, d = { 7u, 8u };
    CHECK(!cas128_llsc(&v, &e, d) && e.lo == 1u && e.hi == 2u && v.lo == 1u && v.hi == 2u);   /* 태그 불일치 */
    CHECK(cas128_llsc(&v, &e, d) && v.lo == 7u && v.hi == 8u);
    e.lo = 7u; e.hi = 0u;
    CHECK(!cas128_lse(&v, &e, d) && e.lo == 7u && e.hi == 8u);
    d.lo = 0xFFFFFFFFFFFFFFFFull; d.hi = 1u;
    CHECK(cas128_lse(&v, &e, d) && v.lo == 0xFFFFFFFFFFFFFFFFull && v.hi == 1u);
    memset(&g_pair, 0, sizeof g_pair); run_threads(NT, c128_worker, 0);
    CHECK(g_pair.lo == NT * C128_ITERS && g_pair.hi == 3u * NT * C128_ITERS);
    memset(&g_pair, 0, sizeof g_pair); run_threads(NT, c128_worker, 1);
    CHECK(g_pair.lo == NT * C128_ITERS && g_pair.hi == 3u * NT * C128_ITERS);

    /* 연습 4 */
    uint32_t ev = 0u;
    ev_post(&ev, 0x5u); ev_post(&ev, 0x4u);
    CHECK(ev_take(&ev) == 0x5u && ev == 0u && ev_take(&ev) == 0u);   /* 같은 비트는 합쳐진다, take 는 비운다 */
    uint32_t ev_bad = 0, ev_rounds_ok = 0;
    for (uint32_t r = 0; r < EV_ROUNDS; r++) {
        g_round = r; g_ev = 0u;
        pthread_t th[EV_PROD];
        warg_t a[EV_PROD];
        atomic_store(&g_go, 0);
        for (int i = 0; i < EV_PROD; i++) { a[i].id = i; a[i].kind = 0; pthread_create(&th[i], NULL, ev_producer, &a[i]); }
        atomic_store_explicit(&g_go, 1, memory_order_release);
        uint32_t seen = 0u;
        while (seen != (1u << EV_PROD) - 1u) {
            uint32_t got = ev_take(&g_ev);
            if (!got) { cpu_relax(); continue; }
            HB_ACQUIRE(&g_ev);
            for (int i = 0; i < EV_PROD; i++)
                if (got & (1u << i)) { if (g_slot[i] != r * 100u + (uint32_t)i) ev_bad++; }
            if (got & seen) ev_bad++;                       /* 한 생산자의 비트를 두 번 받으면 안 됨 */
            seen |= got;
        }
        for (int i = 0; i < EV_PROD; i++) pthread_join(th[i], NULL);
        ev_rounds_ok++;
    }
    CHECK(ev_bad == 0u && ev_rounds_ok == EV_ROUNDS);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
