/* LLAT-B06 demo_bench.c — 원자 연산 비용을 잰다 (Apple Silicon arm64 전용: 인라인 asm 사용)
 *
 *   실험 1  단일 스레드 지연 (dependent chain): 한 연산의 결과가 다음 연산의 입력이 되게 이어서
 *           "연산 1개가 끝날 때까지 걸리는 시간" 을 잰다. plain add / fetch_add 각 memory_order /
 *           결과 버림 / LSE stadd / CAS 루프 / LL/SC(ldxr·stxr) / load 체인 / store / stlr→load 쌍
 *   실험 2  경합 처리량: 스레드 1~8 이 같은 캐시 라인의 카운터 하나를 증가 (ldadd / ldaddal /
 *           LL/SC / CAS 루프) vs 스레드별 칸(128 B 패딩). CAS·LL/SC 재시도 횟수도 센다.
 *
 * 측정법: clock_gettime_nsec_np(CLOCK_UPTIME_RAW) 로 구간 전체를 재고 연산 수로 나눈다 (연산 1개는
 * 수 ns 라 개별로 재면 타이머 비용이 더 크다). 구성마다 워밍업 1회 + RUNS 회, 중앙값(median) 과 최소.
 * 숫자는 이 머신·이 순간의 값이다 — 다른 프로세스도 돌고 있으므로 비율로 읽는다. CHECK 하지 않는다.
 * macOS 에는 sched_setaffinity 가 없어 코어 고정을 못 한다 (QOS_CLASS_USER_INTERACTIVE 는 P 코어 선호 힌트).
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench && ./bench [runs=7] [실험="12"]   (예: ./bench 7 1, ./bench 5 2)
 * asm:   cc -std=c11 -O2 -S demo_bench.c -o - | less      (함수 lat_* / th_* 의 루프 본문)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if !defined(__aarch64__)
#error "arm64 only (inline asm ldxr/stxr/stadd)"
#endif

#define LINE 128
#define NOINL __attribute__((noinline))
#define MAXT 8
#define MAXRUNS 15

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* 결과 v 에 실제로 의존하는 "1" — 컴파일러는 asm 안을 모르므로 접을 수 없고, CPU 는 v 가 나와야 계산한다.
 * v < 2^63 이므로 lsr #63 = 0. 모든 체인에 똑같이 붙여서 비교를 공정하게 한다 (오버헤드는 실험 1 의 reg 줄). */
static inline uint64_t dep1(uint64_t v) {
    uint64_t z;
    __asm__("lsr %0, %1, #63" : "=r"(z) : "r"(v));
    return z + 1;
}

/* 전역은 static 이 아니게 둔다: static 이고 아무도 안 쓰는 원자 변수는 clang 이 "항상 0" 으로 보고
 * seq_cst load 까지 지워 버렸다 (실측 — 아래 store+load 실험이 처음엔 load 없이 컴파일됐다). */
_Alignas(LINE) _Atomic uint64_t g_a;                 /* 실험 대상 원자 변수 (라인 하나 독차지) */
_Alignas(LINE) _Atomic uint64_t g_b;                 /* stlr→load 쌍에서 load 할 다른 주소 */
_Alignas(LINE) volatile uint64_t g_plain;            /* plain add: ldr/add/str */
_Alignas(LINE) _Atomic(uintptr_t) g_self;            /* load 체인 1: 자기 자신을 가리키는 포인터 */
#define RING 256                                     /* load 체인 2: 128 B 간격 노드 256개를 무작위 순서로 */
_Alignas(LINE) _Atomic(uintptr_t) g_ring[RING][LINE / sizeof(uintptr_t)];
_Alignas(LINE) uint64_t g_raw;                       /* 인라인 asm 대상 (원자 접근은 asm 만) */
_Alignas(LINE) uint64_t g_raw4[4][LINE / 8];         /* 서로 다른 라인 4개 */

/* ---------------- 실험 1 — 단일 스레드 체인 ---------------- */
NOINL static uint64_t lat_reg(uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n; i++) { v += dep1(v); __asm__ volatile("" : "+r"(v)); }
    return v;
}
NOINL static uint64_t lat_plain(uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n; i++) { v = g_plain; g_plain = v + dep1(v); }
    return v;
}
#define LAT_FA(name, mo) \
NOINL static uint64_t name(uint64_t n) { \
    uint64_t v = 0; \
    for (uint64_t i = 0; i < n; i++) v = atomic_fetch_add_explicit(&g_a, dep1(v), mo); \
    return v; }
