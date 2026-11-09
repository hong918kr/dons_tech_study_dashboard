/* OS-M11 demo_bench.c — mini-lockdep 비용: lock+unlock 한 쌍(또는 3중 중첩 한 세트)당 ns, 스레드 1..8 처리량.
 *   plain     = pthread_mutex_t (기본 타입)          errchk = PTHREAD_MUTEX_ERRORCHECK (ld_mutex_t 안의 것)
 *   ld        = ld_lock/ld_unlock, 간선이 이미 알려진 정상 상태 (g_known 빠른 길: 전역 락 안 잡음)
 *   ld_nc     = 같은 검사를 "매번 G.lock 을 잡고" 하는 판 (캐시가 없을 때 = 모든 스레드가 전역 락 하나에 줄 섬)
 *   cold edge = 새 간선 하나 기록 비용 (DFS + backtrace) — ld_reset 비용을 빼고 계산
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
 */
#define LD_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"
#include <time.h>

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

/* 캐시 없는 판: 이미 아는 간선인지 G.lock 아래에서 확인 */
static int add_edge_nc(const ld_held_t *h, int to, const char *site) {
    pthread_mutex_lock(&G.lock);
    bool known = G.eidx[h->node][to] != 0;
    pthread_mutex_unlock(&G.lock);
    return known ? LD_OK : add_edge(h, to, site);
}
static int ld_lock_nc(ld_mutex_t *m, const char *site) {
    int node = m->cls * LD_MAX_SUB, rc = LD_OK;
    for (int i = 0; i < t_nheld; i++) if (t_held[i].m == m) return LD_SELF;
    for (int i = 0; i < t_nheld; i++)
        if (t_held[i].node != node && add_edge_nc(&t_held[i], node, site) == LD_CYCLE) rc = LD_CYCLE;
    pthread_mutex_lock(&m->m);
    t_held[t_nheld++] = (ld_held_t){ .m = m, .node = node, .site = site };
    return rc;
}

enum { MODE_PLAIN, MODE_ERRCHK, MODE_LD, MODE_LD_NC };
static const char *mode_name[] = { "plain", "errchk", "ld", "ld_nc" };
typedef struct {                                  /* 스레드마다 하나: 128B 정렬로 false sharing 제거 */
    _Alignas(128) int mode, depth; long iters; double ns;
    _Alignas(128) ld_mutex_t L[3];
    _Alignas(128) pthread_mutex_t P[3];
    _Alignas(128) char pad;
} job_t;

static void run_job(job_t *j) {
    for (long i = 0; i < j->iters; i++) {
        for (int d = 0; d < j->depth; d++) {
            switch (j->mode) {
            case MODE_PLAIN:  pthread_mutex_lock(&j->P[d]); break;
            case MODE_ERRCHK: pthread_mutex_lock(&j->L[d].m); break;
            case MODE_LD:     ld_lock(&j->L[d], "bench"); break;
            default:          ld_lock_nc(&j->L[d], "bench"); break;
            }
        }
        for (int d = j->depth - 1; d >= 0; d--) {
            if (j->mode == MODE_PLAIN) pthread_mutex_unlock(&j->P[d]);
            else if (j->mode == MODE_ERRCHK) pthread_mutex_unlock(&j->L[d].m);
            else ld_unlock(&j->L[d]);
        }
    }
}
static void *thr(void *a) { job_t *j = a; double t0 = now_ns(); run_job(j); j->ns = now_ns() - t0; return NULL; }
static void job_init(job_t *j, int mode, int depth, long iters) {
    static const char *cls[] = { "bench_A", "bench_B", "bench_C" };   /* 인스턴스는 스레드마다, 클래스는 공유 */
    j->mode = mode; j->depth = depth; j->iters = iters;
    for (int d = 0; d < 3; d++) { ld_mutex_init(&j->L[d], cls[d]); pthread_mutex_init(&j->P[d], NULL); }
}

int main(void) {
    const long N = 2000000;
    printf("single thread, ns per lock+unlock set (best of 5)\n%-8s %10s %10s\n", "mode", "depth=1", "depth=3");
    for (int mode = 0; mode < 4; mode++) {
        printf("%-8s", mode_name[mode]);
        for (int depth = 1; depth <= 3; depth += 2) {
            double best = 1e18;
            for (int r = 0; r < 5; r++) {
                job_t j; job_init(&j, mode, depth, N);
                run_job(&j);                         /* 데움: 간선 기록은 여기서 한 번 */
                double t0 = now_ns(); run_job(&j); double dt = (now_ns() - t0) / (double)N;
                if (dt < best) best = dt;
            }
            printf(" %10.1f", best);
        }
        printf("\n");
    }
    {   /* 새 간선 비용: 매번 그래프를 비우고(시간 밖) A->B 첫 기록 한 번을 잰다. 시계 자체 비용은 빈 구간으로 뺀다 */
        job_t j; job_init(&j, MODE_LD, 2, 1);
        const int R = 20000; double sum = 0, sum_clk = 0, warm = 0;
        for (int i = 0; i < R; i++) {
            ld_reset();
            double t0 = now_ns(); run_job(&j); double t1 = now_ns(); double t2 = now_ns();
            sum += t1 - t0; sum_clk += t2 - t1;
        }
        double t0 = now_ns(); for (int i = 0; i < R; i++) run_job(&j); warm = (now_ns() - t0) / R;
        printf("first time an edge is seen (DFS + backtrace + insert): %.0f ns per A,B set  (steady state %.1f ns, clock %.0f ns subtracted)\n",
               (sum - sum_clk) / R, warm, sum_clk / R);
    }
    printf("\nthreads, depth=3, each thread has its OWN 3 locks (classes shared), Msets/s total\n%-8s", "threads");
    for (int mode = 0; mode < 4; mode++) printf(" %10s", mode_name[mode]);
    printf("\n");
    static job_t jobs[8];
    for (int T = 1; T <= 8; T *= 2) {
        printf("%-8d", T);
        for (int mode = 0; mode < 4; mode++) {
            long it = mode == MODE_LD_NC ? 200000 : 1000000;
            double best = 0;
            for (int rep = 0; rep < 3; rep++) {                /* best of 3 (다른 작업이 같이 도는 맥) */
                for (int k = 0; k < T; k++) { job_init(&jobs[k], mode, 3, 1000); run_job(&jobs[k]); jobs[k].iters = it; }
                pthread_t th[8];
                double t0 = now_ns();
                for (int k = 0; k < T; k++) pthread_create(&th[k], NULL, thr, &jobs[k]);
                for (int k = 0; k < T; k++) pthread_join(th[k], NULL);
                double r = (double)T * it / (now_ns() - t0) * 1e3;
                if (r > best) best = r;
            }
            printf(" %10.1f", best);
        }
        printf("\n");
    }
    printf("edges in graph: %d, cycles: %d\n", G.nedges, G.ncycle);
    return 0;
}
