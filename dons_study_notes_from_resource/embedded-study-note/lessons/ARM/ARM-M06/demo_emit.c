/* ARM-M06 demo_emit: 벡터화를 막지 않은 평범한 C 루프를 clang 이 어떻게 내리나 (자동 벡터화는 ARM-A11 이 자세히)
 *   cc -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_emit.c -o -      (asm 보기)
 *   cc -std=c11 -O2 -DRUN demo_emit.c -o de && ./de                          (같은 함수, n 에 따라 결과 비트가 다름)
 * demo_bench.c 도 이 파일을 #include 해서 "auto" 열로 쓴다.
 */
#include <stddef.h>
#include <stdint.h>

void vadd_plain(float *dst, const float *a, const float *b, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = a[i] + b[i];
}
float dot_f32_plain(const float *a, const float *b, size_t n) {      /* 재결합 금지 → 벡터화 불가? */
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
int32_t dot_s8_plain(const int8_t *a, const int8_t *b, size_t n) {   /* 정수는 재결합 자유 */
    int32_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

#ifdef RUN
#include <stdio.h>
int main(void) {   /* ARM-B11 의 특별 입력: x*x 를 따로 반올림하면 정확히 tie → fused 와 결과가 갈린다 */
    float x = 1.0f + 0x1p-12f, a[16] = { 1, x }, b[16] = { -(1.0f + 0x1p-11f), x };   /* 나머지는 0 */
    float (*volatile f)(const float *, const float *, size_t) = dot_f32_plain;      /* 인라인·상수 접기 막기 */
    printf("dot_f32_plain n=2  (스칼라 루프: fmadd)      = %a\n", f(a, b, 2));
    printf("dot_f32_plain n=16 (벡터 본문: fmul + fadd) = %a\n", f(a, b, 16));
    return 0;
}
#endif