LAT_FA(lat_fa_relaxed, memory_order_relaxed)
LAT_FA(lat_fa_acquire, memory_order_acquire)
LAT_FA(lat_fa_release, memory_order_release)
LAT_FA(lat_fa_acq_rel, memory_order_acq_rel)
LAT_FA(lat_fa_seq_cst, memory_order_seq_cst)

/* 결과를 쓰지 않는 fetch_add — 체인 없음 (연산끼리 서로 기다리지 않아도 됨) */
NOINL static uint64_t lat_fa_unused_relaxed(uint64_t n) {
    for (uint64_t i = 0; i < n; i++) atomic_fetch_add_explicit(&g_a, 1, memory_order_relaxed);
    return 0;
}
NOINL static uint64_t lat_fa_unused_seq_cst(uint64_t n) {
    for (uint64_t i = 0; i < n; i++) atomic_fetch_add_explicit(&g_a, 1, memory_order_seq_cst);
    return 0;
}
/* 서로 다른 캐시 라인 4개에 번갈아 — 같은 주소가 아니면 겹쳐 실행되나? */
NOINL static uint64_t lat_fa_unused_4lines(uint64_t n) {
    for (uint64_t i = 0; i < n; i += 4)
        for (int k = 0; k < 4; k++)
            atomic_fetch_add_explicit((_Atomic uint64_t *)&g_raw4[k][0], 1, memory_order_relaxed);
    return 0;
}
/* LSE STADD: 값을 돌려주지 않는 원자 덧셈 (clang 은 결과를 안 써도 ldadd 를 낸다 → asm 으로 직접) */
NOINL static uint64_t lat_stadd(uint64_t n) {
    for (uint64_t i = 0; i < n; i++)
        __asm__ volatile("stadd %x1, [%0]" : : "r"(&g_raw), "r"((uint64_t)1) : "memory");
    return 0;
}
NOINL static uint64_t lat_stadd_4lines(uint64_t n) {
    for (uint64_t i = 0; i < n; i += 4)
        for (int k = 0; k < 4; k++)
            __asm__ volatile("stadd %x1, [%0]" : : "r"(&g_raw4[k][0]), "r"((uint64_t)1) : "memory");
    return 0;
}
/* CAS 루프로 만든 fetch_add (단일 스레드라 실패 없음) */
#define LAT_CAS(name, mo) \
NOINL static uint64_t name(uint64_t n) { \
    uint64_t v = 0; \
    for (uint64_t i = 0; i < n; i++) { \
        uint64_t old = atomic_load_explicit(&g_a, memory_order_relaxed), d = dep1(v); \
        while (!atomic_compare_exchange_weak_explicit(&g_a, &old, old + d, mo, memory_order_relaxed)) { } \
        v = old; } \
    return v; }
LAT_CAS(lat_cas_relaxed, memory_order_relaxed)
LAT_CAS(lat_cas_seq_cst, memory_order_seq_cst)

/* LL/SC fetch_add (ARMv8.0 방식). 이 맥의 clang 은 C11 atomic 을 LSE 로만 내므로 asm 으로 쓴다. */
static inline uint64_t llsc_add(uint64_t *p, uint64_t d) {
    uint64_t old, nw; uint32_t fail;
    __asm__ volatile("1: ldxr  %0, [%3]\n"
                     "   add   %1, %0, %4\n"
                     "   stxr  %w2, %1, [%3]\n"
                     "   cbnz  %w2, 1b"
                     : "=&r"(old), "=&r"(nw), "=&r"(fail) : "r"(p), "r"(d) : "memory");
    return old;
}
static inline uint64_t llsc_add_acqrel(uint64_t *p, uint64_t d) {
    uint64_t old, nw; uint32_t fail;
    __asm__ volatile("1: ldaxr %0, [%3]\n"
                     "   add   %1, %0, %4\n"
                     "   stlxr %w2, %1, [%3]\n"
                     "   cbnz  %w2, 1b"
                     : "=&r"(old), "=&r"(nw), "=&r"(fail) : "r"(p), "r"(d) : "memory");
    return old;
}
NOINL static uint64_t lat_llsc(uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n; i++) v = llsc_add(&g_raw, dep1(v));
    return v;
}
NOINL static uint64_t lat_llsc_acqrel(uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n; i++) v = llsc_add_acqrel(&g_raw, dep1(v));
    return v;
}
/* load 체인 (포인터 추적): p = *p. 다음 load 의 주소가 이전 load 의 결과.
 * self = 늘 같은 값을 돌려주는 자기 포인터 (예측하기 쉬움), ring = 무작위 순환 (예측 어려움) */
