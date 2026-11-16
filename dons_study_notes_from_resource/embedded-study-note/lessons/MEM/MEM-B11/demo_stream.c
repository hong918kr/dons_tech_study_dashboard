/* MEM-B11 demo_stream.c — 직접 짠 STREAM 을 이 맥(Apple M6)에서 실제로 잰다 (관찰용, CHECK 없음)
 *
 *   ./demo_stream [N] [ntimes] [maxthreads] [sections]
 *     N          배열 하나의 double 원소 수 (기본 2^26 = 512 MiB/배열, 3 배열 1.5 GiB)
 *     ntimes     STREAM 반복 수 (기본 10, 첫 반복은 워밍업으로 버림)
 *     maxthreads 스레드 스윕 상한 (기본 hw.ncpu = 12)
 *     sections   돌릴 절 (기본 "123")
 *
 *   [1] 스레드 스윕: t = 1..maxthreads, 커널마다 best-of-(ntimes-1) GB/s + 결과 검증 (stream_check)
 *   [2] 크기 스윕: Triad, 배열 하나 16 KiB .. N, t = 1 과 t = maxthreads — 캐시 안이면 "메모리 대역폭"이 아니다
 *   [3] write-allocate 실험: read-only sum / fill / copy, 보통 store vs __builtin_nontemporal_store (stnp)
 *
 * 스레드: 메인 스레드 = 0 번 작업자, 나머지는 스핀 대기 풀. macOS 는 코어 고정이 안 된다 (affinity →
 * KERN_NOT_SUPPORTED) → QoS USER_INTERACTIVE 로 빠른 코어를 "선호"만 한다.
 * 시간: clock_gettime(CLOCK_MONOTONIC_RAW). 측정 1 회 = 모든 스레드 시작 ~ 마지막 스레드 끝.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_stream.c -o demo_stream
 *   주의: 기본 -O2 에서 clang 은 k_copy 를 memcpy 호출로, k_fill 을 memset_pattern16 호출로 바꾼다
 *   (loop idiom recognition). 손으로 짠 루프 그대로 재려면 -fno-builtin 을 더해 한 번 더 빌드한다.
 */
#define _DARWIN_C_SOURCE
#include <math.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>

#define LINE_DBL 16u          /* 128 B 캐시라인 = 16 double: 내부 경계를 맞춰 경계 라인 공유를 없앤다 */
#define MAXT 16
#define STREAM_EPS 1e-13
static const double Q = 3.0;

