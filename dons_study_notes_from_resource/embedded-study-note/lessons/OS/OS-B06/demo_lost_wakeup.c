/* OS-B06 demo_lost_wakeup.c — 조건을 락 없이 검사하면 signal 을 잃는다 (lost wakeup)
 *
 *   기본(-DFIX 없음):  if (count == 0) { [틈] lock; wait; }   ← 검사와 잠들기 사이에 틈
 *   -DFIX          :  lock; while (count == 0) wait;          ← 검사 + 잠들기가 락 안에서 한 덩어리
 * 틈은 sleep 으로 일부러 넓혔다 (강제 스케줄). 생산자는 그 틈에 count=1 + signal 을 한다.
 * 잃어버린 signal 을 영원히 기다리지 않도록 소비자는 timedwait(1 s) 를 쓴다.
 *
 * build: cc -std=c11 -O2 -pthread demo_lost_wakeup.c -o dlw && ./dlw
 *        cc -std=c11 -O2 -pthread -DFIX demo_lost_wakeup.c -o dlw_fix && ./dlw_fix
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_lost_wakeup.c -o dlw_t && ./dlw_t
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
static int count;                  /* 공유 상태: 항상 m 으로 보호해야 한다 */

static void sleep_ms(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}
static struct timespec deadline_1s(void) {
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 1; return dl;
}

static void *producer(void *a) {
    (void)a;
    sleep_ms(10);                  /* 소비자가 먼저 count==0 을 보게 */
    pthread_mutex_lock(&m);
    count = 1;
    pthread_cond_signal(&fill);    /* 이 순간 fill 에서 잠든 스레드가 없으면 signal 은 사라진다 */
    pthread_mutex_unlock(&m);
    return NULL;
}

int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    double t0 = now_ms();
    int rc = 0;
    struct timespec dl = deadline_1s();
#ifndef FIX
    if (count == 0) {              /* ← 락 없이 읽음: 데이터 레이스 */
        sleep_ms(50);              /* ← 틈 (일부러 넓힘): 생산자가 여기서 count=1 + signal */
        pthread_mutex_lock(&m);
        rc = pthread_cond_timedwait(&fill, &m, &dl);   /* 이미 지나간 signal 을 기다린다 */
        pthread_mutex_unlock(&m);
    }
#else
    pthread_mutex_lock(&m);
    sleep_ms(50);                  /* 같은 틈을 락 안에 둬도: 생산자는 락을 못 잡아 끼어들 수 없다 */
    while (count == 0 && rc != ETIMEDOUT)
        rc = pthread_cond_timedwait(&fill, &m, &dl);
    pthread_mutex_unlock(&m);
#endif
    double el = now_ms() - t0;
    pthread_join(t, NULL);
    printf("%s: waited %.0f ms, rc=%s, count=%d\n",
#ifndef FIX
           "unlocked check",
#else
           "check under lock",
#endif
           el, rc == ETIMEDOUT ? "ETIMEDOUT (lost wakeup)" : "0", count);
    return 0;
}
