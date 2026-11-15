/* ARCH-B04 게이트 수준 가산기 시뮬레이터
 *
 *   Part A  반가산기 → 전가산기 → n비트 리플 캐리 가산기 (게이트 함수만, '+' 금지)
 *   Part B  4비트 블록 캐리 룩어헤드(CLA) 가산기 — 블록 사이는 캐리가 리플
 *   Part C  ALU 슬라이스(ADD/ADC/SUB/SBC) + 바이트 단위 다중 정밀도 덧셈 (8비트 MCU 의 ADD+ADC 체인)
 *
 * 신호(sig) 하나 = 값(0/1) + 도착 시각 두 가지:
 *   t  : 단위 지연 모델 — 게이트 하나 = 1 단계, 입력 수(fan-in)와 무관
 *   t2 : 2입력 게이트 트리로 바꿨을 때의 단계 (도착이 늦은 입력을 트리 위쪽에 두는 최적 배치)
 * 게이트를 부를 때마다 카운터가 올라가므로, 덧셈 한 번을 돌리면 게이트 수와 임계 경로 깊이가 같이 나온다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdint.h>
#include <stdio.h>

/* ======================= 게이트 모델 (주어짐) ======================= */
typedef struct { uint8_t v, t, t2; } sig;

static unsigned long g_gates;    /* 게이트 개수 */
static unsigned long g_gates2;   /* 2입력 환산 개수: AND/OR/XOR = fan-in - 1, NOT = 1 */

enum { OP_AND, OP_OR, OP_XOR };

static sig gate(int op, const sig *in, int n) {
    uint8_t v = (op == OP_AND) ? 1 : 0, t = 0, a2[8];
    for (int i = 0; i < n; i++) {
        if (op == OP_AND) v &= in[i].v;
        else if (op == OP_OR) v |= in[i].v;
        else v ^= in[i].v;
        if (in[i].t > t) t = in[i].t;
        a2[i] = in[i].t2;
    }
    /* 2입력 트리 환산: 가장 일찍 도착한 둘을 2입력 게이트로 묶기(max+1)를 하나 남을 때까지 = 최적 트리 */
    for (int m = n; m > 1; m--) {
        for (int i = 0; i < m; i++)              /* 작은 두 개를 앞으로 (n <= 8 이라 단순 정렬) */
            for (int j = i + 1; j < m; j++)
                if (a2[j] < a2[i]) { uint8_t x = a2[i]; a2[i] = a2[j]; a2[j] = x; }
        a2[0] = (uint8_t)((a2[1] > a2[0] ? a2[1] : a2[0]) + 1);
        a2[1] = a2[m - 1];
    }
    uint8_t t2 = a2[0];
    g_gates++;
    g_gates2 += (unsigned long)(n - 1);
    return (sig){ v, (uint8_t)(t + 1), t2 };
}
static sig NOT(sig a) { g_gates++; g_gates2++; return (sig){ (uint8_t)(!a.v), (uint8_t)(a.t + 1), (uint8_t)(a.t2 + 1) }; }

#define NARGS_(...) ((int)(sizeof((sig[]){__VA_ARGS__}) / sizeof(sig)))
#define AND(...) gate(OP_AND, (sig[]){__VA_ARGS__}, NARGS_(__VA_ARGS__))
#define OR(...)  gate(OP_OR,  (sig[]){__VA_ARGS__}, NARGS_(__VA_ARGS__))
#define XOR(...) gate(OP_XOR, (sig[]){__VA_ARGS__}, NARGS_(__VA_ARGS__))

static sig lit(unsigned v) { return (sig){ (uint8_t)(v & 1u), 0, 0 }; }   /* 시각 0 에 도착하는 입력 */
static void unpack(uint64_t x, sig out[], int n) { for (int i = 0; i < n; i++) out[i] = lit((unsigned)(x >> i)); }
static uint64_t pack(const sig s[], int n) { uint64_t x = 0; for (int i = 0; i < n; i++) x |= (uint64_t)s[i].v << i; return x; }
static int depth(const sig s[], int n) { int d = 0; for (int i = 0; i < n; i++) if (s[i].t > d) d = s[i].t; return d; }
static int depth2(const sig s[], int n) { int d = 0; for (int i = 0; i < n; i++) if (s[i].t2 > d) d = s[i].t2; return d; }

#define MAXW 64

/* ======================= Part A — 반가산기 → 전가산기 → 리플 ======================= */
/* s = a XOR b, c = a AND b */
static void half_add(sig a, sig b, sig *s, sig *c) {
    *s = XOR(a, b);
    *c = AND(a, b);
}

