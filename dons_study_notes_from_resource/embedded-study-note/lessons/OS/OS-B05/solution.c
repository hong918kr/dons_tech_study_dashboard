/* OS-B05 뮤텍스: 레이스 해결과 경합 비용 측정
 *
 * 공유 카운터를 세 가지로 고친다 (레이스 재현 자체는 OS-B04).
 *   Part A  전역 뮤텍스 카운터 mc_*  /  atomic fetch_add 카운터 ac_*
 *   Part B  락 범위(scope): check-then-act 를 한 임계 구역으로 (mc_try_add)
 *           락 세분성(granularity): sloppy counter sc_* (OSTEP 29장, 스레드별 로컬 + 임계값 flush)
 *   Part C  워커별 통계 카운터: 단일 writer 슬롯 + baseline 리셋 (stats_*)
 *
 * 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 검사한다 (최종 합계 == N*K,
 * 한도 초과 없음, 읽은 값이 줄어들지 않음). 처리량(Mops/s)은 절대 CHECK 하지 않는다 → demo_bench.c.
 * 락 범위 버그의 강제 스케줄 재현은 demo_scope.c.
 * 각 Part 의 멀티스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CACHE_LINE 128   /* Apple M 시리즈 hw.cachelinesize = 128. Cortex-A 는 보통 64, Cortex-M7 D-cache 32 */

/* ======================= Part A — 전역 뮤텍스 카운터 / atomic 카운터 ======================= */
typedef struct {
    pthread_mutex_t lock;
    long value;                  /* lock 을 잡은 스레드만 읽고 쓴다 */
} mcounter_t;

static int mc_init(mcounter_t *c) {           /* (주어진 코드) */
    c->value = 0;
    return pthread_mutex_init(&c->lock, NULL);
}
static void mc_destroy(mcounter_t *c) { pthread_mutex_destroy(&c->lock); }

/* value += n — 여러 스레드가 동시에 불러도 증가를 하나도 잃지 않는다. */
static void mc_add(mcounter_t *c, long n) {
    pthread_mutex_lock(&c->lock);
    c->value += n;
    pthread_mutex_unlock(&c->lock);
}
/* 현재 값. 읽기도 락 안에서 (쓰기와 동시에 일반 읽기 = 데이터 레이스). */
static long mc_get(mcounter_t *c) {
    pthread_mutex_lock(&c->lock);
    long v = c->value;
    pthread_mutex_unlock(&c->lock);
    return v;
}

typedef struct { atomic_long value; } acounter_t;

/* value += n — 원자 RMW 한 번. 개수만 세므로 순서 보장은 필요 없다 (memory_order 는 C-A03). */
static void ac_add(acounter_t *c, long n) {
    atomic_fetch_add_explicit(&c->value, n, memory_order_relaxed);
}
static long ac_get(acounter_t *c) {
    return atomic_load_explicit(&c->value, memory_order_relaxed);
}

/* ======================= Part B — 락 범위와 세분성 ======================= */
/* B1. 한도 카운터: value + n <= limit 일 때만 더하고 true. (TX 크레딧, 로그 큐 바이트 한도 등)
 *     "검사" 와 "갱신" 이 같은 임계 구역 안에 있어야 한다. mc_get() 후 mc_add() 는 틀린 답 (demo_scope.c). */
static bool mc_try_add(mcounter_t *c, long n, long limit) {
    pthread_mutex_lock(&c->lock);
    bool ok = c->value + n <= limit;
    if (ok) c->value += n;
    pthread_mutex_unlock(&c->lock);
    return ok;
}

/* B2. sloppy (approximate) counter — OSTEP 29.1.
 *     스레드마다 로컬 카운트(+ 로컬 락)를 두고, 로컬이 threshold 이상이 되면 전역으로 옮긴다.
 *     락 순서 규칙: 둘 다 잡을 때는 항상 local → global. */
#define SC_MAXT 8
typedef struct {
    _Alignas(CACHE_LINE) pthread_mutex_t lock;   /* 슬롯 하나 = 캐시 라인 하나 (false sharing 방지, ARCH-M08) */
    long count;
} sc_local_t;
_Static_assert(sizeof(sc_local_t) % CACHE_LINE == 0, "one slot per cache line");

