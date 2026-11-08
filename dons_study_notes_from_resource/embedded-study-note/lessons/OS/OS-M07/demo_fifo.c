/* OS-M07 demo_fifo.c — 같은 강제 스케줄에서 TAS / TTAS / ticket 의 입장 순서.
 *
 * main 이 락을 잡은 채로 대기자 6 명을 1, 2, ... 6 순서로 출발시킨다 (각 출발 뒤 3 ms 쉼 → 모두 스핀 중).
 * 그다음 해제 → 입장 순서를 기록. 이것을 20 회 반복해 "도착 순서 = 입장 순서" 인 회수를 센다.
 * ticket 은 설계상 항상 FIFO (solution.c 가 CHECK 로 검증). TAS/TTAS 는 보장이 없다 — 여기서는 관찰만.
 *
 *   cc -std=c11 -O2 -pthread demo_fifo.c -o fifo && ./fifo
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

typedef struct { _Alignas(128) atomic_uint v; } spin_t;
typedef struct { _Alignas(128) atomic_uint next; atomic_uint serving; } ticket_t;
static void tas_lock(spin_t *l) { while (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) { } }
static void ttas_lock(spin_t *l) {
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed)) __asm__ volatile("yield" ::: "memory");
        if (!atomic_exchange_explicit(&l->v, 1u, memory_order_acquire)) return;
    }
}
static void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }
static void ticket_lock(ticket_t *t) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) __asm__ volatile("yield" ::: "memory");
}
static void ticket_unlock(ticket_t *t) {
    atomic_store_explicit(&t->serving, atomic_load_explicit(&t->serving, memory_order_relaxed) + 1u,
                          memory_order_release);
}

#define W 6
static int g_kind;              /* 0 TAS, 1 TTAS, 2 ticket */
static spin_t g_s;
static ticket_t g_t;
static int g_order[W];
static atomic_int g_pos;
static void lk(void) { if (g_kind == 0) tas_lock(&g_s); else if (g_kind == 1) ttas_lock(&g_s); else ticket_lock(&g_t); }
static void ul(void) { if (g_kind == 2) ticket_unlock(&g_t); else spin_unlock(&g_s); }
static void *waiter(void *arg) {
    int id = (int)(long)arg;
    lk();
    g_order[atomic_fetch_add_explicit(&g_pos, 1, memory_order_relaxed)] = id;
    struct timespec ts = { 0, 200000 };      /* 임계 구역 0.2 ms: 해제 순간 나머지 모두가 기다리는 중 */
    nanosleep(&ts, NULL);
    ul();
    return NULL;
}

int main(void) {
    static const char *const nm[3] = { "TAS", "TTAS", "ticket" };
    for (g_kind = 0; g_kind < 3; g_kind++) {
        int fifo_runs = 0;
        char example[64] = "";
        for (int run = 0; run < 20; run++) {
            atomic_store(&g_pos, 0);
            lk();
            pthread_t th[W];
            for (int i = 0; i < W; i++) {
                pthread_create(&th[i], NULL, waiter, (void *)(long)(i + 1));
                struct timespec ts = { 0, 3000000 };
                nanosleep(&ts, NULL);
            }
            ul();
            for (int i = 0; i < W; i++) pthread_join(th[i], NULL);
            bool fifo = true;
            for (int i = 0; i < W; i++) fifo = fifo && g_order[i] == i + 1;
            fifo_runs += fifo;
            if (run == 0) {
                int o = 0;
                for (int i = 0; i < W; i++) o += snprintf(example + o, sizeof example - (size_t)o, "%d ", g_order[i]);
            }
        }
        printf("%-7s FIFO in %2d/20 runs   (run 0 order: %s)\n", nm[g_kind], fifo_runs, example);
    }
    return 0;
}
