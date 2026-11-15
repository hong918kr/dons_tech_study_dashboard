/* OS-B12 demo_sweep.c — 정책 비교표, RR quantum 스윕, MLFQ 부스트 주기 스윕, 게이밍 표.
 * 결정적 시뮬레이션이라 매번 같은 숫자가 나온다 (시간 측정 없음).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep
 *        (solution.c 를 #include 한다 — 같은 디렉터리에 있어야 한다)
 */
#define SCHED_NO_MAIN
#include "solution.c"

static sim_t R;

/* 임베디드 리눅스 보드의 작업 묶음 (tick = 1 ms 라고 생각) */
static const job_t W1[] = {
    {0, 60, 0, 0},    /* A 로그 압축 (CPU 만)              */
    {2, 12, 2, 6},    /* B 센서 읽기: 2 ms 계산 → 6 ms I²C  */
    {5, 8, 1, 4},     /* C UI 이벤트: 1 ms 계산 → 4 ms 대기 */
    {10, 30, 0, 0},   /* D OTA 이미지 검증 (CPU 만)         */
    {12, 4, 0, 0},    /* E 짧은 명령 (CLI)                  */
};
#define NW1 ((int)(sizeof W1 / sizeof W1[0]))

static int idle_ticks(const sim_t *s) { int k = 0; for (int t = 0; t < s->t; t++) k += s->gantt[t] == '.'; return k; }
static cfg_t mlfq(int nl, const int *q, int boost, bool stay) {
    cfg_t c = {0}; c.pol = POL_MLFQ; c.nlevels = nl; c.boost = boost; c.stay_on_io = stay;
    for (int i = 0; i < nl; i++) { c.qlen[i] = q[i]; c.allot[i] = 1; }
    return c;
}
static void row(const char *name, const job_t *w, int n, cfg_t c) {
    if (simulate(w, n, &c, &R) != 0) { printf("%-16s simulate failed\n", name); return; }
    printf("%-16s %8.2f %8.2f %8.2f %6d %6d %5d   ", name, R.avg_tat, R.avg_resp, R.avg_wait, R.dispatches, R.t, idle_ticks(&R));
    printf("resp:"); for (int j = 0; j < n; j++) printf(" %c=%d", 'A' + j, R.response[j]);
    printf("\n");
}
static void gantt(const char *name, const job_t *w, int n, cfg_t c) {
    if (simulate(w, n, &c, &R) != 0) return;
    printf("%-10s ", name);
    for (int t = 0; t < R.t;) { int u = t; while (u < R.t && R.gantt[u] == R.gantt[t]) u++; printf("%c%d ", R.gantt[t], u - t); t = u; }
    printf("\n");
}

int main(void) {
    (void)longest_gap;
    static const int q3[3] = {2, 4, 8}, q10[3] = {10, 10, 10};
    cfg_t c = {0};

    printf("== 1. policy comparison on W1 (5 jobs, arrivals 0..12, B/C do I/O) ==\n");
    printf("%-16s %8s %8s %8s %6s %6s %5s\n", "policy", "avgTAT", "avgResp", "avgWait", "disp", "end", "idle");
    c.pol = POL_FCFS; row("FCFS", W1, NW1, c);
    c.pol = POL_SJF;  row("SJF", W1, NW1, c);
    c.pol = POL_STCF; row("STCF", W1, NW1, c);
    c.pol = POL_RR; c.quantum = 2; row("RR q=2", W1, NW1, c);
    c.quantum = 8; row("RR q=8", W1, NW1, c);
    row("MLFQ 2/4/8 B100", W1, NW1, mlfq(3, q3, 100, false));

    printf("\n== 2. Gantt (W1) ==\n");
    c.pol = POL_FCFS; gantt("FCFS", W1, NW1, c);
    c.pol = POL_STCF; gantt("STCF", W1, NW1, c);
    c.pol = POL_RR; c.quantum = 2; gantt("RR q=2", W1, NW1, c);
    gantt("MLFQ", W1, NW1, mlfq(3, q3, 100, false));

    printf("\n== 3. RR quantum sweep (W1) ==\n");
    printf("%6s %8s %8s %6s\n", "q", "avgTAT", "avgResp", "disp");
    for (int q = 1; q <= 64; q *= 2) {
        c.pol = POL_RR; c.quantum = q;
        if (simulate(W1, NW1, &c, &R) == 0) printf("%6d %8.2f %8.2f %6d\n", q, R.avg_tat, R.avg_resp, R.dispatches);
    }
    c.pol = POL_FCFS;
    if (simulate(W1, NW1, &c, &R) == 0) printf("%6s %8.2f %8.2f %6d\n", "FCFS", R.avg_tat, R.avg_resp, R.dispatches);

    printf("\n== 4. MLFQ boost sweep: batch A + 2 interactive (arrive 20, 1 run / 1 I/O), old rules 4a/4b ==\n");
    static const job_t ST[] = {{0, 200, 0, 0}, {20, 100, 1, 1}, {20, 100, 1, 1}};
    printf("%8s %14s %8s %8s %10s\n", "boost S", "A ticks[20,220)", "A gap", "A TAT", "B,C resp");
    int Ss[] = {0, 200, 100, 50, 25, 10};
    for (int i = 0; i < 6; i++) {
        cfg_t m = mlfq(3, q10, Ss[i], true);
        if (simulate(ST, 3, &m, &R) != 0) continue;
        printf("%8d %14d %8d %8d %6d,%d\n", Ss[i], cpu_ticks_in(&R, 0, 20, 220), longest_gap(&R, 0), R.turnaround[0], R.response[1], R.response[2]);
    }
    {
        cfg_t m = mlfq(3, q10, 0, false);
        if (simulate(ST, 3, &m, &R) == 0)
            printf("%8s %14d %8d %8d %6d,%d   <- new rule 4, no boost\n", "new", cpu_ticks_in(&R, 0, 20, 220), longest_gap(&R, 0), R.turnaround[0], R.response[1], R.response[2]);
    }

    printf("\n== 5. gaming: batch A vs gamer G (9 run / 1 I/O, slice 10), share of [0,300) ==\n");
    static const job_t GM[] = {{0, 300, 0, 0}, {0, 300, 9, 1}};
    const char *nm[] = {"old 4a/4b", "old + boost 100", "new rule 4", "new + boost 100"};
    bool stay[] = {true, true, false, false}; int bs[] = {0, 100, 0, 100};
    for (int i = 0; i < 4; i++) {
        cfg_t m = mlfq(3, q10, bs[i], stay[i]);
        if (simulate(GM, 2, &m, &R) != 0) continue;
        int g = cpu_ticks_in(&R, 1, 0, 300), a = cpu_ticks_in(&R, 0, 0, 300);
        printf("%-16s G=%3d (%4.1f%%)  A=%3d (%4.1f%%)\n", nm[i], g, 100.0 * g / 300, a, 100.0 * a / 300);
    }
    return 0;
}
