/* ARM-M04 demo_bench.c — 같은 캐시 라인 하나를 두고 1..8 스레드가 경합할 때 LL/SC vs LSE.
 *
 *   add      : LDAXR/ADD/STLXR 루프            vs  LDADDAL
 *   cas-inc  : LL/SC strong CAS 로 +1 루프      vs  CASAL 로 +1 루프
 *   cas-inc  : CAS 루프는 현재 값을 LDR 로 읽고 시작 (일반적인 모양)
 *   lock     : LDAXR/STXR + WFE 스핀락          vs  SWPA + (LDXR/WFE) 스핀락   (임계 구역 = 일반 변수 +1)
 *              + 비교용: 매번 SEVL;WFE 를 거치는 LL/SC 락, WFE 없이 도는 LL/SC 락
 *   retries  : LL/SC add 에서 STLXR 가 실패한 횟수 / 연산 (경합의 직접 증거)
 *
 * 시간: clock_gettime_nsec_np(CLOCK_UPTIME_RAW), 출발 신호 후 마지막 스레드 종료까지의 벽시계.
 * 각 칸 = 5회 중 중앙값. macOS 는 스레드를 코어에 고정할 수 없다 (P/E 코어 배치는 스케줄러 마음).
 * 타이밍은 체크하지 않는다 — 출력만.
 *
 * build: cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LSE ".arch_extension lse\n\t"
#define MAXT 8
#define REPS 5

static inline uint32_t llsc_add(uint32_t *p, uint32_t v) {
    uint32_t old, sum, st;
    __asm__ volatile("1: ldaxr %w0, %3\n add %w1, %w0, %w4\n stlxr %w2, %w1, %3\n cbnz %w2, 1b\n"
                     : "=&r"(old), "=&r"(sum), "=&r"(st), "+Q"(*p) : "r"(v) : "memory");
    return old;
}
static inline uint32_t llsc_add_retries(uint32_t *p, uint32_t v) {   /* 반환 = STLXR 실패 횟수 */
    uint32_t old, sum, st, n = 0;
    __asm__ volatile("1: ldaxr %w0, %3\n add %w1, %w0, %w5\n stlxr %w2, %w1, %3\n"
                     " cbz %w2, 2f\n add %w4, %w4, #1\n b 1b\n2:\n"
                     : "=&r"(old), "=&r"(sum), "=&r"(st), "+Q"(*p), "+&r"(n) : "r"(v) : "memory");
    return n;
}
static inline uint32_t lse_add(uint32_t *p, uint32_t v) {
    uint32_t old;
    __asm__ volatile(LSE "ldaddal %w2, %w0, %1" : "=r"(old), "+Q"(*p) : "r"(v) : "memory");
    return old;
}
static inline uint32_t llsc_cas(uint32_t *p, uint32_t e, uint32_t d) {
    uint32_t old, st;
    __asm__ volatile("1: ldaxr %w0, %2\n cmp %w0, %w3\n b.ne 2f\n stlxr %w1, %w4, %2\n cbnz %w1, 1b\n b 3f\n"
                     "2: clrex\n3:\n"
                     : "=&r"(old), "=&r"(st), "+Q"(*p) : "r"(e), "r"(d) : "cc", "memory");
    return old;
}
static inline uint32_t lse_cas(uint32_t *p, uint32_t e, uint32_t d) {
    __asm__ volatile(LSE "casal %w0, %w2, %1" : "+r"(e), "+Q"(*p) : "r"(d) : "memory");
    return e;
}
static inline uint32_t ld(const uint32_t *p) {           /* 정렬된 32비트 LDR = single-copy atomic */
    uint32_t v;
    __asm__ volatile("ldr %w0, %1" : "=r"(v) : "Q"(*p) : "memory");
    return v;
}
static inline void llsc_lock(uint32_t *l) {                /* solution.c 와 같은 모양: 빠른 경로 먼저 */
    uint32_t old, st;
    __asm__ volatile("1: ldaxr %w0, %2\n cbnz %w0, 2f\n stxr %w1, %w3, %2\n cbnz %w1, 1b\n b 3f\n"
                     "2: wfe\n b 1b\n3:\n"
                     : "=&r"(old), "=&r"(st), "+Q"(*l) : "r"(1u) : "memory");
}
static inline void llsc_lock_sevl(uint32_t *l) {           /* 매번 SEVL; WFE 를 거치는 모양 (비교용) */
    uint32_t old, st;
    __asm__ volatile(" sevl\n1: wfe\n2: ldaxr %w0, %2\n cbnz %w0, 1b\n stxr %w1, %w3, %2\n cbnz %w1, 2b\n"
                     : "=&r"(old), "=&r"(st), "+Q"(*l) : "r"(1u) : "memory");
}
static inline void llsc_lock_spin(uint32_t *l) {           /* WFE 없이 LDAXR 만 계속 (비교용) */
    uint32_t old, st;
    __asm__ volatile("1: ldaxr %w0, %2\n cbnz %w0, 1b\n stxr %w1, %w3, %2\n cbnz %w1, 1b\n"
                     : "=&r"(old), "=&r"(st), "+Q"(*l) : "r"(1u) : "memory");
}
static inline void lse_lock(uint32_t *l) {
    uint32_t old;
    __asm__ volatile(LSE "1: swpa %w2, %w0, %1\n cbz %w0, 3f\n2: ldxr %w0, %1\n cbz %w0, 1b\n wfe\n b 2b\n3:\n"
                     : "=&r"(old), "+Q"(*l) : "r"(1u) : "memory");
}
static inline void unlock(uint32_t *l) { __asm__ volatile("stlr wzr, %0" : "=Q"(*l) : : "memory"); }

