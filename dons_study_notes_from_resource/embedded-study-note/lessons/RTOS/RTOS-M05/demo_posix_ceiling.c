/* RTOS-M05 demo — POSIX PTHREAD_PRIO_PROTECT (= IPCP) 를 실제 OS 에서 관찰한다.
 *
 * Linux (glibc, root 필요 — SCHED_FIFO):
 *   gcc -std=c11 -Wall -Wextra -O2 -pthread demo_posix_ceiling.c -o dpc && sudo ./dpc
 * macOS (root 불필요, 결과는 md 참고):
 *   cc -std=c11 -Wall -Wextra -O2 -pthread demo_posix_ceiling.c -o dpc && ./dpc
 *
 * 순서: 내 우선순위 20 (SCHED_FIFO) → 천장 40 인 A 잠금 → 천장 30 인 B 잠금 → A 먼저 해제(LIFO 아님) → B 해제
 *       → 내 우선순위(20)보다 낮은 천장 10 인 뮤텍스 잠금 시도 (POSIX: EINVAL).
 */
#if defined(__linux__)
#define _GNU_SOURCE
#else
#define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static void show(const char *tag) {
    int pol;
    struct sched_param sp;
    pthread_getschedparam(pthread_self(), &pol, &sp);
    printf("%-26s pthread_getschedparam=%d", tag, sp.sched_priority);
#if defined(__linux__)
    struct sched_param kp;                       /* 커널이 실제로 쓰는 값 */
    sched_getparam(0, &kp);
    printf("  sched_getparam(kernel)=%d", kp.sched_priority);
#endif
    printf("\n");
}
static pthread_mutex_t make(int ceiling) {
    pthread_mutexattr_t a;
    pthread_mutex_t m;
    pthread_mutexattr_init(&a);
    int r1 = pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_PROTECT);
    int r2 = pthread_mutexattr_setprioceiling(&a, ceiling);
    if (r1 || r2) printf("attr error: setprotocol=%d setprioceiling=%d\n", r1, r2);
    pthread_mutex_init(&m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}

int main(void) {
    struct sched_param sp = { .sched_priority = 20 };
    int r = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    printf("setschedparam(SCHED_FIFO, 20) = %d %s\n", r, r ? strerror(r) : "ok");
    show("start (base 20)");
    pthread_mutex_t a = make(40), b = make(30), low = make(10);
    r = pthread_mutex_lock(&a);   printf("lock A (ceiling 40) = %d\n", r);  show("  holding A");
    r = pthread_mutex_lock(&b);   printf("lock B (ceiling 30) = %d\n", r);  show("  holding A+B");
    pthread_mutex_unlock(&a);                                              show("  unlock A (B still held)");
    pthread_mutex_unlock(&b);                                              show("  unlock B");
    r = pthread_mutex_lock(&low);
    printf("lock (ceiling 10 < my 20) = %d %s\n", r, r ? strerror(r) : "ok");
    if (r == 0) pthread_mutex_unlock(&low);
    return 0;
}
