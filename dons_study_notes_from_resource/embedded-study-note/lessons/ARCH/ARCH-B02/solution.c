/* ARCH-B02 ALU 플래그 계산기: N Z C V
 *
 * Arm 방식 NZCV 를 순수 C 로 모델링한다 (inline asm 없음 — 하드웨어 대조는 demo_hw_flags.c).
 *   nzcv 4비트 = N<<3 | Z<<2 | C<<1 | V   (AArch64 `mrs x, nzcv` 의 비트 31..28 과 같은 순서)
 *   SUB a,b = a + ~b + 1  → C = 1 이면 borrow 없음 (Arm). x86 CF 는 borrow 자체 (반대).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (정답: solution.c)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { FLAG_V = 1u << 0, FLAG_C = 1u << 1, FLAG_Z = 1u << 2, FLAG_N = 1u << 3 };

typedef struct {
    uint32_t res;   /* 결과 (width 비트로 잘림) */
    uint8_t  nzcv;  /* N<<3 | Z<<2 | C<<1 | V   */
} alu_t;

/* ---------------- Part A: ADD / ADC / SUB / SBC 와 NZCV (width = 1..32) ---------------- */

/* width 비트 마스크. width == 32 도 UB 없이 (64비트로 시프트) */
static inline uint32_t wmask(unsigned width) { return (uint32_t)((UINT64_C(1) << width) - 1u); }

/* 가산기 하나: a + b + cin. 다른 연산은 전부 이걸 부른다. */
static alu_t alu_adc(uint32_t a, uint32_t b, unsigned cin, unsigned width) {
    uint32_t m = wmask(width);
    a &= m; b &= m;
    uint64_t sum = (uint64_t)a + b + (cin & 1u);        /* 64비트라 캐리가 안 사라진다 */
    uint32_t r   = (uint32_t)sum & m;
    unsigned n = (r >> (width - 1)) & 1u;               /* 결과의 MSB              */
    unsigned z = (r == 0);
    unsigned c = (unsigned)(sum >> width) & 1u;         /* MSB 밖으로 나간 캐리    */
    unsigned v = (((a ^ r) & (b ^ r)) >> (width - 1)) & 1u; /* 같은 부호 + → 다른 부호 */
    alu_t o = { r, (uint8_t)(n << 3 | z << 2 | c << 1 | v) };
    return o;
}
static alu_t alu_add(uint32_t a, uint32_t b, unsigned width) { return alu_adc(a, b, 0, width); }

/* SUB = a + ~b + 1 (같은 가산기). C = NOT borrow */
static alu_t alu_sub(uint32_t a, uint32_t b, unsigned width) {
    return alu_adc(a, ~b, 1, width);
}
/* SBC = a + ~b + C  (= a - b - !C). C 입력이 0 이면 "앞에서 빌려 갔다" */
static alu_t alu_sbc(uint32_t a, uint32_t b, unsigned cin, unsigned width) {
    return alu_adc(a, ~b, cin, width);
}

/* ---------------- Part B: 조건 코드 (Arm 인코딩 순서: cc ^ 1 = 반대 조건) ---------------- */
typedef enum {
    CC_EQ, CC_NE, CC_HS, CC_LO, CC_MI, CC_PL, CC_VS, CC_VC,
    CC_HI, CC_LS, CC_GE, CC_LT, CC_GT, CC_LE, CC_AL
} cond_t;
#define CC_CS CC_HS
#define CC_CC CC_LO

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

/* x86 EFLAGS 로 번역 (SUB/CMP 직후 기준): CF=bit0, ZF=bit6, SF=bit7, OF=bit11.
 * x86 의 CF 는 borrow 그 자체 → Arm C 의 반대. */
enum { X86_CF = 1u << 0, X86_ZF = 1u << 6, X86_SF = 1u << 7, X86_OF = 1u << 11 };
static uint32_t arm_sub_to_x86(uint8_t nzcv) {
    uint32_t f = 0;
    if (!(nzcv & FLAG_C)) f |= X86_CF;
    if (nzcv & FLAG_Z)    f |= X86_ZF;
    if (nzcv & FLAG_N)    f |= X86_SF;
    if (nzcv & FLAG_V)    f |= X86_OF;
    return f;
}

/* ---------------- Part C: 32비트 코어에서 64비트 연산 (Cortex-M ADDS/ADC, SUBS/SBCS) ---------------- */