static struct { _Alignas(128) uint32_t v; } g_cnt;          /* 경합 대상: 캐시 라인 하나 */
static struct { _Alignas(128) uint32_t v; } g_lock;
static struct { _Alignas(128) uint64_t v; } g_plain;
static struct { _Alignas(128) uint64_t retries; } g_ret[MAXT];
static atomic_int g_go;
static int g_op, g_nt;
static uint32_t g_iters;

enum { OP_LLSC_ADD, OP_LSE_ADD, OP_LLSC_CAS, OP_LSE_CAS, OP_LLSC_LOCK, OP_LLSC_SEVL, OP_LLSC_SPIN,
       OP_LSE_LOCK, OP_RETRIES, NOPS };
static const char *op_name[NOPS] = { "LL/SC add", "LSE ldaddal", "LL/SC cas-inc", "LSE casal-inc",
                                     "LL/SC lock", "LL/SC lock sevl", "LL/SC lock nowfe", "LSE swpa lock",
                                     "retries/op" };
#define IS_LOCK(op) ((op) >= OP_LLSC_LOCK && (op) <= OP_LSE_LOCK)

static void *worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t n = g_iters;
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) { }
    switch (g_op) {
    case OP_LLSC_ADD: for (uint32_t i = 0; i < n; i++) (void)llsc_add(&g_cnt.v, 1u); break;
    case OP_LSE_ADD:  for (uint32_t i = 0; i < n; i++) (void)lse_add(&g_cnt.v, 1u); break;
    case OP_LLSC_CAS: for (uint32_t i = 0; i < n; i++) { uint32_t c = ld(&g_cnt.v), s; while ((s = llsc_cas(&g_cnt.v, c, c + 1u)) != c) c = s; } break;
    case OP_LSE_CAS:  for (uint32_t i = 0; i < n; i++) { uint32_t c = ld(&g_cnt.v), s; while ((s = lse_cas(&g_cnt.v, c, c + 1u)) != c) c = s; } break;
    case OP_LLSC_LOCK: for (uint32_t i = 0; i < n; i++) { llsc_lock(&g_lock.v); g_plain.v++; unlock(&g_lock.v); } break;
    case OP_LLSC_SEVL: for (uint32_t i = 0; i < n; i++) { llsc_lock_sevl(&g_lock.v); g_plain.v++; unlock(&g_lock.v); } break;
    case OP_LLSC_SPIN: for (uint32_t i = 0; i < n; i++) { llsc_lock_spin(&g_lock.v); g_plain.v++; unlock(&g_lock.v); } break;
    case OP_LSE_LOCK:  for (uint32_t i = 0; i < n; i++) { lse_lock(&g_lock.v); g_plain.v++; unlock(&g_lock.v); } break;
    case OP_RETRIES: { uint64_t r = 0; for (uint32_t i = 0; i < n; i++) r += llsc_add_retries(&g_cnt.v, 1u); g_ret[id].retries = r; } break;
    }
    return NULL;
}

/* 한 번 측정: ns/op (전체 벽시계 / 전체 연산 수). 결과 카운트도 확인해 틀린 측정을 거른다. */
static double run_once(int op, int nt, uint32_t iters, double *retries_per_op) {
    pthread_t th[MAXT];
    g_op = op; g_nt = nt; g_iters = iters;
    g_cnt.v = 0; g_lock.v = 0; g_plain.v = 0;
    atomic_store(&g_go, 0);
    for (int i = 0; i < nt; i++) pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    uint64_t total = (uint64_t)nt * iters;
    uint64_t got = IS_LOCK(op) ? g_plain.v : g_cnt.v;
    if (got != total) { fprintf(stderr, "count mismatch op=%d nt=%d: %llu != %llu\n", op, nt,
                                (unsigned long long)got, (unsigned long long)total); exit(1); }
    if (retries_per_op) {
        uint64_t r = 0;
        for (int i = 0; i < nt; i++) r += g_ret[i].retries;
        *retries_per_op = (double)r / (double)total;
    }
    return (double)(t1 - t0) / (double)total;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv) {
    uint32_t iters = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 1000000u;
    static const int nts[] = { 1, 2, 4, 6, 8 };
    printf("ns/op (median of %d, %u ops/thread), same cache line\n", REPS, iters);
    printf("%-16s", "op \\ threads");
    for (size_t k = 0; k < sizeof nts / sizeof nts[0]; k++) printf("%9d", nts[k]);
    printf("\n");
    for (int op = 0; op < NOPS; op++) {
        printf("%-16s", op_name[op]);
        for (size_t k = 0; k < sizeof nts / sizeof nts[0]; k++) {
            double v[REPS], rr[REPS];
            uint32_t it = IS_LOCK(op) ? iters / 4u : iters;
            for (int r = 0; r < REPS; r++) v[r] = run_once(op, nts[k], it, &rr[r]);
            if (op == OP_RETRIES) { qsort(rr, REPS, sizeof rr[0], cmpd); printf("%9.2f", rr[REPS / 2]); }
            else { qsort(v, REPS, sizeof v[0], cmpd); printf("%9.1f", v[REPS / 2]); }
        }
        printf("\n");
    }
    return 0;
}
