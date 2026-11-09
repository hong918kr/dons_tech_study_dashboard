/* OS-M10 demo_bench.c — 해법별 처리량(meals/s)과 공정성(철학자별 식사 min/max).
 *
 * solution.c 의 식탁 엔진을 그대로 쓴다 (PHILO_NO_MAIN 으로 main 만 뺌).
 * 각 판 = run_ms 동안 자유 실행 (강제 스케줄 없음). naive 는 언제든 교착할 수 있으므로
 * 워치독이 잡아서 "몇 ms, 몇 끼 뒤 교착" 을 대신 찍는다.
 *
 * build: cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench [run_ms]
 *   (solution.c 와 같은 폴더에서. 수치는 이 맥 · 이 순간의 부하 기준 — 비율만 믿을 것)
 */
#pragma clang diagnostic ignored "-Wunused-function"
#define PHILO_NO_MAIN
#include "solution.c"
#include <stdlib.h>

static const char *const names[S_COUNT] = { "naive", "ordered", "trylock+rand", "trylock+fixed", "seated n-1" };

static void one(int n, int strat, unsigned think, unsigned eat, unsigned backoff_us, int run_ms, uint32_t seed) {
    cfg_t c = { .n = n, .strat = strat, .meals_target = 0, .run_ms = run_ms, .think_iters = think,
                .eat_iters = eat, .backoff_us = backoff_us, .seed = seed, .quiet = true };
    result_t r;
    run_table(&c, &r);
    long mn = r.meals[0], mx = r.meals[0];
    for (int p = 1; p < n; p++) { if (r.meals[p] < mn) mn = r.meals[p]; if (r.meals[p] > mx) mx = r.meals[p]; }
    double secs = (r.deadlocked ? r.deadlock_at_ms - STALL_MS : r.ms) / 1e3;   /* 교착: 마지막 식사 ≈ 탐지 - STALL_MS */
    if (secs < 1e-3) secs = 1e-3;
    char bo[16] = "-";
    if (strat == S_TRYLOCK || strat == S_TRYLOCK_FIXED) snprintf(bo, sizeof bo, "%u us", backoff_us);
    printf("| %2d | %-13s | %-6s | %10.0f | %7ld | %7ld | %6.2f | %8.3f | %s\n", n, names[strat], bo,
           (double)r.total / secs, mn, mx, mn ? (double)mx / (double)mn : 0.0,
           r.total ? (double)r.retries / (double)r.total : 0.0,
           r.deadlocked ? "DEADLOCK" : (r.stalled ? "STALL" : "ok"));
    if (r.deadlocked)
        printf("|    |   └ naive: deadlocked after %ld meals, ~%.0f ms (%d-cycle)\n", r.total, r.deadlock_at_ms - STALL_MS, r.cycle_len);
}

int main(int argc, char **argv) {
    int run_ms = argc > 1 ? atoi(argv[1]) : 1000;
    const unsigned think = 2000, eat = 2000;     /* 생각 ≈ 먹기: 경합이 꽤 있는 설정 */
    printf("run_ms=%d think_iters=%u eat_iters=%u\n", run_ms, think, eat);
    {   /* spin_work 한 번의 실제 비용 */
        double t0 = now_ms();
        for (int i = 0; i < 2000; i++) spin_work(eat);
        printf("spin_work(%u) = %.2f us\n", eat, (now_ms() - t0) * 1e3 / 2000.0);
    }
    printf("| n  | strategy      | backoff| meals/s    | min     | max     | max/min| retry/meal | result\n");
    printf("|----|---------------|--------|------------|---------|---------|--------|----------|-------\n");
    int ns[2] = { 5, 12 };
    for (int i = 0; i < 2; i++) {
        int n = ns[i];
        one(n, S_NAIVE, think, eat, 0, run_ms, 11);
        one(n, S_ORDERED, think, eat, 0, run_ms, 11);
        one(n, S_TRYLOCK, think, eat, 5, run_ms, 11);
        one(n, S_TRYLOCK_FIXED, think, eat, 5, run_ms, 11);
        one(n, S_TRYLOCK, think, eat, 0, run_ms, 11);
        one(n, S_TRYLOCK_FIXED, think, eat, 0, run_ms, 11);
        one(n, S_SEATED, think, eat, 0, run_ms, 11);
    }
    printf("per-philosopher meals (P0..Pn-1), %d ms each:\n", run_ms);
    int ss[2] = { S_ORDERED, S_SEATED };
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            cfg_t c = { .n = ns[i], .strat = ss[j], .run_ms = run_ms, .think_iters = think, .eat_iters = eat,
                        .seed = 11, .quiet = true };
            result_t r;
            run_table(&c, &r);
            printf("  %-10s n=%2d:", names[ss[j]], ns[i]);
            for (int p = 0; p < ns[i]; p++) printf(" %ld", r.meals[p]);
            printf("\n");
        }
    printf("naive time-to-deadlock, n=5, 8 runs (think=eat=%u):\n", think);
    for (int k = 0; k < 8; k++) {
        cfg_t c = { .n = 5, .strat = S_NAIVE, .run_ms = run_ms, .think_iters = think, .eat_iters = eat,
                    .seed = (uint32_t)k + 1u, .quiet = true };
        result_t r;
        run_table(&c, &r);
        if (r.deadlocked) printf("  run %d: deadlock after %ld meals, %.1f ms\n", k, r.total, r.deadlock_at_ms - STALL_MS);
        else printf("  run %d: no deadlock in %d ms (%ld meals)\n", k, run_ms, r.total);
    }
    return 0;
}
