/* ARCH-B03 추가 연습 — 한 번에 검증
 * 1 bfloat16 RNE 변환  2 next_up / ulp 거리  3 이중 반올림(double→float→half)과 round-to-odd
 * 4 FP16 정수 한계와 누산 정체  5 FP16 전순서(total order) 정수 키
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t f2u(float f)    { uint32_t u; memcpy(&u, &f, 4); return u; }
static float    u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint64_t d2u(double d)   { uint64_t u; memcpy(&u, &d, 8); return u; }

/* 본문 solution.c 의 변환기 (복사) */
static uint16_t f32_to_f16(uint32_t b) {
    uint32_t sign = (b >> 16) & 0x8000u;
    uint32_t e = (b >> 23) & 0xFFu, f = b & 0x7FFFFFu;
    if (e == 0xFFu) return (uint16_t)(f ? (sign | 0x7E00u | (f >> 13)) : (sign | 0x7C00u));
    if (e == 0) return (uint16_t)sign;
    int eh = (int)e - 112;
    if (eh >= 31) return (uint16_t)(sign | 0x7C00u);
    uint32_t m = f | 0x800000u;
    int shift = eh >= 1 ? 13 : 14 - eh;
    if (shift > 24) return (uint16_t)sign;
    uint32_t q = m >> shift, rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1);
    if (rem > half || (rem == half && (q & 1u))) q++;
    if (eh >= 1) return (uint16_t)(sign | (((uint32_t)(eh - 1) << 10) + q));
    return (uint16_t)(sign | q);
}
static uint32_t f16_to_f32(uint16_t h) {
    uint32_t s = ((uint32_t)h & 0x8000u) << 16, e = ((uint32_t)h >> 10) & 0x1Fu, f = (uint32_t)h & 0x3FFu;
    if (e == 0x1Fu) return f ? (s | 0x7FC00000u | (f << 13)) : (s | 0x7F800000u);
    if (e == 0) {
        if (f == 0) return s;
        int k = 1;
        while ((f & 0x400u) == 0) { f <<= 1; k--; }
        return s | ((uint32_t)(k + 112) << 23) | ((f & 0x3FFu) << 13);
    }
    return s | ((e + 112u) << 23) | (f << 13);
}

/* ---- 연습 1: float32 -> bfloat16 (상위 16비트), RNE, NaN 은 quiet ---- */
static uint16_t f32_to_bf16(uint32_t b) {
    if ((b & 0x7FFFFFFFu) > 0x7F800000u) return (uint16_t)((b | 0x00400000u) >> 16);  /* NaN: 버리면 Inf 가 될 수 있다 */
    uint32_t lsb = (b >> 16) & 1u;
    return (uint16_t)((b + 0x7FFFu + lsb) >> 16);   /* 반 넘으면 캐리, 딱 반이면 lsb 가 1 일 때만 캐리 */
}

/* ---- 연습 2: 다음 큰 float / ulp 거리 ---- */
static uint32_t f32_next_up(uint32_t b) {
    if ((b & 0x7FFFFFFFu) > 0x7F800000u || b == 0x7F800000u) return b;   /* NaN, +Inf 그대로 */
    if (b == 0x80000000u) return 0x00000001u;                            /* -0 → 최소 양의 비정규 */
    return (b >> 31) ? b - 1u : b + 1u;                                  /* 음수는 크기가 줄어야 커진다 */
}
static int64_t f32_key(uint32_t b) {             /* 부호-크기 → 단조 증가 정수 (+0 과 -0 은 같은 키) */
    return (b >> 31) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
}
static uint64_t f32_ulp_distance(uint32_t a, uint32_t b) {
    int64_t d = f32_key(a) - f32_key(b);
    return (uint64_t)(d < 0 ? -d : d);
}

