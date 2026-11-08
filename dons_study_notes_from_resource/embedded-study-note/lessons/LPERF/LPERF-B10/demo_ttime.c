/* ENV: linux */
/* LPERF-B10 demo_ttime — 한 프로세스의 모든 스레드를 샘플링해 시간 분해표를 출력하는 도구
 *
 * solution.c 의 함수(파서·account·classify·verify_wall)를 그대로 #include 해서 쓴다.
 *   1) 시작: 모든 tid 의 schedstat/status/stat 샘플 (a)
 *   2) interval_ms 마다 stat 의 state 문자만 읽어 히스토그램
 *   3) 끝: 다시 샘플 (b) → account → 표 + verify_wall
 *   4) 교차 검증: kernel.sched_schedstats=1 이면 /proc/<pid>/task/<tid>/sched 의 sum_sleep_runtime·sum_block_runtime
 *      변화량을 off 와 비교, 그리고 stat 의 utime+stime(틱) 을 run 과 비교
 *
 * usage: ./ttime <pid> <seconds> <interval_ms>
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_ttime.c -o ttime   (solution.c 와 같은 디렉터리)
 */
#define TTIME_LIB_ONLY
#include "solution.c"
#include <dirent.h>

#define MAXT 128
typedef struct {
    int tid, ok;
    sample_t a, b;
    state_hist_t h;
    int sched_ok;
    uint64_t sleep0, block0, sleep1, block1;   /* /proc/.../sched 값 (ns) */
} trow_t;

