// C02_threads.c — OSTEP Figure 2.5 (threads.c). 두 스레드가 공유 counter를 loops번씩 증가.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int counter = 0;
static int loops;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < loops; i++)
        counter++;              // load → add → store: 원자적이지 않다
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <loops>\n", argv[0]);
        return 1;
    }
    loops = atoi(argv[1]);
    pthread_t p1, p2;
    printf("Initial value : %d\n", counter);
    pthread_create(&p1, NULL, worker, NULL);
    pthread_create(&p2, NULL, worker, NULL);
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    printf("Final value   : %d (expected %d)\n", counter, 2 * loops);
    return 0;
}
