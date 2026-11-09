/* ARCH-M08 demo_bench.c — false sharing 거리(stride) 스윕. 시간은 여기서만 잰다 (CHECK 없음).
 *
 *   N 개 스레드가 각자 "자기" uint32 카운터만 올린다. 카운터 i 의 주소 = arena + i*stride (arena 는 4096 정렬).
 *   stride 4..512 B 를 바꿔 가며 ns/증가 를 재고, stride ≥ 256 평균(=기준) 대비 몇 배 느린지 본다.
 *   증가 방식 3가지 (demo_asm.c 로 명령 확인):
 *     atomic : atomic_fetch_add_explicit(relaxed)  → arm64 `ldadd`  (LSE 원자 RMW)
 *     vstore : *p = ++local  (volatile 저장만)      → `str`          (읽기 없음)
 *     vrmw   : *p = *p + 1   (volatile 읽고 쓰기)   → `ldr; add; str` (칸 주인이 한 명이라 정확)
 *   QoS: ui = USER_INTERACTIVE (P/Super 코어 선호), bg = BACKGROUND (E 코어로 감) — 힌트일 뿐이라
 *   1<<16 증가마다 pthread_cpu_number_np 로 코어 번호를 적어 E% (cpu < 6, LLAT-M05 의 추정 매핑) 를 같이 찍는다.
 *
 *   build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o /tmp/ARCH-M08_work/bench
 *   run:   /tmp/ARCH-M08_work/bench sweep ui    # 2 스레드, 3 모드 × 8 stride × 3 run
 *          /tmp/ARCH-M08_work/bench sweep bg
 *          /tmp/ARCH-M08_work/bench nthr ui     # N = 2,4,6,8,12 스레드, atomic, stride 4/64/128/256
 *          /tmp/ARCH-M08_work/bench cpus bg     # 코어 번호 분포만 (매핑 확인용)
 *          /tmp/ARCH-M08_work/bench struct ui   # starter 배치 pq_t vs solution 배치 pq_t (2 스레드, 자기 그룹만 갱신)
 *          /tmp/ARCH-M08_work/bench pairs [bg]  # 2 스레드 atomic, 2^16 증가 묶음마다 "나와 상대의 코어 종류" 로 분류
 *   코어 번호 → 종류: 0–5 E, 6–7 Super, 8–11 P (LLAT-M05 와 같은 추정. 이 레슨에서도 단일 스레드 의존 사슬
 *   속도로 다시 확인: 0–5 ≈ 3.4 ns/iter, 6–7 ≈ 1.25, 8–11 ≈ 1.47 — 클럭 차이로 보이는 세 무리)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <pthread/qos.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXT 12
#define CHUNK (1u << 16)
#define ECUT 6                 /* cpu 번호 < 6 → E 코어 (LLAT-M05 타이밍 추정, Apple 미공개) */

enum { M_ATOMIC, M_VSTORE, M_VRMW };
static const char *mode_name[] = { "atomic", "vstore", "vrmw" };

static _Alignas(4096) unsigned char arena[MAXT * 512 + 4096];

typedef struct {
    int idx, mode;
    size_t stride;
    uint32_t iters;
    qos_class_t qos;
    _Atomic int *ready, *go;
    unsigned e_samples, samples;
    unsigned cpuhist[32];
} job_t;

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

__attribute__((noinline)) static void run_atomic(_Atomic uint32_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) atomic_fetch_add_explicit(p, 1, memory_order_relaxed);
}
__attribute__((noinline)) static void run_vstore(volatile uint32_t *p, uint32_t n, uint32_t start) {
    uint32_t local = start;
    for (uint32_t i = 0; i < n; i++) *p = ++local;
}
__attribute__((noinline)) static void run_vrmw(volatile uint32_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) *p = *p + 1;
}