typedef struct {
    pthread_mutex_t glock;
    long global;                 /* glock 으로 보호 */
    long threshold;              /* S: 로컬이 이만큼 모이면 전역으로 */
    int nthreads;
    sc_local_t local[SC_MAXT];
} sloppy_t;

static int sc_init(sloppy_t *c, int nthreads, long threshold) {   /* (주어진 코드) */
    if (nthreads < 1 || nthreads > SC_MAXT || threshold < 1) return -1;
    c->global = 0;
    c->threshold = threshold;
    c->nthreads = nthreads;
    int rc = pthread_mutex_init(&c->glock, NULL);
    for (int i = 0; i < nthreads; i++) {
        c->local[i].count = 0;
        rc |= pthread_mutex_init(&c->local[i].lock, NULL);
    }
    return rc;
}
static void sc_destroy(sloppy_t *c) {
    for (int i = 0; i < c->nthreads; i++) pthread_mutex_destroy(&c->local[i].lock);
    pthread_mutex_destroy(&c->glock);
}

/* 스레드 tid 가 amt (> 0) 를 더한다. 로컬이 threshold 이상이면 전역으로 옮기고 로컬을 0 으로. */
static void sc_update(sloppy_t *c, int tid, long amt) {
    sc_local_t *l = &c->local[tid];
    pthread_mutex_lock(&l->lock);
    l->count += amt;
    if (l->count >= c->threshold) {
        pthread_mutex_lock(&c->glock);           /* local → global 순서 */
        c->global += l->count;
        pthread_mutex_unlock(&c->glock);
        l->count = 0;
    }
    pthread_mutex_unlock(&l->lock);
}
/* 근사값: 전역만 읽는다. 진짜 합보다 최대 nthreads*(threshold-1) 작을 수 있다. */
static long sc_get(sloppy_t *c) {
    pthread_mutex_lock(&c->glock);
    long v = c->global;
    pthread_mutex_unlock(&c->glock);
    return v;
}
/* 정확한 값: 모든 로컬을 전역으로 옮긴 뒤 전역을 읽는다. 락 순서는 sc_update 와 같게 local → global. */
static long sc_get_exact(sloppy_t *c) {
    for (int i = 0; i < c->nthreads; i++) {
        sc_local_t *l = &c->local[i];
        pthread_mutex_lock(&l->lock);
        pthread_mutex_lock(&c->glock);
        c->global += l->count;
        pthread_mutex_unlock(&c->glock);
        l->count = 0;
        pthread_mutex_unlock(&l->lock);
    }
    return sc_get(c);
}

/* ======================= Part C — 워커별 통계 카운터 (per-core stats) ======================= */
/* 패킷 처리 워커(코어) 마다 슬롯 하나. 슬롯에 쓰는 스레드는 그 워커 하나뿐 (single writer).
 * 텔레메트리 스레드가 합산해 읽고, "통계 지우기" 는 워커 슬롯을 건드리지 않고 기준값(base)을 옮긴다. */
#define ST_MAXW 8
typedef struct {
    _Alignas(CACHE_LINE) atomic_ulong pkts;
    atomic_ulong bytes;
    atomic_ulong drops;
} wstat_t;
_Static_assert(sizeof(wstat_t) == CACHE_LINE, "one worker per cache line");

typedef struct { unsigned long pkts, bytes, drops; } snap_t;
typedef struct {
    wstat_t w[ST_MAXW];
    int nworkers;
    pthread_mutex_t lock;        /* 읽는 쪽끼리만: base 와 read/clear 를 보호. 워커는 잡지 않는다 */
    snap_t base;
} stats_t;

static int stats_init(stats_t *s, int nworkers) {   /* (주어진 코드) */
    if (nworkers < 1 || nworkers > ST_MAXW) return -1;
    for (int i = 0; i < ST_MAXW; i++) {
        atomic_init(&s->w[i].pkts, 0ul);
        atomic_init(&s->w[i].bytes, 0ul);
        atomic_init(&s->w[i].drops, 0ul);
    }
    s->nworkers = nworkers;
    s->base = (snap_t){0, 0, 0};
    return pthread_mutex_init(&s->lock, NULL);
}
static void stats_destroy(stats_t *s) { pthread_mutex_destroy(&s->lock); }

