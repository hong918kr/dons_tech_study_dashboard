/* ENV: linux */
/* OS-M02 demo_linux_pin.c — 리눅스에서는 코어를 고정(sched_setaffinity)할 수 있다: 같은 CPU vs 다른 CPU
 *
 * 맥에는 sched_setaffinity 가 없어서 "같은 코어 강제" 실험을 못 한다. 리눅스(Lima VM)에서 비교한다.
 *   ./pin A B [proc]   핑을 CPU A, 퐁을 CPU B 에 고정 (-1 = 고정 안 함). proc 을 주면 fork 한 프로세스끼리.
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_pin.c -o pin
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROUNDS 200000u
#define WARM 2000u

static int a2b[2], b2a[2];
static uint64_t rt[ROUNDS];

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);       /* 리눅스: vDSO, ns 해상도 */
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static void pin(int cpu) {
    if (cpu < 0) return;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    if (sched_setaffinity(0, sizeof s, &s)) { perror("sched_setaffinity"); exit(1); }   /* 0 = 호출한 스레드 */
}
static int g_cpu_pong;
static void pong(void) {
    pin(g_cpu_pong);
    uint8_t b;
    for (unsigned i = 0; i < ROUNDS; i++) {
        if (read(a2b[0], &b, 1) != 1) exit(2);
        b++;
        if (write(b2a[1], &b, 1) != 1) exit(3);
    }
}
static void *pong_thread(void *arg) { (void)arg; pong(); return NULL; }
static int cmp(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    int ca = argc > 1 ? atoi(argv[1]) : -1;
    g_cpu_pong = argc > 2 ? atoi(argv[2]) : -1;
    int proc = argc > 3 && !strcmp(argv[3], "proc");
    if (pipe(a2b) || pipe(b2a)) return 1;
    pid_t pid = -1;
    pthread_t t;
    if (proc) {
        pid = fork();
        if (pid == 0) { pong(); _exit(0); }
    } else {
        pthread_create(&t, NULL, pong_thread, NULL);
    }
    pin(ca);
    uint8_t b = 0, r;
    for (unsigned i = 0; i < ROUNDS; i++) {
        uint64_t t0 = now_ns();
        if (write(a2b[1], &b, 1) != 1 || read(b2a[0], &r, 1) != 1) return 4;
        rt[i] = now_ns() - t0;
        b = r;
    }
    if (proc) waitpid(pid, NULL, 0); else pthread_join(t, NULL);
    unsigned n = ROUNDS - WARM;
    qsort(rt + WARM, n, sizeof rt[0], cmp);
    printf("%-6s ping@%2d pong@%2d  min %6llu  med %6llu  p99 %7llu ns\n", proc ? "proc" : "thread", ca, g_cpu_pong,
           (unsigned long long)rt[WARM], (unsigned long long)rt[WARM + n / 2],
           (unsigned long long)rt[WARM + (99u * n + 99u) / 100u - 1u]);
    return 0;
}
