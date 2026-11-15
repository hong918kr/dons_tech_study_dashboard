/* COMM-B09 demo: RBIT / REV 가 Cortex-M 에서 몇 명령이 되나
 *
 * 구형 STM32 CRC 유닛으로 zlib CRC-32 를 내려면 워드마다 비트 반사(RBIT)가, MPEG-2 를 내려면 바이트 스왑(REV)이
 * 필요하다. 그 비용을 실제 컴파일러 출력으로 본다 (목표 칩 asm 만 보면 되므로 main 은 맥에서의 동작 확인용).
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S -o - demo_rbit.c   # RBIT 1개, LDR+REV
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -O2 -S -o - demo_rbit.c   # RBIT 없음, LDRB 4개
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_rbit.c -o demo_rbit && ./demo_rbit
 */
#include <stdint.h>
#if !defined(__arm__)          /* 32비트 Arm 대상(-S 로 asm 만 볼 때)은 stdio 없이 */
#include <stdio.h>
#endif

uint32_t reflect32_loop(uint32_t x);
uint32_t reflect32_builtin(uint32_t x);
uint32_t load_be32(const uint8_t *p);

uint32_t reflect32_loop(uint32_t x) {                  /* solution.c 의 reflect(x, 32) 와 같은 루프 */
    uint32_t r = 0;
    for (unsigned i = 0; i < 32; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}
uint32_t reflect32_builtin(uint32_t x) { return __builtin_bitreverse32(x); }   /* clang/gcc 내장 (CMSIS: __RBIT) */
uint32_t load_be32(const uint8_t *p) {                 /* M3/M4: LDR + REV, M0: 비정렬 LDR 불가 → LDRB 4개 */
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

#if !defined(__arm__)
int main(void) {
    uint32_t s = 0x12345678u, bad = 0;
    for (int i = 0; i < 100000; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        if (reflect32_loop(s) != reflect32_builtin(s)) bad++;
    }
    const uint8_t b[4] = { 0x31, 0x32, 0x33, 0x34 };
    printf("loop vs builtin: 100000 values, mismatches=%u; load_be32(\"1234\")=0x%08X; rbit(0x34333231)=0x%08X\n",
           (unsigned)bad, (unsigned)load_be32(b), (unsigned)reflect32_builtin(0x34333231u));
    return bad != 0;
}
#endif