#define LAT_LD(name, start, mo) \
NOINL static uint64_t name(uint64_t n) { \
    uintptr_t p = (uintptr_t)(start); \
    for (uint64_t i = 0; i < n; i++) p = atomic_load_explicit((_Atomic(uintptr_t) *)p, mo); \
    return (uint64_t)p; }
LAT_LD(lat_self_relaxed, &g_self, memory_order_relaxed)
LAT_LD(lat_self_acquire, &g_self, memory_order_acquire)
LAT_LD(lat_self_seq_cst, &g_self, memory_order_seq_cst)
LAT_LD(lat_ring_relaxed, &g_ring[0][0], memory_order_relaxed)
LAT_LD(lat_ring_acquire, &g_ring[0][0], memory_order_acquire)
LAT_LD(lat_ring_seq_cst, &g_ring[0][0], memory_order_seq_cst)
/* store 만 연속 (store 는 결과가 없어 체인이 아님 → 처리량) */
#define LAT_ST(name, mo) \
NOINL static uint64_t name(uint64_t n) { \
    for (uint64_t i = 0; i < n; i++) atomic_store_explicit(&g_a, i, mo); \
    return 0; }
LAT_ST(lat_st_relaxed, memory_order_relaxed)
LAT_ST(lat_st_release, memory_order_release)
LAT_ST(lat_st_seq_cst, memory_order_seq_cst)
/* store 다음 "다른 주소" load — release/acquire (stlr→ldapr) vs seq_cst (stlr→ldar) */
#define LAT_SL(name, smo, lmo) \
NOINL static uint64_t name(uint64_t n) { \
    uint64_t s = 0; \
    for (uint64_t i = 0; i < n; i++) { \
        atomic_store_explicit(&g_a, i, smo); \
        s += atomic_load_explicit(&g_b, lmo); } \
    return s; }
LAT_SL(lat_sl_relaxed, memory_order_relaxed, memory_order_relaxed)
LAT_SL(lat_sl_rel_acq, memory_order_release, memory_order_acquire)
LAT_SL(lat_sl_seq_cst, memory_order_seq_cst, memory_order_seq_cst)

typedef uint64_t (*latfn)(uint64_t);
static const struct { const char *name, *insn; latfn f; } lat_tab[] = {
    { "reg add chain (calibration)", "lsr+add (no memory)", lat_reg },
    { "plain add in memory", "ldr / add / str", lat_plain },
    { "fetch_add relaxed", "ldadd", lat_fa_relaxed },
    { "fetch_add acquire", "ldadda", lat_fa_acquire },
    { "fetch_add release", "ldaddl", lat_fa_release },
    { "fetch_add acq_rel", "ldaddal", lat_fa_acq_rel },
    { "fetch_add seq_cst", "ldaddal", lat_fa_seq_cst },
    { "fetch_add relaxed, result unused", "ldadd (no chain)", lat_fa_unused_relaxed },
    { "fetch_add seq_cst, result unused", "ldaddal (no chain)", lat_fa_unused_seq_cst },
    { "fetch_add relaxed unused, 4 lines", "ldadd x4 addresses", lat_fa_unused_4lines },
    { "asm stadd (no result)", "stadd", lat_stadd },
    { "asm stadd, 4 lines", "stadd x4 addresses", lat_stadd_4lines },
    { "CAS loop relaxed", "ldr + cas", lat_cas_relaxed },
    { "CAS loop seq_cst", "ldr + casal", lat_cas_seq_cst },
    { "LL/SC asm (ldxr/stxr)", "ldxr/add/stxr/cbnz", lat_llsc },
    { "LL/SC asm (ldaxr/stlxr)", "ldaxr/add/stlxr/cbnz", lat_llsc_acqrel },
    { "load chain self-ptr relaxed", "ldr", lat_self_relaxed },
    { "load chain self-ptr acquire", "ldapr", lat_self_acquire },
    { "load chain self-ptr seq_cst", "ldar", lat_self_seq_cst },
    { "load chain random ring relaxed", "ldr", lat_ring_relaxed },
    { "load chain random ring acquire", "ldapr", lat_ring_acquire },
    { "load chain random ring seq_cst", "ldar", lat_ring_seq_cst },
    { "store relaxed", "str", lat_st_relaxed },
    { "store release", "stlr", lat_st_release },
    { "store seq_cst", "stlr", lat_st_seq_cst },
    { "store+load(other) relaxed", "str ; ldr", lat_sl_relaxed },
    { "store+load(other) rel/acq", "stlr ; ldapr", lat_sl_rel_acq },
    { "store+load(other) seq_cst", "stlr ; ldar", lat_sl_seq_cst },
};
#define NLAT (int)(sizeof lat_tab / sizeof lat_tab[0])

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median_d(double *v, int n) { qsort(v, (size_t)n, sizeof *v, cmp_d); return v[n / 2]; }

