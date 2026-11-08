/* LLAT-B06 atomic 연산 비용: relaxed·acq_rel·seq_cst·CAS 경합
 *
 *   Part A  측정 도구 stats_of (min/median/max) · ps_per_op, 그리고 원자 증가 3가지 구현
 *           lse_fetch_add (asm ldadd) / llsc_fetch_add (asm ldxr·stxr) / cas_fetch_add (C11 CAS 루프)
 *   Part B  hot path 에서 원자 연산 줄이기: 배치 카운터 batch_* (로컬 누적 → N 개마다 fetch_add 한 번)
 *           플래그 flags_set (fetch_or 를 쓰고 CAS 루프는 안 쓴다 + 먼저 읽고 이미 있으면 쓰지 않는다)
 *   Part C  듀얼 코어 패킷 경로 텔레메트리: 코어별 칸 + sticky 오류 플래그 + 최대 지연 워터마크(read-before-CAS)
 *
 * 체크 테스트는 정확성만 본다 (최종 합계 == 기대값, RMW 횟수, OR/최대값). 시간은 절대 CHECK 하지 않는다
 * → demo_bench.c (연산별 지연, 1~8 스레드 경합) · demo_hotpath.c (기법별 효과).
 * 인라인 asm 은 arm64 (Apple Silicon, LSE 있음) 전용.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if !defined(__aarch64__)
#error "arm64 only: Part A uses inline ldadd / ldxr / stxr"
#endif

#define LINE 128   /* Apple M 시리즈 hw.cachelinesize = 128 (Cortex-A 는 보통 64) */

/* ======================= Part A — 측정 도구 ======================= */
typedef struct { uint64_t min, median, max; } stat3_t;

/* 표본 n 개(1..64)의 최소·중앙값·최대. 원본은 바꾸지 않는다 (복사해서 삽입 정렬).
 * 중앙값 = 정렬 후 [n/2] (n 이 짝수면 위쪽 가운데). n 이 0 이거나 64 초과면 {0,0,0}. */
static stat3_t stats_of(const uint64_t *s, int n) {
    stat3_t r = { 0, 0, 0 };
    (void)s; (void)n; /* TODO */
    return r;
}

/* 구간 [t0_ns, t1_ns] 동안 ops 번 했을 때 1 연산당 피코초 (정수, 버림). ops == 0 이면 0. */
static uint64_t ps_per_op(uint64_t t0_ns, uint64_t t1_ns, uint64_t ops) {
    (void)t0_ns; (void)t1_ns; (void)ops;
    return 0; /* TODO */
}

/* ======================= Part A — 원자 증가 3가지 ======================= */
/* *p += v 를 원자적으로 하고 이전 값을 돌려준다. memory_order 는 relaxed 에 해당 (순서 보장 없음). */

/* A1. LSE 원자 명령 하나: ldadd Xs, Xt, [Xn]  (Xt ← 옛 값, 메모리 ← 옛 값 + Xs) */
static uint64_t lse_fetch_add(uint64_t *p, uint64_t v) {
    (void)p; (void)v;
    return 0; /* TODO: __asm__ volatile("ldadd ...") */
}

/* A2. LL/SC (ARMv8.0 방식): ldxr 로 읽고 exclusive 모니터를 건 뒤, stxr 이 성공(0)할 때까지 반복 */
static uint64_t llsc_fetch_add(uint64_t *p, uint64_t v) {
    (void)p; (void)v;
    return 0; /* TODO: ldxr / add / stxr / cbnz 루프 */
}

/* A3. C11 CAS 루프: 읽고 → 새 값 계산 → compare_exchange 가 실패하면 (old 가 갱신되어) 다시 */
static uint64_t cas_fetch_add(_Atomic uint64_t *p, uint64_t v) {
    (void)p; (void)v;
    return 0; /* TODO */
}

/* ======================= Part B — 배치 카운터 · 플래그 ======================= */
/* 스레드(코어)마다 하나씩 갖는 배치기. local 에 모았다가 limit 개가 되면 공유 카운터에 한 번 더한다. */
typedef struct {
    _Atomic uint64_t *shared;
    uint64_t local;      /* 아직 공유 카운터에 안 넘긴 양 */
    uint32_t n;          /* local 에 모인 이벤트 수 */
    uint32_t limit;      /* n 이 이 값이 되면 flush */
} batch_t;

static void batch_init(batch_t *b, _Atomic uint64_t *shared, uint32_t limit) {   /* (주어진 코드) */
    b->shared = shared; b->local = 0; b->n = 0; b->limit = limit ? limit : 1;
}