/* ---------------- 커널 (noinline: clang -S 로 루프가 남았는지/벡터화됐는지 이름으로 찾기 위해) ---------------- */
__attribute__((noinline)) static void k_copy(double *restrict c, const double *restrict a, size_t n) {
    for (size_t i = 0; i < n; i++) c[i] = a[i];
}
__attribute__((noinline)) static void k_scale(double *restrict b, const double *restrict c, double q, size_t n) {
    for (size_t i = 0; i < n; i++) b[i] = q * c[i];
}
__attribute__((noinline)) static void k_add(double *restrict c, const double *restrict a, const double *restrict b, size_t n) {
    for (size_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}
__attribute__((noinline)) static void k_triad(double *restrict a, const double *restrict b, const double *restrict c, double q, size_t n) {
    for (size_t i = 0; i < n; i++) a[i] = b[i] + q * c[i];
}
/* 읽기 전용: FP 덧셈은 재결합(reassociate)할 수 없어 컴파일러가 스칼라 사슬로 둔다 → 누산기가 적으면
 * "FP add 지연"이 한계가 되어 메모리를 못 재게 된다 (처음 판: 스칼라 4 사슬 → 1 스레드 33~40 GB/s 에서 멈춤).
 * 2-lane 벡터 누산기 8 개(= 16 사슬)로 지연을 숨긴다. memcpy 는 ldr q 한 개로 컴파일된다. */
typedef double v2d __attribute__((vector_size(16)));
__attribute__((noinline)) static double k_sum(const double *restrict a, size_t n) {
    v2d s[8] = { { 0, 0 } };
    size_t i = 0;
    for (; i + 16 <= n; i += 16)
        for (int j = 0; j < 8; j++) { v2d x; memcpy(&x, a + i + 2 * j, sizeof x); s[j] += x; }
    double r = 0;
    for (; i < n; i++) r += a[i];
    for (int j = 0; j < 8; j++) r += s[j][0] + s[j][1];
    return r;
}
__attribute__((noinline)) static void k_fill(double *restrict c, double v, size_t n) {
    for (size_t i = 0; i < n; i++) c[i] = v;
}
__attribute__((noinline)) static void k_fill_nt(double *restrict c, double v, size_t n) {
    for (size_t i = 0; i < n; i++) __builtin_nontemporal_store(v, &c[i]);
}
__attribute__((noinline)) static void k_copy_nt(double *restrict c, const double *restrict a, size_t n) {
    for (size_t i = 0; i < n; i++) __builtin_nontemporal_store(a[i], &c[i]);
}

enum { OP_COPY, OP_SCALE, OP_ADD, OP_TRIAD, OP_SUM, OP_FILL, OP_FILL_NT, OP_COPY_NT, OP_INIT, OP_N };
static const char *OPNAME[OP_N] = { "Copy", "Scale", "Add", "Triad", "Sum(read)", "Fill", "Fill-NT",
                                    "Copy-NT", "init" };
static const unsigned OPBYTES[OP_N] = { 16, 16, 24, 24, 8, 8, 8, 16, 0 };   /* STREAM 규약 B/원소 */

/* ---------------- solution.c 에서 가져온 결정적 부품 (축약판) ---------------- */
static size_t boundary(size_t n, unsigned t, unsigned j, size_t align) {
    if (j == 0) return 0;
    if (j >= t) return n;
    size_t x = (size_t)((uint64_t)n * j / t);
    if (align > 1) x -= x % align;
    return x;
}
static void stream_expected(int ntimes, double q, double *aj, double *bj, double *cj) {
    double a = 2.0, b = 2.0, c = 0.0;
    for (int t = 0; t < ntimes; t++) { c = a; b = q * c; c = a + b; a = b + q * c; }
    *aj = a; *bj = b; *cj = c;
}
static unsigned stream_check(const double *a, const double *b, const double *c, size_t n, int ntimes, double q) {
    double aj, bj, cj, ea = 0, eb = 0, ec = 0;
    stream_expected(ntimes, q, &aj, &bj, &cj);
    for (size_t i = 0; i < n; i++) { ea += fabs(a[i] - aj); eb += fabs(b[i] - bj); ec += fabs(c[i] - cj); }
    unsigned bad = 0;
    if (fabs(ea / (double)n / aj) > STREAM_EPS) bad |= 1u;
    if (fabs(eb / (double)n / bj) > STREAM_EPS) bad |= 2u;
    if (fabs(ec / (double)n / cj) > STREAM_EPS) bad |= 4u;
    return bad;
}

/* ---------------- 스레드 풀 ---------------- */
static double *g_a, *g_b, *g_c;
static struct { int op; size_t n, reps; unsigned t; } g_job;
static _Alignas(128) _Atomic unsigned g_gen;
static _Alignas(128) _Atomic unsigned g_done;
static _Alignas(128) _Atomic int g_quit;
static unsigned g_start_gen;                            /* 풀을 만들 때의 g_gen: 새 작업자는 이 값부터 기다린다 */
typedef struct { _Alignas(128) double sink; } slot_t;
static slot_t g_sink[MAXT];

static inline void cpu_relax(unsigned *spins) {
    if (++*spins < 4096) __asm__ volatile("yield");
    else { *spins = 0; sched_yield(); }                 /* 공용 머신: 오래 기다리면 CPU 를 양보 */
}

static void do_chunk(unsigned id) {
    size_t lo = boundary(g_job.n, g_job.t, id, LINE_DBL), hi = boundary(g_job.n, g_job.t, id + 1, LINE_DBL);
    size_t m = hi - lo;
    double *a = g_a + lo, *b = g_b + lo, *c = g_c + lo, s = 0;
    for (size_t r = 0; r < g_job.reps; r++) {
        switch (g_job.op) {
        case OP_COPY:    k_copy(c, a, m); break;
        case OP_SCALE:   k_scale(b, c, Q, m); break;
        case OP_ADD:     k_add(c, a, b, m); break;
        case OP_TRIAD:   k_triad(a, b, c, Q, m); break;
        case OP_SUM:     s += k_sum(a, m); break;
        case OP_FILL:    k_fill(c, 1.0, m); break;
        case OP_FILL_NT: k_fill_nt(c, 1.0, m); break;
        case OP_COPY_NT: k_copy_nt(c, a, m); break;
        case OP_INIT:    /* STREAM 초기값 a=1·2, b=2, c=0 — 실행할 스레드가 직접 써서 페이지를 만든다 (first touch) */
            for (size_t i = 0; i < m; i++) { a[i] = 2.0; b[i] = 2.0; c[i] = 0.0; }
            break;
        default: break;
        }
    }
    g_sink[id].sink += s;
}

static void *worker(void *p) {
    unsigned id = (unsigned)(uintptr_t)p, seen = g_start_gen, spins = 0;   /* 0 으로 두면 옛 세대를 새 일로 착각 */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    for (;;) {
        unsigned g;
        while ((g = atomic_load_explicit(&g_gen, memory_order_acquire)) == seen) cpu_relax(&spins);
        seen = g;
        if (atomic_load_explicit(&g_quit, memory_order_relaxed)) break;
        if (id < g_job.t) do_chunk(id);
        atomic_fetch_add_explicit(&g_done, 1, memory_order_release);
    }
    return NULL;
}

static pthread_t g_th[MAXT];
static unsigned g_pool;                                 /* 만든 작업자 수 (0 번 = main 제외) */

static void pool_start(unsigned t) {
    atomic_store(&g_quit, 0);
    g_start_gen = atomic_load(&g_gen);                  /* pthread_create 가 이 쓰기를 작업자에게 보이게 한다 */
    g_pool = t - 1;
    for (unsigned i = 1; i < t; i++) pthread_create(&g_th[i], NULL, worker, (void *)(uintptr_t)i);
}
static void pool_stop(void) {
    atomic_store(&g_quit, 1);
    atomic_fetch_add_explicit(&g_gen, 1, memory_order_release);
    for (unsigned i = 1; i <= g_pool; i++) pthread_join(g_th[i], NULL);
    g_pool = 0;
}
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
/* 풀 크기 = t 로 job 하나를 돌리고 걸린 초를 돌려준다. */
static double run(int op, size_t n, size_t reps, unsigned t) {
    unsigned spins = 0;
    g_job.op = op; g_job.n = n; g_job.reps = reps; g_job.t = t;
    atomic_store_explicit(&g_done, 0, memory_order_relaxed);
    double t0 = now_s();
    atomic_fetch_add_explicit(&g_gen, 1, memory_order_release);
    do_chunk(0);
    while (atomic_load_explicit(&g_done, memory_order_acquire) < g_pool) cpu_relax(&spins);
    return now_s() - t0;
}

static double best_gbs(int op, size_t n, size_t reps, unsigned t, int ntimes, double *avg_gbs) {
    double mn = INFINITY, sum = 0;
    for (int k = 0; k < ntimes; k++) {
        double s = run(op, n, reps, t);
        if (k == 0) continue;                           /* STREAM: 첫 반복 제외 */
        if (s < mn) mn = s;
        sum += s;
    }
    double bytes = (double)OPBYTES[op] * (double)n * (double)reps;
    if (avg_gbs) *avg_gbs = bytes / (sum / (ntimes - 1)) * 1e-9;
    return bytes / mn * 1e-9;
}

static void print_load(const char *tag) {
    double la[3];
    if (getloadavg(la, 3) == 3) printf("[load avg %s: %.2f %.2f %.2f]\n", tag, la[0], la[1], la[2]);
}

int main(int argc, char **argv) {
    size_t N = argc > 1 ? strtoull(argv[1], NULL, 0) : ((size_t)1 << 26);
    int ntimes = argc > 2 ? atoi(argv[2]) : 10;
    int ncpu = 0; size_t len = sizeof ncpu;
    sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);
    unsigned maxt = argc > 3 ? (unsigned)atoi(argv[3]) : (unsigned)ncpu;
    const char *sec = argc > 4 ? argv[4] : "123";
    if (N < 1024 || ntimes < 2 || maxt < 1 || maxt > MAXT) { fprintf(stderr, "bad args\n"); return 2; }

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    size_t bytes = N * sizeof(double);
    if (posix_memalign((void **)&g_a, 16384, bytes) || posix_memalign((void **)&g_b, 16384, bytes) ||
        posix_memalign((void **)&g_c, 16384, bytes)) { fprintf(stderr, "alloc %zu B x3 failed\n", bytes); return 2; }

    printf("MEM-B11 demo_stream: N = %zu doubles = %.0f MiB/array (%.2f GiB total), ntimes = %d, hw.ncpu = %d\n",
           N, bytes / 1048576.0, 3.0 * bytes / 1073741824.0, ntimes, ncpu);
    /* 타이머 분해능: 0 이 아닌 최소 간격 */
    double tick = INFINITY;
    for (int i = 0; i < 1000; i++) { double x = now_s(), y; while ((y = now_s()) == x) {} if (y - x < tick) tick = y - x; }
    printf("timer: CLOCK_MONOTONIC_RAW min nonzero delta = %.0f ns\n", tick * 1e9);
    print_load("start");

    /* first touch: 최대 스레드로 초기화 → 페이지 폴트를 측정 밖으로 */
    pool_start(maxt);
    double ti = run(OP_INIT, N, 1, maxt);
    printf("first-touch init (page faults incl.): %.1f ms\n", ti * 1e3);
    pool_stop();

    /* ---------- [1] 스레드 스윕 ---------- */
    printf("\n[1] thread sweep — best GB/s over %d timed iters (first dropped), STREAM byte counting\n", ntimes - 1);
    printf("  t     Copy    Scale      Add    Triad | Triad avg | valid\n");
    double triad_best[MAXT] = { 0 };
    for (unsigned t = 1; t <= maxt && strchr(sec, '1'); t++) {
        pool_start(t);
        run(OP_INIT, N, 1, t);
        double mn[4] = { INFINITY, INFINITY, INFINITY, INFINITY }, sum3 = 0;
        for (int k = 0; k < ntimes; k++)
            for (int op = OP_COPY; op <= OP_TRIAD; op++) {
                double s = run(op, N, 1, t);
                if (k == 0) continue;
                if (s < mn[op]) mn[op] = s;
                if (op == OP_TRIAD) sum3 += s;
            }
        unsigned bad = stream_check(g_a, g_b, g_c, N, ntimes, Q);
        pool_stop();
        double g[4];
        for (int op = 0; op < 4; op++) g[op] = OPBYTES[op] * (double)N / mn[op] * 1e-9;
        triad_best[t - 1] = g[3];
        printf("%3u  %7.1f  %7.1f  %7.1f  %7.1f | %9.1f | %s\n", t, g[0], g[1], g[2], g[3],
               24.0 * N / (sum3 / (ntimes - 1)) * 1e-9, bad ? "FAIL" : "ok");
    }
    print_load("after [1]");

    /* ---------- [2] 크기 스윕 (Triad) ---------- */
    printf("\n[2] size sweep — Triad best GB/s; per-array size; reps chosen so one timing moves >= ~256 MB\n");
    printf("  per-array     3 arrays   t=1     t=%u\n", maxt);
    for (size_t n = 2048; n <= N && strchr(sec, '2'); n *= 2) {
        size_t reps = ((size_t)1 << 24) / n; if (reps < 1) reps = 1;   /* 24 B × n × reps ≥ 384 MiB 정도 */
        double g1, gm;
        pool_start(1);    g1 = best_gbs(OP_TRIAD, n, reps, 1, ntimes, NULL); pool_stop();
        pool_start(maxt); gm = best_gbs(OP_TRIAD, n, reps, maxt, ntimes, NULL); pool_stop();
        double kib = n * 8.0 / 1024.0;
        if (kib < 1024) printf("  %6.0f KiB  %6.0f KiB  %6.1f  %6.1f\n", kib, 3 * kib, g1, gm);
        else            printf("  %6.0f MiB  %6.0f MiB  %6.1f  %6.1f\n", kib / 1024, 3 * kib / 1024, g1, gm);
    }
    print_load("after [2]");

    /* ---------- [3] write-allocate / non-temporal ---------- */
    unsigned tsat = 1;
    double mx = 0;
    for (unsigned t = 0; t < maxt; t++) if (triad_best[t] > mx) mx = triad_best[t];
    for (unsigned t = 0; t < maxt; t++) if (mx > 0 && triad_best[t] >= 0.9 * mx) { tsat = t + 1; break; }
    printf("\n[3] write-allocate experiment — reported GB/s (STREAM counting: read 8 B + write 8 B per elem, no RFO)\n");
    printf("    saturation threads (>= 90%% of best Triad) = %u\n", tsat);
    printf("  op          B/elem   t=1 best   t=%u best  t=%u best\n", tsat, maxt);
    const int ops[] = { OP_SUM, OP_FILL, OP_FILL_NT, OP_COPY, OP_COPY_NT, OP_TRIAD };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0] && strchr(sec, '3'); i++) {
        double r[3]; unsigned ts[3] = { 1, tsat, maxt };
        for (int j = 0; j < 3; j++) {
            pool_start(ts[j]);
            if (ops[i] == OP_TRIAD) run(OP_INIT, N, 1, ts[j]);
            r[j] = best_gbs(ops[i], N, 1, ts[j], ntimes, NULL);
            pool_stop();
        }
        printf("  %-10s  %6u   %8.1f   %8.1f  %8.1f\n", OPNAME[ops[i]], OPBYTES[ops[i]], r[0], r[1], r[2]);
    }
    print_load("end");

    double s = 0;
    for (int i = 0; i < MAXT; i++) s += g_sink[i].sink;
    printf("\n(sink %.3g — printed so the read-only kernel cannot be deleted)\n", s);
    free(g_a); free(g_b); free(g_c);
    return 0;
}
