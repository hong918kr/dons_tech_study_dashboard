/* ALGO-M03 demo: 노드 수 · 시간 · 재귀 깊이/프레임 바이트 (이 맥, Apple Silicon, -O2)
 *
 *   1) N-Queens: 가지치기 없음(n^n) / 열만(순열) / 열+대각선(비트마스크) — 노드 수와 시간 (중앙값 5회)
 *   2) 중복 순열의 두 가지 건너뛰기 규칙: !used[i-1] (앞 복사본 미사용이면 skip) vs used[i-1] — 결과는 같고 노드 수는 다르다
 *   3) 부분집합 합 n = 24: 가지치기 off/on 노드 수와 시간
 *   4) 비트마스크 N-Queens 재귀의 최대 깊이와 프레임 바이트 (__builtin_frame_address 차이)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_nodes.c -o demo_nodes && ./demo_nodes
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static double median5(double *t) {
    for (int i = 1; i < 5; i++) { double v = t[i]; int j = i; while (j > 0 && t[j - 1] > v) { t[j] = t[j - 1]; j--; } t[j] = v; }
    return t[2];
}

/* ---------- N-Queens 세 가지 (solution.c 와 같은 코드) ---------- */
static int nq_valid(const uint8_t *col, unsigned n) {
    for (unsigned r = 0; r < n; r++)
        for (unsigned s = r + 1; s < n; s++) {
            int dc = (int)col[s] - (int)col[r];
            if (dc == 0 || dc == (int)(s - r) || -dc == (int)(s - r)) return 0;
        }
    return 1;
}
static uint64_t nq_naive_rec(uint8_t *col, unsigned r, unsigned n, uint64_t *nodes) {
    (*nodes)++;
    if (r == n) return (uint64_t)nq_valid(col, n);
    uint64_t cnt = 0;
    for (unsigned c = 0; c < n; c++) { col[r] = (uint8_t)c; cnt += nq_naive_rec(col, r + 1, n, nodes); }
    return cnt;
}
static uint64_t nq_perm_rec(uint8_t *col, unsigned r, unsigned n, uint32_t used, uint64_t *nodes) {
    (*nodes)++;
    if (r == n) return (uint64_t)nq_valid(col, n);
    uint64_t cnt = 0;
    for (unsigned c = 0; c < n; c++) {
        if (used & (1u << c)) continue;
        col[r] = (uint8_t)c;
        cnt += nq_perm_rec(col, r + 1, n, used | (1u << c), nodes);
    }
    return cnt;
}
static uintptr_t g_top, g_low;   /* 프레임 주소: 루트 / 가장 깊은 곳 */
static unsigned g_depth, g_maxdepth;
static uint64_t nq_bits_rec(uint32_t full, uint32_t cols, uint32_t d1, uint32_t d2, uint64_t *nodes) {
    (*nodes)++;
    if (cols == full) return 1;
    uint64_t cnt = 0;
    uint32_t avail = full & ~(cols | d1 | d2);
    while (avail) {
        uint32_t bit = avail & (0u - avail);
        avail ^= bit;
        cnt += nq_bits_rec(full, cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1, nodes);
    }
    return cnt;
}
/* 깊이 측정판: 같은 본문 + 깊이/프레임 주소 기록 (시간 측정에는 안 쓴다) */
static uint64_t nq_bits_depth(uint32_t full, uint32_t cols, uint32_t d1, uint32_t d2) {
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    g_depth++;
    if (g_depth > g_maxdepth) { g_maxdepth = g_depth; g_low = fp; }
    if (g_depth == 1) g_top = fp;
    uint64_t cnt = 0;
    if (cols == full) cnt = 1;
    else {
        uint32_t avail = full & ~(cols | d1 | d2);
        while (avail) {
            uint32_t bit = avail & (0u - avail);
            avail ^= bit;
            cnt += nq_bits_depth(full, cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1);
        }
    }
    g_depth--;
    return cnt;
}

/* ---------- 중복 순열: 두 건너뛰기 규칙 ---------- */
static int32_t P_a[16]; static uint8_t P_used[16]; static unsigned P_n; static int P_rule;
static uint64_t P_nodes, P_leaves;
static void perm_rec(unsigned depth) {
    P_nodes++;
    if (depth == P_n) { P_leaves++; return; }
    for (unsigned i = 0; i < P_n; i++) {
        if (P_used[i]) continue;
        if (i > 0 && P_a[i] == P_a[i - 1] && (P_rule == 0 ? !P_used[i - 1] : P_used[i - 1])) continue;
        P_used[i] = 1; perm_rec(depth + 1); P_used[i] = 0;
    }
}

/* ---------- 부분집합 합 ---------- */
static uint32_t S_w[32]; static uint64_t S_suf[33]; static unsigned S_n; static uint64_t S_target, S_sum, S_nodes, S_count; static int S_prune;
static void ss_rec(unsigned k) {
    S_nodes++;
    if (S_prune) {
        if (S_sum + S_suf[k] < S_target) return;
        if (k == S_n || S_sum + S_w[k] > S_target) { S_count += (S_sum == S_target); return; }
    } else if (k == S_n) { S_count += (S_sum == S_target); return; }
    S_sum += S_w[k]; ss_rec(k + 1); S_sum -= S_w[k];
    ss_rec(k + 1);
}

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

