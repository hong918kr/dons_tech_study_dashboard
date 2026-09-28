#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
static int ready;
static void *setter(void *a) { (void)a; usleep(200000); ready = 1; return NULL; }
static double now_ms(void) { struct timeval tv; gettimeofday(&tv,NULL); return tv.tv_sec*1000.0+tv.tv_usec/1000.0; }
int main(void) {
    pthread_t t; double t0 = now_ms();
    pthread_create(&t, NULL, setter, NULL);
    while (!ready) { }
    printf("v0: %.1f ms 만에 탈출\n", now_ms() - t0);
    pthread_join(t, NULL);
    return 0;
}
