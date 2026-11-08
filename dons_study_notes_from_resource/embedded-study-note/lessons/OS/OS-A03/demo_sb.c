/* OS-A03 demo_sb.c — EBR 읽기 진입의 순서 = store buffering(SB) 리트머스
 *
 * 리더:  slot = ACTIVE ;  p = load(ptr)        (ebr_enter → cfg_read_begin)
 * writer: exchange(ptr, NEW) ;  s = load(slot)  (cfg_update → ebr_try_advance 의 슬롯 검사)
 * 금지돼야 하는 결과: p == OLD (리더가 옛 버전을 쥠) && s == 0 (writer 는 리더를 못 봄 → 옛 버전 free)
 *
 *   V=0  seq_cst store / seq_cst load, writer seq_cst exchange / seq_cst load   (solution.c 의 선택)
 *   V=1  release store / acquire load, writer release exchange / acquire load   ("acquire/release 면 되겠지")
 *   V=2  relaxed + atomic_thread_fence(seq_cst) 양쪽                             (liburcu 식 smp_mb)
 *   V=3  전부 relaxed
 * 라운드마다 새 변수 쌍을 쓰고 (리셋 경쟁 없음), 두 스레드는 라운드마다 스핀 장벽으로 같이 출발한다.
 *
 * build: cc -std=c11 -O2 -pthread -DV=1 demo_sb.c -o sb1 && ./sb1
 *        cc -std=c11 -O2 -pthread -DRV=1 -DWV=3 demo_sb.c -o sb13 && ./sb13   (리더만 release/acquire)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef V
#define V 0
#endif
#ifndef ROUNDS
#define ROUNDS 2000000u
#endif

/* V 는 리더 쪽과 writer 쪽 모두에 같은 순서를 준다. -DRV=.. -DWV=.. 로 한쪽씩 따로 바꿀 수도 있다. */
#ifndef RV
#define RV V
#endif
#ifndef WV
#define WV V
#endif
#define ORD_ST(x) ((x) == 0 ? memory_order_seq_cst : (x) == 1 ? memory_order_release : memory_order_relaxed)
#define ORD_LD(x) ((x) == 0 ? memory_order_seq_cst : (x) == 1 ? memory_order_acquire : memory_order_relaxed)
#define R_FENCE() do { if (RV == 2) atomic_thread_fence(memory_order_seq_cst); } while (0)
#define W_FENCE() do { if (WV == 2) atomic_thread_fence(memory_order_seq_cst); } while (0)

typedef struct { _Atomic uint32_t slot; _Atomic uint32_t ptr; } pair_t;   /* ptr: 1 = OLD, 2 = NEW */
static pair_t *g_v;
static uint8_t *g_rp, *g_ws;
static _Atomic uint32_t g_arrive;

static void barrier(uint32_t round) {          /* 2 스레드 스핀 장벽 */
    atomic_fetch_add_explicit(&g_arrive, 1, memory_order_acq_rel);
    while (atomic_load_explicit(&g_arrive, memory_order_acquire) < 2 * (round + 1)) { }
}
static void *reader(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ROUNDS; i++) {
        barrier(i);
        atomic_store_explicit(&g_v[i].slot, 1, ORD_ST(RV));
        R_FENCE();
        g_rp[i] = (uint8_t)atomic_load_explicit(&g_v[i].ptr, ORD_LD(RV));
    }
    return NULL;
}
static void *writer(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ROUNDS; i++) {
        barrier(i);
        (void)atomic_exchange_explicit(&g_v[i].ptr, 2, ORD_ST(WV));
        W_FENCE();
        g_ws[i] = (uint8_t)atomic_load_explicit(&g_v[i].slot, ORD_LD(WV));
    }
    return NULL;
}

int main(void) {
    g_v = calloc(ROUNDS, sizeof *g_v);
    g_rp = calloc(ROUNDS, 1);
    g_ws = calloc(ROUNDS, 1);
    if (!g_v || !g_rp || !g_ws) return 2;
    for (uint32_t i = 0; i < ROUNDS; i++) atomic_init(&g_v[i].ptr, 1);
    pthread_t a, b;
    pthread_create(&a, NULL, reader, NULL);
    pthread_create(&b, NULL, writer, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    unsigned long bad = 0, both_saw = 0, r_old = 0, w_idle = 0;
    for (uint32_t i = 0; i < ROUNDS; i++) {
        if (g_rp[i] == 1 && g_ws[i] == 0) bad++;            /* 금지: 둘 다 상대를 못 봄 */
        if (g_rp[i] == 2 && g_ws[i] == 1) both_saw++;
        if (g_rp[i] == 1) r_old++;
        if (g_ws[i] == 0) w_idle++;
    }
    printf("RV=%d WV=%d rounds=%u  reader got OLD & writer saw idle (UAF window) = %lu  | both saw = %lu, r_old = %lu, w_idle = %lu\n",
           RV, WV, ROUNDS, bad, both_saw, r_old, w_idle);
    free(g_v); free(g_rp); free(g_ws);
    return 0;
}
