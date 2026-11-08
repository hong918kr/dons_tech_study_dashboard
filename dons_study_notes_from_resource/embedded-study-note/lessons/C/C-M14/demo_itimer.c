/* C-M14 demo_itimer.c — 진짜 타이머 시그널(setitimer → SIGALRM)로 찢어진 읽기를 "통계적으로" 센다.
 * 결과는 실행마다 다르다 (그래서 CHECK 테스트가 아니라 관찰용 데모).
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_itimer.c -o demo_itimer && ./demo_itimer
 *
 * 주의: 비동기 시그널 핸들러가 volatile uint32_t 를 읽고 쓰는 것은 C11 7.14.1.1p5 / POSIX 가 보장하는
 * 범위 밖이다 (보장되는 건 volatile sig_atomic_t 대입과 lock-free atomic 뿐). 실제 MCU ISR 을
 * 흉내 내기 위한 관찰용 코드이고, 이 Mac(clang, arm64)에서는 각 32비트 접근이 명령 하나라 그대로 동작한다.
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define STEP 0x40000000u          /* tick 마다 lo += 2^30 → 4 tick 마다 자리올림 (찢어짐이 자주 보이게) */
#define READS 2000000L
#define GAP 20                    /* hi 와 lo 읽기 사이 지연 (경쟁 창을 넓힌다) */

static volatile uint32_t g_hi, g_lo;
static volatile uint32_t g_isr_runs;
static volatile sig_atomic_t g_flag;
static atomic_uint g_count;

static void isr_ts(int sig) {                 /* 타임스탬프를 두 번에 나눠 갱신 */
    (void)sig;
    uint32_t lo = g_lo + STEP;
    g_lo = lo;
    if (lo < STEP) g_hi = g_hi + 1u;
    g_isr_runs = g_isr_runs + 1u;
}
static void isr_signal_only(int sig) {        /* Part A 설계: 대입 + lock-free atomic 만 */
    (void)sig;
    g_flag = 1;
    atomic_fetch_add_explicit(&g_count, 1u, memory_order_relaxed);
}
static void install(void (*h)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
}
static void timer_on(long usec) {
    struct itimerval it = { { 0, usec }, { 0, usec } };
    setitimer(ITIMER_REAL, &it, NULL);
}
static void timer_off(void) {
    struct itimerval it;
    memset(&it, 0, sizeof it);
    setitimer(ITIMER_REAL, &it, NULL);
}
static void gap(void) { for (volatile int i = 0; i < GAP; i++) { } }
static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static uint64_t rd_naive(void) {
    uint32_t hi = g_hi; gap(); uint32_t lo = g_lo;
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t rd_masked(void) {
    sigset_t b, old;
    sigemptyset(&b); sigaddset(&b, SIGALRM);
    sigprocmask(SIG_BLOCK, &b, &old);
    uint32_t hi = g_hi; gap(); uint32_t lo = g_lo;
    sigprocmask(SIG_SETMASK, &old, NULL);
    return ((uint64_t)hi << 32) | lo;
}
static long g_retries;
static uint64_t rd_reread(void) {
    uint32_t hi, lo;
    for (;;) {
        hi = g_hi; gap(); lo = g_lo;
        if (g_hi == hi) break;
        g_retries++;
    }
    return ((uint64_t)hi << 32) | lo;
}

static void run(const char *name, uint64_t (*rd)(void)) {
    g_hi = 0; g_lo = 0; g_isr_runs = 0; g_retries = 0;
    install(isr_ts);
    timer_on(100);                            /* 100 µs 주기 요청 */
    long back = 0;
    uint64_t prev = 0;
    double t0 = now_s();
    for (long i = 0; i < READS; i++) {
        uint64_t v = rd();
        if (v < prev) back++;                 /* 타임스탬프가 뒤로 감 = 찢어진 읽기 */
        prev = v;
    }
    double dt = now_s() - t0;
    timer_off();
    printf("%-8s reads=%ld  ISR=%u  backwards(torn)=%ld  retries=%ld  %.1f ns/read\n",
           name, READS, (unsigned)g_isr_runs, back, g_retries, dt * 1e9 / (double)READS);
}

int main(void) {
    run("naive", rd_naive);
    run("masked", rd_masked);
    run("reread", rd_reread);

    /* Part A 설계: ISR 은 신호만, main 이 느리게 폴링하면 플래그는 tick 을 합쳐 버린다 */
    g_flag = 0; atomic_store(&g_count, 0u);
    install(isr_signal_only);
    timer_on(100);
    long by_flag = 0, by_count = 0;
    double t0 = now_s();
    while (now_s() - t0 < 0.5) {
        for (volatile int i = 0; i < 20000; i++) { }   /* main 루프의 "다른 일" */
        if (g_flag) { g_flag = 0; by_flag++; }
        by_count += (long)atomic_exchange(&g_count, 0u);
    }
    timer_off();
    by_count += (long)atomic_exchange(&g_count, 0u);
    printf("signal-only 0.5 s: ticks seen by flag=%ld, by atomic counter=%ld\n", by_flag, by_count);
    return 0;
}
