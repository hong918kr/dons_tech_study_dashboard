/* ENV: thumbv7em (asm only) — ARCH-M12 demo: Cortex-M7 clean-by-address 루프가 실제로 어떤 명령이 되나
 *
 * 링크·실행하지 않는다. -S 로 어셈블리만 본다 (freestanding: <stdint.h> 만).
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_m7_asm.c -o -
 *
 * 레지스터 주소는 CMSIS core_cm7.h V5.2.0 에서 확인:
 *   SCS_BASE 0xE000E000, SCB_BASE = SCS_BASE + 0x0D00 = 0xE000ED00
 *   DCIMVAC  offset 0x25C → 0xE000EF5C (invalidate by MVA to PoC)
 *   DCCMVAC  offset 0x268 → 0xE000EF68 (clean by MVA to PoC)
 *   DCCIMVAC offset 0x270 → 0xE000EF70 (clean+invalidate by MVA to PoC)
 */
#include <stdint.h>

#define DCIMVAC  (*(volatile uint32_t *)0xE000EF5Cu)
#define DCCMVAC  (*(volatile uint32_t *)0xE000EF68u)
#define DCCIMVAC (*(volatile uint32_t *)0xE000EF70u)
#define LINE 32u

static inline void dsb(void) { __asm volatile("dsb 0xF" ::: "memory"); }   /* CMSIS __DSB() 와 같은 모양 */
static inline void isb(void) { __asm volatile("isb 0xF" ::: "memory"); }

/* CMSIS cachel1_armv7.h V1.0.3 SCB_CleanDCache_by_Addr 와 같은 루프 (시작 오프셋을 크기에 더함) */
void clean_dcache_by_addr(volatile void *addr, int32_t dsize) {
    if (dsize > 0) {
        int32_t op_size = dsize + (int32_t)((uint32_t)addr & (LINE - 1u));
        uint32_t op_addr = (uint32_t)addr;
        dsb();
        do {
            DCCMVAC = op_addr;                      /* 하위 5 비트는 무시된다 */
            op_addr += LINE;
            op_size -= (int32_t)LINE;
        } while (op_size > 0);
        dsb();
        isb();
    }
}

/* 가상의 DMA 컨트롤러 레지스터 (주소는 예시) */
typedef struct { volatile uint32_t SRC, LEN, CTRL; } dma_regs_t;
#define DMA1 ((dma_regs_t *)0x40020000u)

/* TX 시작: clean (내부 DSB) → 레지스터 설정 → 시작 비트. 시작 비트 쓰기 전에 clean 이 끝나 있어야 한다. */
void uart_dma_tx_start(const uint8_t *buf, uint32_t len) {
    clean_dcache_by_addr((volatile void *)buf, (int32_t)len);
    DMA1->SRC = (uint32_t)buf;
    DMA1->LEN = len;
    DMA1->CTRL = 1u;
}

/* RX 완료 후: invalidate 한 줄 (버퍼가 라인 정렬·라인 배수라고 가정한 짧은 판) */
void rx_complete_invalidate(uint8_t *buf, uint32_t len) {
    dsb();
    for (uint32_t a = (uint32_t)buf; a < (uint32_t)buf + len; a += LINE) DCIMVAC = a;
    dsb();
    isb();
}