/* 단일 writer 증가: 쓰는 스레드가 하나뿐이면 RMW(fetch_add) 가 필요 없다.
 * relaxed load + relaxed store — 읽는 스레드와는 원자 접근끼리라 데이터 레이스가 아니다. */
static void bump(atomic_ulong *p, unsigned long n) {
    atomic_store_explicit(p, atomic_load_explicit(p, memory_order_relaxed) + n, memory_order_relaxed);
}
/* 워커 wid 가 len 바이트 패킷 하나를 받았다. */
static void stats_rx(stats_t *s, int wid, unsigned len) {
    bump(&s->w[wid].pkts, 1ul);
    bump(&s->w[wid].bytes, len);
}
/* 워커 wid 가 패킷 하나를 버렸다. */
static void stats_drop(stats_t *s, int wid) {
    bump(&s->w[wid].drops, 1ul);
}
/* 모든 워커 슬롯의 합 (원시값). */
static snap_t raw_sum(stats_t *s) {
    snap_t r = {0, 0, 0};
    for (int i = 0; i < s->nworkers; i++) {
        r.pkts  += atomic_load_explicit(&s->w[i].pkts, memory_order_relaxed);
        r.bytes += atomic_load_explicit(&s->w[i].bytes, memory_order_relaxed);
        r.drops += atomic_load_explicit(&s->w[i].drops, memory_order_relaxed);
    }
    return r;
}
/* 마지막 clear 이후의 합계 = 원시 합 - base. base 를 읽는 동안 다른 reader 가 clear 하지 못하게 락. */
static snap_t stats_read(stats_t *s) {
    pthread_mutex_lock(&s->lock);
    snap_t r = raw_sum(s);
    r.pkts -= s->base.pkts;
    r.bytes -= s->base.bytes;
    r.drops -= s->base.drops;
    pthread_mutex_unlock(&s->lock);
    return r;
}
/* 통계 지우기: 워커 슬롯은 그대로 두고 현재 원시 합을 base 로. */
static void stats_clear(stats_t *s) {
    pthread_mutex_lock(&s->lock);
    s->base = raw_sum(s);
    pthread_mutex_unlock(&s->lock);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {        /* 단위 테스트가 실패하면 스트레스는 건너뛴다(=실패로 셈) */
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

#define NT 4                                       /* 스트레스 스레드 수 */
#define ITERS 100000
static atomic_int g_go;                            /* 출발 신호: 모든 스레드가 동시에 시작 */
static atomic_int g_done;                          /* 작업 스레드가 끝났음 (관찰 스레드 종료용) */
static void wait_go(void) { while (!atomic_load_explicit(&g_go, memory_order_acquire)) { } }
typedef struct { int tid; long ok; } warg_t;
static void run_threads(void *(*fn)(void *), warg_t *a, void *(*observer)(void *)) {
    pthread_t th[NT], ob;
    atomic_store(&g_go, 0);
    atomic_store(&g_done, 0);
    if (observer) pthread_create(&ob, NULL, observer, NULL);
    for (int i = 0; i < NT; i++) { a[i].tid = i; a[i].ok = 0; pthread_create(&th[i], NULL, fn, &a[i]); }
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    atomic_store_explicit(&g_done, 1, memory_order_release);
    if (observer) pthread_join(ob, NULL);
}

/* --- Part A/B 스트레스 대상 --- */
static mcounter_t g_mc;
static acounter_t g_ac;
static void *mc_worker(void *arg) { (void)arg; wait_go(); for (int i = 0; i < ITERS; i++) mc_add(&g_mc, 1); return NULL; }
static void *ac_worker(void *arg) { (void)arg; wait_go(); for (int i = 0; i < ITERS; i++) ac_add(&g_ac, 1); return NULL; }

#define LIMIT 250000L                              /* NT*ITERS = 400000 번 시도, 250000 번만 성공해야 */
static void *try_worker(void *arg) {
    warg_t *a = arg;
    wait_go();
    for (int i = 0; i < ITERS; i++) a->ok += mc_try_add(&g_mc, 1, LIMIT);
    return NULL;
}

#define SC_S 64
static sloppy_t g_sc;
static long g_obs_bad, g_obs_reads;                /* 관찰 스레드 결과 (join 뒤에 main 이 읽는다) */
static void *sc_worker(void *arg) {
    warg_t *a = arg;
    wait_go();
    for (int i = 0; i < ITERS; i++) sc_update(&g_sc, a->tid, 1);
    return NULL;
}
static void *sc_observer(void *arg) {              /* 텔레메트리: 근사값은 줄어들면 안 되고 총합을 넘으면 안 된다 */
    (void)arg;
    long last = 0, bad = 0, n = 0;
    while (!atomic_load_explicit(&g_done, memory_order_acquire)) {
        long v = (n % 256 == 255) ? sc_get_exact(&g_sc) : sc_get(&g_sc);   /* 가끔은 exact 도 (락 순서 검증) */
        if (v < last || v > (long)NT * ITERS) bad++;
        last = v;
        n++;
    }
    g_obs_bad = bad;
    g_obs_reads = n;
    return NULL;
}

/* --- Part C 스트레스 대상 --- */
static stats_t g_st;
static unsigned pkt_len(int i) { return 64u + (unsigned)(i & 63); }
static void *rx_worker(void *arg) {
    warg_t *a = arg;
    wait_go();
    for (int i = 0; i < ITERS; i++) {
        if (i % 50 == 49) stats_drop(&g_st, a->tid);
        else stats_rx(&g_st, a->tid, pkt_len(i));
    }
    return NULL;
}
static void *st_observer(void *arg) {
    (void)arg;
    snap_t last = {0, 0, 0};
    long bad = 0, n = 0;
    while (!atomic_load_explicit(&g_done, memory_order_acquire)) {
        snap_t s = stats_read(&g_st);
        if (s.pkts < last.pkts || s.bytes < last.bytes || s.drops < last.drops) bad++;   /* 필드별 단조 증가 */
        last = s;
        n++;
    }
    g_obs_bad = bad;
    g_obs_reads = n;
    return NULL;
}

int main(void) {
    warg_t wa[NT];
    /* ---------- Part A: 단위 ---------- */
    int f0 = FAILS();
    mcounter_t m;
    CHECK(mc_init(&m) == 0);
    mc_add(&m, 5);
    mc_add(&m, -2);
    CHECK(mc_get(&m) == 3);
    mc_destroy(&m);
    acounter_t ac;
    atomic_init(&ac.value, 10);
    ac_add(&ac, 7);
    CHECK(ac_get(&ac) == 17);
    CHECK(atomic_is_lock_free(&ac.value));         /* lock-free 가 아니면 "atomic" 도 내부 락이다 */
    /* ---------- Part A: 스트레스 ---------- */
    if (FAILS() == f0) {
        mc_init(&g_mc);
        run_threads(mc_worker, wa, NULL);
        CHECK(mc_get(&g_mc) == (long)NT * ITERS);  /* 증가 하나도 안 잃음 */
        mc_destroy(&g_mc);
        run_threads(ac_worker, wa, NULL);
        CHECK(ac_get(&g_ac) == (long)NT * ITERS);
    } else skip(2, "Part A");

    /* ---------- Part B: 단위 ---------- */
    f0 = FAILS();
    mc_init(&m);
    CHECK(mc_try_add(&m, 6, 10) && mc_get(&m) == 6);
    CHECK(!mc_try_add(&m, 5, 10) && mc_get(&m) == 6);   /* 넘치면 아무것도 안 더한다 */
    CHECK(mc_try_add(&m, 4, 10) && mc_get(&m) == 10);   /* 딱 한도까지는 된다 */
    mc_destroy(&m);
    sloppy_t *sc = &g_sc;
    CHECK(sc_init(sc, 2, 5) == 0);
    for (int i = 0; i < 4; i++) sc_update(sc, 0, 1);
    CHECK(sc_get(sc) == 0 && sc->local[0].count == 4);  /* 아직 로컬에만 */
    sc_update(sc, 0, 1);
    CHECK(sc_get(sc) == 5 && sc->local[0].count == 0);  /* 5 = S → 전역으로 옮김 */
    sc_update(sc, 1, 3);
    CHECK(sc_get(sc) == 5);                             /* 근사값: 스레드 1 의 3 은 아직 안 보인다 */
    CHECK(sc_get_exact(sc) == 8 && sc->local[1].count == 0 && sc_get(sc) == 8);
    sc_update(sc, 1, 12);                               /* S 보다 큰 한 번 → 즉시 전역으로 */
    CHECK(sc_get(sc) == 20);
    sc_destroy(sc);
    CHECK(sc_init(sc, SC_MAXT + 1, 5) != 0 && sc_init(sc, 1, 0) != 0);   /* (주어진 코드) 인자 검사 */
    /* ---------- Part B: 스트레스 ---------- */
    if (FAILS() == f0) {
        mc_init(&g_mc);
        run_threads(try_worker, wa, NULL);
        long ok = 0;
        for (int i = 0; i < NT; i++) ok += wa[i].ok;
        CHECK(mc_get(&g_mc) == LIMIT);                   /* 한도를 절대 넘지 않고, 정확히 채운다 */
        CHECK(ok == LIMIT);                              /* true 를 받은 횟수 == 실제로 더한 횟수 */
        mc_destroy(&g_mc);

        sc_init(&g_sc, NT, SC_S);
        run_threads(sc_worker, wa, sc_observer);
        long approx = sc_get(&g_sc);
        long total = (long)NT * ITERS;
        CHECK(g_obs_bad == 0);                           /* 관찰 중 근사값은 줄지 않고 총합 이하 */
        CHECK(approx <= total && total - approx <= (long)NT * (SC_S - 1));   /* 오차 상한 T*(S-1) */
        CHECK(sc_get_exact(&g_sc) == total);             /* flush 하면 정확 */
        printf("Part B sloppy: approx=%ld exact=%ld (bound %d), observer reads=%ld\n",
               approx, total, NT * (SC_S - 1), g_obs_reads);
        sc_destroy(&g_sc);
    } else skip(5, "Part B");

    /* ---------- Part C: 단위 ---------- */
    f0 = FAILS();
    stats_t *st = &g_st;
    CHECK(stats_init(st, 2) == 0);
    stats_rx(st, 0, 100); stats_rx(st, 0, 100); stats_rx(st, 0, 100);
    stats_rx(st, 1, 50);
    stats_drop(st, 1);
    snap_t s = stats_read(st);
    CHECK(s.pkts == 4 && s.bytes == 350 && s.drops == 1);
    CHECK(atomic_load(&st->w[0].pkts) == 3 && atomic_load(&st->w[1].bytes) == 50);   /* 각자 자기 슬롯에 */
    stats_clear(st);
    s = stats_read(st);
    CHECK(s.pkts == 0 && s.bytes == 0 && s.drops == 0);
    stats_rx(st, 1, 60);
    s = stats_read(st);
    CHECK(s.pkts == 1 && s.bytes == 60 && s.drops == 0);
    CHECK(atomic_load(&st->w[0].pkts) == 3);            /* clear 는 워커 슬롯을 건드리지 않는다 */
    stats_destroy(st);
    /* ---------- Part C: 스트레스 ---------- */
    if (FAILS() == f0) {
        stats_init(&g_st, NT);
        run_threads(rx_worker, wa, st_observer);
        unsigned long want_p = 0, want_b = 0, want_d = 0;
        for (int i = 0; i < ITERS; i++) {
            if (i % 50 == 49) want_d++;
            else { want_p++; want_b += pkt_len(i); }
        }
        s = stats_read(&g_st);
        CHECK(g_obs_bad == 0);                           /* 텔레메트리가 본 값은 필드별로 줄지 않는다 */
        CHECK(s.pkts == NT * want_p && s.bytes == NT * want_b && s.drops == NT * want_d);
        printf("Part C stats: pkts=%lu bytes=%lu drops=%lu, observer reads=%ld\n",
               s.pkts, s.bytes, s.drops, g_obs_reads);
        stats_destroy(&g_st);
    } else skip(2, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