/* ---- 연습 3: double -> float round-to-odd (정규 float 범위만), 이중 반올림 방지 ---- */
static uint32_t f64_to_f32_round_odd(double d) {
    uint64_t u = d2u(d);
    uint32_t sign = (uint32_t)(u >> 63) << 31;
    int e = (int)((u >> 52) & 0x7FF) - 1023 + 127;       /* 전제: 1 <= e <= 254 (정규 float) */
    uint64_t f = u & 0xFFFFFFFFFFFFFull;
    uint32_t frac = (uint32_t)(f >> 29);
    if (f & ((1ull << 29) - 1)) frac |= 1u;               /* 버린 비트가 하나라도 있으면 lsb = 1 (sticky) */
    return sign | ((uint32_t)e << 23) | frac;
}

/* ---- 연습 4: FP16 이 정확히 담는 정수의 한계 ---- */
static uint32_t first_int_not_exact_f16(void) {
    for (uint32_t n = 1; n < 70000u; n++)
        if (f16_to_f32(f32_to_f16(f2u((float)n))) != f2u((float)n)) return n;
    return 0;
}

/* ---- 연습 5: FP16 전순서 정수 키 (NaN 제외, -0 < +0) ---- */
static uint16_t f16_order_key(uint16_t h) {
    return (h & 0x8000u) ? (uint16_t)~h : (uint16_t)(h | 0x8000u);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0xB03C0DEu;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint16_t hw_f16(uint32_t b) { _Float16 h = (_Float16)u2f(b); uint16_t u; memcpy(&u, &h, 2); return u; }
static uint16_t hw_f16d(double d)  { _Float16 h = (_Float16)d;      uint16_t u; memcpy(&u, &h, 2); return u; }
static uint16_t cc_bf16(uint32_t b) { __bf16 x = (__bf16)u2f(b); uint16_t u; memcpy(&u, &x, 2); return u; }

int main(void) {
    /* 연습 1 */
    CHECK(f32_to_bf16(f2u(1.0f)) == 0x3F80u);
    CHECK(f32_to_bf16(f2u(0.1f)) == 0x3DCDu);                    /* 0x3DCC_CCCD: 하위 0xCCCD > 0x8000 → 올림 */
    CHECK(f32_to_bf16(0x3F808000u) == 0x3F80u && f32_to_bf16(0x3F818000u) == 0x3F82u);   /* 동점 → 짝수 */
    CHECK(f32_to_bf16(0x7F7FFFFFu) == 0x7F80u);                  /* FLT_MAX → Inf */
    CHECK(f32_to_bf16(0x7F800001u) == 0x7FC0u);                  /* 단순 >>16 이면 0x7F80 = Inf! */
    {   unsigned bad = 0;
        for (int i = 0; i < 300000; i++) { uint32_t b = xs32(); if (f32_to_bf16(b) != cc_bf16(b)) bad++; }
        CHECK(bad == 0); }

    /* 연습 2 */
    CHECK(f32_next_up(f2u(1.0f)) == 0x3F800001u);
    CHECK(f32_next_up(0x80000000u) == 0x00000001u && f32_next_up(0x80000001u) == 0x80000000u);
    CHECK(f32_next_up(0x7F7FFFFFu) == 0x7F800000u && f32_next_up(0xFF800000u) == 0xFF7FFFFFu);
    {   unsigned bad = 0;
        for (int i = 0; i < 200000; i++) {
            uint32_t b = xs32();
            if ((b & 0x7FFFFFFFu) > 0x7F800000u) continue;          /* NaN 제외 */
            if (f32_next_up(b) != f2u(nextafterf(u2f(b), INFINITY)) && b != 0x80000000u) bad++; }
        CHECK(bad == 0); }
    CHECK(f32_ulp_distance(f2u(1.0f), f2u(1.0f + 0x1p-23f)) == 1);
    CHECK(f32_ulp_distance(0x80000001u, 0x00000001u) == 2);        /* -최소비정규 ~ +최소비정규 */
    {   float s = 0.0f;
        for (int i = 0; i < 10; i++) s += 0.1f;
        CHECK(f2u(s) == 0x3F800001u && f32_ulp_distance(f2u(s), f2u(1.0f)) == 1); }

    /* 연습 3 */
    {   double d = 1.0 + 0x1p-11 + 0x1p-40;                        /* FP16 동점보다 아주 조금 위 */
        CHECK(hw_f16d(d) == 0x3C01u);                              /* 직접: 올림 */
        CHECK(f2u((float)d) == 0x3F801000u);                       /* float 로 먼저 반올림하면 정확히 동점 */
        CHECK(f32_to_f16(f2u((float)d)) == 0x3C00u);               /* → 짝수로 내림: 이중 반올림 오류 */
        CHECK(f32_to_f16(f64_to_f32_round_odd(d)) == 0x3C01u); }   /* round-to-odd 중간값이면 정답 */
    {   unsigned bad_naive = 0, bad_odd = 0;
        for (int i = 0; i < 100000; i++) {
            uint32_t r = xs32();
            int E = (int)(r % 30u) - 24;                           /* 2^-24 .. 2^5 배율: FP16 비정규~정규 */
            double m11 = (double)(1024u + (xs32() & 1023u));
            int s = (int)(xs32() % 3u) - 1;                         /* -1, 0, +1 */
            double d = (m11 + 0.5 + s * 0x1p-25) * ldexp(1.0, E - 10);
            if (r & 0x80000000u) d = -d;
            uint16_t ref = hw_f16d(d);
            if (f32_to_f16(f2u((float)d)) != ref) bad_naive++;
            if (f32_to_f16(f64_to_f32_round_odd(d)) != ref) bad_odd++; }
        CHECK(bad_naive > 0);                                      /* 순진한 경로는 틀리는 경우가 있다 */
        CHECK(bad_odd == 0);
        printf("  [연습3] 100000 개 중 double→float→half 오답 %u 개, round-to-odd 경로 오답 %u 개\n", bad_naive, bad_odd); }

    /* 연습 4 */
    CHECK(first_int_not_exact_f16() == 2049u);
    {   volatile _Float16 acc = (_Float16)2048.0f;
        for (int i = 0; i < 100; i++) acc = (_Float16)(acc + (_Float16)1.0f);
        CHECK(f2u((float)acc) == f2u(2048.0f)); }                  /* +1 이 100 번 사라짐 (FP16 누산 정체) */
    CHECK(f2u((float)16777217) == f2u(16777216.0f));               /* float 은 2^24 + 1 에서 */

    /* 연습 5 */
    CHECK(f16_order_key(0x8000u) < f16_order_key(0x0000u));        /* -0 < +0 */
    CHECK(f16_order_key(0xFC00u) < f16_order_key(0xFBFFu) && f16_order_key(0x7BFFu) < f16_order_key(0x7C00u));
    {   unsigned bad = 0;
        for (int i = 0; i < 300000; i++) {
            uint16_t a = (uint16_t)xs32(), b = (uint16_t)xs32();
            if ((a & 0x7FFFu) > 0x7C00u || (b & 0x7FFFu) > 0x7C00u) continue;   /* NaN 제외 */
            if (((a | b) & 0x7FFFu) == 0) continue;                              /* ±0 쌍은 float 비교로는 같음 */
            int want = u2f(f16_to_f32(a)) < u2f(f16_to_f32(b));
            if ((f16_order_key(a) < f16_order_key(b)) != want) bad++; }
        CHECK(bad == 0); }
    {   unsigned bad = 0;                                          /* 본문 변환기 재확인 (경계 근처) */
        for (int i = 0; i < 50000; i++) {
            uint32_t r = xs32(), b = (r & 0x807FFFFFu) | ((100u + (r >> 23) % 46u) << 23);
            if (f32_to_f16(b) != hw_f16(b)) bad++; }
        CHECK(bad == 0); }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
