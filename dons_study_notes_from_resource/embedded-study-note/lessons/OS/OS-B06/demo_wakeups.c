/* OS-B06 demo_wakeups.c — signal vs broadcast 비용: "헛깨움(futile wakeup)" 을 센다
 *   A: CV 2개 + signal     (본문 정답)
 *   B: CV 1개 + broadcast  (OSTEP 30.3 covering condition 방식 — 맞지만 비쌈)
 *   C: CV 2개 + broadcast
 * 헛깨움 = cond_wait 에서 돌아왔는데 조건이 여전히 거짓이라 다시 잠든 횟수.
 * 시간은 참고용 (실행마다 흔들림). 정확성 체크는 starter/solution 에 있다.
 *
 * build: cc -std=c11 -O2 -pthread demo_wakeups.c -o dw && ./dw [P C cap total runs]
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CAPMAX 64
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t one = PTHREAD_COND_INITIALIZER, empty = PTHREAD_COND_INITIALIZER, fill = PTHREAD_COND_INITIALIZER;
static int buf[CAPMAX], cap, head, count, mode;
static long wakeups, futile;
static int per_prod, per_cons;

static void wait_until(pthread_cond_t *c, bool (*ok)(void)) {
    while (!ok()) {
        pthread_cond_wait(c, &m);
        wakeups++;
        if (!ok()) futile++;
    }
}
static bool has_space(void) { return count < cap; }
static bool has_item(void)  { return count > 0; }
static void wake(pthread_cond_t *two_cv_target) {
    if (mode == 0) pthread_cond_signal(two_cv_target);
    else if (mode == 1) pthread_cond_broadcast(&one);
    else pthread_cond_broadcast(two_cv_target);
}
static void *prod(void *a) {
    (void)a;
    for (int i = 0; i < per_prod; i++) {
        pthread_mutex_lock(&m);
        wait_until(mode == 1 ? &one : &empty, has_space);
        buf[(head + count) % cap] = i; count++;
        wake(&fill);
        pthread_mutex_unlock(&m);
    }
    return NULL;
}
static void *cons(void *a) {
    (void)a; long sum = 0;
    for (int i = 0; i < per_cons; i++) {
        pthread_mutex_lock(&m);
        wait_until(mode == 1 ? &one : &fill, has_item);
        sum += buf[head]; head = (head + 1) % cap; count--;
        wake(&empty);
        pthread_mutex_unlock(&m);
    }
    return (void *)sum;
}
static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv) {
    int np = argc > 1 ? atoi(argv[1]) : 4, nc = argc > 2 ? atoi(argv[2]) : 4;
    cap = argc > 3 ? atoi(argv[3]) : 4;
    int total = argc > 4 ? atoi(argv[4]) : 400000, runs = argc > 5 ? atoi(argv[5]) : 5;
    per_prod = total / np; per_cons = total / nc;
    const char *name[3] = { "A 2CV+signal   ", "B 1CV+broadcast", "C 2CV+broadcast" };
    printf("P=%d C=%d cap=%d items=%d, %d runs (median)\n", np, nc, cap, total, runs);
    for (mode = 0; mode < 3; mode++) {
        double ms[16]; long wk[16], fu[16];
        for (int r = 0; r < runs; r++) {
            head = count = 0; wakeups = futile = 0;
            pthread_t t[64];
            double t0 = now_ms();
            for (int i = 0; i < nc; i++) pthread_create(&t[i], NULL, cons, NULL);
            for (int i = 0; i < np; i++) pthread_create(&t[nc + i], NULL, prod, NULL);
            for (int i = 0; i < np + nc; i++) pthread_join(t[i], NULL);
            ms[r] = now_ms() - t0; wk[r] = wakeups; fu[r] = futile;
        }
        double sorted[16];
        for (int r = 0; r < runs; r++) sorted[r] = ms[r];
        qsort(sorted, (size_t)runs, sizeof sorted[0], cmp_d);
        long wsum = 0, fsum = 0;
        for (int r = 0; r < runs; r++) { wsum += wk[r]; fsum += fu[r]; }
        printf("  %s  time %7.1f ms   wakeups/item %.2f   futile/item %.2f   (futile %.0f%%)\n", name[mode],
               sorted[runs / 2], (double)wsum / runs / total, (double)fsum / runs / total,
               wsum ? 100.0 * (double)fsum / (double)wsum : 0.0);
    }
    return 0;
}
