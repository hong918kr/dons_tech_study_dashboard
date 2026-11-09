/* LLAT-M03 demo_asm.c — hazard pointer 게시-재확인의 기계어 (freestanding: <stdatomic.h> 만).
 *
 *   hp_protect_sc   : seq_cst store(게시) + seq_cst load(재확인)   ← 정답
 *   hp_protect_ra   : release store + acquire load                  ← 틀림 (SB 를 못 막음)
 *   hp_protect_fence: relaxed store + seq_cst fence + relaxed load  ← Michael 2004 논문의 "membar" 형태
 *   scan_load       : scan 쪽 슬롯 읽기 (seq_cst load)
 *
 * cc -std=c11 -O2 -S demo_asm.c -o -                                                     (arm64 macOS)
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -   (Cortex-M4)
 */
#include <stdatomic.h>

typedef struct node node_t;
extern _Atomic(node_t *) g_slot;   /* 내 HP 칸 */
extern _Atomic(node_t *) g_src;    /* 큐 head */

node_t *hp_protect_sc(void) {
    node_t *p = atomic_load_explicit(&g_src, memory_order_relaxed);
    for (;;) {
        atomic_store_explicit(&g_slot, p, memory_order_seq_cst);
        node_t *q = atomic_load_explicit(&g_src, memory_order_seq_cst);
        if (q == p) return p;
        p = q;
    }
}
node_t *hp_protect_ra(void) {
    node_t *p = atomic_load_explicit(&g_src, memory_order_relaxed);
    for (;;) {
        atomic_store_explicit(&g_slot, p, memory_order_release);
        node_t *q = atomic_load_explicit(&g_src, memory_order_acquire);
        if (q == p) return p;
        p = q;
    }
}
node_t *hp_protect_fence(void) {
    node_t *p = atomic_load_explicit(&g_src, memory_order_relaxed);
    for (;;) {
        atomic_store_explicit(&g_slot, p, memory_order_relaxed);
        atomic_thread_fence(memory_order_seq_cst);
        node_t *q = atomic_load_explicit(&g_src, memory_order_acquire);
        if (q == p) return p;
        p = q;
    }
}
node_t *scan_load(void) { return atomic_load_explicit(&g_slot, memory_order_seq_cst); }
