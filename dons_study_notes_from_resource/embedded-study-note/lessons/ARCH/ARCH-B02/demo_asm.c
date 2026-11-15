/* ARCH-B02 demo_asm.c — C 비교식이 어떤 조건 코드(cond)로 컴파일되나 (freestanding: 표준 헤더만)
 *   arm64 (이 맥) : cc -O2 -S -o - demo_asm.c
 *   Cortex-M4     : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdbool.h>
#include <stdint.h>

bool lt_s32(int32_t a, int32_t b)    { return a < b; }   /* signed   → LT */
bool lt_u32(uint32_t a, uint32_t b)  { return a < b; }   /* unsigned → LO (CC) */
bool gt_u32(uint32_t a, uint32_t b)  { return a > b; }   /* unsigned → HI */
bool tick_after(uint32_t a, uint32_t b) { return (int32_t)(b - a) < 0; } /* wrap-safe: 차이의 부호 */
uint64_t add64(uint64_t a, uint64_t b) { return a + b; }  /* 32비트 코어: ADDS + ADC */
bool lt_s64(int64_t a, int64_t b)    { return a < b; }   /* 32비트 코어: CMP lo + SBCS hi → LT */
bool eq_u64(uint64_t a, uint64_t b)  { return a == b; }  /* Z 는 체인으로 못 구한다 → 다른 방법 */
