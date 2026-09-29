#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
static atomic_int ready;
static void *setter(void *a) { (void)a; usleep(200000); atomic_store_explicit(&ready, 1, memory_order_release); return NULL; }
static double now_ms(void) { struct timeval tv; gettimeofday(&tv,NULL); return tv.tv_sec*1000.0+tv.tv_usec/1000.0; }
int main(void) {
    pthread_t t; double t0 = now_ms();
    pthread_create(&t, NULL, setter, NULL);
    while (!atomic_load_explicit(&ready, memory_order_acquire)) { }
    printf("v2: %.1f ms 만에 탈출\n", now_ms() - t0);
    pthread_join(t, NULL);
    return 0;
}
