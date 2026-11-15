/* ALGO-M10 demo_target.c — Cortex-M 에서 cooldown_from_busy 의 ÷/% 가 어떻게 되는지 asm 으로 본다 (실행 안 함).
 * <stdint.h> 만 쓴다 → 맥의 clang 으로 Arm 타깃 asm 생성 가능.
 * build: clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -
 *        clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 */
#include <stdint.h>

uint32_t cooldown_from_busy(uint32_t busy_us, uint32_t slot_us) {
    if (slot_us == 0) return UINT32_MAX;
    uint32_t q = busy_us / slot_us + (busy_us % slot_us != 0);
    return q ? q - 1 : 0;
}

/* 슬롯 길이가 컴파일 시간 상수(1000 µs)일 때 */
uint32_t cooldown_1ms(uint32_t busy_us) {
    uint32_t q = busy_us / 1000u + (busy_us % 1000u != 0);
    return q ? q - 1 : 0;
}
