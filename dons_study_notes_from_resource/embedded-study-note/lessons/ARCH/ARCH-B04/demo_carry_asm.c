/* ARCH-B04 demo — "캐리 사슬"이 실제 ISA 에서는 어떻게 보이나
 *   Cortex-M4 (32비트 코어) 에서 64비트 덧셈/뺄셈 = ADDS + ADC / SUBS + SBC  (C 플래그가 슬라이스 사이 carry)
 *   arm64 에서 128비트 덧셈 = adds + adc
 * freestanding 헤더만 쓴다 (thumbv7em 타깃엔 libc 헤더가 없다).
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_carry_asm.c -o -
 *   cc -O2 -S demo_carry_asm.c -o -
 */
#include <stdint.h>

uint64_t add64(uint64_t a, uint64_t b) { return a + b; }
uint64_t sub64(uint64_t a, uint64_t b) { return a - b; }

/* 바이트 배열 다중 정밀도 덧셈을 C 로 쓰면 carry 를 비교로 다시 계산해야 한다 (C 에는 carry 플래그가 없다) */
void add_u32x4(uint32_t r[4], const uint32_t a[4], const uint32_t b[4]) {
    uint32_t c = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t s = a[i] + c;
        uint32_t c1 = s < c;
        r[i] = s + b[i];
        c = c1 | (r[i] < s);
    }
}

#if defined(__SIZEOF_INT128__)
unsigned __int128 add128(unsigned __int128 a, unsigned __int128 b) { return a + b; }
#endif
