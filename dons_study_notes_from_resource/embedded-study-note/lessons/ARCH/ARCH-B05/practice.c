/* ARCH-B05 추가 연습 — 한 번에 검증
 *   연습 1  NAND 4개로 2:1 MUX
 *   연습 2  4:1 MUX 하나로 임의의 3입력 함수 (Shannon 전개 = LUT)
 *   연습 3  3-to-8 디코더 2개로 4-to-16 디코더
 *   연습 4  온도계 코드(thermometer) → 이진 (flash ADC 인코더), 버블 오류
 *   연습 5  NVIC 식 프로그래머블 우선순위 선택 (낮은 숫자 = 급함, 동순위는 번호 작은 IRQ)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static inline unsigned NOT(unsigned a)              { return (a & 1u) ^ 1u; }
static inline unsigned AND2(unsigned a, unsigned b) { return a & b & 1u; }
static inline unsigned OR2(unsigned a, unsigned b)  { return (a | b) & 1u; }
static inline unsigned NAND2(unsigned a, unsigned b){ return NOT(AND2(a, b)); }
static inline unsigned BIT(unsigned v, unsigned n)  { return (v >> n) & 1u; }

/* 본문(solution.c)과 같은 블록 */
static unsigned mux4(unsigned d0, unsigned d1, unsigned d2, unsigned d3, unsigned s1, unsigned s0) {
    unsigned n1 = NOT(s1), n0 = NOT(s0);
    return OR2(OR2(AND2(AND2(n1, n0), d0), AND2(AND2(n1, s0), d1)),
               OR2(AND2(AND2(s1, n0), d2), AND2(AND2(s1, s0), d3)));
}
static uint8_t dec3to8(unsigned en, unsigned a2, unsigned a1, unsigned a0) {
    unsigned n2 = NOT(a2), n1 = NOT(a1), n0 = NOT(a0);
    unsigned y0 = AND2(en, AND2(AND2(n2, n1), n0)), y1 = AND2(en, AND2(AND2(n2, n1), a0));
    unsigned y2 = AND2(en, AND2(AND2(n2, a1), n0)), y3 = AND2(en, AND2(AND2(n2, a1), a0));
    unsigned y4 = AND2(en, AND2(AND2(a2, n1), n0)), y5 = AND2(en, AND2(AND2(a2, n1), a0));
    unsigned y6 = AND2(en, AND2(AND2(a2, a1), n0)), y7 = AND2(en, AND2(AND2(a2, a1), a0));
    return (uint8_t)(y7 << 7 | y6 << 6 | y5 << 5 | y4 << 4 | y3 << 3 | y2 << 2 | y1 << 1 | y0);
}
static unsigned penc8(uint8_t in, unsigned *valid) {
    unsigned i0 = BIT(in, 0), i1 = BIT(in, 1), i2 = BIT(in, 2), i3 = BIT(in, 3);
    unsigned i4 = BIT(in, 4), i5 = BIT(in, 5), i6 = BIT(in, 6), i7 = BIT(in, 7);
    unsigned y2 = OR2(OR2(i7, i6), OR2(i5, i4));
    unsigned y1 = OR2(OR2(i7, i6), AND2(AND2(NOT(i5), NOT(i4)), OR2(i3, i2)));
    unsigned y0 = OR2(OR2(i7, AND2(NOT(i6), i5)),
                      OR2(AND2(AND2(NOT(i6), NOT(i4)), i3),
                          AND2(AND2(NOT(i6), NOT(i4)), AND2(NOT(i2), i1))));
    *valid = OR2(y2, OR2(OR2(i3, i2), OR2(i1, i0)));
    return y2 << 2 | y1 << 1 | y0;
}
static unsigned lowest8(uint8_t x, unsigned *valid) {
    uint8_t r = (uint8_t)(BIT(x, 0) << 7 | BIT(x, 1) << 6 | BIT(x, 2) << 5 | BIT(x, 3) << 4 |
                          BIT(x, 4) << 3 | BIT(x, 5) << 2 | BIT(x, 6) << 1 | BIT(x, 7));
    unsigned i = penc8(r, valid);
    return *valid ? 7u - i : 0u;
}

/* ---------- 연습 1: NAND 만으로 2:1 MUX (게이트 4개) ---------- */
static unsigned mux2_nand(unsigned d0, unsigned d1, unsigned s) {
    unsigned ns = NAND2(s, s);                      /* NOT s */
    return NAND2(NAND2(d0, ns), NAND2(d1, s));      /* = (d0·~s) + (d1·s), 드모르간 */
}

/* ---------- 연습 2: mux4 하나 = 3입력 LUT ----------
 * tt 의 비트 (a<<2 | b<<1 | c) = f(a,b,c). 선택선 = a,b. 데이터 입력 k 는 c 의 함수 {0, 1, c, ~c}. */
static unsigned lut3(uint8_t tt, unsigned a, unsigned b, unsigned c) {
    unsigned d[4];
    for (unsigned k = 0; k < 4; k++) {              /* k = a*2 + b : 데이터 선 4개를 "배선"으로 준비 */
        unsigned f0 = BIT(tt, k * 2u), f1 = BIT(tt, k * 2u + 1u);
        d[k] = OR2(AND2(f0, NOT(c)), AND2(f1, c));
    }
    return mux4(d[0], d[1], d[2], d[3], a, b);
}

