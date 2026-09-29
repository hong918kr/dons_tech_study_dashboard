#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static int ready;                  /* volatile도 atomic도 아님 */

static void *setter(void *a) { (void)a; usleep(200000); ready = 1; return NULL; }

static double now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
int main(void) {
    pthread_t t; double t0 = now_ms();
    pthread_create(&t, NULL, setter, NULL);
    while (!ready) { }             /* 200 ms 기다려야 한다 */
    printf("루프를 빠져나오는 데 %.1f ms 걸렸다 (ready=%d)\n", now_ms() - t0, ready);
    pthread_join(t, NULL);
    return 0;
}