/* ADDS lo ; ADCS hi. *nzcv = 마지막 ADCS 의 플래그 (Z 는 hi 워드만 본다!) */
static uint64_t add64_chain(uint64_t a, uint64_t b, uint8_t *nzcv) {
    alu_t lo = alu_add((uint32_t)a, (uint32_t)b, 32);
    alu_t hi = alu_adc((uint32_t)(a >> 32), (uint32_t)(b >> 32), (lo.nzcv & FLAG_C) ? 1u : 0u, 32);
    *nzcv = hi.nzcv;
    return (uint64_t)hi.res << 32 | lo.res;
}

/* CMP lo ; SBCS hi  → 64비트 비교 플래그. N,C,V 는 64비트 기준으로 정확, Z 는 hi 만. */
static uint8_t cmp64_flags(uint64_t a, uint64_t b) {
    alu_t lo = alu_sub((uint32_t)a, (uint32_t)b, 32);
    alu_t hi = alu_sbc((uint32_t)(a >> 32), (uint32_t)(b >> 32), (lo.nzcv & FLAG_C) ? 1u : 0u, 32);
    return hi.nzcv;
}

/* wrap-safe 틱 비교: a 가 b 보다 "뒤"(나중)인가. SUBS b,a 의 N (= MI). LT 가 아니다! */
static bool tick_after(uint32_t a, uint32_t b) {
    return cond_holds(CC_MI, alu_sub(b, a, 32).nzcv);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                      /* 고정 시드 xorshift32 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define NZCV(n, z, c, v) (uint8_t)((n) << 3 | (z) << 2 | (c) << 1 | (v))

/* 독립 오라클: 넓은 정수로 직접 계산 (가산기 공식을 쓰지 않는다) */
static int64_t sx(uint32_t x, unsigned w) {             /* w 비트 부호 해석 */
    int64_t v = (int64_t)(x & wmask(w));
    return (v >> (w - 1)) & 1 ? v - ((int64_t)1 << w) : v;
}
static uint8_t oracle(int64_t ures, int64_t sres, unsigned w, bool is_sub) {
    int64_t lim = (int64_t)1 << w;                      /* 2^w */
    uint32_t r = (uint32_t)((uint64_t)ures & wmask(w));
    unsigned c = is_sub ? (ures >= 0) : (ures >= lim);  /* sub: borrow 없음 = C */
    unsigned v = sres < -(lim / 2) || sres > lim / 2 - 1;
    return NZCV((r >> (w - 1)) & 1u, r == 0, c, v);
}

static const uint32_t edge32[] = { 0u, 1u, 2u, 0x7FFFFFFEu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
                                   0xFFFFFFFEu, 0xFFFFFFFFu, 0x40000000u, 0xC0000000u, 0x0000FFFFu };
#define NEDGE (sizeof edge32 / sizeof edge32[0])

/* 비교 a?b 의 정답 (C 비교 연산자) vs cond_holds(SUB 플래그) 를 하나라도 틀리면 false */
static bool conds_match(uint32_t a, uint32_t b, unsigned w) {
    alu_t s = alu_sub(a, b, w);
    uint32_t ua = a & wmask(w), ub = b & wmask(w);
    int64_t sa = sx(a, w), sb = sx(b, w), d = sa - sb;
    bool ok = true;
    ok &= cond_holds(CC_EQ, s.nzcv) == (ua == ub);
    ok &= cond_holds(CC_NE, s.nzcv) == (ua != ub);
    ok &= cond_holds(CC_HS, s.nzcv) == (ua >= ub);
    ok &= cond_holds(CC_LO, s.nzcv) == (ua <  ub);
    ok &= cond_holds(CC_HI, s.nzcv) == (ua >  ub);
    ok &= cond_holds(CC_LS, s.nzcv) == (ua <= ub);
    ok &= cond_holds(CC_GE, s.nzcv) == (sa >= sb);
    ok &= cond_holds(CC_LT, s.nzcv) == (sa <  sb);
    ok &= cond_holds(CC_GT, s.nzcv) == (sa >  sb);
    ok &= cond_holds(CC_LE, s.nzcv) == (sa <= sb);
    ok &= cond_holds(CC_MI, s.nzcv) == (((s.res >> (w - 1)) & 1u) != 0);
    ok &= cond_holds(CC_PL, s.nzcv) == (((s.res >> (w - 1)) & 1u) == 0);
    ok &= cond_holds(CC_VS, s.nzcv) == (d < -((int64_t)1 << (w - 1)) || d > ((int64_t)1 << (w - 1)) - 1);
    ok &= cond_holds(CC_VC, s.nzcv) != cond_holds(CC_VS, s.nzcv);
    ok &= cond_holds(CC_AL, s.nzcv);
    return ok;
}

int main(void) {
    /* ---- Part A: 손계산 표 (8비트) ---- */
    CHECK(alu_add(0x7F, 0x01, 8).res == 0x80 && alu_add(0x7F, 0x01, 8).nzcv == NZCV(1, 0, 0, 1));
    CHECK(alu_add(0xFF, 0x01, 8).res == 0x00 && alu_add(0xFF, 0x01, 8).nzcv == NZCV(0, 1, 1, 0));
    CHECK(alu_add(0x80, 0x80, 8).res == 0x00 && alu_add(0x80, 0x80, 8).nzcv == NZCV(0, 1, 1, 1));
    CHECK(alu_sub(0x05, 0x03, 8).res == 0x02 && alu_sub(0x05, 0x03, 8).nzcv == NZCV(0, 0, 1, 0));
    CHECK(alu_sub(0x03, 0x05, 8).res == 0xFE && alu_sub(0x03, 0x05, 8).nzcv == NZCV(1, 0, 0, 0));
    CHECK(alu_sub(0x80, 0x01, 8).res == 0x7F && alu_sub(0x80, 0x01, 8).nzcv == NZCV(0, 0, 1, 1));
    CHECK(alu_adc(0xFF, 0x00, 1, 8).nzcv == NZCV(0, 1, 1, 0));
    CHECK(alu_sbc(0x00, 0x00, 0, 8).res == 0xFF && alu_sbc(0x00, 0x00, 0, 8).nzcv == NZCV(1, 0, 0, 0));
    /* 32비트 경계 */
    CHECK(alu_add(0x7FFFFFFFu, 1, 32).nzcv == NZCV(1, 0, 0, 1));
    CHECK(alu_add(0xFFFFFFFFu, 1, 32).res == 0 && alu_add(0xFFFFFFFFu, 1, 32).nzcv == NZCV(0, 1, 1, 0));
    CHECK(alu_sub(0x80000000u, 1, 32).nzcv == NZCV(0, 0, 1, 1));
    CHECK(alu_sub(0, 1, 32).res == 0xFFFFFFFFu && alu_sub(0, 1, 32).nzcv == NZCV(1, 0, 0, 0));

    /* ---- Part A: 8비트 전수 (65536쌍 × cin 2 × ADD/ADC/SUB/SBC) vs 독립 오라클 ---- */
    {
        long bad = 0;
        for (uint32_t a = 0; a < 256; a++)
            for (uint32_t b = 0; b < 256; b++)
                for (unsigned cin = 0; cin < 2; cin++) {
                    alu_t x = alu_adc(a, b, cin, 8);
                    if (x.nzcv != oracle((int64_t)a + b + cin, sx(a, 8) + sx(b, 8) + cin, 8, false)) bad++;
                    alu_t y = alu_sbc(a, b, cin, 8);  /* a - b - !cin */
                    if (y.nzcv != oracle((int64_t)a - b - !cin, sx(a, 8) - sx(b, 8) - !cin, 8, true)) bad++;
                    if (y.res != ((a - b - !cin) & 0xFFu)) bad++;
                }
        CHECK(bad == 0);
    }
    /* ---- Part A: 32비트 경계값 전쌍 + 무작위 200000쌍 vs 독립 오라클 ---- */
    {
        long bad = 0;
        for (unsigned i = 0; i < NEDGE * NEDGE + 200000u; i++) {
            uint32_t a = i < NEDGE * NEDGE ? edge32[i / NEDGE] : xs32();
            uint32_t b = i < NEDGE * NEDGE ? edge32[i % NEDGE] : xs32();
            unsigned cin = i & 1u;
            if (alu_adc(a, b, cin, 32).nzcv != oracle((int64_t)a + b + cin, sx(a, 32) + sx(b, 32) + cin, 32, false)) bad++;
            if (alu_sub(a, b, 32).nzcv != oracle((int64_t)a - b, sx(a, 32) - sx(b, 32), 32, true)) bad++;
            if (alu_sbc(a, b, cin, 32).nzcv != oracle((int64_t)a - b - !cin, sx(a, 32) - sx(b, 32) - !cin, 32, true)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- Part B: 조건 코드 = C 비교 연산자 (8비트 전수 + 32비트 경계/무작위) ---- */
    {
        long bad = 0;
        for (uint32_t a = 0; a < 256; a++)
            for (uint32_t b = 0; b < 256; b++) bad += !conds_match(a, b, 8);
        CHECK(bad == 0);
        bad = 0;
        for (unsigned i = 0; i < NEDGE * NEDGE + 200000u; i++) {
            uint32_t a = i < NEDGE * NEDGE ? edge32[i / NEDGE] : xs32();
            uint32_t b = i < NEDGE * NEDGE ? edge32[i % NEDGE] : xs32();
            bad += !conds_match(a, b, 32);
        }
        CHECK(bad == 0);
    }
    /* ---- Part B: 면접 함정 ---- */
    {
        uint8_t f = alu_sub(0x80, 0x01, 8).nzcv;        /* -128 - 1 → 127 (V=1) */
        CHECK(cond_holds(CC_LT, f) && !cond_holds(CC_MI, f)); /* LT 참, MI 거짓 */
        f = alu_sub(0x1234, 0, 32).nzcv;                 /* CMP x, #0 → C = 1 (borrow 없음) */
        CHECK(cond_holds(CC_HS, f) && !cond_holds(CC_HI, alu_sub(0, 0, 32).nzcv));
        CHECK(alu_sub(0, 0x80000000u, 32).nzcv == NZCV(1, 0, 0, 1));   /* NEG INT_MIN → V */
        CHECK(cond_holds(CC_GT, NZCV(1, 0, 0, 1)) && !cond_holds(CC_GT, NZCV(1, 1, 0, 1)));
        CHECK(cond_holds(CC_HI, NZCV(0, 0, 1, 0)) && !cond_holds(CC_HI, NZCV(0, 1, 1, 0)));
        /* x86 번역: CMP 5,3 → CF=0 / CMP 3,5 → CF=1 / CMP 0x80000000,1 → OF=1 */
        CHECK(arm_sub_to_x86(alu_sub(5, 3, 32).nzcv) == 0);
        CHECK(arm_sub_to_x86(alu_sub(3, 5, 32).nzcv) == (X86_CF | X86_SF));
        CHECK(arm_sub_to_x86(alu_sub(7, 7, 32).nzcv) == X86_ZF);
        CHECK(arm_sub_to_x86(alu_sub(0x80000000u, 1, 32).nzcv) == X86_OF);
    }

    /* ---- Part C: 64비트 ADD/CMP 체인 vs 네이티브 uint64_t/int64_t ---- */
    {
        uint8_t f;
        CHECK(add64_chain(0x00000000FFFFFFFFull, 1, &f) == 0x0000000100000000ull && f == NZCV(0, 0, 0, 0));
        CHECK(add64_chain(0xFFFFFFFFFFFFFFFFull, 1, &f) == 0 && (f & FLAG_C) && (f & FLAG_Z));
        CHECK(add64_chain(0x7FFFFFFFFFFFFFFFull, 1, &f) == 0x8000000000000000ull && f == NZCV(1, 0, 0, 1));
        long bad = 0;
        for (int i = 0; i < 100000; i++) {
            uint64_t a = (uint64_t)xs32() << 32 | xs32(), b = (uint64_t)xs32() << 32 | xs32();
            if (i % 4 == 0) b = (b & 0xFFFFFFFF00000000ull) | (uint32_t)a;  /* lo 같게 */
            if (i % 4 == 1) b = (a & 0xFFFFFFFF00000000ull) | (uint32_t)b;  /* hi 같게 */
            if (add64_chain(a, b, &f) != a + b) bad++;
            if (((f & FLAG_C) != 0) != (a + b < a)) bad++;                 /* 64비트 캐리 */
            uint8_t c = cmp64_flags(a, b);
            if (cond_holds(CC_LO, c) != (a < b)) bad++;
            if (cond_holds(CC_HS, c) != (a >= b)) bad++;
            if (cond_holds(CC_LT, c) != ((int64_t)a < (int64_t)b)) bad++;
            if (cond_holds(CC_GE, c) != ((int64_t)a >= (int64_t)b)) bad++;
        }
        CHECK(bad == 0);
        /* 함정: 체인의 Z 는 hi 워드만 → EQ 로 쓰면 틀린다 */
        CHECK(cond_holds(CC_EQ, cmp64_flags(0x0000000500000001ull, 0x0000000500000000ull)) == true);
    }
    /* ---- Part C: wrap-safe tick 비교 ---- */
    CHECK(tick_after(0x00000010u, 0xFFFFFFF0u) == true);   /* 랩어라운드 넘어 0x20 틱 뒤 */
    CHECK(tick_after(0xFFFFFFF0u, 0x00000010u) == false);
    CHECK(tick_after(100, 100) == false && tick_after(101, 100) == true);
    CHECK(tick_after(0x80000001u, 1u) && tick_after(1u, 0x80000001u)); /* 정확히 2^31 차이: 둘 다 "뒤" → 모호 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