/* 반가산기 2개 + OR 1개. s = a^b^cin, cout = ab + cin(a^b) */
static void full_add(sig a, sig b, sig cin, sig *s, sig *cout) {
    sig s1, c1, c2;
    half_add(a, b, &s1, &c1);
    half_add(s1, cin, s, &c2);
    *cout = OR(c1, c2);
}

/* n비트 리플 캐리: 비트 i 의 carry-out 이 비트 i+1 의 carry-in. 마지막 carry-out 을 돌려준다. */
static sig ripple_add(const sig a[], const sig b[], sig cin, sig s[], int n) {
    sig c = cin;
    for (int i = 0; i < n; i++) full_add(a[i], b[i], c, &s[i], &c);
    return c;
}

/* ======================= Part B — 캐리 룩어헤드 ======================= */
/* 4비트 룩어헤드 유닛. 입력 g[0..3], p[0..3], c0.
 * 출력 c[0..4] (c[0] = c0, c[i+1] = g_i + p_i c_i 를 펼친 2단 AND-OR 식).
 * G, P 가 NULL 이 아니면 블록 generate / propagate 도 만든다 (2단 CLA 용; NULL 이면 게이트를 안 만든다). */
static void cla4(const sig g[4], const sig p[4], sig c0, sig c[5], sig *G, sig *P) {
    c[0] = c0;
    c[1] = OR(g[0], AND(p[0], c0));
    c[2] = OR(g[1], AND(p[1], g[0]), AND(p[1], p[0], c0));
    c[3] = OR(g[2], AND(p[2], g[1]), AND(p[2], p[1], g[0]), AND(p[2], p[1], p[0], c0));
    c[4] = OR(g[3], AND(p[3], g[2]), AND(p[3], p[2], g[1]), AND(p[3], p[2], p[1], g[0]),
              AND(p[3], p[2], p[1], p[0], c0));
    if (G) *G = OR(g[3], AND(p[3], g[2]), AND(p[3], p[2], g[1]), AND(p[3], p[2], p[1], g[0]));
    if (P) *P = AND(p[3], p[2], p[1], p[0]);
}

/* n비트 (4의 배수) CLA: 비트마다 g = a AND b, p = a XOR b, 4비트 블록마다 cla4, s = p XOR c.
 * 블록 사이는 블록의 c[4] 가 다음 블록의 c0 (블록 리플). 마지막 carry-out 을 돌려준다. */
static sig cla_add(const sig a[], const sig b[], sig cin, sig s[], int n) {
    sig c = cin;
    for (int blk = 0; blk < n; blk += 4) {
        sig g[4], p[4], cc[5];
        for (int i = 0; i < 4; i++) { g[i] = AND(a[blk + i], b[blk + i]); p[i] = XOR(a[blk + i], b[blk + i]); }
        cla4(g, p, c, cc, NULL, NULL);
        for (int i = 0; i < 4; i++) s[blk + i] = XOR(p[i], cc[i]);
        c = cc[4];
    }
    return c;
}

/* ======================= Part C — ALU 슬라이스와 다중 정밀도 ======================= */
/* 8비트 ALU 슬라이스: b' = b XOR sub (비트마다), r = a + b' + cin  (cla_add 사용)
 *   ADD: sub=0 cin=0   ADC: sub=0 cin=C   SUB: sub=1 cin=1   SBC: sub=1 cin=C  (Arm: C=1 이면 borrow 없음)
 * 결과 8비트를 돌려주고 *cout 에 carry-out (0/1). */
static unsigned alu8(unsigned a, unsigned b, unsigned cin, unsigned sub, unsigned *cout) {
    sig A[8], B[8], S[8], m = lit(sub);
    unpack(a, A, 8);
    unpack(b, B, 8);
    for (int i = 0; i < 8; i++) B[i] = XOR(B[i], m);
    sig c = cla_add(A, B, lit(cin), S, 8);
    *cout = c.v;
    return (unsigned)pack(S, 8);
}

/* 리틀 엔디언 바이트 배열 다중 정밀도 덧셈/뺄셈: 첫 바이트는 ADD/SUB, 나머지는 ADC/SBC.
 * 바이트 사이로 넘기는 건 carry 하나뿐 (8비트 MCU 의 C 플래그). 최종 carry 를 돌려준다. */
