/* ENV: linux */
/* LPERF-B10 demo_workload — 성격이 다른 스레드를 섞어 띄우는 부하 프로그램 (측정 대상)
 *
 *   cpu-N   : 계속 계산만 한다 (on-CPU, CPU 수보다 많이 띄우면 런큐 대기가 생긴다)
 *   lock-N  : 뮤텍스 하나를 두고 다툰다. 잡으면 CPU 2 ms 일하고 놓는다 (나머지는 futex 대기 = S)
 *   io-N    : 4 KiB pwrite + fdatasync 반복 (/var/tmp, ext4) → 디스크 완료 대기 = D
 *   tick-N  : 10 ms 마다 깨어나 0.5 ms 일하는 주기 스레드 (센서 폴링 같은 것, 대부분 S)
 *
 * usage: ./wl <cpu> <lock> <io> <tick> <seconds>
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_workload.c -o wl
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static atomic_int g_stop;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t cpu_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static void burn(uint64_t ns) {               /* 자기 CPU 시간으로 ns 만큼 일한다 (선점당해도 정확) */
    uint64_t t0 = cpu_ns();
    volatile uint64_t x = 0;
    while (cpu_ns() - t0 < ns) x = x + 1;
}

static void *cpu_thr(void *arg) {
    (void)arg;
    volatile uint64_t x = 0;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed))
        for (int i = 0; i < 100000; i++) x = x + 1;
    return NULL;
}
static void *lock_thr(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        pthread_mutex_lock(&g_lock);
        burn(2000000);                        /* 락을 쥔 채 2 ms */
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}
static void *io_thr(void *arg) {
    int id = (int)(intptr_t)arg;
    char path[64], buf[4096];
    memset(buf, 'x', sizeof buf);
    snprintf(path, sizeof path, "/var/tmp/lperf_b10_io.%d", id);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { perror("open"); return NULL; }
    for (off_t off = 0; !atomic_load_explicit(&g_stop, memory_order_relaxed); off = (off + 4096) % (1 << 20)) {
        if (pwrite(fd, buf, sizeof buf, off) != (ssize_t)sizeof buf) { perror("pwrite"); break; }
        if (fdatasync(fd) != 0) { perror("fdatasync"); break; }
    }
    close(fd);
    unlink(path);
    return NULL;
}
static void *tick_thr(void *arg) {
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        next.tv_nsec += 10000000L;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        burn(500000);                         /* 깨어나서 0.5 ms 처리 */
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 6) { fprintf(stderr, "usage: %s cpu lock io tick seconds\n", argv[0]); return 2; }
    int n[4] = { atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]) };
    int secs = atoi(argv[5]);
    void *(*fn[4])(void *) = { cpu_thr, lock_thr, io_thr, tick_thr };
    const char *nm[4] = { "cpu", "lock", "io", "tick" };
    pthread_t th[256];
    int nt = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < n[k] && nt < 256; i++) {
            char name[16];
            snprintf(name, sizeof name, "%s-%d", nm[k], i);
            if (pthread_create(&th[nt], NULL, fn[k], (void *)(intptr_t)i) != 0) { perror("pthread_create"); return 1; }
            pthread_setname_np(th[nt], name);  /* comm (stat 2번째 필드) 에 보이는 이름, 최대 15자 */
            nt++;
        }
    printf("pid %d: %d threads (cpu %d, lock %d, io %d, tick %d) for %d s, nproc %ld\n", (int)getpid(), nt,
           n[0], n[1], n[2], n[3], secs, sysconf(_SC_NPROCESSORS_ONLN));
    fflush(stdout);
    sleep((unsigned)secs);
    atomic_store(&g_stop, 1);
    for (int i = 0; i < nt; i++) pthread_join(th[i], NULL);
    return 0;
}
