/* DS-M13 demo — 0xFFFFFFFFu << (32 - len) 이 len == 0 에서 무슨 일을 하나
 * C11 6.5.7p3: 시프트 양이 왼쪽 피연산자 폭(32) 이상이면 정의되지 않은 동작(UB).
 * volatile 로 상수 접기를 막아 "하드웨어 시프트 명령"이 실제로 내는 값을 본다.
 *   cc -std=c11 -O2 demo_shift_ub.c -o demo_shift_ub && ./demo_shift_ub
 *   cc -std=c11 -O1 -fsanitize=undefined demo_shift_ub.c -o demo_shift_ub_san && ./demo_shift_ub_san
 */
#include <stdint.h>
#include <stdio.h>

static uint32_t mask_naive(uint32_t len) { return 0xFFFFFFFFu << (32u - len); }       /* len 0 → UB */
static uint32_t mask_safe(uint32_t len)  { return len == 0 ? 0u : 0xFFFFFFFFu << (32u - len); }
static uint32_t mask_64(uint32_t len)    { return (uint32_t)~(0xFFFFFFFFull >> len); }  /* 64비트에서 시프트 */

int main(void) {
    static const uint32_t lens[] = { 0, 1, 8, 24, 32 };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        volatile uint32_t len = lens[i];                     /* 컴파일 타임에 값을 모르게 */
        uint32_t naive = mask_naive(len), safe = mask_safe(len), m64 = mask_64(len);
        printf("len=%2u  naive=0x%08X  safe=0x%08X  mask64=0x%08X%s\n", (unsigned)len, naive, safe, m64,
               naive != safe ? "   <-- naive 가 틀림: /0 기본 경로가 0.0.0.0 하나만 매치" : "");
    }
    return 0;
}
