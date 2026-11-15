/* ALGO-B12 asm 관찰용: <stdint.h> 만 써서 호스트(arm64)와 Cortex-M 타깃 둘 다 -S 로 컴파일된다.
 *
 *   clang -O2 -S asm_popcount.c -o - | less                                                   # arm64 (이 맥)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S asm_popcount.c -o - | less      # Cortex-M4
 *   clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -O2 -S asm_popcount.c -o - | less   # Cortex-M0+
 *   (-O0 / -O1 / -Os 로 바꿔 가며 Kernighan·SWAR 이 언제 popcount 명령으로 바뀌는지 볼 것)
 * 호스트에서 실행하면 자기 검사만 하고 exit 0.
 */
#include <stdint.h>

unsigned pc_builtin32(uint32_t x) { return (unsigned)__builtin_popcount(x); }
unsigned pc_builtin64(uint64_t x) { return (unsigned)__builtin_popcountll(x); }
unsigned pc_kernighan(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }
unsigned pc_swar32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
uint32_t lowbit(uint32_t x) { return x & (0u - x); }
unsigned ctz_builtin(uint32_t x) { return (unsigned)__builtin_ctz(x); }   /* x == 0 은 정의 안 됨 */

int main(void) {
    volatile uint32_t v = 0x12345678u;
    return (pc_builtin32(v) == 13 && pc_kernighan(v) == 13 && pc_swar32(v) == 13 &&
            pc_builtin64((uint64_t)v << 32 | v) == 26 && lowbit(v) == 8 && ctz_builtin(v) == 3) ? 0 : 1;
}
