#include <pthread.h>
#include <stdio.h>
#define N 1000000
static long counter = 0;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static void *bump(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) {
        pthread_mutex_lock(&m);
        counter++;
        pthread_mutex_unlock(&m);
    }
    return NULL;
}
int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, bump, NULL);
    pthread_create(&b, NULL, bump, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("expected %d, got %ld, lost %ld\n", 2*N, counter, (long)(2*N)-counter);
    return 0;
}
