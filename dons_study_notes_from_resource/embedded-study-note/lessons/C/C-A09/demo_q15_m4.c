/* C-A09 demo: Q15 연산이 Cortex-M4 / M0+ / arm64 에서 어떤 명령이 되나 (freestanding — libc 없음)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_q15_m4.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -mthumb -O2 -S demo_q15_m4.c -o -
 *   cc -O2 -S demo_q15_m4.c -o -        (호스트 arm64)
 *   clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -O2 -S demo_q15_m4.c -o -
 *                                        (M4F: ⑧ float 은 FPU, ⑨ double 은 소프트웨어 함수 호출)
 */
#include <stdint.h>

typedef int16_t q15_t;

/* floor(x / 2^15) — 구현 정의인 '음수 >>' 에 기대지 않는 두 가지 쓰기 */
static inline int32_t floor_shr15_tilde(int32_t x) { return x < 0 ? ~(~x >> 15) : x >> 15; }
static inline int32_t floor_shr15_div(int32_t x) {
    int32_t q = x / 32768;                 /* C11 6.5.5p6: 0 쪽으로 버림 */
    if (x % 32768 < 0) q--;                /* 음수이고 나머지가 있으면 한 칸 내림 → floor */
    return q;
}
static inline int64_t floor_shr15_div64(int64_t x) {
    int64_t q = x / 32768;
    if (x % 32768 < 0) q--;
    return q;
}
static inline int64_t floor_shr15_tilde64(int64_t x) { return x < 0 ? ~(~x >> 15) : x >> 15; }
static inline q15_t sat_q15(int64_t x) {
    return (q15_t)(x > INT16_MAX ? INT16_MAX : x < INT16_MIN ? INT16_MIN : x);
}

/* ① 반올림 곱 + 포화 (이 레슨의 q15_mul) */
q15_t q15_mul(q15_t a, q15_t b) {
    return sat_q15(floor_shr15_div((int32_t)a * b + 0x4000));
}
/* ② 같은 것을 구현 정의 >> 로 — ①과 같은 코드가 나오나? */
q15_t q15_mul_rawshift(q15_t a, q15_t b) {
    return sat_q15(((int32_t)a * b + 0x4000) >> 15);
}
/* ③ 포화 덧셈 */
q15_t q15_add_sat(q15_t a, q15_t b) {
    return sat_q15((int32_t)a + b);
}
/* ④ Part C mix: 곱 2개를 64비트로 누산, 반올림 1번 — floor 를 / % 로 */
q15_t mix2_div(q15_t m, q15_t gm, q15_t t, q15_t gt) {
    return sat_q15(floor_shr15_div64((int64_t)m * gm + (int64_t)t * gt + 0x4000));
}
/* ⑤ 같은 것을 ~(~x >> k) 로 */
q15_t mix2_tilde(q15_t m, q15_t gm, q15_t t, q15_t gt) {
    return sat_q15(floor_shr15_tilde64((int64_t)m * gm + (int64_t)t * gt + 0x4000));
}
/* ⑥ 32비트 ~ 트릭 버전 곱 (C-B03 sar32 방식) */
q15_t q15_mul_tilde(q15_t a, q15_t b) {
    return sat_q15(floor_shr15_tilde((int32_t)a * b + 0x4000));
}
/* ⑦ 곱은 32비트로(각 곱은 |p| ≤ 2^30 이라 int32 에 들어간다), 합만 64비트로 */
q15_t mix2_p32(q15_t m, q15_t gm, q15_t t, q15_t gt) {
    int32_t p1 = (int32_t)m * gm, p2 = (int32_t)t * gt;
    return sat_q15(floor_shr15_div64((int64_t)p1 + p2 + 0x4000));
}
/* ⑧ float → Q15 (M4F 는 단정도 FPU) : 범위 검사 후 변환, 가장 가까운 값(동점은 0 에서 먼 쪽) */
q15_t q15_from_float(float x) {
    if (x != x) return 0;
    float s = x * 32768.0f;
    if (s >= 32767.5f) return INT16_MAX;
    if (s <= -32768.5f) return INT16_MIN;
    int32_t t = (int32_t)s;
    float frac = s - (float)t;
    if (frac >= 0.5f) t++;
    else if (frac <= -0.5f) t--;
    return (q15_t)t;
}
/* ⑨ double 버전 — M4F 에는 배정도 하드웨어가 없다 */
q15_t q15_from_double_m4(double x) {
    if (x != x) return 0;
    double s = x * 32768.0;
    if (s >= 32767.5) return INT16_MAX;
    if (s <= -32768.5) return INT16_MIN;
    int32_t t = (int32_t)s;
    double frac = s - (double)t;
    if (frac >= 0.5) t++;
    else if (frac <= -0.5) t--;
    return (q15_t)t;
}