/* "/proc/<pid>/task/<tid>/sched" 의 "key   :   12.345678" (ms, 소수 6자리) → ns. 연습 1 의 답과 같은 로직. */
static int sched_ms_to_ns(const char *buf, const char *key, uint64_t *ns) {
    const char *p = find_line_key(buf, key);
    if (!p) return -1;
    while (*p == ' ') p++;
    if (*p++ != ':') return -1;
    uint64_t ip = 0, fp = 0;
    if (next_u64(&p, &ip) != 0) return -1;
    if (*p == '.') {
        p++;
        int d = 0;
        for (; d < 6 && *p >= '0' && *p <= '9'; d++, p++) fp = fp * 10 + (uint64_t)(*p - '0');
        for (; d < 6; d++) fp *= 10;
    }
    *ns = ip * 1000000u + fp;
    return 0;
}
static int read_sched(int pid, int tid, uint64_t *sleep, uint64_t *block) {
    char path[96], buf[8192];
    snprintf(path, sizeof path, "/proc/%d/task/%d/sched", pid, tid);
    if (read_file(path, buf, sizeof buf) <= 0) return -1;
    if (sched_ms_to_ns(buf, "sum_sleep_runtime", sleep) != 0) return -1;   /* schedstats 꺼져 있으면 줄이 없다 */
    if (sched_ms_to_ns(buf, "sum_block_runtime", block) != 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s pid seconds interval_ms\n", argv[0]); return 2; }
    int pid = atoi(argv[1]), secs = atoi(argv[2]), ivl = atoi(argv[3]);
    static trow_t t[MAXT];
    int n = 0;
    char path[96], buf[1024];
    snprintf(path, sizeof path, "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) { perror(path); return 1; }
    for (struct dirent *e; (e = readdir(d)) != NULL && n < MAXT; )
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9') t[n++].tid = atoi(e->d_name);
    closedir(d);

    snprintf(path, sizeof path, "/proc/sys/kernel/sched_schedstats");
    int schedstats_on = read_file(path, buf, sizeof buf) > 0 && buf[0] == '1';

    for (int i = 0; i < n; i++) {
        t[i].ok = read_task_sample(pid, t[i].tid, &t[i].a) == 0;
        t[i].sched_ok = schedstats_on && read_sched(pid, t[i].tid, &t[i].sleep0, &t[i].block0) == 0;
    }
    uint64_t end = now_ns() + (uint64_t)secs * 1000000000u;
    unsigned rounds = 0;
    while (now_ns() < end) {
        struct timespec dl = { ivl / 1000, (long)(ivl % 1000) * 1000000L };
        nanosleep(&dl, NULL);
        for (int i = 0; i < n; i++) {
            taskstat_t ts;
            snprintf(path, sizeof path, "/proc/%d/task/%d/stat", pid, t[i].tid);
            if (read_file(path, buf, sizeof buf) > 0 && parse_stat(buf, &ts) == 0) state_hist_add(&t[i].h, ts.state);
        }
        rounds++;
    }
    for (int i = 0; i < n; i++) {
        t[i].ok = t[i].ok && read_task_sample(pid, t[i].tid, &t[i].b) == 0;
        if (t[i].sched_ok) t[i].sched_ok = read_sched(pid, t[i].tid, &t[i].sleep1, &t[i].block1) == 0;
    }

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN), hz = sysconf(_SC_CLK_TCK);
    breakdown_t bd[MAXT];
    int nb = 0;
    printf("pid %d, %d threads, window %d s, %u state samples/thread, nproc %ld, schedstats %s\n",
           pid, n, secs, rounds, ncpu, schedstats_on ? "on" : "off");
    printf("%-7s %-15s %8s %8s %8s %8s | %5s %5s %5s | %-9s\n", "tid", "comm", "wall_ms", "run_ms", "wait_ms",
           "off_ms", "run%", "wait%", "off%", "verdict");
    for (int i = 0; i < n; i++) {
        breakdown_t *b = &bd[nb];
        if (!t[i].ok || account(&t[i].a, &t[i].b, b) != 0) { t[i].ok = 0; printf("%-7d (gone or reused)\n", t[i].tid); continue; }
        double w = (double)b->wall_ns;
        printf("%-7d %-15s %8.1f %8.1f %8.1f %8.1f | %5.1f %5.1f %5.1f | %-9s\n", t[i].tid, t[i].b.st.comm,
               w / 1e6, b->run_ns / 1e6, b->wait_ns / 1e6, b->off_ns / 1e6, 100.0 * b->run_ns / w,
               100.0 * b->wait_ns / w, 100.0 * b->off_ns / w, tt_class_name[classify(b, &t[i].h)]);
        nb++;
    }
    sys_check_t sc;
    int viol = verify_wall(bd, nb, (int)ncpu, 0.01, &sc);
    uint64_t sum_run = 0, sum_wait = 0, max_skew = 0;
    for (int i = 0; i < nb; i++) {
        sum_run += bd[i].run_ns;
        sum_wait += bd[i].wait_ns;
        if (bd[i].skew_ns > max_skew) max_skew = bd[i].skew_ns;
    }
    printf("sum run %.1f ms, sum wait %.1f ms | util %.3f of %ld CPUs | avg runnable %.2f (%s) | "
           "max skew %.3f ms | violations %d\n", sum_run / 1e6, sum_wait / 1e6, sc.util, ncpu, sc.avg_runnable,
           sc.oversub ? "OVERSUBSCRIBED" : "fits", max_skew / 1e6, viol);

    printf("\ncross-check: state samples vs schedstat, ctx switches, tick-based CPU, /proc/.../sched sleep\n");
    printf("%-15s %5s %6s | %4s %4s %4s | %6s %6s %6s | %8s %8s | %8s %8s %8s\n", "comm", "R%", "(r+w)%", "S%",
           "D%", "oth", "slices", "vcsw", "nvcsw", "tick_ms", "run_ms", "off_ms", "dsleep", "dblock");
    for (int i = 0, j = 0; i < n; i++) {
        if (!t[i].ok) continue;
        const breakdown_t *b = &bd[j++];
        const state_hist_t *h = &t[i].h;
        double hn = h->n ? (double)h->n : 1.0;
        uint64_t ticks = (t[i].b.st.utime + t[i].b.st.stime) - (t[i].a.st.utime + t[i].a.st.stime);
        printf("%-15s %5.1f %6.1f | %4.0f %4.0f %4u | %6" PRIu64 " %6" PRIu64 " %6" PRIu64 " | %8.1f %8.1f | ",
               t[i].b.st.comm, 100.0 * h->R / hn, 100.0 * (double)(b->run_ns + b->wait_ns) / (double)b->wall_ns,
               100.0 * h->S / hn, 100.0 * h->D / hn, h->other, b->slices, b->vcsw, b->nvcsw,
               ticks * 1000.0 / (double)hz, b->run_ns / 1e6);
        if (t[i].sched_ok)
            printf("%8.1f %8.1f %8.1f\n", b->off_ns / 1e6, (t[i].sleep1 - t[i].sleep0) / 1e6,
                   (t[i].block1 - t[i].block0) / 1e6);
        else
            printf("%8.1f %8s %8s\n", b->off_ns / 1e6, "-", "-");
    }
    return viol == 0 ? 0 : 1;
}
