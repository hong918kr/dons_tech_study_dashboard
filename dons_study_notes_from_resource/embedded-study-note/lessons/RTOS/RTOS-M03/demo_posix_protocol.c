/* RTOS-M03 demo: 이 OS 의 pthread 뮤텍스가 어떤 우선순위 프로토콜을 받아 주는지 확인 (관찰용).
 * PTHREAD_PRIO_NONE    = 이 레슨의 "일반" 세마포어처럼 아무 대책 없음 (역전 그대로)
 * PTHREAD_PRIO_INHERIT = 우선순위 상속 (RTOS-M04)
 * PTHREAD_PRIO_PROTECT = 우선순위 천장, 즉시형(IPCP) (RTOS-M05)
 * 지원 여부만 본다 — 맥은 실시간 스케줄러가 아니므로 역전 자체를 여기서 재현하지는 않는다.
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static const char *pname(int p) {
    return p == PTHREAD_PRIO_NONE ? "PTHREAD_PRIO_NONE" : p == PTHREAD_PRIO_INHERIT ? "PTHREAD_PRIO_INHERIT"
         : p == PTHREAD_PRIO_PROTECT ? "PTHREAD_PRIO_PROTECT" : "?";
}
int main(void) {
    pthread_mutexattr_t a;
    int def = -1;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_getprotocol(&a, &def);
    printf("default protocol            : %s\n", pname(def));
    const int protos[] = { PTHREAD_PRIO_NONE, PTHREAD_PRIO_INHERIT, PTHREAD_PRIO_PROTECT };
    for (int k = 0; k < 3; k++) {
        int got = -1, rc = pthread_mutexattr_setprotocol(&a, protos[k]);
        pthread_mutexattr_getprotocol(&a, &got);
        pthread_mutex_t m;
        int rc_init = pthread_mutex_init(&m, &a);
        int rc_lock = rc_init == 0 ? pthread_mutex_lock(&m) : -1;
        if (rc_lock == 0) pthread_mutex_unlock(&m);
        if (rc_init == 0) pthread_mutex_destroy(&m);
        printf("setprotocol(%-20s): rc=%d (%s) get=%s init=%d lock=%d\n", pname(protos[k]), rc,
               rc ? strerror(rc) : "ok", pname(got), rc_init, rc_lock);
    }
    int ceil = -1, rc = pthread_mutexattr_getprioceiling(&a, &ceil);
    printf("prioceiling                 : rc=%d ceiling=%d\n", rc, ceil);
    pthread_mutexattr_destroy(&a);
    return 0;
}
