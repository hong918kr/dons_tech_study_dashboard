/* C-A02 demo_order.c — 배리어 유무에 따른 -O2 명령 순서 비교 (asm 관찰용, main 없음, libc 없음)
 *
 *   cc -std=c11 -O2 -S demo_order.c -o -                                                    # 호스트 arm64
 *   clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_order.c -o -  # Cortex-M4
 *   clang -std=c11 --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_order.c -o - # M0+ 비교
 *
 * 시나리오: TX 디스크립터를 "먼저 ctrl=0 으로 회수 → 버퍼 2바이트 → addr → len → ctrl=OWN|n" 순서로 채운다.
 * -std=c11 에는 asm 키워드가 없으므로 __asm__ 을 쓴다 (gnu11 이면 asm 도 된다).
 */
#include <stdint.h>
#include <stdatomic.h>

#define OWN 0x80000000u
typedef struct { uint32_t ctrl, len, addr, next; } desc_t;
extern desc_t txd;                 /* DMA 디스크립터 (일반 RAM) */
extern uint8_t txbuf[64];          /* DMA 버퍼                  */
#define VOL32(x) (*(volatile uint32_t *)&(x))

/* ① 배리어도 volatile 도 없음 */
void p0_plain(uint32_t n) {
    txd.ctrl = 0;
    txbuf[0] = 0xAA; txbuf[1] = (uint8_t)n;
    txd.addr = (uint32_t)(uintptr_t)txbuf;
    txd.len  = n;
    txd.ctrl = OWN | n;
}
/* ② ctrl 접근만 volatile */
void p1_volatile(uint32_t n) {
    VOL32(txd.ctrl) = 0;
    txbuf[0] = 0xAA; txbuf[1] = (uint8_t)n;
    txd.addr = (uint32_t)(uintptr_t)txbuf;
    txd.len  = n;
    VOL32(txd.ctrl) = OWN | n;
}
/* ③ + 컴파일러 배리어 (GNU inline asm, "memory" 클로버) */
void p2_asm_barrier(uint32_t n) {
    VOL32(txd.ctrl) = 0;
    txbuf[0] = 0xAA; txbuf[1] = (uint8_t)n;
    txd.addr = (uint32_t)(uintptr_t)txbuf;
    txd.len  = n;
    __asm__ __volatile__("" ::: "memory");
    VOL32(txd.ctrl) = OWN | n;
}
/* ④ + atomic_signal_fence (C11 의 컴파일러 배리어) */
void p3_signal_fence(uint32_t n) {
    VOL32(txd.ctrl) = 0;
    txbuf[0] = 0xAA; txbuf[1] = (uint8_t)n;
    txd.addr = (uint32_t)(uintptr_t)txbuf;
    txd.len  = n;
    atomic_signal_fence(memory_order_release);
    VOL32(txd.ctrl) = OWN | n;
}
/* ⑤ + atomic_thread_fence (CPU 배리어 명령까지) */
void p4_thread_fence(uint32_t n) {
    VOL32(txd.ctrl) = 0;
    txbuf[0] = 0xAA; txbuf[1] = (uint8_t)n;
    txd.addr = (uint32_t)(uintptr_t)txbuf;
    txd.len  = n;
    atomic_thread_fence(memory_order_release);
    VOL32(txd.ctrl) = OWN | n;
}

/* 회수 쪽: OWN 이 내려갔으면 DMA 가 써 둔 len 을 읽는다 */
int32_t r0_plain(void)  { if (txd.ctrl & OWN) return -1; return (int32_t)txd.len; }
int32_t r1_volatile(void) { if (VOL32(txd.ctrl) & OWN) return -1; return (int32_t)txd.len; }
int32_t r2_asm_barrier(void) {
    if (VOL32(txd.ctrl) & OWN) return -1;
    __asm__ __volatile__("" ::: "memory");
    return (int32_t)txd.len;
}
int32_t r3_thread_fence(void) {
    if (VOL32(txd.ctrl) & OWN) return -1;
    atomic_thread_fence(memory_order_acquire);
    return (int32_t)txd.len;
}

/* C11 원자 타입으로 쓴 정석 버전 */
typedef struct { _Atomic uint32_t ctrl; uint32_t len, addr, next; } adesc_t;
extern adesc_t ad;
void c_release(uint32_t n) {
    ad.addr = 0x1000u; ad.len = n;
    atomic_store_explicit(&ad.ctrl, OWN | n, memory_order_release);
}
int32_t c_acquire(void) {
    if (atomic_load_explicit(&ad.ctrl, memory_order_acquire) & OWN) return -1;
    return (int32_t)ad.len;
}
void c_seq_cst_fence(void) { atomic_thread_fence(memory_order_seq_cst); }
void c_seq_cst_store(uint32_t n) { atomic_store(&ad.ctrl, OWN | n); }
uint32_t c_seq_cst_load(void) { return atomic_load(&ad.ctrl); }
