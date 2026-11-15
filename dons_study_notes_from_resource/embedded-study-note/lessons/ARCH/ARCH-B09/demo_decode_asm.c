/* ARCH-B09 demo — 디코더의 필드 추출은 하드웨어에선 "전선", 소프트웨어에선 UBFX/SBFX 한 개
 * freestanding (stdint.h 만) → Cortex-M4 와 arm64 asm 을 비교한다.
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_decode_asm.c -o -
 *   cc -O2 -S -o - demo_decode_asm.c
 */
#include <stdint.h>

unsigned f_op(uint16_t w)  { return w >> 12; }            /* 15..12 */
unsigned f_rs(uint16_t w)  { return (w >> 6) & 7u; }      /* 8..6   */
int      f_imm6(uint16_t w) { return (int)((w & 0x3Fu) ^ 0x20u) - 0x20; }    /* sext(5..0) */
int      f_imm9(uint16_t w) { return (int)((w & 0x1FFu) ^ 0x100u) - 0x100; } /* sext(8..0) */
/* 시프트 관용구: 왼쪽으로 밀어 부호 비트를 맨 위에 두고 산술 오른쪽 시프트.
 * (uint32 → int32 변환과 음수 >> 는 C11 에서 구현 정의 — clang/gcc 는 2의 보수 + 산술 시프트) */
int      f_imm6_shift(uint16_t w) { return (int32_t)((uint32_t)w << 26) >> 26; }

/* 실행 단계의 다음 PC 계산 (BEQ): PC + 1 + sext(imm6) 또는 PC + 1 */
uint16_t next_pc_beq(uint16_t pc, uint16_t w, uint16_t a, uint16_t b) {
    return (uint16_t)(pc + 1u + (a == b ? (unsigned)f_imm6(w) : 0u));
}
