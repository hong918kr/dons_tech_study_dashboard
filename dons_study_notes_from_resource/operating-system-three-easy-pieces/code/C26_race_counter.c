// C26_race_counter.c — OSTEP Fig 26.6 (t1.c) 를 macOS(Apple Silicon)에서 재현
// 두 스레드가 공유 counter 를 각각 N 번 ++ 한다.
//   mode race   : 그냥 counter = counter + 1   (경쟁 조건, 틀린 값)
//   mode mutex  : pthread_mutex 로 임계 구역 보호
//   mode atomic : C11 atomic_fetch_add (하드웨어 원자 명령 1개)
// build: cc -Wall -Wextra -O0 -pthread code/C26_race_counter.c -o .work/bin/C26_race_counter
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 10000000

static volatile int counter = 0;           // 책과 동일: volatile 은 원자성을 주지 않는다
static atomic_int acounter = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int mode = 0;                       // 0 race, 1 mutex, 2 atomic

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        if (mode == 0) {
            counter = counter + 1;         // load → add → store : 3단계
        } else if (mode == 1) {
            pthread_mutex_lock(&lock);
            counter = counter + 1;
            pthread_mutex_unlock(&lock);
        } else {
            atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void run(int m, const char *name) {
    pthread_t p1, p2;
    mode = m;
    counter = 0;
    atomic_store(&acounter, 0);
    double t0 = now();
    if (pthread_create(&p1, NULL, worker, "A") || pthread_create(&p2, NULL, worker, "B")) {
        perror("pthread_create");
        exit(1);
    }
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    double t1 = now();
    int result = (m == 2) ? atomic_load(&acounter) : counter;
    printf("%-6s counter = %8d (expected %d, lost %8d)  %.3f s\n",
           name, result, 2 * N, 2 * N - result, t1 - t0);
}

int main(void) {
    for (int r = 0; r < 5; r++) run(0, "race");
    run(1, "mutex");
    run(2, "atomic");
    return 0;
}
