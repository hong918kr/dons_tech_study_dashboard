/* SIV-B02 demo: 접근 계층의 비용 — Cortex-M4 에서 직접 volatile 접근 vs 함수 포인터 bus ops
 * clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * (freestanding: <stdint.h> 만 쓴다. main 없음) */
#include <stdint.h>

#define SPI0_BASE 0x40013000u

/* ① 직접: 컴파일 타임에 백엔드가 정해진 경우 (제품 펌웨어) */
static inline uint32_t mmio_rd(uint32_t off) { return *(volatile uint32_t *)(SPI0_BASE + off); }
static inline void mmio_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(SPI0_BASE + off) = v; }

uint32_t direct_w1c_clear(void) {           /* INT_STAT.OVR 지우고 다시 읽기 */
    mmio_wr(0x00C, 0x2u);
    return mmio_rd(0x00C);
}

/* ② 간접: 런타임에 백엔드를 고르는 경우 (검증 프레임워크) */
typedef struct {
    void *ctx;
    int (*read32)(void *ctx, uint32_t off, uint32_t *val);
    int (*write32)(void *ctx, uint32_t off, uint32_t val);
} bus_ops_t;
extern const bus_ops_t *g_bus;

uint32_t via_ops_w1c_clear(void) {
    uint32_t v;
    g_bus->write32(g_bus->ctx, 0x00C, 0x2u);
    g_bus->read32(g_bus->ctx, 0x00C, &v);
    return v;
}
