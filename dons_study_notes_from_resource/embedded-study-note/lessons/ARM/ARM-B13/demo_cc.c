/* ENV: qemu */
/* ARM-B13 demo — 같은 함수를 C 로 쓰면 arm-none-eabi-gcc -O2 (cortex-m3) 가 무엇을 내는지 비교용.
 *   run_qemu.sh cc  →  arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -c demo_cc.c + objdump -d
 *   c_add_if_gt 는 -march=armv8-a (-mrestrict-it 기본) 와 비교: 32비트 명령을 IT 에 넣는가? */
#include <stdint.h>
int32_t c_max_s32(int32_t a, int32_t b) { return a > b ? a : b; }
int32_t c_abs_s32(int32_t x) { return x < 0 ? (int32_t)(0u - (uint32_t)x) : x; }
int32_t c_clamp_s32(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }
uint32_t c_max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
int32_t c_sum_array_s32(const int32_t *a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (uint32_t)a[i];
    return (int32_t)s;
}
int32_t c_add_if_gt(int32_t x, int32_t y) { return x > y ? x + 300 : y; }   /* IT 안에 32비트 addgt.w ? */
