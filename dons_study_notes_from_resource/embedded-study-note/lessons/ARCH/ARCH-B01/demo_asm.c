/* ARCH-B01 demo: 이식 가능한 부호 확장 C 코드가 실제로 몇 개의 명령이 되나
 *
 *   sext12(v)        상수 폭 12비트: (x ^ m) - m        → 기대: sbfx 하나
 *   sext8(v)         상수 폭 8비트                       → 기대: sxtb 하나
 *   sext_var(v, b)   가변 폭                             → 시프트/빼기 몇 개
 *   as_i32(u)        이식 가능한 uint32 → int32 변환      → 기대: 명령 0개 (mov 도 없음)
 *
 * freestanding (stdint.h 만) → Cortex-M4 타깃으로도 컴파일된다.
 * arm64 : cc -O2 -S -o - demo_asm.c
 * M4    : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
 */
#include <stdint.h>

int32_t sext12(uint32_t v) {
    uint32_t x = v & 0xFFFu, m = 0x800u;
    uint32_t u = (x ^ m) - m;
    return u <= 0x7FFFFFFFu ? (int32_t)u : -(int32_t)(~u) - 1;
}

int32_t sext8(uint32_t v) {
    uint32_t x = v & 0xFFu, m = 0x80u;
    uint32_t u = (x ^ m) - m;
    return u <= 0x7FFFFFFFu ? (int32_t)u : -(int32_t)(~u) - 1;
}

int32_t sext_var(uint32_t v, unsigned bits) {          /* bits = 1..32 */
    uint32_t mask = 0xFFFFFFFFu >> (32 - bits);
    uint32_t m = 1u << (bits - 1);
    uint32_t u = ((v & mask) ^ m) - m;
    return u <= 0x7FFFFFFFu ? (int32_t)u : -(int32_t)(~u) - 1;
}

int32_t as_i32(uint32_t u) {
    return u <= 0x7FFFFFFFu ? (int32_t)u : -(int32_t)(~u) - 1;
}
