/* C-A01 demo_target.c — 같은 드라이버 소스, 빌드 옵션 두 개
 *
 *   타깃(Cortex-M4):  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -DREG_TARGET -S demo_target.c -o -
 *   호스트(mock):     cc -std=c11 -O2 -S demo_target.c -o -
 *
 * freestanding: <stdint.h>/<stdbool.h> 만 쓴다 (thumbv7em-none-eabi 에는 libc 헤더가 없다).
 * 호스트 쪽 mock 함수는 선언만 있다 — -S 로 asm 만 보므로 링크하지 않는다.
 */
#include <stdbool.h>
#include <stdint.h>

#define RCC_APB2RSTR  0x40023824u
#define RCC_APB2ENR   0x40023844u
#define RCC_APB2_SPI1 (1u << 12)
#define SPI1_CR1      0x40013000u
#define SPI1_CR2      0x40013004u

#if defined(REG_TARGET)
static inline uint32_t reg_read32(uintptr_t a, int line) { (void)line; return *(const volatile uint32_t *)a; }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { (void)line; *(volatile uint32_t *)a = v; }
#else
uint32_t mock_reg_read(uintptr_t a, int line);
void     mock_reg_write(uintptr_t a, uint32_t v, int line);
static inline uint32_t reg_read32(uintptr_t a, int line) { return mock_reg_read(a, line); }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { mock_reg_write(a, v, line); }
#endif
#define REG_READ(a)      reg_read32((uintptr_t)(a), __LINE__)
#define REG_WRITE(a, v)  reg_write32((uintptr_t)(a), (uint32_t)(v), __LINE__)
static inline void reg_modify32(uintptr_t a, uint32_t clr, uint32_t set, int line) {
    uint32_t v = reg_read32(a, line);
    reg_write32(a, (v & ~clr) | set, line);
}
#define REG_MODIFY(a, clr, set) reg_modify32((uintptr_t)(a), (uint32_t)(clr), (uint32_t)(set), __LINE__)

/* 본문 spi_init 의 레지스터 부분만 (cr1/cr2 는 인자로) */
void spi_init_regs(uint32_t cr1, uint32_t cr2) {
    REG_MODIFY(RCC_APB2ENR, 0u, RCC_APB2_SPI1);
    (void)REG_READ(RCC_APB2ENR);
    REG_MODIFY(RCC_APB2RSTR, 0u, RCC_APB2_SPI1);
    REG_MODIFY(RCC_APB2RSTR, RCC_APB2_SPI1, 0u);
    REG_WRITE(SPI1_CR1, cr1);
    REG_WRITE(SPI1_CR2, cr2);
    REG_WRITE(SPI1_CR1, cr1 | (1u << 6));
}
