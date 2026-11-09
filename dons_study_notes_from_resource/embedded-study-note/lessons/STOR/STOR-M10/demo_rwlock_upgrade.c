/* STOR-M10 demo_rwlock_upgrade — pthread_rwlock_t 에는 업그레이드가 없다 (이 맥에서 실제로 어떻게 되나)
 *
 *   1) 한 스레드: rdlock 을 쥔 채 trywrlock → 무엇을 돌려주나?
 *   2) 한 스레드: rdlock 을 쥔 채 wrlock → 돌아오나? (POSIX: "may deadlock" 또는 EDEADLK — 구현 마음)
 *   3) 두 스레드: 둘 다 rdlock, 장벽, 둘 다 wrlock → 돌아오나?
 * 2)·3) 은 워치독(500 ms)이 "진행 0" 을 보고 보고만 하고 프로세스를 끝낸다 (멈춘 스레드는 exit 가 정리).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_rwlock_upgrade.c -o rwup && ./rwup
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_rwlock_t L2 = PTHREAD_RWLOCK_INITIALIZER, L3 = PTHREAD_RWLOCK_INITIALIZER;
static atomic_int g_returned2, g_returned3, g_holding3;
static int g_rc2 = -1;

static void *self_upgrade(void *p) {
    (void)p;
    pthread_rwlock_rdlock(&L2);
    g_rc2 = pthread_rwlock_wrlock(&L2);          /* 같은 스레드가 R 을 쥔 채 W */
    atomic_store(&g_returned2, 1);
    return NULL;
}
static void *two_upgrade(void *p) {
    (void)p;
    pthread_rwlock_rdlock(&L3);
    atomic_fetch_add(&g_holding3, 1);
    while (atomic_load(&g_holding3) < 2) { }     /* 둘 다 R 을 쥘 때까지 (간단한 스핀 장벽) */
    pthread_rwlock_wrlock(&L3);
    atomic_fetch_add(&g_returned3, 1);
    pthread_rwlock_unlock(&L3);
    return NULL;
}
static void nap_ms(int ms) { struct timespec d = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&d, NULL); }

int main(void) {
    pthread_rwlock_t L1 = PTHREAD_RWLOCK_INITIALIZER;
    pthread_rwlock_rdlock(&L1);
    int rc = pthread_rwlock_trywrlock(&L1);
    printf("1) same thread: rdlock held, trywrlock -> %d (%s)\n", rc, rc ? strerror(rc) : "ok");
    pthread_rwlock_unlock(&L1);

    pthread_t a; pthread_create(&a, NULL, self_upgrade, NULL); pthread_detach(a);
    nap_ms(500);
    if (atomic_load(&g_returned2)) printf("2) same thread: rdlock held, wrlock -> returned %d (%s)\n", g_rc2, strerror(g_rc2));
    else printf("2) same thread: rdlock held, wrlock -> still blocked after 500 ms (self-deadlock, no EDEADLK)\n");

    pthread_t b[2];
    for (int i = 0; i < 2; i++) { pthread_create(&b[i], NULL, two_upgrade, NULL); pthread_detach(b[i]); }
    nap_ms(500);
    printf("3) two threads: both rdlock, both wrlock -> %d of 2 returned after 500 ms (holding R: %d)\n",
           atomic_load(&g_returned3), atomic_load(&g_holding3));
    if (atomic_load(&g_returned3) == 0)
        printf("   LATCH DEADLOCK: T0 holds R(L) waits W(L) -> T1 holds R(L) waits W(L) -> T0\n");
    return 0;
}
