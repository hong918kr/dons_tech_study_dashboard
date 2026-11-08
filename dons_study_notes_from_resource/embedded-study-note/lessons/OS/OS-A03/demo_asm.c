/* OS-A03 demo_asm.c — 읽기 경로가 실제로 어떤 명령이 되나 (freestanding: Cortex-M 타깃도 컴파일된다)
 *
 *   cc -std=c11 -O2 -S demo_asm.c -o -                                                          # arm64 (Apple clang)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -  # Cortex-M4
 */
#include <stdatomic.h>
#include <stdint.h>

typedef struct { uint64_t version; int32_t gain[32]; } tbl_t;
extern _Atomic(tbl_t *) g_cur;
extern _Atomic(uintptr_t) g_epoch;      /* Cortex-M 에서도 lock-free 가 되도록 포인터 폭 */
extern _Atomic(uintptr_t) g_slot;       /* 리더 한 명의 슬롯 */
extern _Atomic(uintptr_t) g_gp, g_seen;

/* EBR 읽기 구간 진입 + 포인터 읽기 (solution.c: ebr_enter + cfg_read_begin) */
int32_t ebr_read(int i) {
    uintptr_t e = atomic_load_explicit(&g_epoch, memory_order_relaxed);
    atomic_store_explicit(&g_slot, (e << 1) | 1u, memory_order_seq_cst);
    tbl_t *t = atomic_load_explicit(&g_cur, memory_order_seq_cst);
    int32_t v = t->gain[i];
    atomic_store_explicit(&g_slot, 0, memory_order_release);        /* ebr_exit */
    return v;
}
/* 틀린 버전: release store + acquire load (SB 를 못 막음 — demo_sb.c RV=1) */
int32_t ebr_read_ra(int i) {
    uintptr_t e = atomic_load_explicit(&g_epoch, memory_order_relaxed);
    atomic_store_explicit(&g_slot, (e << 1) | 1u, memory_order_release);
    tbl_t *t = atomic_load_explicit(&g_cur, memory_order_acquire);
    int32_t v = t->gain[i];
    atomic_store_explicit(&g_slot, 0, memory_order_release);
    return v;
}
/* QSBR 제어 루프 한 바퀴: 루프 맨 위 보고 + 포인터 읽기 (solution.c: qsbr_quiescent + qsbr_get) */
int32_t qsbr_loop_once(int i) {
    uintptr_t g = atomic_load_explicit(&g_gp, memory_order_acquire);
    atomic_store_explicit(&g_seen, g, memory_order_release);
    tbl_t *t = atomic_load_explicit(&g_cur, memory_order_acquire);
    return t->gain[i];
}
/* writer: 포인터 교체 (seq_cst) */
tbl_t *swap_cur(tbl_t *n) { return atomic_exchange_explicit(&g_cur, n, memory_order_seq_cst); }
