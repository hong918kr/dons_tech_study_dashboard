/* ENV: linux */
/* LLAT-M05 demo_linux_affinity.c — 같은 캐시 라인 핑퐁을 pthread_setaffinity_np 로 CPU 쌍을 고정해 잰다.
 *
 *   ./aff                 행렬: (ping cpu, pong cpu) = (0,0) (0,1) ... (0,N-1), spin 과 cond 두 방식
 *   ./aff 2 3 spin        한 쌍만
 *
 * Lima VM 에서 돌리면 "CPU" 는 vCPU (호스트의 스레드) 이다 — 물리 코어 배치가 아니다.
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_affinity.c -o aff
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static _Alignas(128) _Atomic uint64_t g_seq;
static int g_cond;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static int pin(int cpu) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof s, &s);
}
static void wait_eq(uint64_t want) {
    if (!g_cond) { while (atomic_load_explicit(&g_seq, memory_order_acquire) != want) { } return; }
    pthread_mutex_lock(&g_mu);
    while (atomic_load_explicit(&g_seq, memory_order_relaxed) != want) pthread_cond_wait(&g_cv, &g_mu);
    pthread_mutex_unlock(&g_mu);
}
static void post(uint64_t v) {
    if (!g_cond) { atomic_store_explicit(&g_seq, v, memory_order_release); return; }
    pthread_mutex_lock(&g_mu);
    atomic_store_explicit(&g_seq, v, memory_order_relaxed);
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

typedef struct { int cpu; uint64_t rounds; } parg_t;
static void *pong(void *p) {
    parg_t *a = p;
    pin(a->cpu);
    for (uint64_t k = 1; k <= a->rounds; k++) { wait_eq(2 * k - 1); post(2 * k); }
    return NULL;
}
static int cmp(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y; return (a > b) - (a < b);
}

static void run(int c0, int c1, int cond, int samples, int batch) {
    g_cond = cond; atomic_store(&g_seq, 0);
    parg_t a = { c1, (uint64_t)samples * (uint64_t)batch };
    pthread_t t;
    if (pin(c0) != 0) { printf("pin %d failed\n", c0); return; }
    pthread_create(&t, NULL, pong, &a);
    uint64_t *s = calloc((size_t)samples, sizeof *s), k = 0;
    uint64_t start = now_ns();
    for (int i = 0; i < samples; i++) {
        uint64_t t0 = now_ns();
        for (int j = 0; j < batch; j++) { k++; post(2 * k - 1); wait_eq(2 * k); }
        s[i] = now_ns() - t0;
    }
    uint64_t total = now_ns() - start;
    pthread_join(t, NULL);
    int w = samples / 10, m = samples - w;
    qsort(s + w, (size_t)m, sizeof *s, cmp);
    printf("%-4s cpu%d<->cpu%d  p50 %9.1f  p99 %10.1f  min %8.1f ns/RT   (%d x %d RT, %.1f ms)\n",
           cond ? "cond" : "spin", c0, c1, (double)s[w + (m - 1) / 2] / batch,
           (double)s[w + (99 * m + 99) / 100 - 1] / batch, (double)s[w] / batch, samples, batch, total / 1e6);
    fflush(stdout);
    free(s);
}

int main(int argc, char **argv) {
    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (argc >= 4) { run(atoi(argv[1]), atoi(argv[2]), !strcmp(argv[3], "cond"), 2000, 64); return 0; }
    printf("online cpus: %d\n", n);
    for (int c = 1; c < n; c++) run(0, c, 0, 2000, 64);
    run(0, 0, 0, 20, 1);                 /* 같은 vCPU 에서 스핀: 상대가 돌 차례를 기다려야 함 → 타임슬라이스 단위 */
    for (int c = 0; c < n; c++) run(0, c, 1, 1000, 4);
    return 0;
}
