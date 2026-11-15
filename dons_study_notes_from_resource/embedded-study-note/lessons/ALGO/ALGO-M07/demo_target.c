/* ALGO-M07 demo_target: 공간 최적화 계단 DP 를 Cortex-M 으로 컴파일해 본다 (<stdint.h> 만 사용).
 *   roll_mask : 링 4칸, 인덱스 i & 3        (2의 거듭제곱 링)
 *   roll_mod  : 링 3칸, 인덱스 i % 3        (딱 맞는 크기 링 — 나눗셈이 생긴다)
 *   sat_add   : uint64_t 포화 덧셈 (32비트 코어에서 64비트 덧셈은 adds/adcs 두 명령)
 * asm: clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_target.c -o -
 *      clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 */
#include <stdint.h>

uint64_t sat_add(uint64_t a, uint64_t b) { return (a > UINT64_MAX - b) ? UINT64_MAX : a + b; }

/* steps {1,2,3}, 막힘 없음: ways(n) = tribonacci */
uint64_t roll_mask(uint32_t n) {
    uint64_t r[4] = { 1, 0, 0, 0 };
    for (uint32_t i = 1; i <= n; i++) {
        uint64_t w = r[(i - 1) & 3];
        if (i >= 2) w = sat_add(w, r[(i - 2) & 3]);
        if (i >= 3) w = sat_add(w, r[(i - 3) & 3]);
        r[i & 3] = w;
    }
    return r[n & 3];
}

uint64_t roll_mod(uint32_t n) {
    uint64_t r[3] = { 1, 0, 0 };
    for (uint32_t i = 1; i <= n; i++) {
        uint64_t w = r[(i - 1) % 3];
        if (i >= 2) w = sat_add(w, r[(i - 2) % 3]);
        if (i >= 3) w = sat_add(w, r[(i - 3) % 3]);
        r[i % 3] = w;
    }
    return r[n % 3];
}
