/* TOOL-M09 demo_lock_order.c — 버그 3: 락 순서 역전 (lock-order inversion, 잠재적 데드락)
 *
 * 두 스레드를 "차례로" 돌린다 (T1 끝난 뒤 T2 시작) → 실제로는 절대 멈추지 않는다.
 * 그래도 TSan 은 락 순서 그래프에 A→B, B→A 사이클이 생긴 것을 보고 경고한다.
 * 주의: 이 맥(Apple clang 21)의 TSan 런타임은 detect_deadlocks 기본값이 false 이고, =1 로 켜도
 *       이 보고를 내지 않았다 (실측). 보고는 Linux(Lima VM elinux, clang 21 / gcc 15)에서 본다.
 *
 * Linux (VM 안):
 *   clang -std=c11 -g -O1 -fsanitize=thread -pthread demo_lock_order.c -o dl_t
 *   ./dl_t                                                        # lock-order-inversion 보고 (exit 66)
 *   TSAN_OPTIONS=second_deadlock_stack=1 ./dl_t                   # 각 락을 "먼저" 잡은 곳까지
 *   clang -std=c11 -g -O1 -fsanitize=thread -pthread -DFIX demo_lock_order.c -o dl_ft && ./dl_ft   # 보고 없음
 * Mac:
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_lock_order.c -o dl_t
 *   TSAN_OPTIONS=detect_deadlocks=1 ./dl_t                        # 보고 없음 (이 런타임의 한계)
 *   cc -std=c11 -O2 -pthread -DCONCURRENT demo_lock_order.c -o dl_c && ./dl_c   # 동시에 돌리면 진짜로 멈춤 (2초 타임아웃)
 *   cc -std=c11 -O2 -pthread -DCONCURRENT -DFIX demo_lock_order.c -o dl_cf && ./dl_cf   # 고친 버전: 끝까지 돈다
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t lock;
    unsigned id;                    /* 락 순서용 고유 번호 */
    long balance;                   /* 예: DMA 버퍼 크레딧, 큐의 원소 수 */
} pool_t;

static pool_t g_rx, g_tx;

static void pool_init(pool_t *p, unsigned id, long balance) {
    pthread_mutex_init(&p->lock, NULL);    /* 동적 초기화 → TSan 이 "Mutex Mx created at" 을 찍는다 */
    p->id = id;
    p->balance = balance;
}

__attribute__((noinline)) static void move_credit(pool_t *from, pool_t *to, long n) {
#ifdef FIX
    pool_t *first  = from->id < to->id ? from : to;    /* 항상 id 가 작은 쪽부터 */
    pool_t *second = from->id < to->id ? to : from;
#else
    pool_t *first = from, *second = to;                /* 버그: 인자 순서대로 잠금 */
#endif
    pthread_mutex_lock(&first->lock);
    pthread_mutex_lock(&second->lock);
    from->balance -= n;
    to->balance += n;
    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
}

#ifdef CONCURRENT
#define REPS 1000000
#else
#define REPS 1
#endif
static void *rx_to_tx(void *a) { (void)a; for (int i = 0; i < REPS; i++) move_credit(&g_rx, &g_tx, 1); return NULL; }
static void *tx_to_rx(void *a) { (void)a; for (int i = 0; i < REPS; i++) move_credit(&g_tx, &g_rx, 1); return NULL; }

static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "TIMEOUT: deadlock (each thread holds one lock and waits for the other)\n";
    ssize_t r = write(2, msg, sizeof msg - 1);
    (void)r;
    _exit(3);
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(2);
    pool_init(&g_rx, 1u, 1000);
    pool_init(&g_tx, 2u, 1000);
    pthread_t t1, t2;
    pthread_create(&t1, NULL, rx_to_tx, NULL);
#ifndef CONCURRENT
    pthread_join(t1, NULL);         /* T1 이 끝난 뒤에 T2 를 만든다 → 실제 교착은 불가능 */
#endif
    pthread_create(&t2, NULL, tx_to_rx, NULL);
#ifdef CONCURRENT
    pthread_join(t1, NULL);
#endif
    pthread_join(t2, NULL);
    printf("rx=%ld tx=%ld sum=%ld\n", g_rx.balance, g_tx.balance, g_rx.balance + g_tx.balance);
    return 0;
}
