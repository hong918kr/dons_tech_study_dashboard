/* SIV-M02 demo: "랜덤만으로 충분한가?" — RANDOM job 만 N 개 돌리는 soak 회귀
 *
 * solution.c 의 엔진 mock · 생성기 · checker 를 그대로 쓴다 (main 만 이름을 바꿔 끌어온다).
 * 결함마다 RANDOM 시드 1..N 을 돌려 (1) 처음 잡힌 시드 (2) 잡힌 job 비율을 출력한다.
 * 같은 결함을 directed 경계 스위트(8 kind × 4 seed = 32 job)는 전부 잡는다 — solution 의 출력과 비교.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_soak.c -o demo_soak && ./demo_soak 2000
 */
#include <stdlib.h>
#include <time.h>
#define main siv_m02_selftest   /* solution.c 의 테스트 main 은 쓰지 않는다 */
#include "solution.c"
#undef main

int main(int argc, char **argv) {
    unsigned n = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2000u;
    if (n == 0) n = 1;
    struct { const char *name; dma_fault_t f; } fl[] = {
        {"none",         {0}},
        {"wrap_4k",      {.wrap_4k = true}},
        {"tail_drop",    {.tail_drop = true}},
        {"len_15bit",    {.len_15bit = true}},
        {"len0_hang",    {.len0_hang = true}},
        {"err_continue", {.err_continue = true}},
        {"err_as_done",  {.err_as_done = true}},
        {"irq_early",    {.irq_early = true}},
        {"bytecnt_last", {.bytecnt_last = true}},
    };
    static job_t J;
    static fails_t F;
    clock_t t0 = clock();
    unsigned long total_jobs = 0;
    printf("RANDOM-only soak: %u jobs per fault (n=1..8 desc, len 0..4096, any alignment)\n", n);
    printf("%-13s %10s %12s %10s\n", "fault", "first_seed", "detected", "rate");
    for (size_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        dma_init(&fl[i].f);
        unsigned first = 0, hit = 0;
        for (unsigned sd = 1; sd <= n; sd++) {
            gen_job(K_RANDOM, sd, &J);
            run_job(&J, &F);
            total_jobs++;
            if (F.n) { hit++; if (!first) first = sd; }
        }
        if (first) printf("%-13s %10u %7u/%-5u %9.1f%%\n", fl[i].name, first, hit, n, 100.0 * hit / n);
        else       printf("%-13s %10s %7u/%-5u %9.1f%%\n", fl[i].name, "never", hit, n, 0.0);
    }
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%lu jobs in %.2f s CPU (%.0f jobs/s)\n", total_jobs, s, s > 0 ? total_jobs / s : 0.0);
    return 0;
}
