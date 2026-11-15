/* ALGO-A12 demo: 1M 레코드(16 MiB) 외부 정렬 — 입력 종류 × 런 생성 방법, fan-in k 스윕, N 스윕.
 * 모든 줄은 solution.c 의 run_once() 로 끝까지 정렬하고 verify(정렬 + multiset 해시 + payload)까지 한다.
 * 시간 = 벽시계 ms, 3번 중 중앙값 [최소–최대] (첫 실행 전 워밍업 1회). 블록 수는 결정적이라 매번 같다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_full.c -o demo_full && ./demo_full "$SCRATCH/flash.bin"
 */
#define ALGO_A12_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 일부만 쓴다 */
#pragma clang diagnostic ignored "-Wunused-variable"
#include "solution.c"
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static int okv(const run_t *o) { return run_good(o) ? 1 : 0; }

static void row(int kind, uint32_t n, uint32_t param, int m, uint32_t k, int reps) {
    double t[3] = { 0, 0, 0 };
    run_t o;
    for (int r = 0; r < reps; r++) {
        double t0 = now_ms();
        o = run_once(kind, n, param, 42, m, k, 0);
        t[r] = now_ms() - t0;
    }
    const uint64_t D = (uint64_t)n * sizeof(rec_t);
    double med = t[0], lo = t[0], hi = t[0];
    if (reps == 3) {
        for (int i = 0; i < 3; i++) { if (t[i] < lo) lo = t[i]; if (t[i] > hi) hi = t[i]; }
        med = t[0] + t[1] + t[2] - lo - hi;
    }
    printf("%-8s %-3s n=%-7u k=%-2u runs=%-5u avg_run=%-7.1f P=%u (theory %u)  read=%.3f MiB write=%.3f MiB  "
           "(D(1+P)=%.3f MiB, pad=%llu B)  meta r/w=%llu/%llu  peak=%zu  ok=%d stable=%d",
           GNAME[kind], m ? "RS" : "LSW", n, k, o.r.runs0, o.r.runs0 ? (double)n / o.r.runs0 : 0.0, o.r.passes,
           passes_theory(o.r.runs0, k), o.rd * BLK / 1048576.0, o.wr * BLK / 1048576.0,
           D * (1.0 + o.r.passes) / 1048576.0, (unsigned long long)(o.wr * BLK - D * (1 + o.r.passes)),
           (unsigned long long)o.rdm, (unsigned long long)o.wrm, o.peak, okv(&o), o.v.stable);
    if (reps == 3) printf("  time %.0f ms [%.0f-%.0f]", med, lo, hi);
    printf("\n");
}

int main(int argc, char **argv) {
    static char path[1024];
    const char *tmp = getenv("TMPDIR");
    if (argc > 1) g_path = argv[1];
    else { snprintf(path, sizeof path, "%s/algo_a12_flash.bin", tmp && *tmp ? tmp : "/tmp"); g_path = path; }
    g_f.fd = open(g_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_f.fd < 0) { perror(g_path); return 2; }
    const uint32_t K = merge_fanin_max(), N = 1u << 20;

    printf("== 1M records (16 MiB), k=%u: input kind x run generation ==\n", K);
    (void)run_once(G_RANDOM, N, 0, 42, M_LSW, K, 0);                     /* 워밍업 */
    row(G_RANDOM, N, 0, M_LSW, K, 3);
    row(G_RANDOM, N, 0, M_RS, K, 3);
    static const int kinds[] = { G_DUPS, G_SORTED, G_REVERSE, G_NEARLY };
    static const uint32_t params[] = { 1u << 18, 0, 0, 8 };
    for (int i = 0; i < 4; i++) {
        row(kinds[i], N, params[i], M_LSW, K, 1);
        row(kinds[i], N, params[i], M_RS, K, 1);
    }

    printf("== 1M random, LSW (4096 runs): fan-in sweep ==\n");
    static const uint32_t ks[] = { 2, 4, 8, 13 };
    for (int i = 0; i < 4; i++) row(G_RANDOM, N, 0, M_LSW, ks[i], 1);

    printf("== N sweep, random, k=%u: where RS saves a pass ==\n", K);
    static const uint32_t ns[] = { 400000, 600000, 900000 };
    for (int i = 0; i < 3; i++) { row(G_RANDOM, ns[i], 0, M_LSW, K, 1); row(G_RANDOM, ns[i], 0, M_RS, K, 1); }

    close(g_f.fd);
    unlink(g_path);
    return 0;
}
