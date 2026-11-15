/* ARM-M02 demo_timer.c — EL0 에서 읽을 수 있는 카운터는? CNTFRQ / CNTVCT 실측 (+ mach_absolute_time 비교)
 *   cc -std=c11 -O2 demo_timer.c -o /tmp/ARM-M02_timer && /tmp/ARM-M02_timer
 *   (VM: gcc -std=gnu11 -O2 demo_timer.c -o /tmp/ARM-M02_timer && /tmp/ARM-M02_timer)
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#endif

static inline uint64_t cntvct(void) { uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t cntfrq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }

/* 자식 프로세스에서 시스템 레지스터 하나를 읽어 본다 → 트랩(SIGILL)이면 EL0 접근 불가 */
static void probe(const char *name, int which) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        uint64_t v = 0;
        if (which == 0) __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
        if (which == 1) __asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v));
        if (which == 2) __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
        printf("  %-12s readable: 0x%llx\n", name, (unsigned long long)v);
        fflush(stdout);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("  %-12s TRAP: signal %d (%s)\n", name, WTERMSIG(st), WTERMSIG(st) == SIGILL ? "SIGILL" : "other");
}

int main(void) {
    printf("CNTFRQ_EL0 = %llu Hz\n", (unsigned long long)cntfrq());
#if defined(__APPLE__)
    uint64_t tb = 0; size_t sz = sizeof tb;
    sysctlbyname("hw.tbfrequency", &tb, &sz, NULL, 0);
    mach_timebase_info_data_t ti; mach_timebase_info(&ti);
    printf("hw.tbfrequency = %llu Hz, mach_timebase_info = %u/%u ns per tick\n", (unsigned long long)tb, ti.numer, ti.denom);
#endif
    /* 1) 카운터가 실제로 몇씩 뛰나: 값이 바뀔 때의 증분 24개 */
    uint64_t prev = cntvct(), d[24]; int n = 0;
    while (n < 24) { uint64_t c = cntvct(); if (c != prev) { d[n++] = c - prev; prev = c; } }
    printf("CNTVCT increments:");
    for (int i = 0; i < 24; i++) printf(" %llu", (unsigned long long)d[i]);
    printf("\n");
    /* 2) 같은 구간을 CNTVCT 와 OS 시계로 */
    struct timespec t0, t1, req = {0, 50 * 1000 * 1000};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t c0 = cntvct();
#if defined(__APPLE__)
    uint64_t m0 = mach_absolute_time();
#endif
    nanosleep(&req, NULL);
    uint64_t c1 = cntvct();
#if defined(__APPLE__)
    uint64_t m1 = mach_absolute_time();
#endif
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
    printf("sleep 50ms: dCNTVCT = %llu ticks = %.3f ms (via CNTFRQ), clock_gettime = %.3f ms\n",
           (unsigned long long)(c1 - c0), (double)(c1 - c0) * 1e3 / (double)cntfrq(), ns / 1e6);
#if defined(__APPLE__)
    printf("            dmach   = %llu ticks, dCNTVCT/dmach = %.4f\n", (unsigned long long)(m1 - m0),
           (double)(c1 - c0) / (double)(m1 - m0));
#endif
    printf("EL0 access probe (fork per register):\n");
    probe("cntvct_el0", 2);
    probe("cntpct_el0", 0);
    probe("pmccntr_el0", 1);
    return 0;
}
