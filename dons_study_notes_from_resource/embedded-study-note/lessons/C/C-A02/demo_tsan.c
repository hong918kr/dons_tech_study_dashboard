/* C-A02 demo_tsan.c — 디스크립터 핑퐁을 네 가지 방식으로 동기화하고 ThreadSanitizer 로 본다
 *
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_tsan.c -o demo_tsan
 *   ./demo_tsan volatile   # ctrl 을 volatile uint32_t 로    → C11 data race (UB). TSan 이 잡는다
 *   ./demo_tsan relaxed    # ctrl 은 원자, 순서는 relaxed     → 필드(len/buf)에 data race. TSan 이 잡는다
 *   ./demo_tsan fence      # relaxed + atomic_thread_fence   → C11 상 올바름. 그런데 TSan 은 fence 를 모른다
 *   ./demo_tsan relacq     # release store / acquire load    → 올바름, TSan 조용
 * volatile 모드는 일부러 UB 를 만든다 (관찰용). starter/solution 에는 없는 코드다.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OWN 0x80000000u
#define N   20000u
static int mode;                                  /* 0 volatile, 1 relaxed, 2 fence, 3 relacq */
static volatile uint32_t v_ctrl;
static _Atomic uint32_t a_ctrl;
static uint32_t d_len, d_buf[4];                  /* 일반(non-atomic) 디스크립터 필드 */

static void own_put(uint32_t v) {
    switch (mode) {
    case 0: v_ctrl = v; break;
    case 1: atomic_store_explicit(&a_ctrl, v, memory_order_relaxed); break;
    case 2: atomic_thread_fence(memory_order_release);
            atomic_store_explicit(&a_ctrl, v, memory_order_relaxed); break;
    default: atomic_store_explicit(&a_ctrl, v, memory_order_release); break;
    }
}
static uint32_t own_get(void) {
    uint32_t v;
    switch (mode) {
    case 0: return v_ctrl;
    case 1: return atomic_load_explicit(&a_ctrl, memory_order_relaxed);
    case 2: v = atomic_load_explicit(&a_ctrl, memory_order_relaxed);
            atomic_thread_fence(memory_order_acquire); return v;
    default: return atomic_load_explicit(&a_ctrl, memory_order_acquire);
    }
}
static void *dma(void *arg) {                     /* DMA 엔진 역할 */
    unsigned bad = 0;
    (void)arg;
    for (uint32_t i = 1; i <= N; i++) {
        while (!(own_get() & OWN)) { }
        bad += d_len != i || d_buf[3] != i * 3u;
        own_put(0u);                              /* 소유권 반환 */
    }
    printf("dma: %u packets, bad=%u\n", N, bad);
    return NULL;
}
int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "relacq";
    mode = !strcmp(m, "volatile") ? 0 : !strcmp(m, "relaxed") ? 1 : !strcmp(m, "fence") ? 2 : 3;
    pthread_t th;
    pthread_create(&th, NULL, dma, NULL);
    for (uint32_t i = 1; i <= N; i++) {           /* CPU: 필드 채우기 → OWN */
        d_len = i;
        for (int k = 0; k < 4; k++) d_buf[k] = i * (uint32_t)k;
        own_put(OWN);
        while (own_get() & OWN) { }               /* DMA 가 돌려줄 때까지 */
    }
    pthread_join(th, NULL);
    printf("mode=%s done\n", m);
    return 0;
}
