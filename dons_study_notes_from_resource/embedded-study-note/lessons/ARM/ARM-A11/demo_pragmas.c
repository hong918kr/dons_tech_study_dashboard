/* ARM-A11 demo_pragmas: pragma 가 "실패"하거나 "이상하게 성공"하는 예 — 테스트 아님, 컴파일해서 리마크·asm 만 본다
 *   bash demo_remarks.sh   (이 파일을 -O2 / -Os 로 컴파일한 리마크·경고가 remarks_mac.txt 에)
 *   bash demo_asm.sh       (saxpy_os_r 의 -Os asm 이 asm_mac.txt 에)
 */
#include <stddef.h>
#include <stdint.h>

/* -Os: 겹침 검사(런타임 버전 분기)가 크기 때문에 금지 → pragma 를 줘도 실패 → -Wpass-failed 경고 */
void saxpy_os(float *y, const float *x, float a, size_t n) {
#pragma clang loop vectorize(enable)
    for (size_t i = 0; i < n; i++) y[i] = a * x[i] + y[i];
}

/* -Os + restrict + pragma: "vectorized (4, 1)" 리마크가 나오지만 꼬리를 마스크로 접어서(tail folding)
 * NEON 에는 마스크 load/store 가 없으니 lane 마다 tbz + 스칼라 ldr/str 로 풀린다 → asm 을 봐야 안다 */
void saxpy_os_r(float *restrict y, const float *restrict x, float a, size_t n) {
#pragma clang loop vectorize(enable)
    for (size_t i = 0; i < n; i++) y[i] = a * x[i] + y[i];
}

/* -O2: if 형태 float 조건부 합에 vectorize(enable) — 리덕션으로 인식이 안 돼서 여전히 실패 → 경고 */
float csum_if_hint(const float *a, size_t n, float t) {
    float s = 0.0f;
#pragma clang loop vectorize(enable)
    for (size_t i = 0; i < n; i++)
        if (a[i] > t) s += a[i];
    return s;
}

/* 언롤: 벡터화가 안 되는 루프도 펼칠 수는 있다 (-Rpass=loop-unroll) */
void hist_unroll4(uint32_t *h, const uint8_t *p, size_t n) {
#pragma clang loop unroll_count(4)
    for (size_t i = 0; i < n; i++) h[p[i]]++;
}

/* 펌웨어에서 흔한 "벡터화 차단기" 두 가지: volatile 버퍼, 루프 안의 (인라인 안 되는) 함수 호출 */
void gain_volatile(volatile float *buf, float g, float b, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = g * buf[i] + b;
}
float sum_callback(const float *a, size_t n, float (*f)(float)) {
    float s = 0.0f;
    for (size_t i = 0; i < n; i++) s += f(a[i]);
    return s;
}
