/* SIV-A10 추가 연습 — 한 번에 검증
 *   연습 1  시그니처 = 다항식 나머지: GF(2)[x] 곱셈으로 시뮬레이션과 같은 값을 "닫힌 식"으로 계산
 *   연습 2  MISR 시드를 바꿨을 때 golden 을 재시뮬레이션 없이 고치기 (선형성 + x^L mod P)
 *   연습 3  Complete LFSR(de Bruijn): 주기 2^n, all-0 패턴을 넣어야만 잡히는 결함
 *   연습 4  두 개의 MISR: 서로소 다항식이면 aliasing 2^-(n1+n2), 같은 다항식이면 이득 없음
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs64(void) { uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x; }
static uint32_t low_mask(int n) { return (1u << n) - 1u; }
static uint32_t parity32(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1u; }

#define P4  0x13u
#define P4B 0x19u
#define P16 0x1100Bu

static uint32_t galois_step(uint32_t s, uint32_t poly, int n) { s <<= 1; if (s & (1u << n)) s ^= poly; return s; }
static uint32_t misr_step(uint32_t sig, uint32_t r, uint32_t poly, int n) { return galois_step(sig, poly, n) ^ r; }

/* ---- 연습 1: 곱셈 / 거듭제곱 ---- */
static uint32_t gf_mul(uint32_t a, uint32_t b, uint32_t poly, int n) {
    uint32_t r = 0;
    for (int i = n - 1; i >= 0; i--) {
        r = galois_step(r, poly, n);
        if ((b >> i) & 1u) r ^= a;
    }
    return r;
}
static uint32_t x_pow(uint64_t k, uint32_t poly, int n) {       /* x^k mod P */
    uint32_t base = n > 1 ? 2u : galois_step(1, poly, n), r = 1;
    while (k) {
        if (k & 1u) r = gf_mul(r, base, poly, n);
        base = gf_mul(base, base, poly, n);
        k >>= 1;
    }
    return r;
}
/* 시드 0 에서 e[0..L-1] 를 넣은 시그니처를 닫힌 식으로: Σ e[t](x) · x^(L-1-t) mod P */
static uint32_t sig_closed(const uint32_t *e, int L, uint32_t poly, int n) {
    uint32_t s = 0;
    for (int t = 0; t < L; t++)
        if (e[t]) s ^= gf_mul(e[t], x_pow((uint64_t)(L - 1 - t), poly, n), poly, n);
    return s;
}
static uint32_t sig_sim(uint32_t seed, const uint32_t *e, int L, uint32_t poly, int n) {
    uint32_t s = seed;
    for (int t = 0; t < L; t++) s = misr_step(s, e[t], poly, n);
    return s;
}

/* ---- 연습 2: 시드 보정 ---- */
static uint32_t golden_with_seed(uint32_t golden0, uint32_t seed, int L, uint32_t poly, int n) {
    return golden0 ^ gf_mul(seed, x_pow((uint64_t)L, poly, n), poly, n);
}

/* ---- 연습 3: Complete LFSR (Fibonacci + all-0 삽입) ---- */
static uint32_t fib_tap(uint32_t poly, int n) {
    uint32_t tap = 0;
    for (int i = 0; i < n; i++) if ((poly >> i) & 1u) tap |= 1u << (n - 1 - i);
    return tap;
}
static uint32_t complete_step(uint32_t s, uint32_t poly, int n) {
    uint32_t fb = parity32(s & fib_tap(poly, n));
    if ((s & low_mask(n - 1)) == 0) fb ^= 1u;                 /* 낮은 n-1 비트가 0 이면 궤환 반전 */
    return ((s << 1) | fb) & low_mask(n);
}
/* NOR(n) 의 출력 s-a-0: good 출력이 1 인 패턴(all-0)에서만 보인다 */
static bool nor_sa0_detected(uint32_t patterns, uint32_t seed, uint32_t poly, int n, bool complete) {
    uint32_t s = seed;
    for (uint32_t i = 0; i < patterns; i++) {
        if (s == 0) return true;                               /* good=1, faulty=0 → 검출 */
        s = complete ? complete_step(s, poly, n) : ((s << 1) | parity32(s & fib_tap(poly, n))) & low_mask(n);
    }
    return false;
}

