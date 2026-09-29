/* race.c — 2 threads x 1,000,000 increments, no lock */
#include <pthread.h>
#include <stdio.h>

#define N 1000000
static volatile long counter = 0;          /* shared, unprotected */

static void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++)
        counter++;                /* load -> add -> store */
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, bump, NULL);
    pthread_create(&b, NULL, bump, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("expected %d, got %ld, lost %ld\n",
           2 * N, counter, (long)(2 * N) - counter);
    return 0;
}
