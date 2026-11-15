/* ARCH-B04 추가 연습 — 한 번에 검증
 *   연습 1  게이트로 V(부호 오버플로) 플래그: V = c7 XOR c8  (ARCH-B02 의 플래그 공식과 같은 값인지 전수 확인)
 *   연습 2  16비트 2단 CLA (74x182 스타일 그룹 룩어헤드) — 깊이 8, 블록 리플 10, 리플 33
 *   연습 3  폭 스윕 8/16/32/64: 리플 2n+1 vs 4비트 블록 CLA 2(n/4)+2, 64비트 결과 검증
 *   연습 4  8비트 캐리 셀렉트 가산기 (상위 4비트를 cin=0/1 로 두 번 → MUX)
 *   연습 5  데이터에 따라 달라지는 캐리 사슬 길이 (반복 갱신이 멈추는 횟수 = 가장 긴 전파 사슬)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdint.h>
#include <stdio.h>

/* ---------- 게이트 모델 (solution.c 와 같음) ---------- */
typedef struct { uint8_t v, t, t2; } sig;
static unsigned long g_gates, g_gates2;
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
static sig lit(unsigned v) { return (sig){ (uint8_t)(v & 1u), 0, 0 }; }
static void unpack(uint64_t x, sig out[], int n) { for (int i = 0; i < n; i++) out[i] = lit((unsigned)(x >> i)); }
static uint64_t pack(const sig s[], int n) { uint64_t x = 0; for (int i = 0; i < n; i++) x |= (uint64_t)s[i].v << i; return x; }
static int depth(const sig s[], int n) { int d = 0; for (int i = 0; i < n; i++) if (s[i].t > d) d = s[i].t; return d; }
#define MAXW 64

static void full_add(sig a, sig b, sig cin, sig *s, sig *cout) {
    sig p = XOR(a, b);
    *s = XOR(p, cin);
    *cout = OR(AND(a, b), AND(p, cin));
}
static sig ripple_add(const sig a[], const sig b[], sig cin, sig s[], int n) {
    sig c = cin;
    for (int i = 0; i < n; i++) full_add(a[i], b[i], c, &s[i], &c);
    return c;
}
static void cla4(const sig g[4], const sig p[4], sig c0, sig c[5]) {
    c[0] = c0;
    c[1] = OR(g[0], AND(p[0], c0));
    c[2] = OR(g[1], AND(p[1], g[0]), AND(p[1], p[0], c0));
    c[3] = OR(g[2], AND(p[2], g[1]), AND(p[2], p[1], g[0]), AND(p[2], p[1], p[0], c0));
    c[4] = OR(g[3], AND(p[3], g[2]), AND(p[3], p[2], g[1]), AND(p[3], p[2], p[1], g[0]),
              AND(p[3], p[2], p[1], p[0], c0));
}
static sig cla_add(const sig a[], const sig b[], sig cin, sig s[], int n) {
    sig c = cin;
    for (int blk = 0; blk < n; blk += 4) {
        sig g[4], p[4], cc[5];
        for (int i = 0; i < 4; i++) { g[i] = AND(a[blk + i], b[blk + i]); p[i] = XOR(a[blk + i], b[blk + i]); }
        cla4(g, p, c, cc);
        for (int i = 0; i < 4; i++) s[blk + i] = XOR(p[i], cc[i]);
        c = cc[4];
    }
    return c;
}

/* ---------- 연습 1: V = c_in(MSB) XOR c_out(MSB) ---------- */
/* a + (b XOR sub) + sub 를 리플로. MSB 로 들어가는 carry(c7) 와 나오는 carry(c8) 를 XOR → V */
static unsigned add8_v(unsigned a, unsigned b, unsigned sub, unsigned *v) {
    sig A[8], B[8], S[8], c = lit(sub), c7 = lit(0);
    unpack(a, A, 8); unpack(b, B, 8);
    for (int i = 0; i < 8; i++) {
        B[i] = XOR(B[i], lit(sub));
        if (i == 7) c7 = c;
        full_add(A[i], B[i], c, &S[i], &c);
    }
    *v = XOR(c7, c).v;
    return (unsigned)pack(S, 8);
}

