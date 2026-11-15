/* ARCH-A07 추가 연습 — 한 번에 검증
 *
 * solution.c 의 시뮬레이터를 그대로 가져와(main 만 이름을 바꿔 포함) 5 개 연습의 답을 검사한다.
 *   연습 1  깊이가 2^N 이 아니면 Gray 포인터가 왜 깨지나 → 짝수 깊이용 "가운데 M 개" Gray, 홀수는 불가능(이분 그래프)
 *   연습 2  MTBF 로 동기화기 단 수 고르기 (대표값) — 클럭이 빨라지면 몇 단이 필요한가
 *   연습 3  2-phase(토글) req/ack 핸드셰이크 — 4-phase 대비 처리량
 *   연습 4  토글 펄스 동기화기의 최소 펄스 간격: 식 ⌈(Td + T_W)/Ts⌉ vs 시뮬레이션
 *   연습 5  비동기 FIFO 깊이 vs 처리량 — 왕복 동기화 지연을 덮으려면 몇 칸이 필요한가
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#include <math.h>
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: 비-2^N 깊이 ---------- */
static int ceil_log2(uint32_t m) { int n = 0; while ((1u << n) < m) n++; return n; }

/* 코드 c[0..M-1] 가 순환(마지막 → 처음 포함)하며 이웃끼리 정확히 1 비트 다르고, 모두 서로 다른가 */
static bool cyclic_unit(const uint32_t *c, uint32_t M) {
    for (uint32_t i = 0; i < M; i++) {
        if (__builtin_popcount(c[i] ^ c[(i + 1) % M]) != 1) return false;
        for (uint32_t j = i + 1; j < M; j++) if (c[i] == c[j]) return false;
    }
    return true;
}

/* 짝수 M: 2^n 개 반사 Gray 수열의 가운데 M 개 (k = 2^(n-1) - M/2 … 2^(n-1) + M/2 - 1).
 * 반사 대칭 때문에 처음과 마지막 코드가 최상위 비트 하나만 다르다. */
static uint32_t mid_gray(uint32_t i, uint32_t M) {
    int n = ceil_log2(M);
    uint32_t off = (1u << (n - 1)) - M / 2;
    return bin2gray(i + off);
}

/* n 비트 하이퍼큐브에서 길이 M 의 단순 순환이 있는가 (작은 경우 전수 DFS) */
static bool dfs_cycle(uint32_t start, uint32_t cur, uint32_t used, uint32_t len, uint32_t M, int n) {
    if (len == M) return __builtin_popcount(cur ^ start) == 1;
    for (int b = 0; b < n; b++) {
        uint32_t nx = cur ^ (1u << b);
        if (used & (1u << nx)) continue;
        if (dfs_cycle(start, nx, used | (1u << nx), len + 1, M, n)) return true;
    }
    return false;
}

static void ex1(void) {
    printf("== 연습 1: 깊이 M 의 순환 Gray (마지막 → 처음 도 1 비트) ==\n");
    uint32_t c[64];
    printf("  plain binary-reflected Gray 0..M-1 cyclic for M = ");
    bool only_pow2 = true;
    for (uint32_t M = 2; M <= 64; M++) {
        for (uint32_t i = 0; i < M; i++) c[i] = bin2gray(i);
        bool ok = cyclic_unit(c, M), pow2 = (M & (M - 1)) == 0;
        if (ok) printf("%u ", M);
        if (ok != pow2) only_pow2 = false;
    }
    printf("\n  e.g. M=6: gray(5)=%u -> gray(0)=0 : %d bits change\n", bin2gray(5), __builtin_popcount(bin2gray(5)));
    CHECK(only_pow2);
    bool even_ok = true;
    for (uint32_t M = 2; M <= 64; M += 2) {
        for (uint32_t i = 0; i < M; i++) c[i] = mid_gray(i, M);
        if (!cyclic_unit(c, M)) even_ok = false;
    }
    printf("  M=6 middle codes:");
    for (uint32_t i = 0; i < 6; i++) printf(" %u", mid_gray(i, 6));
    printf("  (offset %u)\n", (1u << ceil_log2(6)) / 2 - 3);
    CHECK(even_ok);
    bool odd_none = true;
    for (uint32_t M = 3; M <= 9; M += 2)
        if (dfs_cycle(0, 0, 1u, 1, M, 4)) odd_none = false;     /* 4 비트 큐브(16 꼭짓점)에서 홀수 순환 탐색 */
    printf("  odd M = 3,5,7,9 in 4-bit cube: %s\n", odd_none ? "no cycle (bipartite)" : "found?!");
    CHECK(odd_none);
}

