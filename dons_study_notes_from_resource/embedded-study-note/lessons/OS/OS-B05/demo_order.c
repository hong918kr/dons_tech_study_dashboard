/* OS-B05 demo_order.c — sloppy counter 의 락 순서: update 는 local → global, exact 읽기를 global → local 로 쓰면?
 *
 * 워커 1개가 sc_update (local → global) 를, 리더 1개가 sc_get_exact 를 계속 부른다.
 *   기본 빌드         : exact 도 local → global (올바름)
 *   -DWRONG_ORDER    : exact 를 global → local 로 (순환 대기 가능 → 교착)
 * main 이 진행 카운터를 감시하다 1 초 동안 진척이 없으면 "DEADLOCK" 을 찍고 끝낸다 (멈춘 채로 두지 않음).
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread [-DWRONG_ORDER] demo_order.c -o dord && ./dord
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t glock = PTHREAD_MUTEX_INITIALIZER;   /* 전역 */
static pthread_mutex_t llock = PTHREAD_MUTEX_INITIALIZER;   /* 스레드 0 의 로컬 */
static long global, local;
static atomic_long upd_n, rd_n;
#define S 4
#define TARGET 2000000L

static void *updater(void *arg) {           /* sc_update: local → global */
    (void)arg;
    for (long i = 0; i < TARGET; i++) {
        pthread_mutex_lock(&llock);
        if (++local >= S) {
            pthread_mutex_lock(&glock);
            global += local;
            pthread_mutex_unlock(&glock);
            local = 0;
        }
        pthread_mutex_unlock(&llock);
        atomic_fetch_add_explicit(&upd_n, 1, memory_order_relaxed);
    }
    return NULL;
}
static void *reader(void *arg) {            /* sc_get_exact */
    (void)arg;
    for (;;) {
#ifdef WRONG_ORDER
        pthread_mutex_lock(&glock);         /* global → local : update 와 반대 순서 */
        pthread_mutex_lock(&llock);
#else
        pthread_mutex_lock(&llock);         /* local → global : update 와 같은 순서 */
        pthread_mutex_lock(&glock);
#endif
        global += local;
        local = 0;
        pthread_mutex_unlock(&llock);
        pthread_mutex_unlock(&glock);
        atomic_fetch_add_explicit(&rd_n, 1, memory_order_relaxed);
    }
    return NULL;
}

int main(void) {
    pthread_t u, r;
    pthread_create(&u, NULL, updater, NULL);
    pthread_create(&r, NULL, reader, NULL);
    long last = -1;
    for (int tick = 0; tick < 200; tick++) {             /* 최대 20 초, 100 ms 마다 확인 */
        struct timespec ts = {0, 100 * 1000000L};
        nanosleep(&ts, NULL);
        long u_n = atomic_load(&upd_n), r_n = atomic_load(&rd_n);
        if (u_n == TARGET) {
            pthread_mutex_lock(&llock); pthread_mutex_lock(&glock);
            printf("OK: %ld updates finished in ~%d00 ms, %ld exact reads, global+local=%ld\n",
                   u_n, tick + 1, r_n, global + local);
            return 0;                                      /* 리더 스레드는 exit 로 같이 끝난다 */
        }
        if (tick % 10 == 9) {                              /* 1 초마다 진척 확인 */
            if (u_n + r_n == last) {
                printf("DEADLOCK: no progress for 1 s (updates=%ld, exact reads=%ld)\n", u_n, r_n);
                fflush(stdout);
                _exit(2);
            }
            last = u_n + r_n;
        }
    }
    printf("timeout\n");
    return 1;
}
