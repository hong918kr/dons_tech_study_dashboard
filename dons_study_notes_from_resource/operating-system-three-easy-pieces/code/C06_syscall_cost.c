// C06_syscall_cost.c — OSTEP 6장 측정 숙제 (1): 타이머 정밀도와 시스템 콜 비용.
// macOS / Apple Silicon 용. 각 측정은 N회 반복을 5번 하고 최솟값(가장 방해 적은 run)을 쓴다.
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define N 1000000
#define RUNS 5

static uint64_t ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

__attribute__((noinline)) static int plain_function(int x) {
    __asm__ volatile("" ::: "memory");   // 컴파일러가 호출을 없애지 못하게
    return x + 1;
}

typedef void (*bench_fn)(int fd);
static void b_func(int fd)    { (void)plain_function(fd); }
static void b_getppid(int fd) { (void)fd; (void)getppid(); }
static void b_read0(int fd)   { char c; (void)read(fd, &c, 0); }       // 0바이트 read
static void b_write1(int fd)  { (void)write(fd, "x", 1); }             // /dev/null 에 1바이트
static void b_gtod(int fd)    { struct timeval tv; (void)fd; gettimeofday(&tv, NULL); }

static double bench(bench_fn f, int fd) {
    double best = 1e30;
    for (int r = 0; r < RUNS; r++) {
        uint64_t t0 = ns();
        for (int i = 0; i < N; i++) f(fd);
        double per = (double)(ns() - t0) / N;
        if (per < best) best = per;
    }
    return best;
}

int main(void) {
    // 1) 타이머 해상도: 연속 두 번 호출했을 때 0이 아닌 최소 차이
    struct timeval a, b;
    long min_us = 1000000;
    int zero = 0;
    for (int i = 0; i < 100000; i++) {
        gettimeofday(&a, NULL);
        gettimeofday(&b, NULL);
        long d = (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_usec - a.tv_usec);
        if (d == 0) zero++;
        else if (d < min_us) min_us = d;
    }
    printf("gettimeofday: back-to-back diff==0 in %d/100000 pairs, min nonzero = %ld us\n", zero, min_us);

    uint64_t m0 = mach_absolute_time(), m1 = mach_absolute_time();
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    printf("mach_absolute_time: tick = %u/%u ns (%.2f ns), back-to-back diff = %llu ticks\n",
           tb.numer, tb.denom, (double)tb.numer / tb.denom, (unsigned long long)(m1 - m0));
    uint64_t c0 = ns(), c1 = ns();
    printf("clock_gettime_nsec_np(UPTIME_RAW): back-to-back diff = %llu ns\n\n",
           (unsigned long long)(c1 - c0));

    int fd0 = open("/dev/zero", O_RDONLY);
    int fdn = open("/dev/null", O_WRONLY);
    printf("per-call cost (min of %d runs x %d calls):\n", RUNS, N);
    printf("  plain function call      : %7.1f ns\n", bench(b_func, 0));
    printf("  gettimeofday() (commpage): %7.1f ns\n", bench(b_gtod, 0));
    printf("  getppid()      (syscall) : %7.1f ns\n", bench(b_getppid, 0));
    printf("  read(fd, buf, 0)         : %7.1f ns\n", bench(b_read0, fd0));
    printf("  write(/dev/null, 1 byte) : %7.1f ns\n", bench(b_write1, fdn));
    return 0;
}
