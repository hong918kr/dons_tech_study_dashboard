/* bench.c — how expensive are the pieces? (numbers, not feelings) */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

static uint64_t now_us(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}
static void *nop(void *a) { (void)a; return NULL; }

/* 두 스레드가 번갈아 turn 을 뒤집는다 = 한 번의 손바꿈 */
static volatile int turn = 0;
static void *pong(void *a) {
    (void)a;
    for (long i = 0; i < 200000L; i++) {
        while (turn != 1) sched_yield();
        turn = 0;
    }
    return NULL;
}

int main(void) {
    pthread_t t;
    uint64_t t0 = now_us();
    for (int i = 0; i < 10000; i++) {
        pthread_create(&t, NULL, nop, NULL);
        pthread_join(t, NULL);
    }
    uint64_t t1 = now_us();
    printf("pthread_create + join : %.2f us each\n", (t1 - t0) / 10000.0);

    t0 = now_us();
    for (long i = 0; i < 1000000L; i++) sched_yield();
    t1 = now_us();
    printf("sched_yield           : %.3f us each\n", (t1 - t0) / 1000000.0);

    pthread_create(&t, NULL, pong, NULL);
    t0 = now_us();
    for (long i = 0; i < 200000L; i++) {
        while (turn != 0) sched_yield();
        turn = 1;
    }
    t1 = now_us();
    pthread_join(t, NULL);
    printf("thread ping-pong      : %.3f us per handoff\n", (t1 - t0) / 200000.0);
    return 0;
}
