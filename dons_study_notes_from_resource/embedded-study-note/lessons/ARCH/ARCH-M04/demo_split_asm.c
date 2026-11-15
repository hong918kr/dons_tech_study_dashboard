/* ARCH-M04 demo — 고정 형상 (C = 4 KB, B = 32 B) 의 tag/index/offset 분해가 어떤 명령이 되나
 * 하드웨어에서는 그냥 "배선" (주소 비트를 나눠 SRAM 주소와 비교기에 연결). 소프트웨어 모델에서는 비트필드 추출 1개씩.
 * freestanding 헤더만 쓴다 → Cortex-M 타깃으로도 컴파일된다.
 *   arm64     : cc -O2 -S -o - demo_split_asm.c
 *   Cortex-M4 : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_split_asm.c -o -
 */
#include <stdint.h>

#define OFF_BITS 5u   /* log2 32   */
#define IDX_BITS 7u   /* log2 128  */

uint32_t dm_offset(uint32_t a) { return a & ((1u << OFF_BITS) - 1); }
uint32_t dm_index(uint32_t a)  { return (a >> OFF_BITS) & ((1u << IDX_BITS) - 1); }
uint32_t dm_tag(uint32_t a)    { return a >> (OFF_BITS + IDX_BITS); }
