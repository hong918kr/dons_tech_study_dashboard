/* C pthread 실측: volatile long ++ (data race, UB) vs atomic_fetch_add.
 *   cc -std=c11 -O2 experiments/gil/c_threads_race.c -o /tmp/c_race -lpthread && /tmp/c_race
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#define K 2000000
static volatile long plain;          /* volatile 이어도 원자적이지 않다 */
static atomic_long atom;
static void *work(void *arg) {
    (void)arg;
    for (long i = 0; i < K; i++) {
        plain++;                                   /* data race: UB */
        atomic_fetch_add_explicit(&atom, 1, memory_order_relaxed);
    }
    return NULL;
}
int main(void) {
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, work, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("C 4 threads x %d: volatile long lost %ld | atomic lost %ld\n", K, 4L * K - plain, 4L * K - atom);
    return 0;
}
