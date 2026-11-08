/* RTOS-A08 demo: IPI 대용 3가지의 한쪽 방향 지연 (호스트 측정 — MCU 값이 아니다)
 *
 *   doorbell : 받는 "코어"가 idle — 조건 변수에서 잠들어 있다 (solution.c 의 idle_wait / ipi_send)
 *   poll     : 받는 "코어"가 태스크 실행 중 — step 마다 atomic 펜딩 비트를 본다 (solution.c 의 명령어 경계)
 *   signal   : 받는 "코어"가 실행 중 — pthread_kill(SIGUSR1), 핸들러가 lock-free atomic 비트를 세운다
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_ipi_latency.c -o dil && ./dil
 */
#define _DARWIN_C_SOURCE                 /* clock_gettime_nsec_np, pthread_kill, sigaction */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NS 4000
#define WARM 200
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static atomic_bool g_ipi;
static atomic_uint_fast64_t g_t0;
static atomic_int g_ack;
static uint64_t g_lat[NS + WARM];
static int g_mode;                       /* 0 doorbell, 1 poll, 2 signal */
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;

static void on_sigusr1(int s) { (void)s; atomic_store_explicit(&g_ipi, true, memory_order_release); }

static void *receiver(void *a) {
    (void)a;
    volatile uint32_t work = 0;
    for (int i = 0; i < NS + WARM; i++) {
        if (g_mode == 0) {
            pthread_mutex_lock(&g_m);
            while (!atomic_load(&g_ipi)) pthread_cond_wait(&g_cv, &g_m);
            pthread_mutex_unlock(&g_m);
        } else {
            while (!atomic_load_explicit(&g_ipi, memory_order_acquire)) work = work + 1;  /* "태스크 실행" */
        }
        uint64_t t1 = now_ns();
        atomic_store(&g_ipi, false);
        g_lat[i] = t1 - atomic_load(&g_t0);
        atomic_store_explicit(&g_ack, i + 1, memory_order_release);
    }
    return NULL;
}
static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
static void run(int mode, const char *name) {
    g_mode = mode;
    atomic_store(&g_ack, 0);
    atomic_store(&g_ipi, false);
    pthread_t th;
    pthread_create(&th, NULL, receiver, NULL);
    struct timespec gap = {0, 20000};    /* 20 µs 간격 (상대가 다시 잠들/돌 시간) */
    for (int i = 0; i < NS + WARM; i++) {
        nanosleep(&gap, NULL);
        atomic_store(&g_t0, now_ns());
        if (mode == 0) {
            atomic_store(&g_ipi, true);
            pthread_mutex_lock(&g_m); pthread_cond_signal(&g_cv); pthread_mutex_unlock(&g_m);
        } else if (mode == 1) {
            atomic_store_explicit(&g_ipi, true, memory_order_release);
        } else {
            pthread_kill(th, SIGUSR1);
        }
        while (atomic_load_explicit(&g_ack, memory_order_acquire) != i + 1) { }
    }
    pthread_join(th, NULL);
    qsort(g_lat + WARM, NS, sizeof g_lat[0], cmp);
    uint64_t *v = g_lat + WARM;
    printf("%-9s n=%d  min=%6llu ns  median=%6llu ns  p99=%7llu ns  max=%8llu ns\n", name, NS,
           (unsigned long long)v[0], (unsigned long long)v[NS / 2],
           (unsigned long long)v[NS * 99 / 100], (unsigned long long)v[NS - 1]);
}
int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigusr1;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    printf("atomic_bool lock-free: %d (핸들러에서 써도 되는 조건, C11 7.14.1.1p5)\n", (int)atomic_is_lock_free(&g_ipi));
    run(0, "doorbell");
    run(1, "poll");
    run(2, "signal");
    return 0;
}
