/* OS-B03 demo_bench.c — 호출 한 번의 비용(ns/call): 함수 호출 vs 시계 읽기 vs 시스템 콜
 *
 * 각 대상(target)을 N 번 부르는 배치를 RUNS 번 잰다. 처음 1 배치는 버린다(워밍업).
 * N 은 대상마다 자동으로 정한다: 1000 번 시험 호출 → 배치 하나가 약 30 ms 가 되게 (인자로 고정 가능).
 *   ns/call = (배치 시간 / N) − 빈 루프 1 회 비용(loop 의 median)
 * 결과는 run 별 ns/call 의 min / median / max. 숫자 자체보다 "비율" 을 본다.
 *
 * 타이머: macOS = mach_absolute_time (commpage + CNTVCT_EL0, Apple Silicon 24 MHz → 41.67 ns 눈금)
 *         Linux = clock_gettime(CLOCK_MONOTONIC) (vDSO, ns 단위)
 * 배치 하나가 수 ms 이상이라 눈금 오차(±1 tick)는 1e-5 이하다.
 *
 * build (mac)  : cc  -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench [N] [RUNS]
 * build (linux): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench [N] [RUNS]
 *                strace -c ./bench 100000 3        (시스템 콜 집계 — vDSO 호출은 안 보인다)
 * 기본: N = 0 (자동, 배치 ≈ 30 ms), RUNS = 7
 */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <sys/syscall.h>
#endif

/* 컴파일러가 루프·호출을 지우지 못하게 하는 장벽 (명령은 0 개, "메모리를 건드렸을 수 있다" 고만 알림) */
#define BARRIER() __asm__ volatile("" ::: "memory")

static volatile uint64_t g_sink;          /* 반환값을 여기에 더해 "결과가 쓰인다" 고 보이게 */
static int g_fd = -1;                     /* /dev/null */

/* ---------- 타이머 ---------- */
#if defined(__APPLE__)
static mach_timebase_info_data_t g_tb;
static uint64_t now_ticks(void) { return mach_absolute_time(); }
static double ticks_to_ns(uint64_t t) { return (double)t * g_tb.numer / g_tb.denom; }
#else
static uint64_t now_ticks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static double ticks_to_ns(uint64_t t) { return (double)t; }
#endif

/* ---------- 측정 대상 ---------- */
__attribute__((noinline)) static uint64_t empty_fn(uint64_t x) {
    BARRIER();                            /* 본문이 비어도 call/ret 은 남는다 */
    return x;
}
static uint64_t (*volatile g_fnptr)(uint64_t) = empty_fn;   /* 간접 호출: 컴파일러가 대상을 모름 */

typedef enum {
    T_LOOP, T_EMPTY, T_FNPTR, T_GETPID, T_CG_MONO, T_CG_REAL, T_CG_RAW, T_NSEC_NP, T_MACH_ABS,
    T_GETPPID, T_READ0, T_WRITE1, T_CG_THREAD, T_SYS_CG, T_COUNT
} target_t;

static const char *const k_name[T_COUNT] = {
    "loop", "empty_fn()", "empty via fnptr", "getpid()",
    "clock_gettime(MONO)", "clock_gettime(REAL)", "clock_gettime(MONO_RAW)",
    "clock_gettime_nsec_np", "mach_absolute_time", "getppid()", "read(fd,buf,0)",
    "write(/dev/null,1)", "clock_gettime(THREAD_CPU)", "syscall(SYS_clock_gettime)",
};

static int supported(target_t t) {
#if defined(__APPLE__)
    return t != T_SYS_CG;                 /* syscall(2) 은 macOS 에서 deprecated */
#else
    return t != T_NSEC_NP && t != T_MACH_ABS;
#endif
}

/* 대상 하나를 n 번 → 걸린 tick. switch 는 루프 밖 — 루프 안에는 대상 호출과 BARRIER 만 */
static uint64_t run_batch(target_t t, uint64_t n) {
    struct timespec ts;
    char c = 'x';
    uint64_t acc = 0;
    uint64_t t0 = now_ticks();
    switch (t) {
    case T_LOOP:     for (uint64_t i = 0; i < n; i++) { BARRIER(); } break;
    case T_EMPTY:    for (uint64_t i = 0; i < n; i++) { acc += empty_fn(i); BARRIER(); } break;
    case T_FNPTR:    for (uint64_t i = 0; i < n; i++) { acc += g_fnptr(i); BARRIER(); } break;
    case T_GETPID:   for (uint64_t i = 0; i < n; i++) { acc += (uint64_t)getpid(); BARRIER(); } break;
    case T_GETPPID:  for (uint64_t i = 0; i < n; i++) { acc += (uint64_t)getppid(); BARRIER(); } break;
    case T_READ0:    for (uint64_t i = 0; i < n; i++) { acc += (uint64_t)read(g_fd, &c, 0); BARRIER(); } break;
    case T_WRITE1:   for (uint64_t i = 0; i < n; i++) { acc += (uint64_t)write(g_fd, &c, 1); BARRIER(); } break;
    case T_CG_MONO:  for (uint64_t i = 0; i < n; i++) { clock_gettime(CLOCK_MONOTONIC, &ts); acc += (uint64_t)ts.tv_nsec; BARRIER(); } break;
    case T_CG_REAL:  for (uint64_t i = 0; i < n; i++) { clock_gettime(CLOCK_REALTIME, &ts); acc += (uint64_t)ts.tv_nsec; BARRIER(); } break;
    case T_CG_RAW:   for (uint64_t i = 0; i < n; i++) { clock_gettime(CLOCK_MONOTONIC_RAW, &ts); acc += (uint64_t)ts.tv_nsec; BARRIER(); } break;
    case T_CG_THREAD:for (uint64_t i = 0; i < n; i++) { clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); acc += (uint64_t)ts.tv_nsec; BARRIER(); } break;
#if defined(__APPLE__)
    case T_NSEC_NP:  for (uint64_t i = 0; i < n; i++) { acc += clock_gettime_nsec_np(CLOCK_UPTIME_RAW); BARRIER(); } break;
    case T_MACH_ABS: for (uint64_t i = 0; i < n; i++) { acc += mach_absolute_time(); BARRIER(); } break;
#else
    case T_SYS_CG:   for (uint64_t i = 0; i < n; i++) { syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); acc += (uint64_t)ts.tv_nsec; BARRIER(); } break;