/* ---------- 연습 2: MTBF ---------- */
/* MTBF = e^(t_r/τ) / (T_W · f_clk · f_data). 단 k 개(FF1..FFk)면 결정 시간 t_r = (k-1)(T_clk - tcq - tsu). */
static double mtbf_s(int stages, double fclk, double fdata, double tau_ps, double tw_ps) {
    double Tps = 1e12 / fclk, tr = (stages - 1) * (Tps - (double)TM.tcq - (double)TM.tsu);
    if (tr <= 0) return 0.0;
    return exp(tr / tau_ps) / (tw_ps * 1e-12 * fclk * fdata);
}
static int stages_for(double target_s, double fclk, double fdata, double tau_ps, double tw_ps) {
    for (int k = 2; k <= 20; k++) if (mtbf_s(k, fclk, fdata, tau_ps, tw_ps) >= target_s) return k;
    return -1;
}

static void ex2(void) {
    printf("== 연습 2: MTBF (대표값 tau=20 ps, T_W=250 ps, tcq=200, tsu=150, f_data = f_clk/10) ==\n");
    const double tau = 20.0, tw = 250.0, year = 365.25 * 86400.0, target = 1000.0 * year;
    const double F[] = { 77e6, 500e6, 1e9, 2e9 };
    int need[4];
    printf("  %10s %14s %14s %8s\n", "f_clk", "MTBF 2-FF (s)", "MTBF 3-FF (s)", "stages");
    for (int i = 0; i < 4; i++) {
        need[i] = stages_for(target, F[i], F[i] / 10, tau, tw);
        printf("  %7.0f MHz %14.3e %14.3e %8d\n", F[i] / 1e6, mtbf_s(2, F[i], F[i] / 10, tau, tw),
               mtbf_s(3, F[i], F[i] / 10, tau, tw), need[i]);
    }
    CHECK(need[0] == 2 && need[1] == 2 && need[2] == 3 && need[3] == 7);
    /* 손 계산: 1 GHz 2-FF → t_r = 650 ps, e^32.5 = 1.30e14, 분모 = 250e-12·1e9·1e8 = 2.5e7 → 5.2e6 s (약 60 일) */
    double m = mtbf_s(2, 1e9, 1e8, tau, tw);
    CHECK(m > 5.1e6 && m < 5.3e6);
}

/* ---------- 연습 3: 2-phase 핸드셰이크 ---------- */
static hres_t run_hs2(int ci, long n, uint32_t seed, int64_t J) {
    hres_t r; memset(&r, 0, sizeof r);
    int64_t Ts = CLK[ci].Tw, Td = CLK[ci].Tr;
    clk_t cs, cd;
    clk_init(&cs, Ts, 1000 + J, J, seed);
    clk_init(&cd, Td, 1000 + J + (int64_t)((seed * 7919u) % (uint32_t)Td), J, seed * 31u + 7u);
    wire_t wd, wreq, wack; wire_init(&wd, 16, 40, 0); wire_init(&wreq, 1, 0, 0); wire_init(&wack, 1, 0, 0);
    sync2_t reqs = { 0, 0 }, acks = { 0, 0 };
    uint32_t rs = seed * 2654435761u | 1u, gen = seed | 1u, chk = seed | 1u, req = 0, ack = 0;
    uint32_t word = xs32(&gen) & 0xFFFFu;
    for (long e = 0; e < 200 * n && (r.recv < n || req != acks.q2); e++) {
        if (cs.next <= cd.next) {
            int64_t t = cs.next;
            if (req == acks.q2 && r.sent < n) {              /* 이전 전송 끝(ack == req) → 실고 req 토글 */
                wire_drive(&wd, t, word); req ^= 1u; r.sent++; word = xs32(&gen) & 0xFFFFu;
            }
            wire_drive(&wreq, t, req);
            sync2_clock(&acks, &wack, t, &rs, NULL);
            clk_adv(&cs);
        } else {
            int64_t t = cd.next;
            if (reqs.q2 != ack) {                            /* req 가 바뀌었다 = 새 단어 → 잡고 ack 를 req 에 맞춤 */
                uint32_t got = wire_sample(&wd, t, &rs, &r.sd), want = xs32(&chk) & 0xFFFFu;
                r.recv++; if (got != want) r.bad++;
                ack = reqs.q2;
            }
            wire_drive(&wack, t, ack);
            sync2_clock(&reqs, &wreq, t, &rs, NULL);
            r.t_end = t;
            clk_adv(&cd);
        }
    }
    return r;
}

