/* C-M11 demo: 매크로 / static inline / _Generic 이 만드는 코드 비교 (-S 로 asm 만 본다, 링크 안 함)
 * 호스트:  cc -std=c11 -O0 -S demo_codegen.c -o -      (그리고 -O2)
 * Cortex-M4: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_codegen.c -o -
 * freestanding 헤더(<stdint.h>)만 쓴다 — thumbv7em 타깃에는 libc 헤더가 없다.
 */
#include <stdint.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline int32_t  max_i32(int32_t a, int32_t b)   { return a > b ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

#define max_of(a, b) _Generic((a) + (b), int: max_i32, unsigned int: max_u32)((a), (b))

extern volatile uint32_t UART_DR;      /* 가짜 레지스터: 읽으면 FIFO 에서 한 바이트가 빠진다 */

int32_t  f_macro(int32_t a, int32_t b)   { return MAX(a, b); }
int32_t  f_inline(int32_t a, int32_t b)  { return max_i32(a, b); }
int32_t  f_generic(int32_t a, int32_t b) { return max_of(a, b); }

uint32_t peak_macro(void)   { return MAX(UART_DR, 4u); }      /* DR 을 몇 번 읽나? */
uint32_t peak_inline(void)  { return max_u32(UART_DR, 4u); }
