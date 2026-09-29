/* interleave.c — two threads, three steps each. Print the order that
 * actually happened. The OS picks it, not you.                        */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static char log_[16];
static int  n = 0;

static void step(char who) {
    pthread_mutex_lock(&m);
    log_[n++] = who;                  /* 기록만 한다. 순서는 스케줄러가 정한다 */
    pthread_mutex_unlock(&m);
}
static void *worker(void *arg) {
    char who = *(char *)arg;
    for (int i = 0; i < 3; i++) { step(who); sched_yield(); }
    return NULL;
}
int main(void) {
    pthread_t a, b; char A = 'A', B = 'B';
    memset(log_, 0, sizeof log_);
    pthread_create(&a, NULL, worker, &A);
    pthread_create(&b, NULL, worker, &B);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("%s\n", log_);
    return 0;
}
