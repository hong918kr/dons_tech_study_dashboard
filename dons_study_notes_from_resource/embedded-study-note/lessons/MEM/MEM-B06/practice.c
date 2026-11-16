/* MEM-B06 추가 연습 — 한 번에 검증
 *
 * 연습 1: 확장 Hamming(8,4) SECDED — bit 0 에 전체 패리티 p0. 단일 정정 / 2비트 검출(DED) / 3비트는? (72/64 는 MEM-B07)
 * 연습 2: 표 디코더 — 128 B LUT (7비트 수신 워드 → 데이터) 를 init 에 만들고 비트 연산 디코더와 전수 대조
 * 연습 3: 일반 위치형 Hamming(15,11) — 신드롬 = "1 인 비트들의 위치 번호 XOR". 2048 × 15 단일 오류 전수
 * 연습 4: 디코드 실패 확률 — 독립 비트 오류율 p 에서 P_fail(7,4) 를 닫힌 식 vs 무게별 전수 합으로, 무부호 니블과의 교차점
 * 연습 5: 32비트 패리티 세 가지 (XOR 접기 / 0x6996 니블 표 / __builtin_parity) 일치 + 바이트 패리티가 놓치는 버스 오류
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static unsigned popcount32(uint32_t x) { unsigned n = 0; while (x) { x &= x - 1u; n++; } return n; }

/* ---- solution.c 의 Hamming(7,4) 축약판 (bit i = 위치 i, bit 0 예약) ---- */
static unsigned parity8(uint8_t x) { x ^= (uint8_t)(x >> 4); x ^= (uint8_t)(x >> 2); x ^= (uint8_t)(x >> 1); return x & 1u; }
static uint8_t h74_place(uint8_t d) { return (uint8_t)((d & 1u) << 3 | (d & 2u) << 4 | (d & 4u) << 4 | (d & 8u) << 4); }
static uint8_t h74_extract(uint8_t cw) { return (uint8_t)((cw >> 3 & 1u) | (cw >> 4 & 2u) | (cw >> 4 & 4u) | (cw >> 4 & 8u)); }
static unsigned h74_syndrome(uint8_t cw) { return parity8(cw & 0xAAu) | parity8(cw & 0xCCu) << 1 | parity8(cw & 0xF0u) << 2; }
static uint8_t h74_encode(uint8_t d) {
    uint8_t c = h74_place(d & 0x0Fu);
    unsigned s = h74_syndrome(c);
    return (uint8_t)(c | (s & 1u) << 1 | (s & 2u) << 1 | (s & 4u) << 2);
}
static uint8_t h74_decode_data(uint8_t rx) {
    uint8_t cw = rx & 0xFEu;
    unsigned s = h74_syndrome(cw);
    if (s) cw ^= (uint8_t)(1u << s);
    return h74_extract(cw);
}

/* ---------------- 연습 1: Hamming(8,4) SECDED ---------------- */
typedef enum { S_OK, S_CE, S_DED } sec_status;
static uint8_t h84_encode(uint8_t d) {
    uint8_t c = h74_encode(d);
    return (uint8_t)(c | parity8(c));                 /* bit0 = 위치 1..7 의 패리티 → 8비트 전체가 짝수 */
}
static sec_status h84_decode(uint8_t rx, uint8_t *data) {
    unsigned s = h74_syndrome(rx & 0xFEu);
    unsigned P = parity8(rx);                          /* 전체 패리티: 1 이면 홀수 개 오류 */
    if (s == 0 && P == 0) { *data = h74_extract(rx); return S_OK; }
    if (P == 1) {                                      /* 홀수 개 → 단일 오류로 가정 */
        rx ^= (uint8_t)(1u << s);                      /* s == 0 이면 bit 0 (p0 자신) 을 뒤집는다 */
        *data = h74_extract(rx);
        return S_CE;
    }
    *data = h74_extract(rx);                           /* s ≠ 0, P = 0 → 짝수 개(2) 오류: 고치지 않는다 */
    return S_DED;
}

/* ---------------- 연습 2: 128 B 표 디코더 ---------------- */
static uint8_t g_lut[128];
static void lut_init(void) {
    for (unsigned d = 0; d < 16; d++) {
        uint8_t c = h74_encode((uint8_t)d);
        g_lut[c >> 1] = (uint8_t)d;                    /* 코드워드 자신 */
        for (unsigned pos = 1; pos <= 7; pos++)       /* 이웃 7개 — 완전 부호라 128 칸이 정확히 한 번씩 채워진다 */
            g_lut[(uint8_t)(c ^ (1u << pos)) >> 1] = (uint8_t)d;
    }
}
static uint8_t lut_decode(uint8_t rx) { return g_lut[(rx >> 1) & 0x7Fu]; }