static volatile uint64_t g_sink;
static void exp1(int runs) {
    const uint64_t N = 20u * 1000 * 1000;
    printf("\n[exp1] single-thread, N=%llu ops per run, warm-up 1 + %d runs\n", (unsigned long long)N, runs);
    printf("%-36s %-22s %9s %9s %8s\n", "variant", "instruction", "median", "min", "x ldadd");
    double base = 0;
    for (int k = 0; k < NLAT; k++) {
        double r[MAXRUNS];
        g_sink = lat_tab[k].f(N / 4);                 /* 워밍업: 코드·데이터 캐시, 주파수 올리기 */
        for (int i = 0; i < runs; i++) {
            uint64_t t0 = now_ns();
            g_sink = lat_tab[k].f(N);
            uint64_t t1 = now_ns();
            r[i] = (double)(t1 - t0) / (double)N;
        }
        double med = median_d(r, runs), mn = r[0];
        if (k == 2) base = med;
        printf("%-36s %-22s %7.3f ns %7.3f ns", lat_tab[k].name, lat_tab[k].insn, med, mn);
        if (base > 0) printf(" %7.2f", med / base);
        printf("\n");
    }
}

/* ---------------- 실험 2 — 경합 처리량 ---------------- */
enum { C_LDADD, C_LDADDAL, C_LLSC, C_CAS, C_SLOT, NC };
static const char *cname[NC] = { "ldadd relaxed", "ldaddal seq_cst", "LL/SC ldxr/stxr", "CAS loop (cas)", "per-thread slot" };

static _Alignas(LINE) _Atomic uint64_t c_shared;
static _Alignas(LINE) uint64_t c_raw;
static struct { _Alignas(LINE) _Atomic uint64_t v; } c_slot[MAXT];
static _Alignas(LINE) atomic_int c_go;
static _Alignas(LINE) _Atomic uint64_t c_retries;

typedef struct { int kind, tid; uint64_t ops; } targ_t;

static inline uint64_t llsc_add_count(uint64_t *p, uint64_t d, uint64_t *retry) {
    uint64_t old, nw; uint32_t fail; uint64_t r = 0;
    for (;;) {
        __asm__ volatile("ldxr  %0, [%3]\n"
                         "add   %1, %0, %4\n"
                         "stxr  %w2, %1, [%3]"
                         : "=&r"(old), "=&r"(nw), "=&r"(fail) : "r"(p), "r"(d) : "memory");
        if (fail == 0) break;
        r++;
    }
    *retry += r;
    return old;
}

NOINL static void *th_body(void *vp) {
    targ_t *a = vp;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    while (!atomic_load_explicit(&c_go, memory_order_acquire)) { }
    uint64_t retry = 0;
    switch (a->kind) {
    case C_LDADD:   for (uint64_t i = 0; i < a->ops; i++) atomic_fetch_add_explicit(&c_shared, 1, memory_order_relaxed); break;
    case C_LDADDAL: for (uint64_t i = 0; i < a->ops; i++) atomic_fetch_add_explicit(&c_shared, 1, memory_order_seq_cst); break;
    case C_LLSC:    for (uint64_t i = 0; i < a->ops; i++) llsc_add_count(&c_raw, 1, &retry); break;
    case C_CAS:
        for (uint64_t i = 0; i < a->ops; i++) {
            uint64_t old = atomic_load_explicit(&c_shared, memory_order_relaxed);
            while (!atomic_compare_exchange_weak_explicit(&c_shared, &old, old + 1,
                                                          memory_order_relaxed, memory_order_relaxed)) retry++;
        }
        break;
    case C_SLOT: {
        _Atomic uint64_t *s = &c_slot[a->tid].v;            /* 단일 writer: load + store (OS-B05 Part C) */
        for (uint64_t i = 0; i < a->ops; i++)
            atomic_store_explicit(s, atomic_load_explicit(s, memory_order_relaxed) + 1, memory_order_relaxed);
        break; }
    }
    atomic_fetch_add_explicit(&c_retries, retry, memory_order_relaxed);
    return NULL;
}