/* ---- 연습 4: 이중 MISR ---- */
static uint64_t alias_dual(uint32_t p1, int n1, uint32_t p2, int n2, int L) {
    uint64_t cnt = 0;
    for (uint64_t E = 1; E < (1ull << L); E++) {
        uint32_t s1 = 0, s2 = 0;
        for (int t = 0; t < L; t++) {
            uint32_t b = (uint32_t)(E >> t) & 1u;
            s1 = misr_step(s1, b, p1, n1);
            s2 = misr_step(s2, b, p2, n2);
        }
        if (s1 == 0 && s2 == 0) cnt++;
    }
    return cnt;
}
static uint32_t clmul(uint32_t a, uint32_t b) { uint32_t r = 0; for (; b; b >>= 1, a <<= 1) if (b & 1u) r ^= a; return r; }

int main(void) {
    uint32_t e[256];
    /* 연습 1: 닫힌 식 == 시뮬레이션 */
    for (int rep = 0; rep < 50; rep++) {
        int L = 1 + (int)(xs64() % 200);
        for (int t = 0; t < L; t++) e[t] = (uint32_t)xs64() & 0x3F;
        CHECK(sig_closed(e, L, P16, 16) == sig_sim(0, e, L, P16, 16));
    }
    CHECK(x_pow(65535, P16, 16) == 1);                       /* 원시: x 의 위수 = 2^16-1 */
    CHECK(x_pow(32767, P16, 16) != 1);
    CHECK(x_pow(15, P4, 4) == 1 && x_pow(5, P4, 4) != 1);
    CHECK(x_pow(5, 0x1F, 4) == 1);                           /* 비원시 x^4+x^3+x^2+x+1: 위수 5 */
    /* 연습 2: 시드 보정 */
    for (int rep = 0; rep < 50; rep++) {
        int L = 1 + (int)(xs64() % 200);
        uint32_t seed = (uint32_t)xs64() & 0xFFFF;
        for (int t = 0; t < L; t++) e[t] = (uint32_t)xs64() & 0x3F;
        CHECK(golden_with_seed(sig_sim(0, e, L, P16, 16), seed, L, P16, 16) == sig_sim(seed, e, L, P16, 16));
    }
    /* 연습 3: complete LFSR */
    for (int n = 3; n <= 12; n++) {
        static const uint32_t polys[] = { 0xB, 0x13, 0x25, 0x43, 0x83, 0x11D, 0x211, 0x409, 0x805, 0x1053 };
        uint32_t p = polys[n - 3], s = 1, cnt = 0;
        static uint8_t seen[4096];
        memset(seen, 0, sizeof seen);
        do { seen[s]++; s = complete_step(s, p, n); cnt++; } while (s != 1 && cnt <= (1u << n));
        bool all = cnt == (1u << n);
        for (uint32_t v = 0; v < (1u << n); v++) all = all && seen[v] == 1;
        CHECK(all);
    }
    CHECK(!nor_sa0_detected(255, 1, 0x11D, 8, false));        /* 8비트 LFSR 한 주기: all-0 은 안 나온다 */
    CHECK(nor_sa0_detected(256, 1, 0x11D, 8, true));          /* complete: 256 패턴 안에 나온다 */
    /* 연습 4: 이중 MISR — L=12, 4+4 비트 */
    {
        uint64_t single = alias_dual(P4, 4, P4, 4, 12);       /* 같은 다항식: 두 레지스터가 똑같아 이득 0 */
        uint64_t dual = alias_dual(P4, 4, P4B, 4, 12);        /* 서로소 */
        uint32_t prod = clmul(P4, P4B);
        uint64_t cnt8 = 0;
        for (uint64_t E = 1; E < 4096; E++) {
            uint32_t s = 0;
            for (int t = 0; t < 12; t++) s = misr_step(s, (uint32_t)(E >> t) & 1u, prod, 8);
            cnt8 += s == 0;
        }
        printf("dual: same_poly=%llu coprime=%llu product_8bit=%llu expect 255/15/15\n",
               (unsigned long long)single, (unsigned long long)dual, (unsigned long long)cnt8);
        CHECK(single == 255 && dual == 15 && cnt8 == 15);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
