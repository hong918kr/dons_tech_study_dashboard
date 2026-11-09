/* MEM-M04 demo_bench.c — 일관성 트래픽 실측: 세 패턴 × 1~12 스레드 처리량 (관찰용, CHECK 없음)
 *
 *   ./bench curve [ms] [reps] [qos]   패턴 a / a' / b / c / d 를 k = 1..12 스레드로, 점 하나 = ms 밀리초 실행,
 *                                     reps 번 재서 중앙값 (기본 100 ms × 3 → 약 20 초)
 *   ./bench runs  [ms] [qos]          패턴 a 에서 "핸드오프 비율": fetch_add 가 돌려준 옛 값이 내 직전 값 + 1 이
 *                                     아니면 그 사이에 다른 코어가 썼다 → 핸드오프 1 (k = 1..12, 약 5 초)
 *   qos : ui (USER_INTERACTIVE, 기본) | bg (BACKGROUND → E 코어 쪽으로)
 *
 *   a  : 공유 카운터 fetch_add relaxed (ldadd)        a' : 같은 카운터 CAS 루프 (ldr + cas), 실패 횟수 셈
 *   b  : 스레드별 카운터 (128 B 라인마다), fetch_add   c  : 읽기 전용 테이블 4 라인을 모두 읽기 (ldr)
 *   d  : read-mostly — 스레드 0 이 설정 워드를 계속 갱신 (store 1 번 + 자기 라인 읽기 256 번),
 *        나머지 k-1 스레드는 그 워드를 읽기만. reader 처리량만 보고 (k = 1 은 reader 없음)
 *
 * 처리량 = 모든 스레드 연산 합 / 경과 시간 (Mops/s). E% = 1024 연산마다 pthread_cpu_number_np 로 본 cpu 번호 중
 * 0..5 (LLAT-M05 의 추정 매핑: 0–5 E, 6–7 Super, 8–11 Performance) 의 비율.
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o bench
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

#define LINE 128
#define MAXT 12
#define BLK 1024
#define RO_WORDS (4 * LINE / 8)

static _Alignas(LINE) _Atomic uint64_t g_shared;
typedef struct { _Alignas(LINE) _Atomic uint64_t v; } slot_t;
static slot_t g_slot[MAXT];
static _Alignas(LINE) _Atomic uint64_t g_ro[RO_WORDS];
static _Alignas(LINE) _Atomic uint64_t g_cfg;
static _Alignas(LINE) _Atomic int g_ready;
static _Alignas(LINE) _Atomic int g_go;
static _Alignas(LINE) _Atomic int g_stop;
static qos_class_t g_qos = QOS_CLASS_USER_INTERACTIVE;

enum { P_ADD, P_CAS, P_PRIV, P_RO, P_RMOSTLY, P_RUNS, NPAT };
static const char *pname[] = { "a  shared fetch_add", "a' shared CAS loop", "b  per-thread line",
                               "c  read-only shared", "d  read-mostly", "runs" };

typedef struct {
    _Alignas(LINE) int pat, t;
    uint64_t ops, fails, sink, handoffs, eblk, blks;
} targ_t;

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int is_e(void) { size_t c = 0; pthread_cpu_number_np(&c); return c < 6; }

static void *worker(void *p) {
    targ_t *a = p;
    pthread_set_qos_class_self_np(g_qos, 0);
    atomic_fetch_add(&g_ready, 1);
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
    uint64_t ops = 0, fails = 0, sink = 0, hand = 0, prev = UINT64_MAX - 1, v = 0;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        a->eblk += (uint64_t)is_e();
        a->blks++;
        switch (a->pat) {
        case P_ADD:
            for (int i = 0; i < BLK; i++) atomic_fetch_add_explicit(&g_shared, 1, memory_order_relaxed);
            ops += BLK; break;
        case P_CAS:
            for (int i = 0; i < BLK; i++) {
                uint64_t old = atomic_load_explicit(&g_shared, memory_order_relaxed);
                while (!atomic_compare_exchange_weak_explicit(&g_shared, &old, old + 1,
                                                              memory_order_relaxed, memory_order_relaxed)) fails++;
            }
            ops += BLK; break;
        case P_PRIV:
            for (int i = 0; i < BLK; i++) atomic_fetch_add_explicit(&g_slot[a->t].v, 1, memory_order_relaxed);
            ops += BLK; break;
        case P_RO:
            for (int i = 0; i < BLK; i++) sink += atomic_load_explicit(&g_ro[i % RO_WORDS], memory_order_relaxed);
            ops += BLK; break;
        case P_RMOSTLY:
            if (a->t == 0) {                 /* writer: 갱신 1 번 + 자기 라인 읽기 256 번 (간격) */
                atomic_store_explicit(&g_cfg, ++v, memory_order_relaxed);
                for (int i = 0; i < 256; i++) sink += atomic_load_explicit(&g_slot[0].v, memory_order_relaxed);
                ops += 1;
            } else {
                for (int i = 0; i < BLK; i++) sink += atomic_load_explicit(&g_cfg, memory_order_relaxed);
                ops += BLK;
            }
            break;
        case P_RUNS:
            for (int i = 0; i < BLK; i++) {
                uint64_t o = atomic_fetch_add_explicit(&g_shared, 1, memory_order_relaxed);
                if (o != prev + 1) hand++;
                prev = o;
            }
            ops += BLK; break;
        default: break;
        }
    }
    a->ops = ops; a->fails = fails; a->sink = sink; a->handoffs = hand;
    return NULL;
}