#endif
    default: break;
    }
    uint64_t t1 = now_ticks();
    g_sink += acc;
    return t1 - t0;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define MAX_RUNS 31

#define BATCH_NS 30e6                      /* 자동 N: 배치 하나 ≈ 30 ms ≫ 눈금 41.67 ns */

/* n == 0 이면 1000 번 시험 호출로 n 을 정한다. 처음 1 배치는 워밍업으로 버린다.
 * runs 개의 "호출당 ns(루프 포함)" 를 out 에 채우고, 쓴 n 을 돌려준다 */
static uint64_t measure(target_t t, uint64_t n, int runs, double *out) {
    if (n == 0) {
        double est = ticks_to_ns(run_batch(t, 1000)) / 1000.0;
        n = est > 0 ? (uint64_t)(BATCH_NS / est) : 10000000u;
        if (n < 1000) n = 1000;
        if (n > 100000000u) n = 100000000u;
    }
    (void)run_batch(t, n);
    for (int r = 0; r < runs; r++) out[r] = ticks_to_ns(run_batch(t, n)) / (double)n;
    return n;
}

int main(int argc, char **argv) {
    uint64_t n = argc > 1 ? strtoull(argv[1], NULL, 10) : 0;   /* 0 = 자동 */
    int runs = argc > 2 ? atoi(argv[2]) : 7;
    if (n != 0 && n < 1000) n = 1000;
    if (runs < 3) runs = 3;
    if (runs > MAX_RUNS) runs = MAX_RUNS;
#if defined(__APPLE__)
    mach_timebase_info(&g_tb);
    printf("timer: mach_absolute_time, timebase %u/%u → 1 tick = %.2f ns\n",
           g_tb.numer, g_tb.denom, (double)g_tb.numer / g_tb.denom);
#else
    struct timespec res;
    clock_getres(CLOCK_MONOTONIC, &res);
    printf("timer: clock_gettime(CLOCK_MONOTONIC), res %ld ns\n", res.tv_nsec);
#endif
    g_fd = open("/dev/null", O_RDWR);
    if (g_fd < 0) { perror("open /dev/null"); return 1; }

    /* 첫 호출(cold) — 한 번만, 다른 대상보다 먼저. 눈금이 굵으니 µs 단위만 의미 있음 */
    {
        uint64_t a = now_ticks(); g_sink += (uint64_t)getppid(); uint64_t b = now_ticks();
        uint64_t c2 = now_ticks(); g_sink += (uint64_t)getppid(); uint64_t d = now_ticks();
        printf("cold: 1st getppid %.0f ns, 2nd %.0f ns (단발 측정, 눈금 단위로 반올림됨)\n",
               ticks_to_ns(b - a), ticks_to_ns(d - c2));
    }

    double loop_runs[MAX_RUNS], v[MAX_RUNS];
    measure(T_LOOP, n, runs, loop_runs);
    qsort(loop_runs, (size_t)runs, sizeof loop_runs[0], cmp_d);
    double loop_ns = loop_runs[runs / 2];
    printf("%d runs (+1 warm-up) per target. loop overhead median %.3f ns/iter (아래 값에서 뺐다)\n\n",
           runs, loop_ns);
    double mn[T_COUNT] = {0}, md[T_COUNT] = {0}, mx[T_COUNT] = {0};
    uint64_t used[T_COUNT] = {0};
    for (int t = T_EMPTY; t < T_COUNT; t++) {
        if (!supported((target_t)t)) continue;
        used[t] = measure((target_t)t, n, runs, v);
        for (int r = 0; r < runs; r++) v[r] = v[r] > loop_ns ? v[r] - loop_ns : 0.0;
        qsort(v, (size_t)runs, sizeof v[0], cmp_d);
        mn[t] = v[0]; md[t] = v[runs / 2]; mx[t] = v[runs - 1];
    }
    printf("%-28s %10s %9s %9s %9s %10s\n", "target", "N/batch", "min", "median", "max", "/getppid");
    for (int t = T_EMPTY; t < T_COUNT; t++) {
        if (!supported((target_t)t)) continue;
        printf("%-28s %10llu %9.2f %9.2f %9.2f %10.3f\n", k_name[t], (unsigned long long)used[t],
               mn[t], md[t], mx[t], md[T_GETPPID] > 0 ? md[t] / md[T_GETPPID] : 0.0);
    }
    close(g_fd);
    return 0;
}