int main(void) {
    uint8_t col[16];
    printf("1) N-Queens (median of 5 runs, ms)\n");
    printf("   n | solutions |   naive nodes   ms     |   perm nodes     ms     | bitmask nodes   ms    | ns/node(bits)\n");
    for (unsigned n = 4; n <= 15; n++) {
        double t[5]; uint64_t nb = 0, cb = 0, nn = 0, cn = 0, np = 0, cp = 0;
        double tn = -1, tp = -1;
        for (int r = 0; r < 5; r++) { nb = 0; double t0 = now_ms(); cb = nq_bits_rec((1u << n) - 1, 0, 0, 0, &nb); t[r] = now_ms() - t0; }
        double tb = median5(t);
        if (n <= 8)  { double tt[5]; for (int r = 0; r < 5; r++) { nn = 0; double t0 = now_ms(); cn = nq_naive_rec(col, 0, n, &nn); tt[r] = now_ms() - t0; } tn = median5(tt); }
        if (n <= 11) { double tt[5]; for (int r = 0; r < 5; r++) { np = 0; double t0 = now_ms(); cp = nq_perm_rec(col, 0, n, 0, &np); tt[r] = now_ms() - t0; } tp = median5(tt); }
        printf("  %2u | %9llu |", n, (unsigned long long)cb);
        if (n <= 8) printf(" %12llu %9.2f |", (unsigned long long)nn, tn); else printf(" %12s %9s |", "-", "-");
        if (n <= 11) printf(" %12llu %9.2f |", (unsigned long long)np, tp); else printf(" %12s %9s |", "-", "-");
        printf(" %12llu %8.2f |", (unsigned long long)nb, tb);
        if (tb >= 0.1) printf(" %6.2f", tb * 1e6 / (double)nb); else printf(" %6s", "-");   /* 너무 짧으면 의미 없음 */
        printf("%s\n", ((n <= 8 && cn != cb) || (n <= 11 && cp != cb)) ? "  MISMATCH" : "");
    }

    printf("\n2) duplicate-skip rule: !used[i-1] vs used[i-1]  (same leaves, different nodes)\n");
    static const struct { const char *name; unsigned n; int32_t a[12]; } ms[] = {
        { "[1,1,2]",                  3, { 1, 1, 2 } },
        { "[1,1,1,2,2,3,3,3]",        8, { 1, 1, 1, 2, 2, 3, 3, 3 } },
        { "[1,1,1,1,2,2,2,2]",        8, { 1, 1, 1, 1, 2, 2, 2, 2 } },
        { "[0 x10]",                 10, { 0 } },
        { "[0x5,1x5]",               10, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 } },
    };
    for (size_t s = 0; s < sizeof ms / sizeof ms[0]; s++) {
        uint64_t nd[2], lv[2];
        for (int rule = 0; rule <= 1; rule++) {
            P_n = ms[s].n; P_rule = rule; memcpy(P_a, ms[s].a, sizeof P_a); memset(P_used, 0, sizeof P_used);
            P_nodes = P_leaves = 0; perm_rec(0); nd[rule] = P_nodes; lv[rule] = P_leaves;
        }
        printf("   %-20s leaves %6llu / %6llu   nodes !used=%8llu  used=%8llu\n", ms[s].name,
               (unsigned long long)lv[0], (unsigned long long)lv[1], (unsigned long long)nd[0], (unsigned long long)nd[1]);
    }

    printf("\n3) subset sum n=24 (weights 1..1000), median of 5 runs\n");
    S_n = 24; uint64_t tot = 0;
    for (unsigned i = 0; i < S_n; i++) { S_w[i] = 1 + xorshift32() % 1000; tot += S_w[i]; }
    for (unsigned i = 1; i < S_n; i++) { uint32_t v = S_w[i]; unsigned j = i; while (j > 0 && S_w[j - 1] > v) { S_w[j] = S_w[j - 1]; j--; } S_w[j] = v; }
    S_suf[S_n] = 0; for (unsigned i = S_n; i-- > 0;) S_suf[i] = S_suf[i + 1] + S_w[i];
    const uint64_t tg[3] = { 2000, tot / 2, tot - 3 };
    printf("   sum=%llu\n   target | solutions | nodes no-prune   ms    | nodes prune   ms\n", (unsigned long long)tot);
    for (int t = 0; t < 3; t++) {
        uint64_t nd[2], cnt[2]; double tm[2];
        for (int pr = 0; pr <= 1; pr++) {
            double tt[5];
            for (int r = 0; r < 5; r++) { S_prune = pr; S_target = tg[t]; S_sum = 0; S_nodes = S_count = 0; double t0 = now_ms(); ss_rec(0); tt[r] = now_ms() - t0; }
            nd[pr] = S_nodes; cnt[pr] = S_count; tm[pr] = median5(tt);
        }
        printf("   %6llu | %9llu | %12llu %8.2f | %11llu %6.2f%s\n", (unsigned long long)tg[t], (unsigned long long)cnt[0],
               (unsigned long long)nd[0], tm[0], (unsigned long long)nd[1], tm[1], cnt[0] != cnt[1] ? "  MISMATCH" : "");
    }

    printf("\n4) recursion depth & frame bytes (bitmask N-Queens, depth-instrumented copy)\n");
    for (unsigned n = 8; n <= 12; n += 4) {
        g_depth = g_maxdepth = 0; g_top = g_low = 0;
        uint64_t c = nq_bits_depth((1u << n) - 1, 0, 0, 0);
        printf("   n=%2u solutions=%llu max depth=%u frames (rows + root), stack span=%lu B -> %.1f B/frame\n",
               n, (unsigned long long)c, g_maxdepth, (unsigned long)(g_top - g_low),
               (double)(g_top - g_low) / (g_maxdepth - 1));
    }
    return 0;
}