/* ---------- 연습 2: 16비트 2단 CLA ---------- */
static void gp4(const sig g[4], const sig p[4], sig *G, sig *P) {
    *G = OR(g[3], AND(p[3], g[2]), AND(p[3], p[2], g[1]), AND(p[3], p[2], p[1], g[0]));
    *P = AND(p[3], p[2], p[1], p[0]);
}
static sig cla16_2level(const sig a[], const sig b[], sig cin, sig s[], int n) {
    (void)n;                                   /* 16 고정 */
    sig g[16], p[16], G[4], P[4], C[5];
    for (int i = 0; i < 16; i++) { g[i] = AND(a[i], b[i]); p[i] = XOR(a[i], b[i]); }
    for (int k = 0; k < 4; k++) gp4(&g[4 * k], &p[4 * k], &G[k], &P[k]);
    cla4(G, P, cin, C);                        /* 2단: 블록 G/P 로 c4, c8, c12, c16 을 한 번에 (74x182 역할) */
    for (int k = 0; k < 4; k++) {
        sig cc[5];
        cla4(&g[4 * k], &p[4 * k], C[k], cc); /* 블록 안쪽 carry. cc[4] 는 C[k+1] 과 같은 값 — 실제 회로라면 안 만든다 */
        for (int i = 0; i < 4; i++) s[4 * k + i] = XOR(p[4 * k + i], cc[i]);
    }
    return C[4];
}

/* ---------- 연습 4: 캐리 셀렉트 ---------- */
static sig mux(sig x0, sig x1, sig sel) { return OR(AND(x1, sel), AND(x0, NOT(sel))); }
static sig csel_add8(const sig a[], const sig b[], sig cin, sig s[], int n) {
    (void)n;
    sig lo_c = ripple_add(a, b, cin, s, 4);
    sig s0[4], s1[4];
    sig c0 = ripple_add(a + 4, b + 4, lit(0), s0, 4);   /* 상위: cin=0 가정 */
    sig c1 = ripple_add(a + 4, b + 4, lit(1), s1, 4);   /* 상위: cin=1 가정 — 둘 다 미리 계산 */
    for (int i = 0; i < 4; i++) s[4 + i] = mux(s0[i], s1[i], lo_c);
    return mux(c0, c1, lo_c);
}

/* ---------- 연습 5: 캐리 사슬 길이 ---------- */
/* 방법 1: 모든 c_{i+1} = g_i | p_i c_i 를 동시에 갱신, 바뀌는 게 없을 때까지 몇 번 도나 (cin = 0) */
static int settle_steps(unsigned a, unsigned b, int n) {
    unsigned g = a & b, p = a ^ b, c = 0, steps = 0;   /* c 의 비트 i = c_{i+1} */
    for (;;) {
        unsigned cin_vec = c << 1;                       /* 비트 i 에 들어오는 carry = c_i */
        unsigned nc = (g | (p & cin_vec)) & ((1u << n) - 1u);
        if (nc == c) return (int)steps;
        c = nc;
        steps++;
    }
}
/* 방법 2: generate 된 비트 i 마다 1 + (i+1 부터 연속된 propagate 수) 의 최댓값 */
static int longest_chain(unsigned a, unsigned b, int n) {
    unsigned g = a & b, p = a ^ b;
    int best = 0;
    for (int i = 0; i < n; i++) {
        if (!((g >> i) & 1u)) continue;
        int len = 1;
        for (int j = i + 1; j < n && ((p >> j) & 1u); j++) len++;
        if (len > best) best = len;
    }
    return best;
}

