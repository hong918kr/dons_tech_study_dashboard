/* ARCH-M03 demo_cm.c — 같은 임계값 필터가 Cortex-M 에서 어떻게 나오나 (freestanding: stdint/stddef 만)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_cm.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_cm.c -o -
 */
#include <stddef.h>
#include <stdint.h>

/* ADC 블록에서 임계값 이상 샘플의 합 — 평범하게 쓴 코드 */
int32_t adc_sum_ge(const int16_t *s, size_t n, int16_t t) {
    int32_t acc = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] >= t) acc += s[i];
    return acc;
}

/* 같은 것을 마스크로 */
int32_t adc_sum_ge_mask(const int16_t *s, size_t n, int16_t t) {
    int32_t acc = 0;
    for (size_t i = 0; i < n; i++)
        acc += -(int32_t)(s[i] >= t) & s[i];
    return acc;
}

/* 비교 연산자 없이 부호 비트로 마스크: int16 끼리의 차는 int32 에서 넘치지 않는다.
 * (음수 >> 는 구현 정의 — clang/gcc 는 산술 시프트. 이식성이 필요하면 -(uint32_t)(... >> 31 & 1) 형태로) */
int32_t adc_sum_ge_sign(const int16_t *s, size_t n, int16_t t) {
    int32_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t d = (int32_t)s[i] - (int32_t)t;   /* -65535..65535: 넘침 없음 */
        int32_t keep = ~(d >> 31);                /* d >= 0 → -1(전부 1), d < 0 → 0 */
        acc += keep & s[i];
    }
    return acc;
}

/* 컴파일러가 위 관용구를 다시 cmp+분기로 되돌리는 것을 막는 값 장벽 (opaque value barrier) */
int32_t adc_sum_ge_sign_opaque(const int16_t *s, size_t n, int16_t t) {
    int32_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t d = (int32_t)s[i] - (int32_t)t;
        __asm__("" : "+r"(d));                    /* d 가 "s - t" 인 것을 숨김 (안 숨기면 cmp s,t 로 되돌린다) */
        int32_t neg = d >> 31;                    /* d < 0 → -1, 아니면 0 */
        __asm__("" : "+r"(neg));                  /* 컴파일러는 neg 가 0/-1 인 것을 더 이상 모른다 */
        int32_t keep = ~neg;
        acc += keep & s[i];
    }
    return acc;
}
