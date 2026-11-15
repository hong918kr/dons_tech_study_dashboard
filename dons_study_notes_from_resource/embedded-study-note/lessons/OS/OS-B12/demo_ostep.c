/* OS-B12 demo_ostep.c — 내 시뮬레이터를 OSTEP 숙제 시뮬레이터와 같은 입력·같은 출력 형식으로 돌린다.
 *
 *   ./demo_ostep sched FIFO|SJF|RR <quantum> <len,len,...>        ← cpu-sched/scheduler.py -c 의 Final statistics 형식
 *   ./demo_ostep mlfq [-n N] [-q Q] [-Q q0,q1,..] [-a A] [-A a0,a1,..] [-B S] [-i IO] [-S] -l s,r,f:s,r,f:...
 *                                                                ← cpu-sched-mlfq/mlfq.py -c 의 trace + Final statistics 형식
 * mlfq.py 는 우선순위 번호가 거꾸로다 (가장 높은 큐 = N-1). 출력할 때 N-1-level 로 바꾼다.
 * -Q/-A 리스트는 mlfq.py 처럼 "가장 높은 큐부터" 적는다. I/O 길이(-i)는 모든 작업에 공통.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_ostep.c -o demo_ostep   (solution.c 를 #include 한다)
 */
#define SCHED_NO_MAIN
#include "solution.c"

#include <stdlib.h>

static job_t J[MAXJ];
static sim_t R;

static int parse_list(const char *s, int *out, int max) {          /* "10,20,30" → 개수 */
    int n = 0;
    while (*s && n < max) {
        out[n++] = (int)strtol(s, (char **)&s, 10);
        if (*s == ',') s++;
    }
    return n;
}

static int do_sched(int argc, char **argv) {
    if (argc < 5) return 2;
    cfg_t c = {0};
    if (!strcmp(argv[2], "FIFO")) c.pol = POL_FCFS;
    else if (!strcmp(argv[2], "SJF")) c.pol = POL_SJF;
    else if (!strcmp(argv[2], "RR")) c.pol = POL_RR;
    else return 2;
    c.quantum = atoi(argv[3]);
    int lens[MAXJ], n = parse_list(argv[4], lens, MAXJ);
    for (int j = 0; j < n; j++) J[j] = (job_t){0, lens[j], 0, 0};
    if (simulate(J, n, &c, &R) != 0) return 1;
    printf("Final statistics:\n");
    for (int j = 0; j < n; j++)
        printf("  Job %3d -- Response: %3.2f  Turnaround %3.2f  Wait %3.2f\n", j,
               (double)R.response[j], (double)R.turnaround[j], (double)R.waiting[j]);
    printf("\n  Average -- Response: %3.2f  Turnaround %3.2f  Wait %3.2f\n\n", R.avg_resp, R.avg_tat, R.avg_wait);
    return 0;
}

static int do_mlfq(int argc, char **argv) {
    int nq = 3, q = 10, a = 1, boost = 0, io = 5, n = 0;
    int ql[MAXL], al[MAXL], nql = 0, nal = 0;
    bool stay = false;
    for (int i = 2; i < argc; i++) {
        const char *o = argv[i], *v = i + 1 < argc ? argv[i + 1] : "0";
        if (!strcmp(o, "-S")) { stay = true; continue; }
        if (!strcmp(o, "-n")) nq = atoi(v);
        else if (!strcmp(o, "-q")) q = atoi(v);
        else if (!strcmp(o, "-a")) a = atoi(v);
        else if (!strcmp(o, "-B")) boost = atoi(v);
        else if (!strcmp(o, "-i")) io = atoi(v);
        else if (!strcmp(o, "-Q")) nql = parse_list(v, ql, MAXL);
        else if (!strcmp(o, "-A")) nal = parse_list(v, al, MAXL);
        else if (!strcmp(o, "-l")) {
            const char *s = v;
            while (*s && n < MAXJ) {                                     /* start,run,iofreq:... */
                int t3[3];
                char buf[64]; size_t k = 0;
                while (*s && *s != ':' && k + 1 < sizeof buf) buf[k++] = *s++;
                buf[k] = '\0';
                if (*s == ':') s++;
                if (parse_list(buf, t3, 3) != 3) return 2;
                J[n++] = (job_t){t3[0], t3[1], t3[2], 0};
            }
        } else return 2;
        i++;
    }
    if (nql > 0) nq = nql;
    if (nq < 1 || nq > MAXL || n == 0) return 2;
    cfg_t c = {0};
    c.pol = POL_MLFQ; c.nlevels = nq; c.boost = boost; c.stay_on_io = stay;
    for (int lv = 0; lv < nq; lv++) {
        c.qlen[lv] = nql > 0 ? ql[lv] : q;
        c.allot[lv] = nal > 0 ? al[lv] : a;
    }
    for (int j = 0; j < n; j++) J[j].io_len = io;
    if (simulate(J, n, &c, &R) != 0) return 1;
    for (int t = 0; t < R.t; t++)
        if (R.gantt[t] != '.')
            printf("[ time %d ] Run JOB %d at PRIORITY %d\n", t, R.gantt[t] - 'A', nq - 1 - R.glevel[t]);
    printf("\nFinal statistics:\n");
    for (int j = 0; j < n; j++)
        printf("  Job %2d: startTime %3d - response %3d - turnaround %3d\n", j, J[j].arrival, R.response[j], R.turnaround[j]);
    printf("\n  Avg %2d: startTime n/a - response %.2f - turnaround %.2f\n", n - 1, R.avg_resp, R.avg_tat);
    return 0;
}

int main(int argc, char **argv) {
    int rc = 2;
    (void)cpu_ticks_in; (void)longest_gap;                 /* Part C 지표는 여기서 안 쓴다 */
    if (argc >= 2 && !strcmp(argv[1], "sched")) rc = do_sched(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "mlfq")) rc = do_mlfq(argc, argv);
    if (rc == 2) fprintf(stderr, "usage: see header comment\n");
    return rc;
}