/* ---------------- 연습 3: Hamming(15,11), uint16_t bit i = 위치 i (bit 0 예약) ---------------- */
static unsigned xor_of_positions(uint16_t w) {        /* 1 인 비트의 위치 번호를 전부 XOR = 신드롬 */
    unsigned s = 0;
    for (unsigned i = 1; i <= 15; i++) if (w >> i & 1u) s ^= i;
    return s;
}
static uint16_t h1511_encode(uint16_t d) {           /* 데이터 11비트를 2의 거듭제곱이 아닌 위치에 차례로 */
    uint16_t c = 0;
    unsigned k = 0;
    for (unsigned i = 1; i <= 15; i++) {
        if ((i & (i - 1u)) == 0) continue;            /* 1,2,4,8 = 검사 비트 자리 */
        if (d >> k & 1u) c |= (uint16_t)(1u << i);
        k++;
    }
    unsigned s = xor_of_positions(c);                 /* 데이터만의 신드롬 → 검사 비트 = s 의 각 비트 */
    for (unsigned b = 0; b < 4; b++) if (s >> b & 1u) c |= (uint16_t)(1u << (1u << b));
    return c;
}
static uint16_t h1511_extract(uint16_t c) {
    uint16_t d = 0;
    unsigned k = 0;
    for (unsigned i = 1; i <= 15; i++) {
        if ((i & (i - 1u)) == 0) continue;
        if (c >> i & 1u) d |= (uint16_t)(1u << k);
        k++;
    }
    return d;
}
static uint16_t h1511_decode(uint16_t rx) {
    rx &= 0xFFFEu;
    unsigned s = xor_of_positions(rx);
    if (s) rx ^= (uint16_t)(1u << s);
    return h1511_extract(rx);
}

/* ---------------- 연습 4: 실패 확률 ---------------- */
static double binom(unsigned n, unsigned k) { double r = 1; for (unsigned i = 1; i <= k; i++) r = r * (n - k + i) / i; return r; }
static double pfail_closed(double p) { return 1.0 - pow(1 - p, 7) - 7 * p * pow(1 - p, 6); }
static double pfail_enum(double p) {                  /* 전수: 7비트 오류 128 개 중 데이터가 틀리는 패턴의 확률 합 */
    double sum = 0;
    for (unsigned x = 0; x < 128; x++) {
        uint8_t e = (uint8_t)(x << 1);
        if (h74_decode_data((uint8_t)(h74_encode(0xB) ^ e)) != 0xB) {
            unsigned w = popcount32(e);
            sum += pow(p, w) * pow(1 - p, 7 - w);
        }
    }
    return sum;
}
static double pfail_raw(double p) { return 1.0 - pow(1 - p, 4); }   /* 부호 없는 니블 4비트 */

/* ---------------- 연습 5: 32비트 패리티 세 가지 ---------------- */
static unsigned par32_fold(uint32_t x) { x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return x & 1u; }
static unsigned par32_6996(uint32_t x) { x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; return (0x6996u >> (x & 0xFu)) & 1u; }

