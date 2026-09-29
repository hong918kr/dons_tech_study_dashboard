#include <pthread.h>
#include <stdint.h>
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static float    g_val;
static uint64_t g_ts;
static int      g_have;

/* 샘플러 스레드만 호출한다 */
static void publish(float v, uint64_t ts) {
    pthread_mutex_lock(&g_mu);
    g_val = v;
    g_ts  = ts;
    g_have = 1;
    pthread_mutex_unlock(&g_mu);
}

/* 독자 N명이 호출한다 */
int get_latest(float *v, uint64_t *ts) {
    pthread_mutex_lock(&g_mu);
    int ok = g_have;
    if (ok) { *v = g_val; *ts = g_ts; }
    pthread_mutex_unlock(&g_mu);
    return ok ? 0 : -1;
}
int main(void){ float v; uint64_t t; publish(1.0f, 42); return get_latest(&v,&t); }