typedef struct { double mops, wmops, fails_per_op, hand_per_op, epct; } res_t;

static res_t one(int pat, int k, int ms) {
    pthread_t th[MAXT];
    static targ_t a[MAXT];
    memset(a, 0, sizeof a);
    atomic_store(&g_ready, 0); atomic_store(&g_go, 0); atomic_store(&g_stop, 0);
    atomic_store(&g_shared, 0);
    for (int t = 0; t < k; t++) { a[t].pat = pat; a[t].t = t; pthread_create(&th[t], NULL, worker, &a[t]); }
    while (atomic_load(&g_ready) < k) { }
    uint64_t t0 = now_ns();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    struct timespec d = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&d, NULL);
    atomic_store(&g_stop, 1);
    for (int t = 0; t < k; t++) pthread_join(th[t], NULL);
    uint64_t t1 = now_ns();
    uint64_t ops = 0, wops = 0, fails = 0, hand = 0, eb = 0, bl = 0;
    for (int t = 0; t < k; t++) {
        if (pat == P_RMOSTLY && t == 0) wops += a[t].ops; else ops += a[t].ops;
        fails += a[t].fails; hand += a[t].handoffs; eb += a[t].eblk; bl += a[t].blks;
    }
    double us = (double)(t1 - t0) / 1000.0;
    res_t r = { (double)ops / us, (double)wops / us, ops ? (double)fails / (double)ops : 0,
                ops ? (double)hand / (double)ops : 0, bl ? 100.0 * (double)eb / (double)bl : 0 };
    return r;
}

static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
static res_t med(int pat, int k, int ms, int reps) {
    double m[16], w[16], f[16], h[16], e[16];
    for (int i = 0; i < reps; i++) { res_t r = one(pat, k, ms); m[i] = r.mops; w[i] = r.wmops; f[i] = r.fails_per_op; h[i] = r.hand_per_op; e[i] = r.epct; }
    qsort(m, (size_t)reps, sizeof(double), cmpd); qsort(w, (size_t)reps, sizeof(double), cmpd);
    qsort(f, (size_t)reps, sizeof(double), cmpd); qsort(h, (size_t)reps, sizeof(double), cmpd);
    qsort(e, (size_t)reps, sizeof(double), cmpd);
    res_t r = { m[reps / 2], w[reps / 2], f[reps / 2], h[reps / 2], e[reps / 2] };
    return r;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "curve";
    int ms = argc > 2 ? atoi(argv[2]) : 100;
    int reps = argc > 3 ? atoi(argv[3]) : 3;
    const char *q = !strcmp(mode, "runs") ? (argc > 3 ? argv[3] : "ui") : (argc > 4 ? argv[4] : "ui");
    if (!strcmp(q, "bg")) g_qos = QOS_CLASS_BACKGROUND;
    if (ms < 10) ms = 10;
    if (ms > 500) ms = 500;
    if (reps < 1) reps = 1;
    if (reps > 15) reps = 15;
    for (int j = 0; j < RO_WORDS; j++) atomic_store(&g_ro[j], (uint64_t)j);
    pthread_set_qos_class_self_np(g_qos, 0);
    (void)one(P_ADD, 2, 50);                      /* 워밍업 (클럭 올리기), 버림 */
    if (!strcmp(mode, "runs")) {
        printf("pattern a, value-order handoffs (qos %s, %d ms/point)\n", q, ms);
        printf(" k |  Mops/s | handoff/op | ops per run | E%%\n");
        for (int k = 1; k <= MAXT; k++) {
            res_t r = one(P_RUNS, k, ms);
            printf("%2d | %7.1f | %10.4f | %11.1f | %3.0f\n", k, r.mops, r.hand_per_op,
                   r.hand_per_op > 0 ? 1.0 / r.hand_per_op : 0.0, r.epct);
        }
        return 0;
    }
    printf("Mops/s total (median of %d x %d ms), qos %s. [E%%] ; a' also CAS fails/op ; d also writer updates/us\n", reps, ms, q);
    printf(" k | %-14s | %-22s | %-14s | %-14s | %s\n", pname[0], pname[1], pname[2], pname[3], pname[4]);
    for (int k = 1; k <= MAXT; k++) {
        res_t a = med(P_ADD, k, ms, reps), c = med(P_CAS, k, ms, reps), b = med(P_PRIV, k, ms, reps);
        res_t ro = med(P_RO, k, ms, reps);
        printf("%2d | %8.1f [%3.0f] | %7.1f [%3.0f] f=%5.2f | %8.1f [%3.0f] | %8.1f [%3.0f] |", k, a.mops, a.epct,
               c.mops, c.epct, c.fails_per_op, b.mops, b.epct, ro.mops, ro.epct);
        if (k >= 2) { res_t d = med(P_RMOSTLY, k, ms, reps); printf(" %8.1f [%3.0f] w=%.2f\n", d.mops, d.epct, d.wmops); }
        else printf("        -\n");
        fflush(stdout);
    }
    return 0;
}