/* ---------- 하네스 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng = 0x9E3779B9u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint64_t rnd64(void) { return ((uint64_t)xorshift32() << 32) | xorshift32(); }

typedef sig (*adder_fn)(const sig[], const sig[], sig, sig[], int);
typedef struct { uint64_t sum; unsigned cout; unsigned long gates; int depth; } run_t;
static run_t run_adder(adder_fn f, uint64_t a, uint64_t b, unsigned cin, int n) {
    sig A[MAXW], B[MAXW], S[MAXW + 1];
    unpack(a, A, n); unpack(b, B, n);
    g_gates = g_gates2 = 0;
    sig c = f(A, B, lit(cin), S, n);
    run_t r = { pack(S, n), c.v, g_gates, 0 };
    S[n] = c;
    r.depth = depth(S, n + 1);
    return r;
}
static uint64_t ref_sum(uint64_t a, uint64_t b, unsigned cin, int n, unsigned *cout) {
    if (n == 64) { uint64_t s = a + b, s2 = s + cin; *cout = (s < a) | (s2 < s); return s2; }
    uint64_t m = (1ull << n) - 1, s = (a & m) + (b & m) + cin;
    *cout = (unsigned)(s >> n);
    return s & m;
}

int main(void) {
    /* 연습 1 */
    long badv = 0;
    for (unsigned a = 0; a < 256; a++)
        for (unsigned b = 0; b < 256; b++)
            for (unsigned sub = 0; sub < 2; sub++) {
                unsigned v, r = add8_v(a, b, sub, &v);
                int sa = (int8_t)a, sb = (int8_t)b, exact = sub ? sa - sb : sa + sb;
                badv += (r != (unsigned)(uint8_t)exact) || (v != (unsigned)(exact < -128 || exact > 127));
            }
    CHECK(badv == 0);

    /* 연습 2 */
    long bad2 = 0;
    for (int k = 0; k < 200000; k++) {
        uint64_t a = k < 256 ? (uint64_t)(k * 257) : xorshift32() & 0xFFFFu, b = k < 256 ? 0xFFFFu - (uint64_t)k : xorshift32() & 0xFFFFu;
        unsigned cin = (unsigned)k & 1u, rc;
        run_t r = run_adder(cla16_2level, a, b, cin, 16);
        uint64_t want = ref_sum(a, b, cin, 16, &rc);
        bad2 += r.sum != want || r.cout != rc;
    }
    CHECK(bad2 == 0);
    run_t r2 = run_adder(cla16_2level, 0, 0, 0, 16), rb16 = run_adder(cla_add, 0, 0, 0, 16), rr16 = run_adder(ripple_add, 0, 0, 0, 16);
    CHECK(r2.depth == 8 && rb16.depth == 10 && rr16.depth == 33);

    /* 연습 3 */
    printf("\n%5s | %12s %12s | %12s %12s\n", "n", "ripple gates", "ripple depth", "cla gates", "cla depth");
    int sweep_ok = 1;
    for (int n = 8; n <= 64; n *= 2) {
        run_t rr = run_adder(ripple_add, 0, 0, 0, n), rc = run_adder(cla_add, 0, 0, 0, n);
        printf("%5d | %12lu %12d | %12lu %12d\n", n, rr.gates, rr.depth, rc.gates, rc.depth);
        sweep_ok &= rr.depth == 2 * n + 1 && rc.depth == 2 * (n / 4) + 2;
        sweep_ok &= rr.gates == 5ul * (unsigned long)n && rc.gates == 26ul * (unsigned long)(n / 4);
    }
    CHECK(sweep_ok);
    long bad64 = 0;
    for (int k = 0; k < 5000; k++) {
        uint64_t a = k == 0 ? ~0ull : rnd64(), b = k == 0 ? 1 : rnd64();
        unsigned cin = (unsigned)k & 1u, c1, c2;
        run_t r1 = run_adder(ripple_add, a, b, cin, 64), r3 = run_adder(cla_add, a, b, cin, 64);
        uint64_t want = ref_sum(a, b, cin, 64, &c1);
        c2 = c1;
        bad64 += r1.sum != want || r1.cout != c1 || r3.sum != want || r3.cout != c2;
    }
    CHECK(bad64 == 0);

    /* 연습 4 */
    long bad4 = 0;
    for (unsigned cin = 0; cin < 2; cin++)
        for (unsigned a = 0; a < 256; a++)
            for (unsigned b = 0; b < 256; b++) {
                run_t r = run_adder(csel_add8, a, b, cin, 8);
                unsigned w = a + b + cin;
                bad4 += r.sum != (w & 0xFFu) || r.cout != (w >> 8);
            }
    CHECK(bad4 == 0);
    run_t r4 = run_adder(csel_add8, 0, 0, 0, 8);
    CHECK(r4.depth == 12 && r4.gates == 80);
    printf("\ncarry-select 8-bit: gates=%lu depth=%d (ripple 40/17, cla4-blk 52/6)\n", r4.gates, r4.depth);

    /* 연습 5 */
    long hist[9] = {0}, agree = 0, sum = 0;
    for (unsigned a = 0; a < 256; a++)
        for (unsigned b = 0; b < 256; b++) {
            int s = settle_steps(a, b, 8), l = longest_chain(a, b, 8);
            agree += s == l;
            hist[s]++;
            sum += s;
        }
    CHECK(agree == 65536);
    CHECK(hist[0] == 6561 && hist[8] == 128);   /* 3^8 (generate 없음), 2^7 (g0 + p1..p7) */
    CHECK(settle_steps(0xFF, 0x01, 8) == 8 && settle_steps(0x0F, 0xF1, 8) == 8 && settle_steps(0x55, 0xAA, 8) == 0);
    printf("\ncarry chain length histogram (8-bit, cin=0, all 65536 pairs):\n");
    for (int i = 0; i <= 8; i++) printf("  len %d: %5ld\n", i, hist[i]);
    printf("  average = %.3f\n\n", (double)sum / 65536.0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