int main(void) {
    /* 연습 1 */
    {
        unsigned ce_ok = 0, ded = 0, mis2 = 0, mis3 = 0, und4 = 0, ded4 = 0, n1 = 0, n2 = 0, n3 = 0, n4 = 0;
        bool clean = true;
        for (unsigned d = 0; d < 16; d++) {
            uint8_t c = h84_encode((uint8_t)d), out;
            clean &= parity8(c) == 0 && h84_decode(c, &out) == S_OK && out == d;
            for (unsigned e = 1; e < 256; e++) {
                unsigned w = popcount32(e);
                sec_status st = h84_decode((uint8_t)(c ^ e), &out);
                if (w == 1) { n1++; ce_ok += st == S_CE && out == d; }
                if (w == 2) { n2++; ded += st == S_DED; mis2 += st != S_DED; }
                if (w == 3) { n3++; mis3 += st == S_CE && out != d; }
                if (w == 4) { n4++; und4 += st == S_OK; ded4 += st == S_DED; }
            }
        }
        printf("ex1 H(8,4): single %u/%u corrected, double %u/%u DED (miscorrect %u), triple %u/%u miscorrected, "
               "quad %u undetected + %u DED of %u\n", ce_ok, n1, ded, n2, mis2, mis3, n3, und4, ded4, n4);
        CHECK(clean);
        CHECK(n1 == 128 && ce_ok == 128);
        CHECK(n2 == 448 && ded == 448 && mis2 == 0);
        CHECK(n3 == 896 && mis3 == 896);              /* HD 4: 3비트 = 다른 코드워드에서 1 거리 → 오정정 */
        CHECK(n4 == 1120 && und4 == 14 * 16 && ded4 == 56 * 16);
    }
    /* 연습 2 */
    {
        lut_init();
        bool same = true;
        for (unsigned x = 0; x < 256; x++) same &= lut_decode((uint8_t)x) == h74_decode_data((uint8_t)x);
        printf("ex2 LUT: %zu bytes, matches bitwise decoder on all 256 inputs: %s\n", sizeof g_lut, same ? "yes" : "no");
        CHECK(same);
        CHECK(sizeof g_lut == 128);
    }
    /* 연습 3 */
    {
        bool rt = true, single = true, eq74 = true;
        unsigned nsingle = 0, dmin = 16;
        for (unsigned d = 0; d < 2048; d++) {
            uint16_t c = h1511_encode((uint16_t)d);
            rt &= xor_of_positions(c) == 0 && h1511_decode(c) == d && (c & 1u) == 0;
            for (unsigned pos = 1; pos <= 15; pos++) {
                single &= h1511_decode((uint16_t)(c ^ (1u << pos))) == d;
                nsingle++;
            }
            if (d) { unsigned w = popcount32(c); if (w < dmin) dmin = w; }   /* 선형 부호: d_min = 최소 무게 */
        }
        for (unsigned x = 0; x < 256; x++) eq74 &= xor_of_positions((uint16_t)(x & 0xFEu)) == h74_syndrome((uint8_t)x);
        printf("ex3 H(15,11): %u single errors corrected=%s, d_min=%u, (7,4) mask syndrome == XOR of positions: %s\n",
               nsingle, single ? "all" : "NOT all", dmin, eq74 ? "yes" : "no");
        CHECK(rt);
        CHECK(single && nsingle == 2048 * 15);
        CHECK(dmin == 3);
        CHECK(eq74);
    }
    /* 연습 4 */
    {
        static const double P[] = { 1e-6, 1e-4, 1e-3, 1e-2, 0.1 };
        bool agree = true;
        printf("ex4 p        raw nibble    H(7,4) fail   ratio      closed-form rel.err\n");
        for (unsigned i = 0; i < 5; i++) {
            double c = pfail_closed(P[i]), e = pfail_enum(P[i]), r = pfail_raw(P[i]);
            agree &= fabs(c - e) <= 1e-4 * e;          /* 닫힌 식은 1 − (거의 1) 이라 작은 p 에서 자릿수를 잃는다 */
            printf("    %-8.0e %.4e    %.4e    %-9.1fx %.1e\n", P[i], r, e, r / e, fabs(c - e) / e);
        }
        bool better = true;                            /* 0 < p < 1/2 에서 항상 부호가 낫고, p = 1/2 에서 같다 */
        for (unsigned k = 1; k < 500; k++) better &= pfail_enum(k / 1000.0) < pfail_raw(k / 1000.0);
        double at_half = fabs(pfail_enum(0.5) - pfail_raw(0.5));
        printf("    coded < raw for all p = 0.001..0.499: %s; at p = 0.5 both = %.4f (diff %.1e)\n",
               better ? "yes" : "no", pfail_raw(0.5), at_half);
        CHECK(agree);
        CHECK(fabs(pfail_enum(1e-3) - (binom(7, 2) * 1e-6 * pow(0.999, 5) + binom(7, 3) * 1e-9 * pow(0.999, 4)
                                        + binom(7, 4) * 1e-12 * pow(0.999, 3) + binom(7, 5) * 1e-15 * pow(0.999, 2)
                                        + binom(7, 6) * 1e-18 * 0.999 + 1e-21)) < 1e-15);
        CHECK(better && at_half < 1e-12);
    }
    /* 연습 5 */
    {
        uint32_t rng = 0xB06u;
        bool same = true;
        static const uint32_t E[] = { 0, 1, 0x80000000u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0xAAAAAAAAu, 0x00010001u };
        for (unsigned i = 0; i < 7; i++)
            same &= par32_fold(E[i]) == (popcount32(E[i]) & 1u) && par32_6996(E[i]) == (unsigned)__builtin_parity(E[i]);
        for (unsigned i = 0; i < 100000; i++) {
            uint32_t x = xs32(&rng);
            same &= par32_fold(x) == par32_6996(x) && par32_6996(x) == (unsigned)__builtin_parity(x);
        }
        CHECK(same);
        /* 바이트 패리티 버스: 데이터 라인 두 개가 함께 뒤집히면(이웃 short, 크로스토크) 못 본다 */
        unsigned miss = 0;
        for (unsigned v = 0; v < 256; v++) {
            unsigned p = parity8((uint8_t)v);
            uint8_t bad = (uint8_t)(v ^ 0x18u);       /* DQ3, DQ4 동시 반전 */
            miss += parity8(bad) == p;
        }
        printf("ex5 parity32 3 ways agree on 100007 words: %s; byte parity misses DQ3+DQ4 double flip %u/256\n",
               same ? "yes" : "no", miss);
        CHECK(miss == 256);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