/* local 에 남은 것을 공유 카운터에 넘긴다. 넘길 게 없으면 RMW 를 하지 않고 false. 넘겼으면 true. */
static bool batch_flush(batch_t *b) {
    (void)b;
    return false; /* TODO */
}

/* 이벤트 하나 (양 k). n 이 limit 에 닿으면 flush 하고 true (= 이번 호출이 RMW 를 했다). */
static bool batch_add(batch_t *b, uint64_t k) {
    (void)b; (void)k;
    return false; /* TODO */
}

/* sticky 플래그: bits 를 *f 에 OR 한다. 이미 전부 켜져 있으면 쓰지 않는다 (캐시 라인을 독점하지 않음).
 * 실제로 RMW 를 했으면 true. RMW 는 fetch_or 한 번 (CAS 루프 금지). */
static bool flags_set(_Atomic uint32_t *f, uint32_t bits) {
    (void)f; (void)bits;
    return false; /* TODO */
}

/* ======================= Part C — 듀얼 코어 패킷 경로 텔레메트리 ======================= */
/* 최대값 워터마크: v 가 현재 최대보다 클 때만 CAS. 실제로 값을 바꿨으면 true.
 * (먼저 읽고, 커야 CAS — read-before-CAS. 다른 코어가 더 큰 값을 넣었으면 CAS 실패 후 다시 비교) */
static bool wm_update(_Atomic uint32_t *m, uint32_t v) {
    (void)m; (void)v;
    return false; /* TODO */
}

#define NCORE 2
typedef struct {
    struct { _Alignas(LINE) _Atomic uint64_t pkts, bytes; } core[NCORE];   /* 코어별 칸: writer 는 그 코어 하나 */
    _Alignas(LINE) _Atomic uint32_t err;       /* sticky 오류 비트 (CRC=1, OVERRUN=2, ...) */
    _Alignas(LINE) _Atomic uint32_t max_lat;   /* 최대 처리 지연 (cycles) */
} tel_t;

typedef struct { uint64_t pkts, bytes; uint32_t err, max_lat; } tel_snap_t;

static void tel_init(tel_t *t) {                                     /* (주어진 코드) */
    for (int c = 0; c < NCORE; c++) { atomic_init(&t->core[c].pkts, 0); atomic_init(&t->core[c].bytes, 0); }
    atomic_init(&t->err, 0); atomic_init(&t->max_lat, 0);
}

/* 단일 writer 증가 (OS-B05 Part C): RMW 없이 relaxed load + relaxed store */
static void bump(_Atomic uint64_t *p, uint64_t n) {                  /* (주어진 코드) */
    atomic_store_explicit(p, atomic_load_explicit(p, memory_order_relaxed) + n, memory_order_relaxed);
}

/* 코어 `core` 의 패킷 경로에서 패킷 하나마다 호출된다. 반환 = 이번 호출이 한 공유 RMW 횟수
 * (플래그 RMW 0/1 + 워터마크 갱신 0/1). 코어별 칸 갱신은 RMW 가 아니므로 세지 않는다. */
static int tel_on_packet(tel_t *t, int core, uint32_t len, uint32_t lat, uint32_t err_bits) {
    (void)t; (void)core; (void)len; (void)lat; (void)err_bits; (void)bump;
    return -1; /* TODO: bump x2, flags_set, wm_update */
}