static unsigned mp_addsub(const uint8_t *a, const uint8_t *b, uint8_t *r, int nbytes, unsigned sub) {
    unsigned c = sub;                       /* ADD → cin 0, SUB → cin 1 (borrow 없음) */
    for (int i = 0; i < nbytes; i++) r[i] = (uint8_t)alu8(a[i], b[i], c, sub, &c);
    return c;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef sig (*adder_fn)(const sig[], const sig[], sig, sig[], int);

/* 한 번 돌려서 n비트 결과/게이트 수/깊이를 잰다 */
typedef struct { unsigned sum, cout; unsigned long gates, gates2; int depth, depth2, cout_t; int st[MAXW]; } run_t;
static run_t run_adder(adder_fn f, unsigned a, unsigned b, unsigned cin, int n) {
    sig A[MAXW], B[MAXW], S[MAXW + 1];
    run_t r = {0};
    unpack(a, A, n); unpack(b, B, n);
    g_gates = g_gates2 = 0;
    sig c = f(A, B, lit(cin), S, n);
    r.sum = (unsigned)pack(S, n); r.cout = c.v; r.gates = g_gates; r.gates2 = g_gates2;
    S[n] = c;
    r.depth = depth(S, n + 1); r.depth2 = depth2(S, n + 1); r.cout_t = c.t;
    for (int i = 0; i < n; i++) r.st[i] = S[i].t;
    return r;
}

/* 8비트 전수: 65536쌍 × cin 2 = 131072 번. 틀린 횟수를 돌려준다 */
static long exhaustive8(adder_fn f) {
    long bad = 0;
    for (unsigned cin = 0; cin < 2; cin++)
        for (unsigned a = 0; a < 256; a++)
            for (unsigned b = 0; b < 256; b++) {
                run_t r = run_adder(f, a, b, cin, 8);
                unsigned want = a + b + cin;            /* 기준은 C 의 '+' */
                if (r.sum != (want & 0xFFu) || r.cout != (want >> 8)) bad++;
            }
    return bad;
}

int main(void) {
    /* ---------- 게이트 모델 자체 (주어진 코드) ---------- */
    CHECK(NOT(lit(0)).v == 1 && NOT(lit(1)).v == 0);
    {   /* 도착 시각 3 인 신호 하나 + 0 인 신호 4개를 5입력 AND 로: 단위 지연 4, 2입력 트리 4 (늦은 입력은 위쪽) */
        sig late = { 1, 3, 3 };
        sig y = AND(late, lit(1), lit(1), lit(1), lit(1));
        CHECK(y.v == 1 && y.t == 4 && y.t2 == 4);
    }

    /* ---------- Part A ---------- */
    int ha_ok = 1, fa_ok = 1;
    for (unsigned x = 0; x < 4; x++) {
        sig s, c;
        half_add(lit(x), lit(x >> 1), &s, &c);
        unsigned want = (x & 1u) + (x >> 1);
        ha_ok &= (s.v == (want & 1u)) && (c.v == (want >> 1));
    }
    CHECK(ha_ok);
    for (unsigned x = 0; x < 8; x++) {
        sig s, c;
        full_add(lit(x), lit(x >> 1), lit(x >> 2), &s, &c);
        unsigned want = (x & 1u) + ((x >> 1) & 1u) + (x >> 2);
        fa_ok &= (s.v == (want & 1u)) && (c.v == (want >> 1));
    }
    CHECK(fa_ok);
    {   /* 전가산기 구조: 게이트 5개, sum 은 2단계, carry-out 은 3단계 */
        sig s, c;
        g_gates = 0;
        full_add(lit(1), lit(0), lit(1), &s, &c);
        CHECK(g_gates == 5);
        CHECK(s.t == 2 && c.t == 3);
    }
    CHECK(exhaustive8(ripple_add) == 0);
    run_t rr = run_adder(ripple_add, 0, 0, 0, 8);
    CHECK(rr.gates == 40);
    CHECK(rr.depth == 17 && rr.cout_t == 17 && rr.st[7] == 16);   /* 2n+1 */

    /* ---------- Part B ---------- */
    int cla4_ok = 1;
    for (unsigned x = 0; x < 512; x++) {          /* g 4비트 × p 4비트 × c0 = 2^9 전수 */
        sig g[4], p[4], c[5], G, P;
        unpack(x, g, 4); unpack(x >> 4, p, 4);
        cla4(g, p, lit(x >> 8), c, &G, &P);
        unsigned cr = (x >> 8) & 1u, gg = 0, pp = 1;
        for (int i = 0; i < 4; i++) {
            cla4_ok &= c[i].v == cr;
            cr = ((x >> i) & 1u) | (((x >> (4 + i)) & 1u) & cr);       /* c_{i+1} = g_i + p_i c_i */
            gg = ((x >> i) & 1u) | (((x >> (4 + i)) & 1u) & gg);
            pp &= (x >> (4 + i)) & 1u;
        }
        cla4_ok &= (c[4].v == cr) && (G.v == gg) && (P.v == pp);
    }
    CHECK(cla4_ok);
    CHECK(exhaustive8(cla_add) == 0);
    run_t rc = run_adder(cla_add, 0, 0, 0, 8);
    CHECK(rc.gates == 52);
    CHECK(rc.depth == 6 && rc.cout_t == 5);
    CHECK(rr.depth2 == 17 && rc.depth2 == 9);      /* 2입력 게이트로만 지으면 CLA 이점이 줄어든다 */
    CHECK(rc.st[0] == 2 && rc.st[3] == 4 && rc.st[4] == 4 && rc.st[7] == 6);

    /* ---------- Part C ---------- */
    long bad = 0;
    for (unsigned a = 0; a < 256; a++)
        for (unsigned b = 0; b < 256; b++) {
            unsigned c, r;
            r = alu8(a, b, 0, 0, &c);  bad += r != ((a + b) & 0xFFu) || c != ((a + b) >> 8);          /* ADD */
            r = alu8(a, b, 1, 0, &c);  bad += r != ((a + b + 1) & 0xFFu) || c != ((a + b + 1) >> 8);  /* ADC C=1 */
            r = alu8(a, b, 1, 1, &c);  bad += r != ((a - b) & 0xFFu) || c != (a >= b);                 /* SUB */
            r = alu8(a, b, 0, 1, &c);  bad += r != ((a - b - 1) & 0xFFu) || c != (a > b);              /* SBC C=0 */
        }
    CHECK(bad == 0);

    static const uint64_t edge[] = { 0, 1, 0xFF, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x100000000ull,
                                     0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull };
    long bad32 = 0, bad64 = 0;
    for (int k = 0; k < 20000 + 121; k++) {
        uint64_t x, y;
        if (k < 121) { x = edge[k / 11]; y = edge[k % 11]; }
        else { x = ((uint64_t)xorshift32() << 32) | xorshift32(); y = ((uint64_t)xorshift32() << 32) | xorshift32(); }
        for (unsigned sub = 0; sub < 2; sub++) {
            uint8_t A[8], B[8], R[8];
            for (int i = 0; i < 8; i++) { A[i] = (uint8_t)(x >> (8 * i)); B[i] = (uint8_t)(y >> (8 * i)); }
            unsigned c32 = mp_addsub(A, B, R, 4, sub);
            uint32_t r32 = (uint32_t)R[0] | (uint32_t)R[1] << 8 | (uint32_t)R[2] << 16 | (uint32_t)R[3] << 24;
            uint32_t x32 = (uint32_t)x, y32 = (uint32_t)y;
            if (!sub) bad32 += r32 != (uint32_t)(x32 + y32) || c32 != (unsigned)(((uint64_t)x32 + y32) >> 32);
            else      bad32 += r32 != (uint32_t)(x32 - y32) || c32 != (unsigned)(x32 >= y32);
            unsigned c64 = mp_addsub(A, B, R, 8, sub);
            uint64_t r64 = 0;
            for (int i = 0; i < 8; i++) r64 |= (uint64_t)R[i] << (8 * i);
            if (!sub) bad64 += r64 != x + y || c64 != (unsigned)(x + y < x);
            else      bad64 += r64 != x - y || c64 != (unsigned)(x >= y);
        }
    }
    CHECK(bad32 == 0);
    CHECK(bad64 == 0);
    {   /* 0 - 1 = 0xFFFFFFFF, 마지막 carry 0 = borrow 발생 */
        uint8_t A[4] = {0}, B[4] = {1, 0, 0, 0}, R[4];
        unsigned c = mp_addsub(A, B, R, 4, 1);
        CHECK(R[0] == 0xFF && R[3] == 0xFF && c == 0);
    }

    /* ---------- 측정표 (8비트) ---------- */
    printf("\n%-10s %6s %8s %7s %8s\n", "adder", "gates", "gates2in", "depth", "depth2in");
    printf("%-10s %6lu %8lu %7d %8d\n", "ripple", rr.gates, rr.gates2, rr.depth, rr.depth2);
    printf("%-10s %6lu %8lu %7d %8d\n", "cla4-blk", rc.gates, rc.gates2, rc.depth, rc.depth2);
    printf("arrival s0..s7,cout  ripple:");
    for (int i = 0; i < 8; i++) printf(" %d", rr.st[i]);
    printf(" | %d\n", rr.cout_t);
    printf("arrival s0..s7,cout  cla   :");
    for (int i = 0; i < 8; i++) printf(" %d", rc.st[i]);
    printf(" | %d\n\n", rc.cout_t);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
