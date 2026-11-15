/* ARCH-M10 demo — "하드웨어 나눗셈이 있나, 0 으로 나누면 누가 잡나" 를 컴파일러 출력으로 본다.
 * freestanding (stdint 만) → Cortex-M 타깃으로도 컴파일된다.
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_div_asm.c -o -   # UDIV 한 개
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_div_asm.c -o -   # __aeabi_uidiv 호출
 *   cc -O2 -S -o - demo_div_asm.c                                                          # arm64 UDIV
 */
#include <stdint.h>

uint32_t div_u32(uint32_t a, uint32_t b) { return a / b; }

/* 0 검사를 소프트웨어로: 하드웨어가 트랩하지 않는 코어에서 펌웨어가 흔히 하는 방어 */
uint32_t div_checked(uint32_t a, uint32_t b) { return b ? a / b : UINT32_MAX; }
