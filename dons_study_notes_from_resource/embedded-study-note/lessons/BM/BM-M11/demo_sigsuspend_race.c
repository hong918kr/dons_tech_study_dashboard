/* BM-M11 demo_sigsuspend_race.c — 같은 레이스를 호스트(POSIX)에서 진짜로 일으킨다.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sigsuspend_race.c -o dsr && ./dsr
 *
 * SIGUSR1 = "IRQ" (핸들러가 g_flag = 1), SIGUSR2 = "RTC 백스톱" (2 ms 안에 main 이 안 깨면 보조 스레드가 보냄).
 * naive:      if (!g_flag) { 창(W µs); pause(); }          ← 확인과 잠들기 사이에 신호가 오면 2 ms 백스톱까지 잠듦
 * sigsuspend: 막고 → while (!g_flag) { 창; sigsuspend(풀린 마스크); } → 복원
 *             sigsuspend 는 "마스크 풀기 + 잠들기" 를 원자적으로 한다 = Cortex-M 의 PRIMASK + WFI 와 같은 역할.
 * 보조 스레드는 신호를 막아 두고 pthread_kill 로 main 스레드에만 보낸다. 지연 d 는 고정 시드 xorshift.
 * 신호 핸들러는 volatile sig_atomic_t 대입만 한다 (C11 7.14.1.1p5).
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TRIALS 2000
#define WINDOW_NS 20000u       /* 확인 ~ 잠들기 사이의 창: 20 µs (실제 MCU 는 2~3 명령) */
#define DELAY_MAX_NS 60000u    /* IRQ 도착 시각: 0..60 µs 균등 */
#define BACKSTOP_NS 2000000u   /* 2 ms */

static volatile sig_atomic_t g_flag, g_backstop;
static atomic_int g_go = -1, g_done = -1;
static pthread_t g_main;
static int g_mode;             /* 0 = naive, 1 = sigsuspend */
static int g_backstops;        /* 보조 스레드가 백스톱을 보낸 횟수 (= 웨이크업 유실) */

static void on_usr1(int s) { (void)s; g_flag = 1; }
static void on_usr2(int s) { (void)s; g_backstop = 1; }

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static void spin_ns(uint64_t ns) { uint64_t t0 = now_ns(); while (now_ns() - t0 < ns) { } }
static uint32_t xs = 2463534242u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static void *helper(void *arg) {
    (void)arg;
    sigset_t all;
    sigemptyset(&all); sigaddset(&all, SIGUSR1); sigaddset(&all, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &all, NULL);
    for (int k = 0; k < TRIALS; k++) {
        while (atomic_load(&g_go) != k) { }
        spin_ns(xorshift32() % DELAY_MAX_NS);
        pthread_kill(g_main, SIGUSR1);                      /* IRQ */
        uint64_t t0 = now_ns();
        while (atomic_load(&g_done) != k && now_ns() - t0 < BACKSTOP_NS) { }
        if (atomic_load(&g_done) != k) {                    /* 아직도 자고 있다 → 백스톱 */
            g_backstops++;
            pthread_kill(g_main, SIGUSR2);
            while (atomic_load(&g_done) != k) { }
        }
    }
    return NULL;
}

static void run(int mode) {
    g_mode = mode;
    g_backstops = 0;
    xs = 2463534242u;
    atomic_store(&g_go, -1); atomic_store(&g_done, -1);
    sigset_t blk, old;
    sigemptyset(&blk); sigaddset(&blk, SIGUSR1); sigaddset(&blk, SIGUSR2);
    pthread_t th;
    pthread_create(&th, NULL, helper, NULL);
    uint64_t t0 = now_ns();
    for (int k = 0; k < TRIALS; k++) {
        g_flag = 0; g_backstop = 0;
        if (g_mode == 0) {
            atomic_store(&g_go, k);
            if (!g_flag) { spin_ns(WINDOW_NS); pause(); }   /* 레이스 */
            while (!g_flag) pause();                         /* (백스톱으로 깬 뒤에도 IRQ 는 이미 처리됨) */
        } else {
            sigprocmask(SIG_BLOCK, &blk, &old);              /* = CPSID i */
            atomic_store(&g_go, k);
            while (!g_flag) { spin_ns(WINDOW_NS); sigsuspend(&old); }   /* = WFI; CPSIE i 를 한 번에 */
            sigprocmask(SIG_SETMASK, &old, NULL);
        }
        atomic_store(&g_done, k);
    }
    uint64_t dt = now_ns() - t0;
    pthread_join(th, NULL);
    printf("%-10s trials=%d  lost(backstop needed)=%4d (%.1f%%)  total %.1f ms\n",
           mode ? "sigsuspend" : "naive", TRIALS, g_backstops, 100.0 * g_backstops / TRIALS, dt / 1e6);
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_usr1; sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = on_usr2; sigaction(SIGUSR2, &sa, NULL);
    g_main = pthread_self();
    printf("window=%u us, IRQ delay 0..%u us, backstop=%u ms\n", WINDOW_NS / 1000u, DELAY_MAX_NS / 1000u,
           BACKSTOP_NS / 1000000u);
    run(0);
    run(1);
    return 0;
}
