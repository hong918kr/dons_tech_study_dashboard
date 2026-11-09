/* OS-M11 demo_abba.c — ABBA 를 두 방식으로 돌린다. 엔진은 solution.c 를 그대로 가져다 쓴다.
 *   ./demo_abba          순차: T1 이 끝난 뒤 T2. 실제 교착은 절대 안 일어나지만 mini-lockdep 은 보고한다.
 *   ./demo_abba conc     동시: 장벽으로 두 스레드가 첫 락을 쥔 뒤 둘째 락을 요청 → 진짜 교착.
 *                        보고는 막히기 "전에" 찍히고, main 의 감시(1 초 timedwait)가 교착을 확인한 뒤 종료한다.
 * build: cc -std=c11 -Wall -Wextra -O2 -g -pthread demo_abba.c -o demo_abba
 */
#define LD_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* 엔진의 안 쓰는 도우미 */
#include "solution.c"
#include <time.h>
#include <unistd.h>

static ld_mutex_t uart_lock, dma_lock;
static bool g_conc;
static pthread_mutex_t bar_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bar_c = PTHREAD_COND_INITIALIZER, done_c = PTHREAD_COND_INITIALIZER;
static int bar_n, done_n;

static void barrier2(void) {          /* 맥엔 pthread_barrier_t 가 없다 → mutex+cond 로 2 명짜리 장벽 */
    if (!g_conc) return;
    pthread_mutex_lock(&bar_m);
    if (++bar_n == 2) pthread_cond_broadcast(&bar_c);
    while (bar_n < 2) pthread_cond_wait(&bar_c, &bar_m);
    pthread_mutex_unlock(&bar_m);
}
static void finished(void) {
    pthread_mutex_lock(&bar_m); done_n++; pthread_cond_signal(&done_c); pthread_mutex_unlock(&bar_m);
}
__attribute__((noinline)) void uart_start_dma_tx(void) {     /* 경로 1: uart -> dma */
    ld_lock(&uart_lock, "uart_start_dma_tx:uart");
    barrier2();
    ld_lock(&dma_lock, "uart_start_dma_tx:dma");
    ld_unlock(&dma_lock);
    ld_unlock(&uart_lock);
}
__attribute__((noinline)) void dma_done_callback(void) {      /* 경로 2: dma -> uart (순서 위반) */
    ld_lock(&dma_lock, "dma_done_callback:dma");
    barrier2();
    ld_lock(&uart_lock, "dma_done_callback:uart");
    ld_unlock(&uart_lock);
    ld_unlock(&dma_lock);
}
static void *t1(void *a) { (void)a; ld_set_thread_name("tx_thread");  uart_start_dma_tx(); finished(); return NULL; }
static void *t2(void *a) { (void)a; ld_set_thread_name("dma_thread"); dma_done_callback(); finished(); return NULL; }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_conc = argc > 1 && strcmp(argv[1], "conc") == 0;
    ld_mutex_init(&uart_lock, "uart_lock"); ld_mutex_init(&dma_lock, "dma_lock");
    G.verbose = true;
    pthread_t a, b;
    if (!g_conc) {
        pthread_create(&a, NULL, t1, NULL); pthread_join(a, NULL);
        pthread_create(&b, NULL, t2, NULL); pthread_join(b, NULL);
        printf("sequential run finished normally: reports=%d, both threads completed\n", G.ncycle);
        return 0;
    }
    pthread_create(&a, NULL, t1, NULL); pthread_create(&b, NULL, t2, NULL);
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 1;
    pthread_mutex_lock(&bar_m);
    while (done_n < 2 && pthread_cond_timedwait(&done_c, &bar_m, &dl) == 0) {}
    int done = done_n;
    pthread_mutex_unlock(&bar_m);
    if (done < 2) {
        printf("WATCHDOG: no progress for 1 s, %d/2 threads finished -> real deadlock "
               "(tx_thread holds uart_lock waits dma_lock, dma_thread holds dma_lock waits uart_lock)\n", done);
        fflush(stdout);
        _exit(3);
    }
    printf("concurrent run finished (no deadlock this time)\n");
    return 0;
}