/* ---------- 연습 3: 4-to-16 디코더 = 3-to-8 두 개 + a3 로 enable 나누기 ---------- */
static uint16_t dec4to16(unsigned en, unsigned a3, unsigned a2, unsigned a1, unsigned a0) {
    uint8_t lo = dec3to8(AND2(en, NOT(a3)), a2, a1, a0);
    uint8_t hi = dec3to8(AND2(en, a3), a2, a1, a0);
    return (uint16_t)((unsigned)hi << 8 | lo);
}

/* ---------- 연습 4: 7비트 온도계 코드 → 3비트 이진 ----------
 * 정상 코드: 0000000, 0000001, 0000011, ... 1111111 → 0..7.
 * 비트 0 아래에 "항상 1" 선을 하나 더 꽂으면 penc8 가 바로 답: idx = (가장 높은 1 의 위치) + 1. */
static unsigned therm2bin(uint8_t t7) {
    unsigned v;
    return penc8((uint8_t)((t7 & 0x7Fu) << 1 | 1u), &v);
}

/* ---------- 연습 5: NVIC 식 프로그래머블 우선순위 ----------
 * prio[i] = 0..3 (2비트, 작을수록 급함). 동순위면 IRQ 번호가 작은 쪽. pending & enable 만 후보. */
static bool nvic_pick(uint8_t pending, uint8_t enable, const uint8_t prio[8], unsigned *vec) {
    uint8_t act = (uint8_t)(pending & enable);
    for (unsigned lvl = 0; lvl < 4; lvl++) {        /* 레벨마다 비트맵 → 가장 급한 비어있지 않은 레벨 */
        uint8_t bm = 0;
        for (unsigned i = 0; i < 8; i++)
            if (prio[i] == lvl) bm = (uint8_t)(bm | (1u << i));
        unsigned v, idx = lowest8((uint8_t)(act & bm), &v);
        if (v) { *vec = idx; return true; }
    }
    return false;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x9E3779B9u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned popc(unsigned x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }

int main(void) {
    unsigned bad = 0;

    /* 연습 1 */
    for (unsigned in = 0; in < 8; in++) {
        unsigned d0 = BIT(in, 0), d1 = BIT(in, 1), s = BIT(in, 2);
        if (mux2_nand(d0, d1, s) != (s ? d1 : d0)) bad++;
    }
    CHECK(bad == 0);

    /* 연습 2: 256 개 함수 x 8 행 = 2048 */
    bad = 0;
    for (unsigned tt = 0; tt < 256; tt++)
        for (unsigned in = 0; in < 8; in++)
            if (lut3((uint8_t)tt, BIT(in, 2), BIT(in, 1), BIT(in, 0)) != BIT(tt, in)) bad++;
    CHECK(bad == 0);
    CHECK(lut3(0xE8, 1, 1, 0) == 1 && lut3(0xE8, 1, 0, 0) == 0);  /* 0xE8 = 다수결(majority) */
    CHECK(lut3(0x96, 1, 1, 1) == 1 && lut3(0x96, 1, 1, 0) == 0);  /* 0x96 = 3입력 XOR (패리티) */

    /* 연습 3: en x 16 = 32 행 */
    bad = 0;
    for (unsigned in = 0; in < 32; in++) {
        unsigned en = BIT(in, 4), a = in & 15u;
        uint16_t y = dec4to16(en, BIT(a, 3), BIT(a, 2), BIT(a, 1), BIT(a, 0));
        if (y != (en ? (uint16_t)(1u << a) : 0u) || popc(y) != en) bad++;
    }
    CHECK(bad == 0);

    /* 연습 4 */
    bad = 0;
    for (unsigned n = 0; n <= 7; n++)
        if (therm2bin((uint8_t)((1u << n) - 1u)) != n) bad++;
    CHECK(bad == 0);
    CHECK(therm2bin(0x0B) == 4);          /* 0001011: 버블 — 우선순위 인코더는 4 */
    CHECK(popc(0x0B) == 3);               /* 1 개수 세기(ones counter)는 3 — 버블에 덜 민감 */

    /* 연습 5 */
    {
        const uint8_t pr[8] = { 2, 2, 1, 3, 0, 0, 1, 2 };
        unsigned vec = 99;
        CHECK(nvic_pick(0x22, 0xFF, pr, &vec) && vec == 5);   /* IRQ1(prio2) vs IRQ5(prio0) → 5 */
        CHECK(nvic_pick(0x44, 0xFF, pr, &vec) && vec == 2);   /* IRQ2, IRQ6 둘 다 prio1 → 번호 작은 2 */
        CHECK(nvic_pick(0x30, 0xEF, pr, &vec) && vec == 5);   /* IRQ4 마스크 → 5 */
        CHECK(!nvic_pick(0x00, 0xFF, pr, &vec));
    }
    bad = 0;
    for (int n = 0; n < 200000; n++) {
        uint8_t pr[8];
        uint32_t r = xs32();
        for (int i = 0; i < 8; i++) pr[i] = (uint8_t)((r >> (2 * i)) & 3u);
        uint8_t p = (uint8_t)xs32(), e = (uint8_t)xs32();
        int best = -1;                                        /* 정답표: 직접 비교 */
        for (int i = 0; i < 8; i++)
            if ((p & e) >> i & 1u)
                if (best < 0 || pr[i] < pr[best]) best = i;
        unsigned vec = 99;
        bool got = nvic_pick(p, e, pr, &vec);
        if (got != (best >= 0) || (got && (int)vec != best)) bad++;
    }
    CHECK(bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
