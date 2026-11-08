/* OS-B06 demo_freerun.c — 강제 스케줄 없이, 진짜 pthread_cond 로 깨진 버전을 그냥 돌려 본다.
 *   v1 = if + CV 1개 (OSTEP 그림 30.6), v2 = while + CV 1개 (30.8), v3 = while + CV 2개 (30.10)
 * 감시(watchdog): 200 ms 동안 소비 개수가 그대로이고 모두 잠들어 있으면 HANG(전원 수면)으로 기록하고
 * closed + broadcast 로 구조한 뒤 다음 실행으로 넘어간다.
 *
 * build: cc -std=c11 -O2 -pthread demo_freerun.c -o dfr && ./dfr [P C N runs]
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t one, empty, fill;
    int item, count;              /* 용량 1 (OSTEP 과 같음) */
    bool closed;
    int waiting, nthreads_alive;
    long consumed, underflow, overflow;
} buf_t;
static buf_t B;
static int g_ver;                 /* 1, 2, 3 */

static void wait_c(pthread_cond_t *c) { B.waiting++; pthread_cond_wait(c, &B.m); B.waiting--; }
static void put_raw(int v) { if (B.count == 1) B.overflow++; else { B.item = v; B.count = 1; } }
static int get_raw(void) { if (B.count == 0) { B.underflow++; return -1; } B.count = 0; return B.item; }

static bool put(int v) {
    pthread_mutex_lock(&B.m);
    if (g_ver == 1) { if (B.count == 1 && !B.closed) wait_c(&B.one); }
    else { pthread_cond_t *c = g_ver == 2 ? &B.one : &B.empty; while (B.count == 1 && !B.closed) wait_c(c); }
    if (B.closed) { pthread_mutex_unlock(&B.m); return false; }
    put_raw(v);
    pthread_cond_signal(g_ver == 3 ? &B.fill : &B.one);
    pthread_mutex_unlock(&B.m);
    return true;
}
static bool get(int *v) {
    pthread_mutex_lock(&B.m);
    if (g_ver == 1) { if (B.count == 0 && !B.closed) wait_c(&B.one); }
    else { pthread_cond_t *c = g_ver == 2 ? &B.one : &B.fill; while (B.count == 0 && !B.closed) wait_c(c); }
    if (B.count == 0 && B.closed) { pthread_mutex_unlock(&B.m); return false; }
    *v = get_raw();
    if (*v >= 0) B.consumed++;
    pthread_cond_signal(g_ver == 3 ? &B.empty : &B.one);
    pthread_mutex_unlock(&B.m);
    return true;
}
static int g_per;
static void *prod(void *a) {
    (void)a;
    for (int i = 0; i < g_per; i++) if (!put(i)) break;
    pthread_mutex_lock(&B.m); B.nthreads_alive--; pthread_mutex_unlock(&B.m);
    return NULL;
}
static void *cons(void *a) {
    (void)a; int v;
    while (get(&v)) { }
    pthread_mutex_lock(&B.m); B.nthreads_alive--; pthread_mutex_unlock(&B.m);
    return NULL;
}
static void sleep_ms(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
static void close_all(void) {
    B.closed = true;
    pthread_cond_broadcast(&B.one); pthread_cond_broadcast(&B.empty); pthread_cond_broadcast(&B.fill);
}

int main(int argc, char **argv) {
    int np = argc > 1 ? atoi(argv[1]) : 1, nc = argc > 2 ? atoi(argv[2]) : 2;
    g_per = argc > 3 ? atoi(argv[3]) : 200000;
    int runs = argc > 4 ? atoi(argv[4]) : 5;
    for (g_ver = 1; g_ver <= 3; g_ver++) {
        printf("v%d P=%d C=%d N=%d/producer:\n", g_ver, np, nc, g_per);
        for (int r = 0; r < runs; r++) {
            B = (buf_t){ .m = PTHREAD_MUTEX_INITIALIZER, .one = PTHREAD_COND_INITIALIZER,
                         .empty = PTHREAD_COND_INITIALIZER, .fill = PTHREAD_COND_INITIALIZER };
            B.nthreads_alive = np + nc;
            pthread_t t[64];
            for (int i = 0; i < nc; i++) pthread_create(&t[i], NULL, cons, NULL);
            for (int i = 0; i < np; i++) pthread_create(&t[nc + i], NULL, prod, NULL);
            long last = -1; int still = 0; bool hang = false; int sleepers = 0;
            for (;;) {
                sleep_ms(10);
                pthread_mutex_lock(&B.m);
                if (B.consumed + B.overflow >= (long)np * g_per && !B.closed) close_all();   /* 다 보냄 → 닫기 */
                if (B.nthreads_alive == 0) { pthread_mutex_unlock(&B.m); break; }
                if (B.consumed == last && B.waiting == B.nthreads_alive) still++; else still = 0;
                last = B.consumed;
                if (still >= 20) { hang = true; sleepers = B.waiting; close_all(); }   /* 200 ms 정지 + 전원 수면 */
                pthread_mutex_unlock(&B.m);
            }
            for (int i = 0; i < np + nc; i++) pthread_join(t[i], NULL);
            printf("  run %d: consumed=%-7ld underflow=%-5ld overflow=%-5ld %s", r, B.consumed, B.underflow,
                   B.overflow, hang ? "HANG (all asleep)" : "finished");
            if (hang) printf(" sleepers=%d", sleepers);
            printf("\n");
        }
    }
    return 0;
}
