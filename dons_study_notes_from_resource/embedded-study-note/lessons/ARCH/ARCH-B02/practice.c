/* ARCH-B02 추가 연습 — 한 번에 검증
 *   1. V = (MSB 로 들어간 캐리) XOR (MSB 에서 나간 캐리)
 *   2. 포화 덧셈 (Arm QADD 처럼) — V 플래그로 판정
 *   3. 8비트 플래그를 32비트 ALU 로: 피연산자 << 24 트릭
 *   4. 다중 워드(128비트) 덧셈 — ADDS + ADCS 체인
 *   5. 반대 조건 = cc ^ 1 (Arm 조건 인코딩)
 *   6. 64비트 EQ: CMP lo ; (EQ 이면) CMP hi  — Thumb IT / AArch64 CCMP 모양
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { FLAG_V = 1u << 0, FLAG_C = 1u << 1, FLAG_Z = 1u << 2, FLAG_N = 1u << 3 };
typedef struct { uint32_t res; uint8_t nzcv; } alu_t;

/* ---- 본문 모델 (solution.c 와 같음) ---- */
static inline uint32_t wmask(unsigned width) { return (uint32_t)((UINT64_C(1) << width) - 1u); }
static alu_t alu_adc(uint32_t a, uint32_t b, unsigned cin, unsigned width) {
    uint32_t m = wmask(width);
    a &= m; b &= m;
    uint64_t sum = (uint64_t)a + b + (cin & 1u);
    uint32_t r = (uint32_t)sum & m;
    unsigned n = (r >> (width - 1)) & 1u, z = (r == 0), c = (unsigned)(sum >> width) & 1u;
    unsigned v = (((a ^ r) & (b ^ r)) >> (width - 1)) & 1u;
    alu_t o = { r, (uint8_t)(n << 3 | z << 2 | c << 1 | v) };
    return o;
}
static alu_t alu_add(uint32_t a, uint32_t b, unsigned w) { return alu_adc(a, b, 0, w); }
static alu_t alu_sub(uint32_t a, uint32_t b, unsigned w) { return alu_adc(a, ~b, 1, w); }

typedef enum { CC_EQ, CC_NE, CC_HS, CC_LO, CC_MI, CC_PL, CC_VS, CC_VC,
               CC_HI, CC_LS, CC_GE, CC_LT, CC_GT, CC_LE, CC_AL } cond_t;
static bool cond_holds(cond_t cc, uint8_t nzcv) {
    bool n = nzcv & FLAG_N, z = nzcv & FLAG_Z, c = nzcv & FLAG_C, v = nzcv & FLAG_V;
    switch (cc) {
    case CC_EQ: return z;           case CC_NE: return !z;
    case CC_HS: return c;           case CC_LO: return !c;
    case CC_MI: return n;           case CC_PL: return !n;
    case CC_VS: return v;           case CC_VC: return !v;
    case CC_HI: return c && !z;     case CC_LS: return !c || z;
    case CC_GE: return n == v;      case CC_LT: return n != v;
    case CC_GT: return !z && n == v; case CC_LE: return z || n != v;
    case CC_AL: return true;
    }
    return false;
}

/* ---- 연습 1: V = carry_in(MSB) ^ carry_out(MSB) ---- */
static unsigned v_from_carries(uint32_t a, uint32_t b, unsigned cin, unsigned w) {
    uint32_t lowm = wmask(w - 1);                                   /* MSB 아래 비트들 (w=1 이면 0) */
    unsigned c_in_msb  = (unsigned)((((uint64_t)(a & lowm) + (b & lowm) + cin) >> (w - 1)) & 1u);
    unsigned c_out_msb = (unsigned)((((uint64_t)(a & wmask(w)) + (b & wmask(w)) + cin) >> w) & 1u);
    return c_in_msb ^ c_out_msb;
}

/* ---- 연습 2: 포화 덧셈 (QADD) ---- */
static int32_t sat_add32(int32_t a, int32_t b) {
    alu_t r = alu_add((uint32_t)a, (uint32_t)b, 32);
    if (!(r.nzcv & FLAG_V)) return (int32_t)r.res;   /* 2의 보수 변환: C11 은 구현 정의, clang/gcc 는 모듈러 */
    return a < 0 ? INT32_MIN : INT32_MAX;            /* 넘쳤으면 a(=b) 의 부호 쪽 끝으로 */
}

/* ---- 연습 3: 8비트 플래그 = 32비트 ALU 에 (x << 24) ---- */
static uint8_t sub8_via32(uint8_t a, uint8_t b) { return alu_sub((uint32_t)a << 24, (uint32_t)b << 24, 32).nzcv; }
static uint8_t add8_via32(uint8_t a, uint8_t b) { return alu_add((uint32_t)a << 24, (uint32_t)b << 24, 32).nzcv; }

/* ---- 연습 4: n 워드 덧셈 (리틀 엔디언 워드 배열), 마지막 캐리 반환 ---- */
static unsigned addn(uint32_t *dst, const uint32_t *a, const uint32_t *b, unsigned n) {
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) {
        alu_t r = alu_adc(a[i], b[i], c, 32);        /* i==0 이면 c=0 → ADDS, 이후 ADCS */
        dst[i] = r.res;
        c = (r.nzcv & FLAG_C) ? 1u : 0u;
    }
    return c;
}

