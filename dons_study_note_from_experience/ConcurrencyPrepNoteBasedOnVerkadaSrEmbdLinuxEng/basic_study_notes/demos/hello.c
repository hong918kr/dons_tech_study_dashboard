#include <pthread.h>
#include <stdio.h>
#include <string.h>

static void *worker(void *arg) {
    printf("worker: %s\n", (const char *)arg);
    return NULL;
}
int main(void) {
    pthread_t t;
    int rc = pthread_create(&t, NULL, worker, "hello");
    if (rc != 0) { fprintf(stderr, "create: %s\n", strerror(rc)); return 1; }
    pthread_join(t, NULL);
    return 0;
}
