/* BUILD: -pthread */
/* OS-M09 demo_bench.c — 정책별 처리량과 writer 대기 시간 (체크 아님, 실측 출력)
 *
 * solution.c 의 rwl_t 를 그대로 쓴다 (RWL_NO_MAIN 으로 main 만 뺌) + 비교 대상 두 개:
 *   pthread_rwlock_t (이 맥의 libpthread), pthread_mutex_t (읽기도 배타 = 기준선)
 * 워크로드 W1 "95/5": 8 스레드가 매 연산마다 5 % 확률로 쓰기, 95 % 읽기. 임계 구역 = 설정 테이블 16 워드
 *   검사합 계산(읽기) / 다시 채우기(쓰기). 임계 구역 밖 일 = spin_work(OUT_ITERS).
 * 워크로드 W2 "overlap": reader 7 개가 긴 임계 구역(spin_work(RD_CS_ITERS))을 쉬지 않고 반복,
 *   writer 1 개가 100 µs 마다 쓰기 시도 → reader 가 끊임없이 겹칠 때 writer 가 얼마나 기다리나.
 * 대기 시간 = lock 호출 직전 ~ 획득 직후 (clock_gettime_nsec_np(CLOCK_UPTIME_RAW), 42 ns 눈금). writer 는 전부, reader 는 8 번에 1 번 기록.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench [run_ms]
 *   (solution.c 와 같은 폴더에서. 공유 · 부하 걸린 맥 → 비율만 믿을 것)
 */
#define _DARWIN_C_SOURCE   /* clock_gettime_nsec_np(CLOCK_UPTIME_RAW): 42 ns 단위. CLOCK_MONOTONIC 은 이 맥에서 1 µs 단위였다 */
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트 함수들은 여기서 안 쓴다 */
#define RWL_NO_MAIN
#include "solution.c"
#include <stdlib.h>

#define NTHREADS     8
#define OUT_ITERS    200     /* W1: 임계 구역 밖 일 */
#define RD_CS_ITERS  2000    /* W2: reader 임계 구역 안 일 */
#define MAXS         (1 << 17)

static void spin_work(unsigned iters) { volatile unsigned x = 0; for (unsigned i = 0; i < iters; i++) x += i; }
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* ---- 락 다섯 가지를 같은 모양으로 ---- */
enum { L_RP, L_WP, L_PF, L_PTHREAD_RW, L_MUTEX, L_COUNT };
static const char *const lname[L_COUNT] = { "reader-pref", "writer-pref", "phase-fair", "pthread_rwlock", "mutex" };
typedef struct { int kind; rwl_t rwl; pthread_rwlock_t prw; pthread_mutex_t mx; } anylock_t;
static void al_init(anylock_t *l, int kind) {
    l->kind = kind;
    if (kind <= L_PF) rwl_init(&l->rwl, kind == L_RP ? RW_READER_PREF : kind == L_WP ? RW_WRITER_PREF : RW_PHASE_FAIR);
    else if (kind == L_PTHREAD_RW) pthread_rwlock_init(&l->prw, NULL);
    else pthread_mutex_init(&l->mx, NULL);
}
static void al_destroy(anylock_t *l) {
    if (l->kind <= L_PF) rwl_destroy(&l->rwl);
    else if (l->kind == L_PTHREAD_RW) pthread_rwlock_destroy(&l->prw);
    else pthread_mutex_destroy(&l->mx);
}
static void al_rd(anylock_t *l)   { if (l->kind <= L_PF) rwl_rdlock(&l->rwl);   else if (l->kind == L_PTHREAD_RW) pthread_rwlock_rdlock(&l->prw); else pthread_mutex_lock(&l->mx); }
static void al_rdun(anylock_t *l) { if (l->kind <= L_PF) rwl_rdunlock(&l->rwl); else if (l->kind == L_PTHREAD_RW) pthread_rwlock_unlock(&l->prw); else pthread_mutex_unlock(&l->mx); }
static void al_wr(anylock_t *l)   { if (l->kind <= L_PF) rwl_wrlock(&l->rwl);   else if (l->kind == L_PTHREAD_RW) pthread_rwlock_wrlock(&l->prw); else pthread_mutex_lock(&l->mx); }
static void al_wrun(anylock_t *l) { if (l->kind <= L_PF) rwl_wrunlock(&l->rwl); else if (l->kind == L_PTHREAD_RW) pthread_rwlock_unlock(&l->prw); else pthread_mutex_unlock(&l->mx); }

typedef struct {
    anylock_t *l; cfg_table_t *tab; atomic_int *stop;
    int role;                 /* 0 = W1 혼합, 1 = W2 reader, 2 = W2 writer */
    uint32_t seed;
    long reads, writes, bad;
    double *wwait; int nw;    /* writer 대기 (ns) */
    double *rwait; int nr;    /* reader 대기 (ns), 8 번에 1 번 */
} targ_t;