/* ---- 연습 5: 반대 조건 ---- */
static cond_t cond_invert(cond_t cc) { return (cond_t)(cc ^ 1u); }

/* ---- 연습 6: 64비트 EQ — CMP lo; IT EQ; CMPEQ hi ---- */
static bool eq64_ccmp(uint64_t a, uint64_t b) {
    uint8_t f = alu_sub((uint32_t)a, (uint32_t)b, 32).nzcv;
    if (cond_holds(CC_EQ, f)) f = alu_sub((uint32_t)(a >> 32), (uint32_t)(b >> 32), 32).nzcv;
    return cond_holds(CC_EQ, f);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng = 0x2545F491u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    /* 1 */
    {
        long bad = 0;
        for (unsigned w = 1; w <= 8; w++)
            for (uint32_t a = 0; a < (1u << w); a++)
                for (uint32_t b = 0; b < (1u << w); b++)
                    for (unsigned c = 0; c < 2; c++)
                        if (v_from_carries(a, b, c, w) != (alu_adc(a, b, c, w).nzcv & FLAG_V)) bad++;
        CHECK(bad == 0);
        CHECK(v_from_carries(0x7F, 0x01, 0, 8) == 1 && v_from_carries(0xFF, 0x01, 0, 8) == 0);
    }
    /* 2 */
    CHECK(sat_add32(INT32_MAX, 1) == INT32_MAX);
    CHECK(sat_add32(INT32_MIN, -1) == INT32_MIN);
    CHECK(sat_add32(-5, 3) == -2);
    {
        long bad = 0;
        for (int i = 0; i < 200000; i++) {
            int32_t a = (int32_t)xs32(), b = (int32_t)xs32();
            int64_t s = (int64_t)a + b;
            int32_t want = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s;
            if (sat_add32(a, b) != want) bad++;
        }
        CHECK(bad == 0);
    }
    /* 3 */
    {
        long bad = 0;
        for (uint32_t a = 0; a < 256; a++)
            for (uint32_t b = 0; b < 256; b++) {
                if (sub8_via32((uint8_t)a, (uint8_t)b) != alu_sub(a, b, 8).nzcv) bad++;
                if (add8_via32((uint8_t)a, (uint8_t)b) != alu_add(a, b, 8).nzcv) bad++;
            }
        CHECK(bad == 0);
    }
    /* 4 */
    {
        uint32_t a[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu }, b[4] = { 1, 0, 0, 0 }, d[4];
        CHECK(addn(d, a, b, 4) == 0 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0x80000000u);
        uint32_t m[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
        CHECK(addn(d, m, b, 4) == 1 && (d[0] | d[1] | d[2] | d[3]) == 0);
        long bad = 0;
        for (int i = 0; i < 50000; i++) {                      /* 128비트 = uint64 두 개로 오라클 */
            uint64_t alo = (uint64_t)xs32() << 32 | xs32(), ahi = (uint64_t)xs32() << 32 | xs32();
            uint64_t blo = (uint64_t)xs32() << 32 | xs32(), bhi = (uint64_t)xs32() << 32 | xs32();
            uint32_t aw[4] = { (uint32_t)alo, (uint32_t)(alo >> 32), (uint32_t)ahi, (uint32_t)(ahi >> 32) };
            uint32_t bw[4] = { (uint32_t)blo, (uint32_t)(blo >> 32), (uint32_t)bhi, (uint32_t)(bhi >> 32) };
            unsigned c = addn(d, aw, bw, 4);
            uint64_t lo = alo + blo, c0 = lo < alo, hi = ahi + bhi + c0;
            unsigned cout = (hi < ahi) || (c0 && hi == ahi);
            if (d[0] != (uint32_t)lo || d[1] != (uint32_t)(lo >> 32) || d[2] != (uint32_t)hi ||
                d[3] != (uint32_t)(hi >> 32) || c != cout) bad++;
        }
        CHECK(bad == 0);
    }
    /* 5 */
    {
        long bad = 0;
        for (unsigned cc = CC_EQ; cc <= CC_LE; cc++)
            for (unsigned f = 0; f < 16; f++)
                if (cond_holds(cond_invert((cond_t)cc), (uint8_t)f) == cond_holds((cond_t)cc, (uint8_t)f)) bad++;
        CHECK(bad == 0);
        CHECK(cond_invert(CC_LT) == CC_GE && cond_invert(CC_HI) == CC_LS && cond_invert(CC_HS) == CC_LO);
    }
    /* 6 */
    {
        CHECK(eq64_ccmp(0x0000000500000001ull, 0x0000000500000000ull) == false);  /* 체인 Z 가 틀리던 쌍 */
        CHECK(eq64_ccmp(0x0000000600000000ull, 0x0000000500000000ull) == false);
        CHECK(eq64_ccmp(0x123456789ABCDEF0ull, 0x123456789ABCDEF0ull) == true);
        long bad = 0;
        for (int i = 0; i < 100000; i++) {
            uint64_t a = (uint64_t)xs32() << 32 | xs32(), b = (uint64_t)xs32() << 32 | xs32();
            if (i % 3 == 0) b = (b & 0xFFFFFFFF00000000ull) | (uint32_t)a;
            if (i % 3 == 1) b = (a & 0xFFFFFFFF00000000ull) | (uint32_t)b;
            if (i % 7 == 0) b = a;
            if (eq64_ccmp(a, b) != (a == b)) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
