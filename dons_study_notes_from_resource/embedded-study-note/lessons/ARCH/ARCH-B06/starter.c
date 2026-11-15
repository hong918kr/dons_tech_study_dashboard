/* ARCH-B06 순차 논리: 플립플롭·카운터·LFSR
 *
 *   Part A  D 플립플롭(상승 에지) vs D 래치(레벨) · 2-phase 클럭 엔진 tick() · 4비트 동기 카운터(EN, 동기 CLR, TC)
 *   Part B  in-place 갱신 레이스: 교차 연결 swap, 8비트 SIPO 시프트 레지스터, "순서 트릭"의 한계
 *   Part C  16비트 LFSR — Galois(mask 0xB400) / Fibonacci(탭 16,14,13,11) 주기 65535 전수 검증,
 *           비최대 다항식(x^16+1, CRC-16-CCITT)의 짧은 주기, PRBS 자기동기(self-sync) 검사기
 *
 * 모델: 플립플롭 n 개의 출력 q[0..n-1] (각 0/1). 플립플롭 i 의 D 입력은 조합 논리 함수 dfn(q, i, in).
 * 클럭 에지 한 번 = ① 모든 D 를 "에지 직전의 q" 로 계산 ② 한꺼번에 q ← D  (two-phase: compute → commit)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c (또는 solution.c) -o x && ./x
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXFF 32u

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* q[] <-> 정수 (비트 i = q[i]) — 테스트 도우미 */
static uint32_t pack(const uint8_t *q, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint32_t)(q[i] & 1u) << i;
    return v;
}
static void unpack(uint8_t *q, unsigned n, uint32_t v) {
    for (unsigned i = 0; i < n; i++) q[i] = (uint8_t)((v >> i) & 1u);
}

/* ============================ Part A ============================ */
typedef struct { uint8_t q; uint8_t prev_clk; } dff_t;   /* 에지 트리거 D 플립플롭 */
typedef struct { uint8_t q; } dlatch_t;                   /* 레벨 감지 D 래치 (비교용, 주어짐) */

static void dlatch_eval(dlatch_t *l, unsigned en, unsigned d) {
    if (en & 1u) l->q = (uint8_t)(d & 1u);   /* en=1 동안 투명: D 가 그대로 Q 로 흐른다 */
}

/* TODO A1: 상승 에지(prev_clk 0 -> clk 1)에서만 q <- d. 어느 경우든 prev_clk <- clk 로 갱신. */
static void dff_eval(dff_t *ff, unsigned clk, unsigned d) {
    (void)ff; (void)clk; (void)d; /* TODO */
}

typedef unsigned (*dfn_t)(const uint8_t *q, unsigned i, unsigned in);

/* TODO A2: 2-phase 클럭 에지. ① d[i] = dfn(q, i, in) & 1 (모든 i; 이 동안 q 는 옛 값 그대로)
 *                           ② q[i] = d[i] 를 한꺼번에.  n <= MAXFF */
static void tick(uint8_t *q, unsigned n, dfn_t dfn, unsigned in) {
    (void)q; (void)n; (void)dfn; (void)in; /* TODO */
}

/* 비교용 잘못된 엔진 (주어짐): 계산하자마자 덮어쓴다 = Verilog always @(posedge clk) 안의 blocking '=' */
static void tick_in_place(uint8_t *q, unsigned n, dfn_t dfn, unsigned in) {
    for (unsigned i = 0; i < n; i++) q[i] = (uint8_t)(dfn(q, i, in) & 1u);
}
static void tick_in_place_rev(uint8_t *q, unsigned n, dfn_t dfn, unsigned in) {
    for (unsigned i = n; i-- > 0;) q[i] = (uint8_t)(dfn(q, i, in) & 1u);
}

/* 4비트 동기 업 카운터. in 비트0 = EN, 비트1 = CLR (동기 클리어, EN 보다 우선) */
#define CNT_EN  1u
#define CNT_CLR 2u

/* TODO A3: 비트 i 의 D 입력. CLR 이면 0. 아니면 아래 비트가 모두 1 이고 EN 일 때 토글:
 *          d_i = q_i XOR (EN AND q_0 AND ... AND q_{i-1})          (덧셈 연산자 쓰지 말 것) */
