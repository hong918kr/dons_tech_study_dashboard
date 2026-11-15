/* ALGO-M15 demo_target — 컴파일러가 상수 나눗셈을 어떻게 바꾸는지 asm 으로 본다.
 * <stdint.h> 만 써서 libc 헤더 없이 Cortex-M 타깃으로도 컴파일된다.
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -   # UDIV/UMULL 있음
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -   # 나눗셈·64비트 곱 없음
 *   cc -O2 -S demo_target.c -o -                                                            # 이 맥 (arm64)
 */
#include <stdint.h>

uint32_t div10_c(uint32_t x) { return x / 10; }
uint32_t mod10_c(uint32_t x) { return x % 10; }
uint32_t div7_c(uint32_t x) { return x / 7; }
int32_t  sdiv10_c(int32_t x) { return x / 10; }
uint32_t div_var(uint32_t a, uint32_t b) { return a / b; }

/* 손으로 쓴 매직 곱셈: M0 에서는 32×32→64 곱이 없다 */
uint32_t div10_magic(uint32_t x) { return (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35); }

/* 곱셈 없는 ÷10 (연습 3) */
uint32_t divu10(uint32_t n) {
    uint32_t q = (n >> 1) + (n >> 2);
    q += q >> 4; q += q >> 8; q += q >> 16; q >>= 3;
    uint32_t r = n - (((q << 2) + q) << 1);
    return q + (r > 9);
}
