/* ARCH-B05 조합 논리: MUX · 디코더 · 우선순위 인코더
 *
 * 규칙: Part A 회로 함수는 "게이트 식"만 쓴다 — 0/1 값에 NOT/AND2/OR2 (또는 & | 와 ^1).
 *       d >> s, 1u << a, for 루프 같은 "동작 모델"은 정답표(spec_*) 쪽만 쓴다.
 *       BIT(v, n) 과 상수 시프트로 묶기(pack)는 "배선(wiring)"이라 허용.
 * Part B/C 는 Part A 블록을 조립한다 (산술 31 - idx 정도는 허용, __builtin_* 은 테스트에서만).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (solution.c 도 같은 방식)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- 게이트와 배선 (값은 항상 0 또는 1) ---------------- */
static inline unsigned NOT(unsigned a)              { return (a & 1u) ^ 1u; }
static inline unsigned AND2(unsigned a, unsigned b) { return a & b & 1u; }
static inline unsigned OR2(unsigned a, unsigned b)  { return (a | b) & 1u; }
static inline unsigned BIT(unsigned v, unsigned n)  { return (v >> n) & 1u; } /* 버스에서 선 하나 */

/* ======================= Part A: 진리표 + 게이트 모델 ======================= */

/* 4:1 MUX: y = d[s1 s0]. 곱의 합(SOP) 4항. */
static unsigned mux4(unsigned d0, unsigned d1, unsigned d2, unsigned d3, unsigned s1, unsigned s0) {
    (void)d0; (void)d1; (void)d2; (void)d3; (void)s1; (void)s0; (void)NOT; (void)AND2; (void)OR2;
    return 0; /* TODO: 곱의 합(SOP) 4항 — NOT/AND2/OR2 만 */
}

/* 3-to-8 디코더, active-high 출력, en=0 이면 전부 0. 출력 y7..y0 을 8비트로 묶어 돌려준다. */
static uint8_t dec3to8(unsigned en, unsigned a2, unsigned a1, unsigned a0) {
    (void)en; (void)a2; (void)a1; (void)a0;
    return 0; /* TODO: y_k = en AND (a2 a1 a0 의 minterm k), 8개를 묶어 반환 */
}

/* 8-input 우선순위 인코더 (i7 이 가장 높다). *valid = 입력에 1이 하나라도 있나.
 * 입력이 0 이면 *valid = 0, 반환 0 (don't care 를 0 으로 정함). */
static unsigned penc8(uint8_t in, unsigned *valid) {
    (void)in;
    *valid = 0;
    return 0; /* TODO: y2, y1, y0, valid 게이트 식 */
}

/* 진리표: 입력 벡터(비트 묶음) in = 0 .. 2^nin - 1 마다 출력 벡터를 tt[in] 에 적는다. */
typedef unsigned (*comb_fn)(unsigned in);
static void tt_build(comb_fn f, unsigned nin, uint16_t *tt) {
    (void)f; (void)nin; (void)tt; /* TODO: 모든 2^nin 행을 채운다 */
}

/* ======================= Part B: CLZ · 계층 조립 · 반대 방향 ======================= */

/* 8비트 CLZ: 우선순위 인코더로. x == 0 → 8 (ARM CLZ 관례: CLZ(0) = 폭). */
static unsigned clz8(uint8_t x) {
    (void)x; (void)penc8;
    return 0; /* TODO: penc8 로. x == 0 → 8 */
}

/* 32-input 우선순위 인코더 = penc8 4개(바이트별) + 그룹 인코더(penc8 하나, 4입력만 사용)
 * + 인덱스 하위 3비트를 고르는 mux4 3개. 입력 0 → *valid = 0, 반환 0. */
static unsigned penc32(uint32_t x, unsigned *valid) {
    (void)x; (void)mux4;
    *valid = 0;
    return 0; /* TODO: penc8 x4 + 그룹 인코더(penc8) + mux4 x3 */
}

/* 32비트 CLZ = 31 - (가장 높은 1 의 위치). x == 0 → 32. */
static unsigned clz32(uint32_t x) {
    (void)x;
    return 0; /* TODO: penc32 로. x == 0 → 32 */
}