/* 텔레메트리 태스크: 코어별 칸을 합하고 플래그·워터마크를 읽는다 (relaxed — 통계라 근사 시점이면 된다) */
static tel_snap_t tel_snapshot(tel_t *t) {
    tel_snap_t s = { 0, 0, 0, 0 };
    (void)t; /* TODO */
    return s;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- Part A 스트레스: 4 스레드 × OPS, 세 구현 각각 ---- */
#define NT 4
#define OPS 200000u
static _Alignas(LINE) uint64_t a_lse, a_llsc;
static _Alignas(LINE) _Atomic uint64_t a_cas;
static void *a_worker(void *arg) {
    int which = *(int *)arg;
    for (uint32_t i = 0; i < OPS; i++) {
        if (which == 0) lse_fetch_add(&a_lse, 1);
        else if (which == 1) llsc_fetch_add(&a_llsc, 1);
        else cas_fetch_add(&a_cas, 1);
    }
    return NULL;
}
static bool run_threads(void *(*fn)(void *), void *args, size_t argsz, int n) {   /* n <= 16 */
    pthread_t th[16];
    for (int i = 0; i < n; i++)
        if (pthread_create(&th[i], NULL, fn, (char *)args + (size_t)i * argsz) != 0) return false;
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    return true;
}

static void test_part_a(void) {
    printf("-- Part A: stats_of / ps_per_op\n");
    uint64_t s1[] = { 50, 10, 40, 20, 30 };
    stat3_t r = stats_of(s1, 5);
    CHECK(r.min == 10 && r.median == 30 && r.max == 50);
    CHECK(s1[0] == 50 && s1[1] == 10);                          /* 원본 순서 유지 */
    uint64_t s2[] = { 7, 3, 9, 1 };
    r = stats_of(s2, 4);
    CHECK(r.min == 1 && r.median == 7 && r.max == 9);           /* 짝수: 위쪽 가운데 [2] */
    uint64_t s3[] = { 5 };
    r = stats_of(s3, 1);
    CHECK(r.min == 5 && r.median == 5 && r.max == 5);
    r = stats_of(s3, 0);
    CHECK(r.min == 0 && r.median == 0 && r.max == 0);
    uint64_t s4[] = { 9, 1000, 9, 9, 9, 9, 9 };                 /* 튀는 값 하나 — 중앙값은 안 흔들린다 */
    r = stats_of(s4, 7);
    CHECK(r.median == 9 && r.max == 1000);
    CHECK(ps_per_op(1000, 1000 + 1390, 1000) == 1390);          /* 1.39 ns/op = 1390 ps */
    CHECK(ps_per_op(0, 2000000, 1000000) == 2000);
    CHECK(ps_per_op(5, 9, 0) == 0);

    printf("-- Part A: fetch_add 3 implementations\n");
    uint64_t x = 40;
    CHECK(lse_fetch_add(&x, 2) == 40 && x == 42);
    CHECK(lse_fetch_add(&x, UINT64_MAX) == 42 && x == 41);      /* 2^64 으로 감싸기 (부호 없는 덧셈) */
    uint64_t y = 7;
    CHECK(llsc_fetch_add(&y, 5) == 7 && y == 12);
    CHECK(llsc_fetch_add(&y, 0) == 12 && y == 12);
    _Atomic uint64_t z = 100;
    CHECK(cas_fetch_add(&z, 1) == 100 && atomic_load(&z) == 101);
    CHECK(cas_fetch_add(&z, 1000) == 101 && atomic_load(&z) == 1101);

    a_lse = 0; a_llsc = 0; atomic_store(&a_cas, 0);
    int which[3 * NT];
    for (int i = 0; i < 3 * NT; i++) which[i] = i / NT;
    CHECK(run_threads(a_worker, which, sizeof which[0], 3 * NT));
    CHECK(a_lse == (uint64_t)NT * OPS);
    CHECK(a_llsc == (uint64_t)NT * OPS);
    CHECK(atomic_load(&a_cas) == (uint64_t)NT * OPS);
}

/* ---- Part B ---- */
#define BT 8
#define BEV 100003u                              /* 16 의 배수가 아니다 → 마지막 flush 가 꼭 필요 */
static _Alignas(LINE) _Atomic uint64_t b_shared;
static _Alignas(LINE) _Atomic uint32_t b_flags;
typedef struct { int tid; uint32_t flushes, flag_rmw; } bargs_t;
static void *b_worker(void *vp) {
    bargs_t *a = vp;
    batch_t b;
    batch_init(&b, &b_shared, 16);
    for (uint32_t i = 0; i < BEV; i++) if (batch_add(&b, 1)) a->flushes++;
    if (batch_flush(&b)) a->flushes++;
    for (uint32_t i = 0; i < 1000; i++) if (flags_set(&b_flags, 1u << (a->tid & 7))) a->flag_rmw++;
    return NULL;
}

static void test_part_b(void) {
    printf("-- Part B: batch counter\n");
    _Atomic uint64_t sh = 0;
    batch_t b;
    batch_init(&b, &sh, 16);
    int flushed = 0;
    for (int i = 0; i < 15; i++) flushed += batch_add(&b, 1);
    CHECK(flushed == 0 && atomic_load(&sh) == 0 && b.n == 15);  /* 15 개까지는 공유 카운터를 안 건드린다 */
    CHECK(batch_add(&b, 1) == true && atomic_load(&sh) == 16 && b.n == 0 && b.local == 0);
    CHECK(batch_add(&b, 100) == false && atomic_load(&sh) == 16);   /* 양 k 는 local 에 */
    CHECK(batch_flush(&b) == true && atomic_load(&sh) == 116);
    CHECK(batch_flush(&b) == false && atomic_load(&sh) == 116);     /* 넘길 게 없으면 RMW 없음 */
    batch_init(&b, &sh, 1);                                        /* limit 1 = 매번 fetch_add */
    CHECK(batch_add(&b, 4) == true && atomic_load(&sh) == 120);

    printf("-- Part B: flags_set\n");
    _Atomic uint32_t f = 0;
    int rmw = 0;
    for (int i = 0; i < 1000; i++) rmw += flags_set(&f, 0x1);
    CHECK(rmw == 1 && atomic_load(&f) == 0x1);                     /* 1000 번 불러도 쓰기는 1 번 */
    CHECK(flags_set(&f, 0x3) == true && atomic_load(&f) == 0x3);  /* 하나라도 빠졌으면 쓴다 */
    CHECK(flags_set(&f, 0x2) == false);
    CHECK(flags_set(&f, 0) == false && atomic_load(&f) == 0x3);

    printf("-- Part B: 8 threads\n");
    atomic_store(&b_shared, 0); atomic_store(&b_flags, 0);
    bargs_t ba[BT];
    for (int t = 0; t < BT; t++) ba[t] = (bargs_t){ t, 0, 0 };
    CHECK(run_threads(b_worker, ba, sizeof ba[0], BT));
    CHECK(atomic_load(&b_shared) == (uint64_t)BT * BEV);
    int fl_ok = 1, rmw_ok = 1;
    for (int t = 0; t < BT; t++) {
        if (ba[t].flushes != (BEV + 15) / 16) fl_ok = 0;            /* 6251 = ceil(100003/16) */
        if (ba[t].flag_rmw != 1) rmw_ok = 0;                         /* 자기 비트는 각자 1 번만 */
    }
    CHECK(fl_ok);
    CHECK(rmw_ok && atomic_load(&b_flags) == 0xFF);
}

/* ---- Part C ---- */
#define CEV 200000u
static tel_t c_tel;
typedef struct { int core; uint32_t seed; uint64_t bytes; uint32_t err, max; } cargs_t;
static void *c_worker(void *vp) {
    cargs_t *a = vp;
    uint32_t s = a->seed;
    for (uint32_t i = 0; i < CEV; i++) {
        uint32_t r = xs32(&s);
        uint32_t len = 60 + (r & 1023), lat = (r >> 10) & 0xFFFFF;
        uint32_t err = (r >> 30) == 3 ? (a->core == 0 ? 1u : 4u) : 0u;      /* 1/4 확률로 오류 비트 */
        tel_on_packet(&c_tel, a->core, len, lat, err);
        a->bytes += len; a->err |= err; if (lat > a->max) a->max = lat;
    }
    return NULL;
}

static void test_part_c(void) {
    printf("-- Part C: wm_update\n");
    _Atomic uint32_t m = 0;
    const uint32_t seq[] = { 5, 3, 5, 9, 9, 1, 12, 12, 11 };
    int writes = 0;
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) writes += wm_update(&m, seq[i]);
    CHECK(writes == 3 && atomic_load(&m) == 12);                  /* 새 최대값 5, 9, 12 에서만 쓴다 */
    CHECK(wm_update(&m, 12) == false && wm_update(&m, 0) == false);
    CHECK(wm_update(&m, UINT32_MAX) == true && atomic_load(&m) == UINT32_MAX);

    printf("-- Part C: telemetry, scripted\n");
    tel_t t;
    tel_init(&t);
    CHECK(tel_on_packet(&t, 0, 64, 100, 0) == 1);                  /* 첫 지연값 → 워터마크 갱신 1 */
    CHECK(tel_on_packet(&t, 1, 1500, 80, 0) == 0);                 /* 오류 없음, 최대 아님 → 공유 RMW 0 */
    CHECK(tel_on_packet(&t, 1, 128, 90, 2) == 1);                  /* 새 오류 비트 → 플래그 RMW 1 */
    CHECK(tel_on_packet(&t, 0, 64, 300, 2) == 1);                  /* 같은 비트(쓰지 않음) + 새 최대 */
    tel_snap_t s = tel_snapshot(&t);
    CHECK(s.pkts == 4 && s.bytes == 64 + 1500 + 128 + 64);
    CHECK(s.err == 2 && s.max_lat == 300);
    CHECK(atomic_load(&t.core[0].pkts) == 2 && atomic_load(&t.core[1].bytes) == 1628);

    printf("-- Part C: 2 cores\n");
    tel_init(&c_tel);
    cargs_t ca[NCORE] = { { 0, 0x12345678u, 0, 0, 0 }, { 1, 0x9abcdef1u, 0, 0, 0 } };
    CHECK(run_threads(c_worker, ca, sizeof ca[0], NCORE));
    s = tel_snapshot(&c_tel);
    CHECK(s.pkts == (uint64_t)NCORE * CEV);
    CHECK(s.bytes == ca[0].bytes + ca[1].bytes);
    CHECK(s.err == (ca[0].err | ca[1].err) && s.err == 5u);
    CHECK(s.max_lat == (ca[0].max > ca[1].max ? ca[0].max : ca[1].max));
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
