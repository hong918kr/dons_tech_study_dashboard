/* RTOS-B03 demo: swapcontext 한 번은 얼마나 걸리나 (이 맥, -O2, 비율만 의미 있음)
 *
 *   main ↔ 태스크 ping-pong 을 N 번 → swapcontext 1회 시간. 비교: sigprocmask 1회, 빈 함수 호출 1회.
 *   macOS 의 swapcontext = getcontext(__sigaltstack + getrlimit + sigprocmask) + setcontext(sigprocmask)
 *   → 시스템 콜 4개. 레지스터 저장/복원 자체는 수십 명령이다 (md 5장의 디스어셈블 참고).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_switch_cost.c -o demo_cost && ./demo_cost
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

static ucontext_t g_main, g_t;
static _Alignas(16) uint8_t g_stk[64 * 1024];
static volatile long g_n;

static void pingpong(void) { for (;;) { g_n++; swapcontext(&g_t, &g_main); } }
__attribute__((noinline)) static void empty_call(void) { __asm__ volatile("" ::: "memory"); }

static double now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}

int main(void) {
    enum { N = 200000, REPS = 7 };
    getcontext(&g_t);
    g_t.uc_stack.ss_sp = g_stk; g_t.uc_stack.ss_size = sizeof g_stk; g_t.uc_stack.ss_flags = 0;
    g_t.uc_link = &g_main;
    makecontext(&g_t, pingpong, 0);
    for (int i = 0; i < 1000; i++) swapcontext(&g_main, &g_t);           /* 워밍업 */

    double sw[REPS], sp[REPS], fc[REPS];
    sigset_t s;
    for (int r = 0; r < REPS; r++) {
        double a = now_ns();
        for (long i = 0; i < N; i++) swapcontext(&g_main, &g_t);         /* 왕복 = swapcontext 2회 */
        sw[r] = (now_ns() - a) / (2.0 * N);
        a = now_ns();
        for (long i = 0; i < N; i++) sigprocmask(SIG_BLOCK, NULL, &s);
        sp[r] = (now_ns() - a) / N;
        a = now_ns();
        for (long i = 0; i < N; i++) empty_call();
        fc[r] = (now_ns() - a) / N;
    }
    qsort(sw, REPS, sizeof sw[0], cmp_d); qsort(sp, REPS, sizeof sp[0], cmp_d); qsort(fc, REPS, sizeof fc[0], cmp_d);
    printf("median of %d reps, N=%d each (this Mac, -O2):\n", REPS, N);
    printf("  swapcontext       : %7.1f ns/switch\n", sw[REPS / 2]);
    printf("  sigprocmask       : %7.1f ns/call\n", sp[REPS / 2]);
    printf("  empty fn call     : %7.2f ns/call\n", fc[REPS / 2]);
    printf("  ratio swap/sigprocmask = %.1f, swap/call = %.0f   (task ran %ld times)\n",
           sw[REPS / 2] / sp[REPS / 2], sw[REPS / 2] / fc[REPS / 2], (long)g_n);
    return 0;
}