/* 가장 낮은 1 의 위치 (bit 0 이 이긴다) = CTZ. 선을 거꾸로 꽂고(reverse) penc8 → 7 - idx.
 * 입력 0 → *valid = 0, 반환 0. */
static unsigned lowest8(uint8_t x, unsigned *valid) {
    (void)x;
    *valid = 0;
    return 0; /* TODO: 선을 거꾸로 꽂고 penc8 */
}

/* ======================= Part C: 칩 셀렉트 · 인터럽트 선택 · RTOS 비트맵 ======================= */

/* 74HC138 식 디코더: G1(active-high), G2A_n·G2B_n(active-low) 셋이 모두 맞아야 동작.
 * 입력 C B A, 출력 Y7_n..Y0_n 은 active-low (선택된 하나만 0). */
static uint8_t hc138(unsigned g1, unsigned g2a_n, unsigned g2b_n, unsigned c, unsigned b, unsigned a) {
    (void)g1; (void)g2a_n; (void)g2b_n; (void)c; (void)b; (void)a; (void)dec3to8;
    return 0xFF; /* TODO: enable 3개 + dec3to8 + active-low 출력 */
}

/* 64 KB 주소 공간을 A15..A13 으로 8 KB 뱅크 8개로 나눈다. MREQ_n(active-low)이 0 일 때만 선택.
 * 반환: CS7_n..CS0_n (active-low). */
static uint8_t chip_selects(uint16_t addr, unsigned mreq_n) {
    (void)addr; (void)mreq_n; (void)hc138;
    return 0xFF; /* TODO: A15..A13 → hc138 */
}

/* 인터럽트 8개, 고정 우선순위: IRQ0 이 가장 높다 (8259 PIC 기본 모드, NVIC 동순위 tie-break 와 같은 방향).
 * pending & enable 중 이길 IRQ 번호를 *vec 에. 하나도 없으면 false. */
static bool irq_pick(uint8_t pending, uint8_t enable, unsigned *vec) {
    (void)pending; (void)enable; (void)lowest8;
    *vec = 0;
    return false; /* TODO: pending AND enable → lowest8 */
}

/* RTOS ready 비트맵: bit p = 우선순위 p 에 ready 태스크가 있다. 큰 숫자 = 높은 우선순위 (FreeRTOS 식). */
typedef struct { uint32_t ready; } rdy_t;
static void rdy_set(rdy_t *r, unsigned p)   { r->ready |= 1u << p; }
static void rdy_clear(rdy_t *r, unsigned p) { r->ready &= ~(1u << p); }
/* 가장 높은 ready 우선순위, 없으면 -1. penc32 를 써라 (= 31 - CLZ). */
static int rdy_top(const rdy_t *r) {
    (void)r; (void)penc32;
    return -1; /* TODO: penc32 로 (= 31 - CLZ) */
}

/* ======================= 정답표(spec)와 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                 /* xorshift32, 고정 시드 → 결정적 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 입력 벡터 배치: mux4 in = [5]s1 [4]s0 [3]d3 [2]d2 [1]d1 [0]d0 */
static unsigned w_mux4(unsigned in) {
    return mux4(BIT(in, 0), BIT(in, 1), BIT(in, 2), BIT(in, 3), BIT(in, 5), BIT(in, 4));
}
static unsigned s_mux4(unsigned in) { return ((in & 0xFu) >> (in >> 4)) & 1u; }
/* dec in = [3]en [2]a2 [1]a1 [0]a0 */
static unsigned w_dec(unsigned in) { return dec3to8(BIT(in, 3), BIT(in, 2), BIT(in, 1), BIT(in, 0)); }
static unsigned s_dec(unsigned in) { return BIT(in, 3) ? 1u << (in & 7u) : 0u; }
/* penc 출력 = [3]valid [2..0]index */
static unsigned w_penc(unsigned in) { unsigned v, i = penc8((uint8_t)in, &v); return v << 3 | i; }
static unsigned s_penc(unsigned in) {
    for (int k = 7; k >= 0; k--) if (BIT(in, (unsigned)k)) return 8u | (unsigned)k;
    return 0u;
}
static unsigned popc(unsigned x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }

static unsigned tt_mismatch(comb_fn dut, comb_fn spec, unsigned nin, unsigned *first) {
    static uint16_t a[256], b[256];
    for (unsigned k = 0; k < 256; k++) { a[k] = 0xFFFFu; b[k] = 0xFFFFu; }
    tt_build(dut, nin, a);
    tt_build(spec, nin, b);
    unsigned bad = 0;
    *first = 0xFFFFu;
    for (unsigned in = 0; in < (1u << nin); in++)
        if (a[in] != b[in] || a[in] == 0xFFFFu) { if (!bad) *first = in; bad++; }
    return bad;
}

int main(void) {
    unsigned first, bad;

    /* ---------- Part A: 전수 진리표 비교 ---------- */
    bad = tt_mismatch(w_mux4, s_mux4, 6, &first);
    if (bad) printf("  mux4: %u/64 rows differ, first in=0x%02X\n", bad, first);
    CHECK(bad == 0);
    CHECK(mux4(0, 0, 1, 0, 1, 0) == 1);                 /* s=2 → d2 */
    bad = tt_mismatch(w_dec, s_dec, 4, &first);
    if (bad) printf("  dec3to8: %u/16 rows differ, first in=0x%X\n", bad, first);
    CHECK(bad == 0);
    bad = 0;                                            /* 성질: 원-핫(one-hot) 또는 0 */
    for (unsigned in = 0; in < 16; in++) {
        unsigned y = w_dec(in);
        if (popc(y) != BIT(in, 3)) bad++;
    }
    CHECK(bad == 0);
    bad = tt_mismatch(w_penc, s_penc, 8, &first);
    if (bad) printf("  penc8: %u/256 rows differ, first in=0x%02X\n", bad, first);
    CHECK(bad == 0);
    { unsigned v = 9; CHECK(penc8(0x00, &v) == 0 && v == 0); }
    { unsigned v = 9; CHECK(penc8(0x01, &v) == 0 && v == 1); }   /* 0 과 구별되는 건 valid 뿐 */
    { unsigned v; CHECK(penc8(0x5A, &v) == 6 && v == 1); }

    /* ---------- Part B: CLZ 동치, 계층 조립, 반대 방향 ---------- */
    bad = 0;
    for (unsigned x = 0; x < 256; x++) {
        unsigned ref = x ? (unsigned)__builtin_clz(x) - 24u : 8u;   /* builtin_clz(0) 은 UB → 따로 */
        if (clz8((uint8_t)x) != ref) bad++;
    }
    CHECK(bad == 0);
    { unsigned v = 9; CHECK(penc32(0, &v) == 0 && v == 0 && clz32(0) == 32); }
    bad = 0;
    for (unsigned k = 0; k < 32; k++) {
        if (clz32(1u << k) != 31u - k) bad++;
        if (clz32(0xFFFFFFFFu >> k) != k) bad++;
    }
    CHECK(bad == 0);
    bad = 0;                                            /* 16비트 전수 x 세 자리 = 196608 */
    for (uint32_t x = 1; x < 65536u; x++) {
        const uint32_t pats[3] = { x, x << 8, x << 16 };
        for (int j = 0; j < 3; j++)
            if (clz32(pats[j]) != (unsigned)__builtin_clz(pats[j])) bad++;
    }
    CHECK(bad == 0);
    {   /* PRNG 100만 개 — 무작위 시프트로 앞자리 0 개수를 고르게 섞는다 */
        unsigned cover[33] = {0};
        bad = 0;
        for (int n = 0; n < 1000000; n++) {
            uint32_t r = xs32(), x = r >> (xs32() & 31u);
            if (!x) continue;
            unsigned c = clz32(x);
            if (c != (unsigned)__builtin_clz(x)) bad++;
            cover[c]++;
        }
        unsigned miss = 0;
        for (int c = 0; c < 32; c++) if (!cover[c]) miss++;
        CHECK(bad == 0);
        CHECK(miss == 0);                               /* clz 0..31 이 모두 시험됐나 */
    }
    bad = 0;
    for (unsigned x = 0; x < 256; x++) {
        unsigned v, i = lowest8((uint8_t)x, &v);
        unsigned ref = x ? (unsigned)__builtin_ctz(x) : 0u;
        if (i != ref || v != (x != 0)) bad++;
    }
    CHECK(bad == 0);

    /* ---------- Part C: 칩 셀렉트, IRQ 선택, RTOS ready 비트맵 ---------- */
    bad = 0;                                            /* 74HC138 전수 64 행 */
    for (unsigned in = 0; in < 64; in++) {
        unsigned g1 = BIT(in, 5), g2a = BIT(in, 4), g2b = BIT(in, 3);
        unsigned en = g1 && !g2a && !g2b;
        uint8_t ref = (uint8_t)(en ? ~(1u << (in & 7u)) : 0xFFu);
        if (hc138(g1, g2a, g2b, BIT(in, 2), BIT(in, 1), BIT(in, 0)) != ref) bad++;
    }
    CHECK(bad == 0);
    CHECK(chip_selects(0x0000, 0) == 0xFE);
    CHECK(chip_selects(0x1FFF, 0) == 0xFE);
    CHECK(chip_selects(0x2000, 0) == 0xFD);
    CHECK(chip_selects(0xFFFF, 0) == 0x7F);
    CHECK(chip_selects(0x4000, 1) == 0xFF);             /* MREQ_n = 1 → 아무도 선택 안 됨 */
    bad = 0;                                            /* 전 주소 x MREQ 2 상태: 정확히 하나 / 0 개 */
    for (uint32_t a = 0; a < 65536u; a++) {
        uint8_t cs0 = chip_selects((uint16_t)a, 0), cs1 = chip_selects((uint16_t)a, 1);
        if (cs0 != (uint8_t)~(1u << (a >> 13)) || popc((uint8_t)~cs0) != 1) bad++;
        if (cs1 != 0xFF) bad++;
    }
    CHECK(bad == 0);
    { unsigned vec = 99; CHECK(irq_pick(0x0C, 0xFF, &vec) && vec == 2); }
    { unsigned vec = 99; CHECK(irq_pick(0x0C, 0xFB, &vec) && vec == 3); }    /* IRQ2 마스크됨 */
    { unsigned vec = 99; CHECK(!irq_pick(0x80, 0x7F, &vec)); }
    bad = 0;                                            /* pending x enable 전수 65536 */
    for (unsigned p = 0; p < 256; p++)
        for (unsigned e = 0; e < 256; e++) {
            unsigned vec = 99, act = p & e;
            bool got = irq_pick((uint8_t)p, (uint8_t)e, &vec);
            if (got != (act != 0) || (act && vec != (unsigned)__builtin_ctz(act))) bad++;
        }
    CHECK(bad == 0);
    {   /* 시나리오: 태스크가 ready/block 될 때마다 top 우선순위 */
        rdy_t r = {0};
        CHECK(rdy_top(&r) == -1);                       /* idle 만 남음 */
        rdy_set(&r, 3);  rdy_set(&r, 0);
        int t1 = rdy_top(&r);
        rdy_set(&r, 17);
        int t2 = rdy_top(&r);
        rdy_set(&r, 31);
        int t3 = rdy_top(&r);
        rdy_clear(&r, 31); rdy_clear(&r, 17);
        int t4 = rdy_top(&r);
        rdy_clear(&r, 3);
        int t5 = rdy_top(&r);
        CHECK(t1 == 3 && t2 == 17 && t3 == 31 && t4 == 3 && t5 == 0);
    }
    {   /* 무작위 set/clear 10만 번 vs 선형 탐색 */
        rdy_t r = {0};
        bad = 0;
        for (int n = 0; n < 100000; n++) {
            uint32_t q = xs32();
            if (q & 0x100u) rdy_set(&r, q & 31u); else rdy_clear(&r, q & 31u);
            int ref = -1;
            for (int k = 31; k >= 0; k--) if (r.ready >> k & 1u) { ref = k; break; }
            if (rdy_top(&r) != ref) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