static void ex3(void) {
    printf("== 연습 3: 2-phase vs 4-phase req/ack (16-bit, 5000 words) ==\n");
    printf("  %-12s %12s %12s %7s\n", "clocks", "4ph ns/word", "2ph ns/word", "ratio");
    bool all_ok = true, faster = true;
    for (int ci = 0; ci < NCLK; ci++) {
        hres_t a = run_hs(ci, 5000, 41u + (uint32_t)ci, 40), b = run_hs2(ci, 5000, 41u + (uint32_t)ci, 40);
        double na = (double)a.t_end / 1000.0 / (double)a.recv, nb = (double)b.t_end / 1000.0 / (double)b.recv;
        printf("  %-12s %12.1f %12.1f %7.2f\n", CLK[ci].name, na, nb, na / nb);
        if (b.recv != 5000 || b.bad != 0 || b.sd.meta != 0) all_ok = false;
        if (na / nb < 1.6) faster = false;
    }
    CHECK(all_ok);
    CHECK(faster);
}

/* ---------- 연습 4: 토글 펄스 최소 간격 ---------- */
static void ex4(void) {
    printf("== 연습 4: 토글 펄스 동기화기 최소 간격 (소스 주기, 간격 = S..S+2) ==\n");
    printf("  %-12s %10s %10s %18s\n", "clocks", "formula", "sim min", "lost at S=formula-1");
    bool ok = true;
    for (int ci = 0; ci < NCLK; ci++) {
        long f = (long)((CLK[ci].Tr + TWIN + CLK[ci].Tw - 1) / CLK[ci].Tw);
        long smin = -1, lost_below = 0;
        for (long S = 1; S <= f && smin < 0; S++) {
            long lost = 0;
            for (uint32_t sd = 1; sd <= 3; sd++) { pres_t p = run_pulse(ci, PM_TOGGLE, S, 5000, 500u + sd); lost += p.sent - p.recv; }
            if (lost == 0) smin = S;
            if (S == f - 1) lost_below = lost;
        }
        long lost_at_f = 0;
        for (uint32_t sd = 1; sd <= 3; sd++) { pres_t p = run_pulse(ci, PM_TOGGLE, f, 5000, 900u + sd); lost_at_f += p.sent - p.recv; }
        if (f > 1) printf("  %-12s %10ld %10ld %18ld\n", CLK[ci].name, f, smin, lost_below);
        else printf("  %-12s %10ld %10ld %18s\n", CLK[ci].name, f, smin, "-");
        if (lost_at_f != 0 || smin < 1 || smin > f) ok = false;
    }
    CHECK(ok);
}

/* ---------- 연습 5: FIFO 깊이 vs 처리량 ---------- */
static void ex5(void) {
    printf("== 연습 5: Gray FIFO 깊이 vs 처리량 (쓰기·읽기 모두 매 에지 시도, 100->99.97 MHz) ==\n");
    printf("  %6s %10s %10s\n", "depth", "reads/edge", "errors");
    double thr[6];
    bool clean = true;
    for (int N = 1; N <= 5; N++) {
        fcfg_t c = { 3, N, 1, true, 200000, 7u, 40, 30, 1.0, 1.0 };
        fres_t r = run_fifo(&c);
        thr[N] = (double)r.reads_main / (double)r.redges_main;
        printf("  %6d %10.3f %10ld\n", 1 << N, thr[N], errs(&r));
        if (errs(&r) != 0) clean = false;
    }
    CHECK(clean);
    CHECK(thr[1] < 0.5 && thr[2] < 0.9 && thr[3] > 0.99);   /* 깊이 ≥ 왕복 지연(약 6 주기)이면 꽉 찬 처리량 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
