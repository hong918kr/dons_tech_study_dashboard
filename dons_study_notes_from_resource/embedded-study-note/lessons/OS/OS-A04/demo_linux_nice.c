/* ENV: linux */
/* OS-A04 실제 커널 교차 확인: 같은 CPU 하나에 묶은 CPU-bound 프로세스들의 nice 별 몫,
 * 그리고 "2 초 잔 뒤 깨어난 프로세스가 CPU 를 독점하나?" 를 사용자 공간에서 관찰한다.
 *   - 몫: 각 자식의 CLOCK_THREAD_CPUTIME_ID 비율 (Σ 대비)
 *   - 독점/기아: 자식이 CLOCK_MONOTONIC 을 계속 읽다가 50 µs 넘게 끊긴 구간 = 그동안 CPU 를 못 받음
 * 권한 없이 돈다 (nice 는 올리기만 — 0, 5, 10). 각 자식은 alarm(15) 워치독, 부모는 전부 wait.
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_linux_nice.c -o demo_linux_nice && ./demo_linux_nice
 */
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { double cpu_ms, cpu_after_ms, max_gap_after_ms, max_run_after_ms; int gaps_after, big_gaps_after; } res_t;

static double mono_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static double cpu_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* t0 기준 [sleep_until, end) 동안 돈다. mark 이후의 끊김/연속 실행을 기록 */
static void child(int cpu, int nice, double t0, double sleep_until, double mark, double end, res_t *r)
{
    alarm(15);
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof set, &set) != 0) _exit(2);
    if (setpriority(PRIO_PROCESS, 0, nice) != 0) _exit(3);
    while (mono_ms() - t0 < sleep_until) {
        struct timespec d = { 0, 1000000 }; nanosleep(&d, NULL);
    }
    double last = mono_ms(), run_start = last, cpu_mark = -1;
    for (;;) {
        double t = mono_ms();
        if (cpu_mark < 0 && t - t0 >= mark) { cpu_mark = cpu_ms(); run_start = t; last = t; }
        if (t - last > 0.05) {                         /* 50 µs 넘게 끊김 = 다른 태스크가 돌았다 */
            if (cpu_mark >= 0) {
                if (t - last > r->max_gap_after_ms) r->max_gap_after_ms = t - last;
                r->gaps_after++;
                if (t - last > 10.0) r->big_gaps_after++;      /* 10 ms 넘는 끊김 (이론상 ~2 슬라이스 ≈ 4 ms) */
                if (last - run_start > r->max_run_after_ms) r->max_run_after_ms = last - run_start;
            }
            run_start = t;
        }
        last = t;
        if (t - t0 >= end) break;
    }
    if (cpu_mark >= 0 && last - run_start > r->max_run_after_ms) r->max_run_after_ms = last - run_start;
    r->cpu_ms = cpu_ms();
    r->cpu_after_ms = cpu_mark >= 0 ? r->cpu_ms - cpu_mark : 0;
    _exit(0);
}

static int run(int cpu, int n, const int *nice, const double *sleep_until, double mark, double end, res_t *r)
{
    pid_t pid[8];
    double t0 = mono_ms() + 50;
    for (int i = 0; i < n; i++) {
        r[i] = (res_t){ 0 };
        pid[i] = fork();
        if (pid[i] < 0) return -1;
        if (pid[i] == 0) child(cpu, nice[i], t0, sleep_until[i], mark, end, &r[i]);
    }
    int bad = 0;
    for (int i = 0; i < n; i++) {
        int st; waitpid(pid[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) bad++;
    }
    return bad;
}

int main(void)
{
    res_t *r = mmap(NULL, 8 * sizeof(res_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) return 1;
    int cpu = (int)sysconf(_SC_NPROCESSORS_ONLN) - 1;
    const int w0 = 1024, w5 = 335, w10 = 110;
    printf("pinned to CPU %d\n", cpu);

    const int sets[3][3] = { { 0, 0, -1 }, { 0, 5, -1 }, { 0, 5, 10 } };
    const double zero[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        int n = sets[k][2] < 0 ? 2 : 3;
        if (run(cpu, n, sets[k], zero, 0, 3000, r) != 0) { printf("child failed\n"); return 1; }
        double sum = 0, wsum = 0;
        for (int i = 0; i < n; i++) {
            sum += r[i].cpu_ms;
            wsum += sets[k][i] == 0 ? w0 : sets[k][i] == 5 ? w5 : w10;
        }
        printf("nice {");
        for (int i = 0; i < n; i++) printf("%s%d", i ? "," : "", sets[k][i]);
        printf("}  cpu ms:");
        for (int i = 0; i < n; i++) printf(" %.0f", r[i].cpu_ms);
        printf("  share:");
        for (int i = 0; i < n; i++) {
            int w = sets[k][i] == 0 ? w0 : sets[k][i] == 5 ? w5 : w10;
            printf(" %.3f(ideal %.3f)", r[i].cpu_ms / sum, w / wsum);
        }
        printf("  (sum %.0f ms of 3000 wall)\n", sum);
    }

    /* 기준선: 잠자는 태스크 없이 hog 3 개 — 1..3 초 구간의 끊김 (VM 잡음 = 호스트 steal, 다른 프로세스) */
    if (run(cpu, 3, (const int[3]){ 0, 0, 0 }, zero, 1000, 3000, r) != 0) { printf("child failed\n"); return 1; }
    printf("baseline 3 hogs (1..3 s): max gap %.2f / %.2f / %.2f ms, gaps>10ms %d / %d / %d of %d / %d / %d\n",
           r[0].max_gap_after_ms, r[1].max_gap_after_ms, r[2].max_gap_after_ms,
           r[0].big_gaps_after, r[1].big_gaps_after, r[2].big_gaps_after,
           r[0].gaps_after, r[1].gaps_after, r[2].gaps_after);

    /* A, B: 처음부터 hog. C: 2 초 자고 깨어나 hog. 깨어난 뒤 2 초를 본다 */
    const int nice3[3] = { 0, 0, 0 };
    const double sl[3] = { 0, 0, 2000 };
    if (run(cpu, 3, nice3, sl, 2000, 4000, r) != 0) { printf("child failed\n"); return 1; }
    printf("sleeper: after C wakes (2..4 s): C cpu %.0f ms, A cpu %.0f ms, B cpu %.0f ms\n",
           r[2].cpu_after_ms, r[0].cpu_after_ms, r[1].cpu_after_ms);
    printf("         C max continuous run %.2f ms, A max gap %.2f ms, B max gap %.2f ms, gaps>10ms A %d B %d C %d\n",
           r[2].max_run_after_ms, r[0].max_gap_after_ms, r[1].max_gap_after_ms,
           r[0].big_gaps_after, r[1].big_gaps_after, r[2].big_gaps_after);
    munmap(r, 8 * sizeof(res_t));
    return 0;
}