static unsigned counter4_d(const uint8_t *q, unsigned i, unsigned in) {
    (void)q; (void)i; (void)in;
    return 0; /* TODO */
}

/* TODO A4: 터미널 카운트(TC, carry out) = EN AND q3 q2 q1 q0. 다음 단 카운터의 EN 으로 쓴다. */
static unsigned counter4_tc(const uint8_t *q, unsigned in) {
    (void)q; (void)in;
    return 0; /* TODO */
}

/* ============================ Part B ============================ */
/* 회로 1 (주어짐): 플립플롭 2개 교차 연결 — d0 = q1, d1 = q0. 에지마다 두 값이 맞바뀌어야 한다. */
static unsigned swap_d(const uint8_t *q, unsigned i, unsigned in) { (void)in; return q[i ^ 1u]; }

/* TODO B1: 8비트 SIPO 시프트 레지스터 (왼쪽으로 민다). d[0] = 직렬 입력 (in & 1), d[i] = q[i-1]. */
static unsigned shift8_d(const uint8_t *q, unsigned i, unsigned in) {
    (void)q; (void)i; (void)in;
    return 0; /* TODO */
}

/* TODO B2: SPI 수신기처럼 byte 를 MSB 먼저 한 비트씩 shift8 에 넣고 (tick 8번) 병렬 값 pack(q,8) 을 돌려준다. */
static uint8_t sipo_receive(uint8_t byte) {
    (void)byte;
    return 0; /* TODO */
}

/* ============================ Part C ============================ */
typedef uint16_t (*step16_t)(uint16_t s, uint16_t param);

/* TODO C1: Galois(내부 XOR) 오른쪽 시프트 LFSR 한 스텝. 밀려 나가는 비트(lsb)가 1 이면 시프트 후 mask 를 XOR.
 *          mask 0xB400 = x^16 + x^14 + x^13 + x^11 + 1 */
static uint16_t galois_step(uint16_t s, uint16_t mask) {
    (void)mask;
    return s; /* TODO */
}

/* TODO C2: Fibonacci(외부 XOR) 오른쪽 시프트 LFSR 한 스텝. fb = parity(s & taps); s = (s >> 1) | (fb << 15).
 *          탭 16,14,13,11 -> taps = 비트 0,2,3,5 = 0x002D.  __builtin_parity 금지. */
static uint16_t fib_step(uint16_t s, uint16_t taps) {
    (void)taps;
    return s; /* TODO */
}

/* TODO C3: seed 에서 출발해 다시 seed 가 될 때까지의 스텝 수. limit 스텝 안에 안 돌아오면 0. */
static uint32_t lfsr_period(step16_t step, uint16_t param, uint16_t seed, uint32_t limit) {
    (void)step; (void)param; (void)seed; (void)limit;
    return 0; /* TODO */
}

/* TODO C4: 전수 조사 — 65536 상태를 사이클로 나눈다. 이미 본 상태(이번 걸음 포함)를 다시 밟고
 *          출발점이 아니면 순열(permutation)이 아니다 -> perm = false 로 즉시 반환.
 *          cycles = 사이클 수, longest = 가장 긴 사이클, fixed = 길이 1 사이클(고정점) 수. */
typedef struct { uint32_t cycles, longest, fixed; bool perm; } census_t;
static uint8_t g_seen[65536];

static census_t lfsr_census(step16_t step, uint16_t param) {
    census_t c = {0, 0, 0, true};
    (void)step; (void)param; (void)g_seen;
    return c; /* TODO */
}

/* TODO C5: 같은 Fibonacci LFSR 를 플립플롭 16개로: d[15] = q0^q2^q3^q5, d[i] = q[i+1] (i < 15) */
static unsigned fib16_d(const uint8_t *q, unsigned i, unsigned in) {
    (void)q; (void)i; (void)in;
    return 0; /* TODO */
}

/* PRBS 송신기 (주어짐): Fibonacci 상태의 lsb 를 매 클럭 내보낸다. */
static void prbs16_gen(uint16_t seed, uint8_t *out, uint32_t n) {
    uint16_t s = seed;
    for (uint32_t k = 0; k < n; k++) { out[k] = (uint8_t)(s & 1u); s = fib_step(s, 0x002D); }
}