static void do_read(targ_t *a, unsigned cs_iters) {
    uint64_t t0 = now_ns();
    al_rd(a->l);
    uint64_t t1 = now_ns();
    bool ok = a->tab->sum == cfg_sum(a->tab);
    if (cs_iters) spin_work(cs_iters);
    al_rdun(a->l);
    if (!ok) a->bad++;
    if ((a->reads & 7) == 0 && a->nr < MAXS) a->rwait[a->nr++] = (double)(t1 - t0);
    a->reads++;
}
static void do_write(targ_t *a) {
    uint64_t t0 = now_ns();
    al_wr(a->l);
    uint64_t t1 = now_ns();
    cfg_fill(a->tab, a->tab->version + 1);
    al_wrun(a->l);
    if (a->nw < MAXS) a->wwait[a->nw++] = (double)(t1 - t0);
    a->writes++;
}
static void *bench_main(void *arg) {
    targ_t *a = arg;
    while (!atomic_load_explicit(a->stop, memory_order_relaxed)) {
        if (a->role == 0) {
            if (xorshift32(&a->seed) % 100 < 5) do_write(a); else do_read(a, 0);
            spin_work(OUT_ITERS);
        } else if (a->role == 1) {
            do_read(a, RD_CS_ITERS);
        } else {
            do_write(a);
            sleep_us(100);
        }
    }
    return NULL;
}
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
static double pct(double *v, int n, double p) { if (n == 0) return 0; return v[(int)(p * (n - 1))]; }

static void run(int workload, int kind, int run_ms) {
    anylock_t l; al_init(&l, kind);
    cfg_table_t tab; cfg_fill(&tab, 0);
    atomic_int stop = 0;
    targ_t args[NTHREADS]; pthread_t th[NTHREADS];
    for (int t = 0; t < NTHREADS; t++) {
        args[t] = (targ_t){ .l = &l, .tab = &tab, .stop = &stop, .seed = 12345u + (uint32_t)t * 7919u,
                            .role = workload == 1 ? 0 : (t == NTHREADS - 1 ? 2 : 1) };
        args[t].wwait = malloc(sizeof(double) * MAXS);
        args[t].rwait = malloc(sizeof(double) * MAXS);
    }
    uint64_t t0 = now_ns();
    for (int t = 0; t < NTHREADS; t++) pthread_create(&th[t], NULL, bench_main, &args[t]);
    sleep_us((unsigned)run_ms * 1000u);
    atomic_store(&stop, 1);
    for (int t = 0; t < NTHREADS; t++) pthread_join(th[t], NULL);
    double secs = (double)(now_ns() - t0) / 1e9;
    long reads = 0, writes = 0, bad = 0;
    int nw = 0, nr = 0;
    double *ww = malloc(sizeof(double) * MAXS * NTHREADS), *rr = malloc(sizeof(double) * MAXS * NTHREADS);
    for (int t = 0; t < NTHREADS; t++) {
        reads += args[t].reads; writes += args[t].writes; bad += args[t].bad;
        memcpy(ww + nw, args[t].wwait, sizeof(double) * (size_t)args[t].nw); nw += args[t].nw;
        memcpy(rr + nr, args[t].rwait, sizeof(double) * (size_t)args[t].nr); nr += args[t].nr;
        free(args[t].wwait); free(args[t].rwait);
    }
    qsort(ww, (size_t)nw, sizeof(double), cmpd);
    qsort(rr, (size_t)nr, sizeof(double), cmpd);
    printf("| %-14s | %9.0f | %8.0f | %9.2f | %9.1f | %10.1f | %9.2f | %ld\n", lname[kind],
           (double)(reads + writes) / secs / 1e3, (double)writes / secs,
           pct(ww, nw, 0.50) / 1e3, pct(ww, nw, 0.99) / 1e3, nw ? ww[nw - 1] / 1e3 : 0.0,
           pct(rr, nr, 0.99) / 1e3, bad);
    free(ww); free(rr);
    al_destroy(&l);
}

int main(int argc, char **argv) {
    int run_ms = argc > 1 ? atoi(argv[1]) : 1000;
    uint64_t t0 = now_ns(); spin_work(1000000); double ns_per = (double)(now_ns() - t0) / 1e6;
    printf("run_ms=%d threads=%d  spin_work: %.2f ns/iter (OUT_ITERS=%d ~ %.2f us, RD_CS_ITERS=%d ~ %.2f us)\n",
           run_ms, NTHREADS, ns_per, OUT_ITERS, ns_per * OUT_ITERS / 1e3, RD_CS_ITERS, ns_per * RD_CS_ITERS / 1e3);
    fflush(stdout);
    for (int w = 1; w <= 2; w++) {
        printf("\nW%d %s\n", w, w == 1 ? "95/5 mixed, 8 threads (every thread reads and writes)"
                                        : "overlap: 7 readers back-to-back long CS + 1 writer every 100 us");
        printf("| lock           | kops/s    | writes/s | W p50 us  | W p99 us  | W max us   | R p99 us  | bad\n");
        for (int k = 0; k < L_COUNT; k++) { run(w, k, run_ms); fflush(stdout); }
    }
    return 0;
}
