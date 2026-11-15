/* ALGO-M13 Cortex-M4 에서 누적합 루프가 어떻게 컴파일되나 (asm 만 본다, 실행하지 않음)
 * uint32 누적 vs int64 누적(레지스터 2개 + 자리올림), 그리고 rect_sum 의 4번 읽기.
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 */
#include <stddef.h>
#include <stdint.h>

void prefix_u32(const uint16_t *a, size_t n, uint32_t *P) {
    uint32_t s = 0;
    P[0] = 0;
    for (size_t i = 0; i < n; i++) { s += a[i]; P[i + 1] = s; }
}

void prefix_i64(const int32_t *a, size_t n, int64_t *P) {
    int64_t s = 0;
    P[0] = 0;
    for (size_t i = 0; i < n; i++) { s += a[i]; P[i + 1] = s; }
}

uint32_t rect_sum(const uint32_t *I, size_t w, size_t x0, size_t y0, size_t x1, size_t y1) {
    size_t W = w + 1;
    return I[y1 * W + x1] - I[y0 * W + x1] - I[y1 * W + x0] + I[y0 * W + x0];
}