static void *worker(void *arg) {
    job_t *j = arg;
    pthread_set_qos_class_self_np(j->qos, 0);
    void *slot = arena + (size_t)j->idx * j->stride;
    atomic_fetch_add(j->ready, 1);
    while (!atomic_load_explicit(j->go, memory_order_acquire)) { }
    for (uint32_t done = 0; done < j->iters; done += CHUNK) {
        uint32_t n = j->iters - done < CHUNK ? j->iters - done : CHUNK;
        if (j->mode == M_ATOMIC) run_atomic((_Atomic uint32_t *)slot, n);
        else if (j->mode == M_VSTORE) run_vstore((volatile uint32_t *)slot, n, done);
        else run_vrmw((volatile uint32_t *)slot, n);
        size_t c = 0;
        pthread_cpu_number_np(&c);
        j->samples++;
        if (c < ECUT) j->e_samples++;
        if (c < 32) j->cpuhist[c]++;
    }
    return NULL;
}

typedef struct { double ns_per_op; unsigned e_pct_min, e_pct_max; int ok; } result_t;

static result_t run_once(int nthr, size_t stride, int mode, uint32_t iters, qos_class_t qos, unsigned *hist) {
    pthread_t th[MAXT];
    job_t jobs[MAXT];
    _Atomic int ready = 0, go = 0;
    memset(arena, 0, sizeof arena);
    for (int i = 0; i < nthr; i++) {
        jobs[i] = (job_t){ .idx = i, .mode = mode, .stride = stride, .iters = iters, .qos = qos,
                           .ready = &ready, .go = &go };
        pthread_create(&th[i], NULL, worker, &jobs[i]);
    }
    while (atomic_load(&ready) < nthr) { }
    uint64_t t0 = now_ns();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
    uint64_t t1 = now_ns();
    result_t r = { (double)(t1 - t0) / iters, 100, 0, 1 };
    for (int i = 0; i < nthr; i++) {
        uint32_t v;
        memcpy(&v, arena + (size_t)i * stride, sizeof v);
        if (v != iters) r.ok = 0;                       /* 정확성은 매번 확인 (CHECK 는 solution.c) */
        unsigned pct = jobs[i].samples ? 100 * jobs[i].e_samples / jobs[i].samples : 0;
        if (pct < r.e_pct_min) r.e_pct_min = pct;
        if (pct > r.e_pct_max) r.e_pct_max = pct;
        if (hist) for (int c = 0; c < 32; c++) hist[c] += jobs[i].cpuhist[c];
    }
    return r;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define REPS 3
static result_t run_median(int nthr, size_t stride, int mode, uint32_t iters, qos_class_t qos) {
    result_t rs[REPS];
    double v[REPS];
    for (int k = 0; k < REPS; k++) { rs[k] = run_once(nthr, stride, mode, iters, qos, NULL); v[k] = rs[k].ns_per_op; }
    qsort(v, REPS, sizeof v[0], cmp_d);
    result_t r = { v[REPS / 2], 100, 0, 1 };
    for (int k = 0; k < REPS; k++) {
        if (rs[k].e_pct_min < r.e_pct_min) r.e_pct_min = rs[k].e_pct_min;
        if (rs[k].e_pct_max > r.e_pct_max) r.e_pct_max = rs[k].e_pct_max;
        if (!rs[k].ok) r.ok = 0;
    }
    return r;
}

/* solution.c 의 estimate_granule 과 같은 규칙: 가장 큰 stride 부터 내려오며, 기준 대비 tol 배 이내인
 * stride 가 연속으로 이어지는 마지막(가장 작은) stride. 기준 = 마지막 두 stride(256, 512) 평균. */
static size_t estimate_granule(const size_t *s, const double *ns, int n, double tol) {
    double base = (ns[n - 1] + ns[n - 2]) / 2;
    size_t g = 0;
    for (int i = n - 1; i >= 0 && ns[i] <= tol * base; i--) g = s[i];
    return g;
}

/* ---------- pairs: 묶음(chunk)마다 두 스레드의 코어 종류를 기록해 쌍 종류별 ns/op 중앙값 ---------- */
static int cls_of(size_t c) { return c < 6 ? 0 : c < 8 ? 1 : 2; }       /* 0=E 1=S(uper) 2=P */
static const char *pair_name[] = { "E-E", "E-S", "E-P", "S-S", "S-P", "P-P" };
static int pair_idx(int a, int b) {
    if (a > b) { int t = a; a = b; b = t; }
    return a == 0 ? b : a == 1 ? 2 + b : 5;                               /* (0,0)0 (0,1)1 (0,2)2 (1,1)3 (1,2)4 (2,2)5 */
}
#define PMAX 8192
static _Alignas(4096) struct { _Atomic int cpu; _Atomic int fin; char pad[4096 - 2 * sizeof(int)]; } peer[2];
static float pvals[6][PMAX];
static int pcount[6];
static pthread_mutex_t pmu = PTHREAD_MUTEX_INITIALIZER;

typedef struct { int idx; size_t stride; uint32_t iters; qos_class_t qos; _Atomic int *ready, *go; } pjob_t;

static void *pworker(void *arg) {
    pjob_t *j = arg;
    pthread_set_qos_class_self_np(j->qos, 0);
    _Atomic uint32_t *slot = (_Atomic uint32_t *)(arena + (size_t)j->idx * j->stride);
    _Atomic uint32_t *oslot = (_Atomic uint32_t *)(arena + (size_t)(1 - j->idx) * j->stride);
    int me = j->idx, other = 1 - j->idx;
    atomic_fetch_add(j->ready, 1);
    while (!atomic_load_explicit(j->go, memory_order_acquire)) { }
    for (uint32_t done = 0; done < j->iters; done += CHUNK) {
        size_t c0 = 0, c1 = 0;
        pthread_cpu_number_np(&c0);
        atomic_store_explicit(&peer[me].cpu, (int)c0, memory_order_relaxed);
        int o0 = atomic_load_explicit(&peer[other].cpu, memory_order_relaxed);
        uint32_t ov0 = atomic_load_explicit(oslot, memory_order_relaxed);
        uint64_t t0 = now_ns();
        run_atomic(slot, CHUNK);
        uint64_t t1 = now_ns();
        uint32_t ov1 = atomic_load_explicit(oslot, memory_order_relaxed);
        pthread_cpu_number_np(&c1);
        int o1 = atomic_load_explicit(&peer[other].cpu, memory_order_relaxed);
        int ofin = atomic_load_explicit(&peer[other].fin, memory_order_relaxed);
        if (c0 == c1 && o0 >= 0 && o1 >= 0 && cls_of((size_t)o0) == cls_of((size_t)o1) && !ofin &&
            ov1 - ov0 >= CHUNK / 4) {   /* 상대가 내 묶음 동안 실제로 돌았나 (선점당했으면 경합이 없다) */
            int k = pair_idx(cls_of(c0), cls_of((size_t)o0));
            pthread_mutex_lock(&pmu);
            if (pcount[k] < PMAX) pvals[k][pcount[k]++] = (float)(t1 - t0) / CHUNK;
            pthread_mutex_unlock(&pmu);
        }
    }
    atomic_store_explicit(&peer[me].fin, 1, memory_order_relaxed);
    return NULL;
}

static int cmp_f(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void pairs_mode(int only_bg) {   /* only_bg: bg+bg 만 6번 (E-E 표본을 모으려고) */
    static const size_t st[] = { 32, 64, 96, 128, 256 };
    static const qos_class_t qa[3][2] = { { QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_USER_INTERACTIVE },
                                          { QOS_CLASS_BACKGROUND, QOS_CLASS_BACKGROUND },
                                          { QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_BACKGROUND } };
    printf("pairs: 2 threads atomic(ldadd), chunks of 2^16, qos %s, 2^24 each;\n",
           only_bg ? "bg+bg x 6 runs" : "ui+ui / bg+bg / ui+bg x 3 runs");
    printf("median ns/op per core-kind pair (n = chunks; kept only if the peer advanced >= 2^14 during the chunk), '-' = < 20\n");
    printf("stride ");
    for (int k = 0; k < 6; k++) printf("| %-14s", pair_name[k]);
    printf("\n");
    for (int s = 0; s < 5; s++) {
        memset(pcount, 0, sizeof pcount);
        for (int c = only_bg; c < (only_bg ? 2 : 3); c++)
            for (int r = 0; r < (only_bg ? 6 : 3); r++) {
                pthread_t th[2];
                pjob_t jb[2];
                _Atomic int ready = 0, go = 0;
                memset(arena, 0, sizeof arena);
                for (int i = 0; i < 2; i++) {
                    atomic_store(&peer[i].cpu, -1);
                    atomic_store(&peer[i].fin, 0);
                }
                for (int i = 0; i < 2; i++) {
                    jb[i] = (pjob_t){ i, st[s], 1u << 24, qa[c][i], &ready, &go };
                    pthread_create(&th[i], NULL, pworker, &jb[i]);
                }
                while (atomic_load(&ready) < 2) { }
                atomic_store_explicit(&go, 1, memory_order_release);
                for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
            }
        printf("%4zu B ", st[s]);
        for (int k = 0; k < 6; k++) {
            if (pcount[k] < 20) { printf("| %-14s", "-"); continue; }
            qsort(pvals[k], (size_t)pcount[k], sizeof(float), cmp_f);
            printf("| %5.2f (n%5d) ", pvals[k][pcount[k] / 2], pcount[k]);
        }
        printf("\n");
    }
}

/* ---------- struct: starter.c 의 pq_t 배치 vs solution.c 의 pq_t 배치 ---------- */
typedef struct {                                      /* starter.c 그대로 */
    _Atomic uint32_t head; _Atomic uint32_t tail; uint8_t *ring; uint32_t mask, irq;
    uint32_t rx_packets, rx_handled, tail_cache, head_cache; char name[16]; uint32_t err_overrun;
} pq_bad_t;
typedef struct {                                      /* solution.c 그대로 */
    uint8_t *ring; uint32_t mask, irq; char name[16]; uint32_t err_overrun;
    _Alignas(128) _Atomic uint32_t head; uint32_t rx_packets, tail_cache;
    _Alignas(128) _Atomic uint32_t tail; uint32_t rx_handled, head_cache;
} pq_good_t;
static _Alignas(128) pq_bad_t g_bad;
static _Alignas(128) pq_good_t g_good;
typedef struct { int good, core; uint32_t iters; qos_class_t qos; _Atomic int *ready, *go; } sjob_t;
#define PQ_BODY(q) do {                                                              \
        if (j->core == 0) for (uint32_t i = 0; i < j->iters; i++) {                  \
            atomic_fetch_add_explicit(&(q)->head, 1, memory_order_release);          \
            ((volatile uint32_t *)&(q)->rx_packets)[0]++; ((volatile uint32_t *)&(q)->tail_cache)[0] = i; } \
        else for (uint32_t i = 0; i < j->iters; i++) {                               \
            atomic_fetch_add_explicit(&(q)->tail, 1, memory_order_release);          \
            ((volatile uint32_t *)&(q)->rx_handled)[0]++; ((volatile uint32_t *)&(q)->head_cache)[0] = i; } \
    } while (0)
static void *sworker(void *arg) {
    sjob_t *j = arg;
    pthread_set_qos_class_self_np(j->qos, 0);
    atomic_fetch_add(j->ready, 1);
    while (!atomic_load_explicit(j->go, memory_order_acquire)) { }
    if (j->good) PQ_BODY(&g_good); else PQ_BODY(&g_bad);
    return NULL;
}
static double struct_once(int good, uint32_t iters, qos_class_t qos) {
    pthread_t th[2];
    sjob_t jb[2];
    _Atomic int ready = 0, go = 0;
    memset(&g_bad, 0, sizeof g_bad);
    memset(&g_good, 0, sizeof g_good);
    for (int i = 0; i < 2; i++) {
        jb[i] = (sjob_t){ good, i, iters, qos, &ready, &go };
        pthread_create(&th[i], NULL, sworker, &jb[i]);
    }
    while (atomic_load(&ready) < 2) { }
    uint64_t t0 = now_ns();
    atomic_store_explicit(&go, 1, memory_order_release);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    return (double)(now_ns() - t0) / iters;
}
static void struct_mode(qos_class_t qos, const char *q) {
    const uint32_t iters = 1u << 23;
    printf("struct: 2 threads (core0 group / core1 group), qos %s, 2^23 updates each, 5 runs, ns per update\n", q);
    printf("  layout  sizeof  runs\n");
    for (int good = 0; good < 2; good++) {
        printf("  %-6s  %4zu  ", good ? "good" : "bad", good ? sizeof(pq_good_t) : sizeof(pq_bad_t));
        for (int r = 0; r < 5; r++) printf(" %6.2f", struct_once(good, iters, qos));
        printf("\n");
    }
}

int main(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "sweep";
    const char *q = argc > 2 ? argv[2] : "ui";
    qos_class_t qos = strcmp(q, "bg") == 0 ? QOS_CLASS_BACKGROUND : QOS_CLASS_USER_INTERACTIVE;
    static const size_t strides[] = { 4, 8, 16, 32, 64, 128, 256, 512 };
    const int ns_ = (int)(sizeof strides / sizeof strides[0]);

    if (strcmp(what, "struct") == 0) { struct_mode(qos, q); return 0; }
    if (strcmp(what, "pairs") == 0) { pairs_mode(argc > 2 && strcmp(argv[2], "bg") == 0); return 0; }
    if (strcmp(what, "cpus") == 0) {
        unsigned hist[32] = { 0 };
        for (int k = 0; k < 5; k++) run_once(2, 512, M_ATOMIC, 1u << 23, qos, hist);
        printf("qos %s, 2 threads, cpu histogram (samples of 2^16 increments):\n", q);
        for (int c = 0; c < 16; c++) if (hist[c]) printf("  cpu %2d : %u\n", c, hist[c]);
        return 0;
    }
    if (strcmp(what, "sweep") == 0) {
        /* 스레드당 증가 수: atomic 2^24, vstore/vrmw 2^27 (한 번에 0.2 ns 라 짧으면 스레드 출발 차이가 섞인다) */
        static const uint32_t iters_m[3] = { 1u << 24, 1u << 27, 1u << 27 };
        printf("sweep: 2 threads, qos %s, increments/thread atomic 2^24 vstore/vrmw 2^27, median of %d runs\n", q, REPS);
        printf("%-7s", "stride");
        for (int m = 0; m < 3; m++) printf(" | %-8s ns/op  x  E%%min-max", mode_name[m]);
        printf("\n");
        double ns[3][8];
        result_t res[3][8];
        for (int i = 0; i < ns_; i++)
            for (int m = 0; m < 3; m++) { res[m][i] = run_median(2, strides[i], m, iters_m[m], qos); ns[m][i] = res[m][i].ns_per_op; }
        for (int i = 0; i < ns_; i++) {
            printf("%5zu B", strides[i]);
            for (int m = 0; m < 3; m++) {
                double base = (ns[m][ns_ - 1] + ns[m][ns_ - 2]) / 2;
                printf(" | %6.2f %6.1fx  %3u-%3u%s", ns[m][i], ns[m][i] / base, res[m][i].e_pct_min,
                       res[m][i].e_pct_max, res[m][i].ok ? "" : " BAD");
            }
            printf("\n");
        }
        for (int m = 0; m < 3; m++)
            printf("estimated granule (%s, tol 1.5x): %zu B\n", mode_name[m], estimate_granule(strides, ns[m], ns_, 1.5));
        return 0;
    }
    if (strcmp(what, "nthr") == 0) {
        static const int nt[] = { 2, 4, 6, 8, 12 };
        static const size_t st[] = { 4, 64, 128, 256 };
        const uint32_t iters = 1u << 21;
        printf("nthr: atomic(ldadd), qos %s, %u increments/thread, median of %d runs; ns/op (E%%min-max)\n", q, iters, REPS);
        printf("threads");
        for (int s = 0; s < 4; s++) printf(" | stride %3zu B       ", st[s]);
        printf("\n");
        for (int t = 0; t < 5; t++) {
            printf("%7d", nt[t]);
            for (int s = 0; s < 4; s++) {
                result_t r = run_median(nt[t], st[s], M_ATOMIC, iters, qos);
                printf(" | %6.2f (%3u-%3u)%s", r.ns_per_op, r.e_pct_min, r.e_pct_max, r.ok ? "" : " BAD");
            }
            printf("\n");
        }
        return 0;
    }
    fprintf(stderr, "usage: %s sweep|nthr|cpus|pairs|struct ui|bg\n", argv[0]);
    return 2;
}
