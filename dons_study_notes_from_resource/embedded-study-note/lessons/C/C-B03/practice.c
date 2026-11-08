/* C-B03 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 본문 Part B 의 헬퍼 (재사용) */
static inline int32_t sar32(int32_t x, unsigned n) {
    if (n >= 32u) n = 31u;
    return (x < 0) ? ~(~x >> n) : (x >> n);
}

/* 연습 1: GENMASK — 비트 h..l 이 1 (0 <= l <= h <= 31). 두 시프트 모두 0..31 칸만 */
static inline uint32_t genmask32(unsigned h, unsigned l) {
    return (0xFFFFFFFFu >> (31u - h)) & (0xFFFFFFFFu << l);
}

/* 연습 2: C 의 '/' 처럼 0 쪽으로 자르는 2^n 나눗셈 (0 <= n <= 30) — 컴파일러가 x/4 를 바꾸는 방식 */
static inline int32_t div_pow2_s32(int32_t x, unsigned n) {
    int32_t bias = (x < 0) ? (int32_t)((1u << n) - 1u) : 0;   /* 음수면 2^n - 1 을 더하고 */
    return sar32(x + bias, n);                                /* floor → 0 방향 자르기  */
}

/* 연습 3: 64비트 레지스터의 비트 n (0..63). 1UL 은 Cortex-M(ILP32) 에서 32비트! */
static inline uint64_t bit64(unsigned n) { return UINT64_C(1) << n; }

/* 연습 4: Q15 고정소수점 곱셈 (반올림 + 포화). -1.0 * -1.0 = +1.0 은 표현 불가 → 0x7FFF */
static inline int16_t q15_mul(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * b;            /* 최대 2^30 → int32 에 들어감          */
    int32_t r = sar32(p + (1 << 14), 15);  /* 음수 p >> 15 대신 sar32 (구현 정의 회피) */
    if (r > INT16_MAX) r = INT16_MAX;
    if (r < INT16_MIN) r = INT16_MIN;
    return (int16_t)r;
}

/* 연습 5: 판정 퀴즈 중 "정의된" 식만 값 확인 (UB/구현 정의 식은 실행하지 않는다) */
static volatile uint8_t  q_b = 0xABu;
static volatile uint16_t q_h = 0x8001u;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(genmask32(31, 0) == 0xFFFFFFFFu);
    CHECK(genmask32(7, 4) == 0x000000F0u);
    CHECK(genmask32(31, 31) == 0x80000000u);
    CHECK(genmask32(0, 0) == 0x00000001u);

    /* 연습 2: '/' 와 전수 비교 (경계값 포함) */
    static const int32_t xs[] = { 0, 1, -1, 7, -7, 8, -8, 12345, -12345, INT32_MAX, INT32_MIN };
    bool all_ok = true;
    for (unsigned n = 0; n <= 30u; n++)
        for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++)
            if (div_pow2_s32(xs[i], n) != xs[i] / (int32_t)(1u << n)) all_ok = false;
    CHECK(all_ok);
    CHECK(div_pow2_s32(-7, 1) == -3 && sar32(-7, 1) == -4);

    /* 연습 3 */
    CHECK(bit64(0) == 1u);
    CHECK(bit64(32) == 0x0000000100000000ull);
    CHECK(bit64(63) == 0x8000000000000000ull);

    /* 연습 4 */
    CHECK(q15_mul(0x4000, 0x4000) == 0x2000);               /* 0.5 * 0.5 = 0.25   */
    CHECK(q15_mul(INT16_MIN, INT16_MIN) == INT16_MAX);      /* -1 * -1 → 포화     */
    CHECK(q15_mul(INT16_MIN, 0x4000) == -0x4000);           /* -1 * 0.5 = -0.5    */
    CHECK(q15_mul(-3, 0x4000) == -1);                       /* -1.5 LSB → 반올림  */

    /* 연습 5 */
    CHECK((q_b << 8) == 0xAB00);              /* int 로 승격, 범위 안 → 정의됨 */
    CHECK(((uint32_t)q_h << 16) == 0x80010000u); /* 캐스트 먼저 → 정의됨      */
    CHECK((0xFFFFFFFFu >> 31) == 1u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
