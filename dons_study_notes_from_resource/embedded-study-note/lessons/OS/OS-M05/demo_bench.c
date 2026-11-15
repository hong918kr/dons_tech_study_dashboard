/* OS-M05 demo_bench.c — 같은 트레이스 T1~T4 를 정책별로 재생해 처리량(ops/s)을 잰다.
 * 엔진은 solution.c 를 그대로 가져온다. 채우기·검사는 끄고(fill=false, check=0) 순수 할당기 비용 + 재생 루프만 잰다.
 * 비교용으로 이 맥의 libc malloc/free/realloc 도 같은 재생 루프로 돌린다 (활용률은 잴 수 없음).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define MM_NO_MAIN
#include "solution.c"
#include <stdlib.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void replay_libc(const trace_t *t) {          /* 같은 재생 루프, 할당기만 libc */
    memset(g_p, 0, sizeof(void *) * (size_t)t->nids);
    for (int k = 0; k < t->n; k++) {
        const top_t *op = &t->op[k];
        if (op->op == 'a') g_p[op->id] = malloc(op->size);
        else if (op->op == 'f') { free(g_p[op->id]); g_p[op->id] = NULL; }
        else { void *q = realloc(g_p[op->id], op->size); if (q) g_p[op->id] = q; }
    }
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* policy = -1 → libc. reps 번 재생한 시간을 5번 재서 중앙값 → Mops/s */
static double bench(const trace_t *t, int policy, int reps) {
    double v[5];
    ropt_t o = { 0, false, false, 0 };
    for (int run = 0; run < 5; run++) {
        double t0 = now_s();
        for (int r = 0; r < reps; r++) {
            if (policy < 0) replay_libc(t);
            else (void)replay(t, policy, o);
        }
        v[run] = (double)t->n * reps / (now_s() - t0) / 1e6;
    }
    qsort(v, 5, sizeof v[0], cmp_d);
    return v[2];
}

int main(void) {
    gen_traces();
    double la[3] = { 0, 0, 0 };
    if (getloadavg(la, 3) != 3) la[0] = la[1] = la[2] = -1;
    printf("load average: %.2f %.2f %.2f  (shared machine: read ratios, not absolute numbers)\n", la[0], la[1], la[2]);
    printf("%-18s %6s | %8s %8s %8s %8s | %6s %6s %6s | %7s %7s\n", "trace", "ops", "first", "best", "seg", "libc",
           "U.fst", "U.bst", "U.seg", "seg/fst", "seg/bst");
    const int reps[NTRACE] = { 400, 40, 4, 40 };
    for (int t = 0; t < NTRACE; t++) {
        double m[NPOL], u[NPOL];
        ropt_t o = { 0, false, false, 0 };
        for (int p = 0; p < NPOL; p++) { m[p] = bench(&g_tr[t], p, reps[t]); u[p] = replay(&g_tr[t], p, o).util; }
        double lc = bench(&g_tr[t], -1, reps[t]);
        printf("%-18s %6d | %8.1f %8.1f %8.1f %8.1f | %6.3f %6.3f %6.3f | %6.1fx %6.1fx\n", g_tr[t].name, g_tr[t].n,
               m[POL_FIRST], m[POL_BEST], m[POL_SEG], lc, u[POL_FIRST], u[POL_BEST], u[POL_SEG],
               m[POL_SEG] / m[POL_FIRST], m[POL_SEG] / m[POL_BEST]);
    }
    printf("(Mops/s = trace ops per second, median of 5 runs; U = peak live requested / heap high-water)\n");
    return 0;
}
