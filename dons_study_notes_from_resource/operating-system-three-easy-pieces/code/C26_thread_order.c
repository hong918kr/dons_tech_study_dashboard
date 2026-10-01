// C26_thread_order.c — OSTEP Fig 26.2 (t0.c) 를 10000 번 돌려서
// "먼저 만든 스레드가 먼저 실행된다" 는 보장이 없음을 숫자로 확인한다.
// 각 스레드는 시작하자마자 공유 티켓(atomic)을 하나 뽑아 자기 실행 순서를 기록한다.
// build: cc -Wall -Wextra -O0 -pthread code/C26_thread_order.c -o .work/bin/C26_thread_order
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_int ticket;
static int order[2];                 // order[i] = 스레드 i 가 몇 번째로 실행됐나

static void *mythread(void *arg) {
    int id = (int)(long)arg;
    order[id] = atomic_fetch_add(&ticket, 1);
    return NULL;
}

int main(void) {
    const int TRIALS = 10000;
    int a_first = 0, b_first = 0;
    for (int t = 0; t < TRIALS; t++) {
        pthread_t p1, p2;
        atomic_store(&ticket, 0);
        int rc = pthread_create(&p1, NULL, mythread, (void *)0L); assert(rc == 0);   // "A"
        rc = pthread_create(&p2, NULL, mythread, (void *)1L);     assert(rc == 0);   // "B"
        rc = pthread_join(p1, NULL); assert(rc == 0);
        rc = pthread_join(p2, NULL); assert(rc == 0);
        if (order[0] < order[1]) a_first++; else b_first++;
    }
    printf("trials=%d  A ran first: %d  B ran first: %d\n", TRIALS, a_first, b_first);
    return 0;
}