/* TODO C6: 자기동기 PRBS 검사기. 올바른 수신열은 b[k] = b[k-16]^b[k-14]^b[k-13]^b[k-11] 을 만족한다.
 *          k < 16 은 검사기 채우기(lock)에만 쓰고, k >= 16 부터 예측과 다르면 오류 1 개. */
static uint32_t prbs16_check(const uint8_t *rx, uint32_t n) {
    (void)rx; (void)n;
    return 0; /* TODO */
}

/* ============================ 테스트 ============================ */
static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static uint8_t g_bits[4096];

int main(void) {
    /* ---------- Part A ---------- */
    dff_t ff = {0, 0};
    dlatch_t la = {0};
    dff_eval(&ff, 0, 1); dlatch_eval(&la, 0, 1);
    CHECK(ff.q == 0 && la.q == 0);                    /* clk=0: 둘 다 닫혀 있음 */
    dff_eval(&ff, 1, 1); dlatch_eval(&la, 1, 1);
    CHECK(ff.q == 1 && la.q == 1);                    /* 상승 에지: 둘 다 1 을 잡음 */
    dff_eval(&ff, 1, 0); dlatch_eval(&la, 1, 0);
    CHECK(ff.q == 1 && la.q == 0);                    /* clk=1 유지 중 D 변화: 래치는 따라가고 FF 는 유지 */
    dff_eval(&ff, 0, 1); dlatch_eval(&la, 0, 1);
    CHECK(ff.q == 1 && la.q == 0);                    /* 하강 에지: FF 는 반응하지 않는다 */
    dff_eval(&ff, 1, 0);
    CHECK(ff.q == 0);                                 /* 다음 상승 에지에 0 을 잡음 */

    uint8_t c[4] = {0};
    int ok = 1;
    for (unsigned t = 1; t <= 20; t++) {
        tick(c, 4, counter4_d, CNT_EN);
        if (pack(c, 4) != t % 16u) ok = 0;
    }
    CHECK(ok);                                        /* 0 -> 1 -> ... -> 15 -> 0 -> ... */
    ok = 1;                                           /* 진리표 전수: 16 상태 x 4 입력 조합 */
    for (uint32_t v = 0; v < 16; v++)
        for (unsigned in = 0; in < 4; in++) {
            unpack(c, 4, v);
            tick(c, 4, counter4_d, in);
            uint32_t want = (in & CNT_CLR) ? 0 : (in & CNT_EN) ? ((v + 1u) & 15u) : v;
            if (pack(c, 4) != want) ok = 0;
        }
    CHECK(ok);
    unpack(c, 4, 15); CHECK(counter4_tc(c, CNT_EN) == 1);
    CHECK(counter4_tc(c, 0) == 0);
    unpack(c, 4, 14); CHECK(counter4_tc(c, CNT_EN) == 0);

    uint8_t lo[4] = {0}, hi[4] = {0};                 /* 8비트 = 4비트 두 개를 TC 로 동기 연결 */
    for (unsigned t = 0; t < 300; t++) {
        unsigned in_hi = counter4_tc(lo, CNT_EN) ? CNT_EN : 0u;   /* 에지 직전의 lo 로 계산 */
        tick(lo, 4, counter4_d, CNT_EN);
        tick(hi, 4, counter4_d, in_hi);
    }
    CHECK(pack(hi, 4) * 16u + pack(lo, 4) == 300u % 256u);

    /* ---------- Part B ---------- */
    uint8_t s2[2];
    unpack(s2, 2, 0x1); tick(s2, 2, swap_d, 0);
    CHECK(pack(s2, 2) == 0x2);
    tick(s2, 2, swap_d, 0);
    CHECK(pack(s2, 2) == 0x1);
    unpack(s2, 2, 0x1); tick_in_place(s2, 2, swap_d, 0);
    CHECK(pack(s2, 2) == 0x0);                        /* 버그: 1 이 사라짐 */
    unpack(s2, 2, 0x1); tick_in_place_rev(s2, 2, swap_d, 0);
    CHECK(pack(s2, 2) == 0x3);                        /* 버그: 1 이 복제됨 — 어떤 순서도 정답이 아니다 */

    uint8_t r[8] = {0};
    tick(r, 8, shift8_d, 1);
    CHECK(pack(r, 8) == 0x01);
    memset(r, 0, sizeof r);
    tick_in_place(r, 8, shift8_d, 1);
    CHECK(pack(r, 8) == 0xFF);                        /* 버그: 한 클럭에 8칸을 다 통과 */
    uint8_t a[8] = {0}, b[8] = {0};
    ok = 1;
    for (unsigned t = 0; t < 200; t++) {              /* 순수 시프트 체인은 역순 in-place 가 우연히 맞다 */
        unsigned bit = xorshift32() & 1u;
        tick(a, 8, shift8_d, bit);
        tick_in_place_rev(b, 8, shift8_d, bit);
        if (pack(a, 8) != pack(b, 8)) ok = 0;
    }
    CHECK(ok && pack(a, 8) != 0);
    CHECK(sipo_receive(0xB0) == 0xB0);
    CHECK(sipo_receive(0x5A) == 0x5A);
    CHECK(sipo_receive(0x81) == 0x81);

    /* ---------- Part C ---------- */
    CHECK(galois_step(0xACE1, 0xB400) == 0xE270);
    CHECK(galois_step(0x0000, 0xB400) == 0x0000);     /* 잠김 상태(lock-up): 0 은 영원히 0 */
    CHECK(fib_step(0xACE1, 0x002D) == 0x5670);
    CHECK(fib_step(0x0001, 0x002D) == 0x8000);
    CHECK(lfsr_period(galois_step, 0xB400, 0x0001, 70000) == 65535u);
    CHECK(lfsr_period(fib_step, 0x002D, 0xACE1, 70000) == 65535u);
    CHECK(lfsr_period(galois_step, 0x8000, 0x0001, 70000) == 16u);   /* x^16 + 1 = 단순 회전 */

    census_t cg = lfsr_census(galois_step, 0xB400);
    CHECK(cg.perm && cg.cycles == 2 && cg.longest == 65535u && cg.fixed == 1);
    census_t cf = lfsr_census(fib_step, 0x002D);
    CHECK(cf.perm && cf.cycles == 2 && cf.longest == 65535u && cf.fixed == 1);
    census_t cr = lfsr_census(galois_step, 0x8000);
    CHECK(cr.perm && cr.longest == 16u && cr.cycles == 4116u);
    census_t cc = lfsr_census(galois_step, 0x8810);   /* CRC-16-CCITT x^16+x^12+x^5+1 = (x+1) x 15차 */
    CHECK(cc.perm && cc.cycles == 4 && cc.longest == 32767u && cc.fixed == 2);
    census_t cn = lfsr_census(galois_step, 0x3400);   /* x^16 항(mask bit15)이 빠짐 -> 순열이 아님 */
    CHECK(!cn.perm);

    uint8_t f[16];
    uint16_t w = 0xACE1;
    unpack(f, 16, w);
    ok = 1;
    for (unsigned t = 0; t < 1000; t++) {             /* 플립플롭 16개 모델 == 워드 모델 */
        tick(f, 16, fib16_d, 0);
        w = fib_step(w, 0x002D);
        if (pack(f, 16) != w) ok = 0;
    }
    CHECK(ok);
    unpack(f, 16, 0xACE1); tick_in_place(f, 16, fib16_d, 0);
    CHECK(pack(f, 16) == 0xD670);                     /* 정답 0x5670 — 피드백이 이미 민 값을 읽음 */
    unpack(f, 16, 0xACE1); tick_in_place_rev(f, 16, fib16_d, 0);
    CHECK(pack(f, 16) == 0x0000);                     /* 피드백 비트가 16칸 전부로 번짐 */

    prbs16_gen(0xACE1, g_bits, 4096);
    CHECK(prbs16_check(g_bits, 4096) == 0);
    g_bits[100] ^= 1u;
    CHECK(prbs16_check(g_bits, 4096) == 5);           /* 단일 비트 오류 1개 -> 오류 카운트 5 (항 5개) */
    g_bits[1000] ^= 1u;
    CHECK(prbs16_check(g_bits, 4096) == 10);
    g_bits[101] ^= 1u;                                /* 100,101 연속 오류: k=114 에서 두 오류가 상쇄 */
    g_bits[1000] ^= 1u;
    CHECK(prbs16_check(g_bits, 4096) == 8);
    g_bits[100] ^= 1u; g_bits[101] ^= 1u;
    CHECK(prbs16_check(g_bits + 777, 4096 - 777) == 0);   /* 중간부터 받아도 16비트 뒤 lock */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