static double run_contended(int kind, int T, uint64_t ops, double *retry_per_op) {
    pthread_t th[MAXT]; targ_t a[MAXT];
    if (kind == C_SLOT) ops *= 10;          /* 너무 빨라서 스레드 생성 시간이 섞이지 않게 10배 */
    atomic_store(&c_shared, 0); c_raw = 0; atomic_store(&c_go, 0); atomic_store(&c_retries, 0);
    for (int t = 0; t < MAXT; t++) atomic_store(&c_slot[t].v, 0);
    for (int t = 0; t < T; t++) { a[t] = (targ_t){ kind, t, ops }; pthread_create(&th[t], NULL, th_body, &a[t]); }
    uint64_t t0 = now_ns();
    atomic_store_explicit(&c_go, 1, memory_order_release);
    for (int t = 0; t < T; t++) pthread_join(th[t], NULL);
    uint64_t t1 = now_ns();
    uint64_t total = 0;
    if (kind == C_SLOT) for (int t = 0; t < T; t++) total += atomic_load(&c_slot[t].v);
    else if (kind == C_LLSC) total = c_raw;
    else total = atomic_load(&c_shared);
    if (total != (uint64_t)T * ops) { fprintf(stderr, "WRONG TOTAL %s T=%d: %llu\n", cname[kind], T, (unsigned long long)total); exit(2); }
    *retry_per_op = (double)atomic_load(&c_retries) / (double)(T * ops);
    return (double)(T * ops) / (double)(t1 - t0) * 1e3;      /* Mops/s */
}

static void exp2(int runs) {
    const uint64_t OPS = 2u * 1000 * 1000;                    /* 스레드당 */
    printf("\n[exp2] contended throughput, %llu ops/thread, warm-up 1 + %d runs, Mops/s median [min-max]\n",
           (unsigned long long)OPS, runs);
    for (int k = 0; k < NC; k++) {
        printf("%s\n", cname[k]);
        for (int T = 1; T <= MAXT; T++) {
            double r[MAXRUNS], rp[MAXRUNS], d;
            run_contended(k, T, OPS / 4, &d);
            for (int i = 0; i < runs; i++) r[i] = run_contended(k, T, OPS, &rp[i]);
            double mn = r[0], mx = r[0];
            for (int i = 1; i < runs; i++) { if (r[i] < mn) mn = r[i]; if (r[i] > mx) mx = r[i]; }
            double med = median_d(r, runs), rmed = median_d(rp, runs);
            printf("  T=%d %9.1f [%.1f-%.1f]", T, med, mn, mx);
            if (k == C_LLSC || k == C_CAS) printf("  retries/op %.2f", rmed);
            printf("\n");
        }
    }
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 7;
    if (runs < 1 || runs > MAXRUNS) runs = 7;
    const char *only = argc > 2 ? argv[2] : "12";
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    atomic_store(&g_self, (uintptr_t)&g_self);
    {   /* 무작위 순환: 0 → perm[1] → perm[2] … → 0 (고정 시드 xorshift 로 섞기) */
        int perm[RING]; uint32_t x = 2463534242u;
        for (int i = 0; i < RING; i++) perm[i] = i;
        for (int i = RING - 1; i > 1; i--) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int j = 1 + (int)(x % (uint32_t)i); int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
        }
        for (int i = 0; i < RING; i++)
            atomic_store(&g_ring[perm[i]][0], (uintptr_t)&g_ring[perm[(i + 1) % RING]][0]);
        atomic_store(&g_b, (uint64_t)argc);              /* g_b 가 상수로 접히지 않게 */
    }
    double la[3];
    if (getloadavg(la, 3) == 3) printf("load average at start: %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    for (const char *c = only; *c; c++) { if (*c == '1') exp1(runs); if (*c == '2') exp2(runs); }
    if (getloadavg(la, 3) == 3) printf("load average at end:   %.2f %.2f %.2f\n", la[0], la[1], la[2]);
    return 0;
}
