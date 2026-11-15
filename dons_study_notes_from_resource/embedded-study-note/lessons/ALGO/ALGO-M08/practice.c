/* ALGO-M08 추가 연습 — 한 번에 검증
 * 연습 1 장애물 격자 경로 수(LeetCode 63) · 연습 2 최소 비용 경로 + 경로 복원(LeetCode 64)
 * 연습 3 두 전원 레일 부하 균등 분할(LeetCode 416) + O(S) 역추적 · 연습 4 최단 공통 상위수열(LeetCode 1092)
 * 연습 5 최장 공통 부분문자열(연속) · 연습 6 띠(band) 편집 거리 ≤ k — UART CLI "did you mean"
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GW 40          /* 격자 최대 폭/높이 */
#define MAXP 64        /* 문자열 최대 길이 */
#define PMAX_N 24      /* 분할: 최대 아이템 수 */
#define PMAX_S 12000   /* 분할: 최대 합/2 */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- 연습 1: 점유 격자(1 = 장애물)에서 오른쪽/아래로만 가는 경로 수. 한 줄 dp ---- */
static uint64_t grid_paths(const uint8_t g[][GW], int R, int C) {
    uint64_t dp[GW];
    for (int c = 0; c < C; c++) dp[c] = 0;
    dp[0] = g[0][0] ? 0 : 1;
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) {
            if (g[r][c]) dp[c] = 0;                 /* 장애물: 이 칸을 지나는 경로 0 */
            else if (c > 0) dp[c] += dp[c - 1];     /* 위(이전 줄 dp[c]) + 왼쪽(이번 줄 dp[c-1]) */
        }
    return dp[C - 1];
}
static uint64_t ref_paths(const uint8_t g[][GW], int R, int C, int r, int c) {   /* 경로를 하나씩 센다 */
    if (r >= R || c >= C || g[r][c]) return 0;
    if (r == R - 1 && c == C - 1) return 1;
    return ref_paths(g, R, C, r + 1, c) + ref_paths(g, R, C, r, c + 1);
}

/* ---- 연습 2: 최소 비용 경로 (오른쪽/아래), moves 에 "R"/"D" 문자열로 복원 ---- */
static uint32_t min_path(const uint16_t cost[][GW], int R, int C, char *moves) {
    static uint32_t s[GW][GW];
    for (int r = 0; r < R; r++)
        for (int c = 0; c < C; c++) {
            uint32_t best;
            if (r == 0 && c == 0) best = 0;
            else if (r == 0) best = s[r][c - 1];
            else if (c == 0) best = s[r - 1][c];
            else best = s[r - 1][c] < s[r][c - 1] ? s[r - 1][c] : s[r][c - 1];
            s[r][c] = best + cost[r][c];
        }
    int r = R - 1, c = C - 1, k = R + C - 2;
    moves[k] = '\0';
    while (r > 0 || c > 0) {                       /* 어느 이웃에서 왔는지 거꾸로 */
        if (c == 0 || (r > 0 && s[r - 1][c] <= s[r][c - 1])) { moves[--k] = 'D'; r--; }
        else { moves[--k] = 'R'; c--; }
    }
    return s[R - 1][C - 1];
}
static uint32_t ref_min_path(const uint16_t cost[][GW], int R, int C, int r, int c) {
    uint32_t here = cost[r][c];
    if (r == R - 1 && c == C - 1) return here;
    uint32_t best = UINT32_MAX;
    if (r + 1 < R) { uint32_t t = ref_min_path(cost, R, C, r + 1, c); if (t < best) best = t; }
    if (c + 1 < C) { uint32_t t = ref_min_path(cost, R, C, r, c + 1); if (t < best) best = t; }
    return here + best;
}
static uint32_t walk_cost(const uint16_t cost[][GW], int R, int C, const char *mv) {
    int r = 0, c = 0;
    uint32_t s = cost[0][0];
    for (; *mv; mv++) { if (*mv == 'D') r++; else c++; if (r >= R || c >= C) return UINT32_MAX; s += cost[r][c]; }
    return (r == R - 1 && c == C - 1) ? s : UINT32_MAX;
}

/* ---- 연습 3: 부하 x[i] 를 두 레일에 합이 같게. reach[s] + "처음 도달시킨 아이템" parent[s] 로 O(S) 역추적 ---- */
static bool can_partition(const uint16_t *x, int n, uint8_t *side) {
    static bool reach[PMAX_S + 1];
    static int8_t parent[PMAX_S + 1];
    uint32_t tot = 0;
    for (int i = 0; i < n; i++) { tot += x[i]; side[i] = 0; }
    if (tot & 1u) return false;
    uint32_t S = tot / 2;
    for (uint32_t s = 0; s <= S; s++) { reach[s] = false; parent[s] = -1; }
    reach[0] = true;
    for (int i = 0; i < n; i++)
        for (uint32_t s = S + 1; s-- > x[i]; )        /* 내려가며: 같은 아이템 두 번 금지 */
            if (!reach[s] && reach[s - x[i]]) { reach[s] = true; parent[s] = (int8_t)i; }
    if (!reach[S]) return false;
    for (uint32_t s = S; s > 0; s -= x[parent[s]]) side[parent[s]] = 1;   /* parent 는 엄격히 줄어든다 */
    return true;
}
static bool ref_partition(const uint16_t *x, int n) {
    uint32_t tot = 0;
    for (int i = 0; i < n; i++) tot += x[i];
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        uint32_t s = 0;
        for (int i = 0; i < n; i++) if (mask >> i & 1u) s += x[i];
        if (2 * s == tot) return true;
    }
    return false;
}

/* ---- 연습 4: 최단 공통 상위수열 (a, b 를 둘 다 부분수열로 포함하는 가장 짧은 문자열) ---- */
static uint16_t g_l[MAXP + 1][MAXP + 1];
static int lcs_fill(const char *a, int n, const char *b, int m) {
    for (int i = 0; i <= n; i++)
        for (int j = 0; j <= m; j++)
            g_l[i][j] = (uint16_t)((i == 0 || j == 0) ? 0
                       : a[i - 1] == b[j - 1] ? g_l[i - 1][j - 1] + 1
                       : g_l[i - 1][j] >= g_l[i][j - 1] ? g_l[i - 1][j] : g_l[i][j - 1]);
    return g_l[n][m];
}
static int scs(const char *a, int n, const char *b, int m, char *out) {
    int len = n + m - lcs_fill(a, n, b, m), i = n, j = m, k = len;
    out[len] = '\0';
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { out[--k] = a[i - 1]; i--; j--; }     /* 공통 글자는 한 번만 */
        else if (g_l[i - 1][j] >= g_l[i][j - 1]) out[--k] = a[--i];      /* LCS 에 안 쓰이는 쪽을 그대로 */
        else out[--k] = b[--j];
    }
    while (i > 0) out[--k] = a[--i];
    while (j > 0) out[--k] = b[--j];
    return len;
}
static bool is_subseq(const char *s, int k, const char *b, int m) {
    int j = 0;
    for (int i = 0; i < k; i++) { while (j < m && b[j] != s[i]) j++; if (j == m) return false; j++; }
    return true;
}
static int ref_lcs(const char *a, int n, const char *b, int m) {
    int best = 0;
    char sub[MAXP + 1];
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        int k = 0;
        for (int i = 0; i < n; i++) if (mask >> i & 1u) sub[k++] = a[i];
        if (k > best && is_subseq(sub, k, b, m)) best = k;
    }
    return best;
}

/* ---- 연습 5: 최장 공통 부분문자열(연속). E[i][j] = a[i-1]==b[j-1] ? E[i-1][j-1]+1 : 0, 한 줄 + 역방향 j ---- */
static int lcsubstr(const char *a, int n, const char *b, int m, int *ea) {
    uint16_t row[MAXP + 1] = { 0 };
    int best = 0;
    *ea = 0;
    for (int i = 1; i <= n; i++)
        for (int j = m; j >= 1; j--) {                  /* 내려가며: row[j-1] 은 아직 "이전 줄" 값 */
            row[j] = a[i - 1] == b[j - 1] ? (uint16_t)(row[j - 1] + 1) : 0;
            if (row[j] > best) { best = row[j]; *ea = i; }   /* a[ea-best .. ea) */
        }
    return best;
}
static int ref_lcsubstr(const char *a, int n, const char *b, int m) {
    int best = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            int l = 0;
            while (i + l < n && j + l < m && a[i + l] == b[j + l]) l++;
            if (l > best) best = l;
        }
    return best;
}

/* ---- 연습 6: 편집 거리 ≤ k 인가? |i − j| ≤ k 띠만 계산. 반환 = min(d, k + 1). O(k·n) ---- */
static int ed_band(const char *a, int n, const char *b, int m, int k) {
    const int INF = k + 1;
    if (n - m > k || m - n > k) return INF;
    int prev[MAXP + 2], cur[MAXP + 2];
    for (int j = 0; j <= m; j++) prev[j] = j <= k ? j : INF;
    for (int i = 1; i <= n; i++) {
        int lo = i - k > 1 ? i - k : 1, hi = i + k < m ? i + k : m;
        cur[0] = i <= k ? i : INF;
        if (lo - 1 >= 1) cur[lo - 1] = INF;            /* 띠 왼쪽 바깥 */
        for (int j = lo; j <= hi; j++) {
            int best = prev[j - 1] + (a[i - 1] != b[j - 1]);
            if (prev[j] + 1 < best) best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
            cur[j] = best < INF ? best : INF;
        }
        if (hi + 1 <= m) cur[hi + 1] = INF;            /* 다음 줄이 읽을 띠 오른쪽 바깥 */
        for (int j = lo - 1 >= 0 ? lo - 1 : 0; j <= hi + 1 && j <= m; j++) prev[j] = cur[j];
    }
    return prev[m] < INF ? prev[m] : INF;
}
static int ed_full(const char *a, int n, const char *b, int m) {
    static uint16_t D[MAXP + 1][MAXP + 1];
    for (int i = 0; i <= n; i++)
        for (int j = 0; j <= m; j++) {
            if (i == 0 || j == 0) { D[i][j] = (uint16_t)(i + j); continue; }
            int best = D[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            if (D[i - 1][j] + 1 < best) best = D[i - 1][j] + 1;
            if (D[i][j - 1] + 1 < best) best = D[i][j - 1] + 1;
            D[i][j] = (uint16_t)best;
        }
    return D[n][m];
}

static int rand_str(uint32_t *seed, char *s, int maxlen, int alpha) {
    int n = (int)(xs32(seed) % (uint32_t)(maxlen + 1));
    for (int i = 0; i < n; i++) s[i] = (char)('a' + xs32(seed) % (uint32_t)alpha);
    s[n] = '\0';
    return n;
}

int main(void) {
    uint32_t seed = 0x5EED0808u;
    static uint8_t g[GW][GW];
    static uint16_t cost[GW][GW];
    char mv[2 * GW + 1];

    /* 연습 1 */
    memset(g, 0, sizeof g);
    g[1][1] = 1;
    CHECK(grid_paths(g, 3, 3) == 2);                                   /* LeetCode 63 예 1 */
    memset(g, 0, sizeof g);
    {
        static uint64_t P[67][67];                                     /* 파스칼 삼각형 (기준) */
        for (int a = 0; a <= 66; a++) for (int b = 0; b <= a; b++) P[a][b] = (b == 0 || b == a) ? 1 : P[a - 1][b - 1] + P[a - 1][b];
        int bad = 0;
        for (int R = 1; R <= 34; R++) for (int C = 1; C <= 34; C++) bad += grid_paths(g, R, C) != P[R + C - 2][R - 1];
        CHECK(bad == 0);
        printf("ex1: empty grids 1..34 x 1..34 vs C(R+C-2, R-1) bad=%d, 34x34 = %llu\n", bad,
               (unsigned long long)grid_paths(g, 34, 34));
        int badr = 0;
        for (int t = 0; t < 2000; t++) {
            int R = 1 + (int)(xs32(&seed) % 7u), C = 1 + (int)(xs32(&seed) % 7u);
            for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) g[r][c] = xs32(&seed) % 4u == 0;
            badr += grid_paths(g, R, C) != ref_paths(g, R, C, 0, 0);
        }
        CHECK(badr == 0);
        printf("ex1: random 7x7 grids with obstacles vs path enumeration: 2000, bad=%d\n", badr);
    }

    /* 연습 2 */
    {
        const uint16_t ex[3][3] = { { 1, 3, 1 }, { 1, 5, 1 }, { 4, 2, 1 } };
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) cost[r][c] = ex[r][c];
        CHECK(min_path(cost, 3, 3, mv) == 7 && walk_cost(cost, 3, 3, mv) == 7);   /* LeetCode 64 예 1 */
        printf("ex2: LeetCode 64 example min = 7, moves %s\n", mv);
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            int R = 1 + (int)(xs32(&seed) % 6u), C = 1 + (int)(xs32(&seed) % 6u);
            for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) cost[r][c] = (uint16_t)(xs32(&seed) % 100u);
            uint32_t v = min_path(cost, R, C, mv);
            bad += v != ref_min_path(cost, R, C, 0, 0) || walk_cost(cost, R, C, mv) != v;
        }
        CHECK(bad == 0);
        printf("ex2: random <=6x6 vs all-paths brute force: 2000, bad=%d\n", bad);
    }

    /* 연습 3 */
    {
        uint8_t side[PMAX_N];
        const uint16_t e1[4] = { 1, 5, 11, 5 }, e2[4] = { 1, 2, 3, 5 };
        CHECK(can_partition(e1, 4, side) && !can_partition(e2, 4, side));   /* LeetCode 416 예 1, 2 */
        uint16_t x[PMAX_N];
        int bad = 0, yes = 0;
        for (int t = 0; t < 1500; t++) {
            int n = (int)(xs32(&seed) % 17u);
            for (int i = 0; i < n; i++) x[i] = (uint16_t)(1 + xs32(&seed) % (t % 2 ? 20u : 1000u));
            bool r = ref_partition(x, n), v = can_partition(x, n, side);
            if (v) {
                uint32_t s0 = 0, s1 = 0;
                for (int i = 0; i < n; i++) { if (side[i]) s1 += x[i]; else s0 += x[i]; }
                bad += s0 != s1;
                yes++;
            }
            bad += r != v;
        }
        CHECK(bad == 0);
        printf("ex3: partition 1500 random (n<=16) vs 2^n brute: bad=%d, partitionable=%d\n", bad, yes);
    }

    /* 연습 4 */
    {
        char out[2 * MAXP + 1], a[MAXP + 1], b[MAXP + 1];
        int len = scs("abac", 4, "cab", 3, out);
        CHECK(len == 5 && is_subseq("abac", 4, out, len) && is_subseq("cab", 3, out, len));   /* LeetCode 1092 */
        printf("ex4: SCS(abac, cab) = %s (len %d)\n", out, len);
        int bad = 0;
        for (int t = 0; t < 800; t++) {
            int n = rand_str(&seed, a, 12, 3), m = rand_str(&seed, b, 12, 3);
            len = scs(a, n, b, m, out);
            bad += len != n + m - ref_lcs(a, n, b, m) || (int)strlen(out) != len
                   || !is_subseq(a, n, out, len) || !is_subseq(b, m, out, len);
        }
        CHECK(bad == 0);
        printf("ex4: SCS 800 random (len<=12) — length = n+m-LCS(brute), both subsequences: bad=%d\n", bad);
    }

    /* 연습 5 */
    {
        char a[MAXP + 1], b[MAXP + 1];
        int ea;
        CHECK(lcsubstr("abcba", 5, "cbadg", 5, &ea) == 3 && memcmp(&"abcba"[ea - 3], "cba", 3) == 0);   /* LeetCode 718 예와 같은 모양 */
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            int n = rand_str(&seed, a, MAXP, 2 + t % 3), m = rand_str(&seed, b, MAXP, 2 + t % 3);
            int L = lcsubstr(a, n, b, m, &ea);
            bool found = L == 0;
            for (int j = 0; !found && j + L <= m; j++) found = memcmp(a + ea - L, b + j, (size_t)L) == 0;
            bad += L != ref_lcsubstr(a, n, b, m) || !found;
        }
        CHECK(bad == 0);
        printf("ex5: longest common substring 2000 random (len<=64) vs O(n*m*L) brute: bad=%d\n", bad);
    }

    /* 연습 6 */
    {
        char a[MAXP + 1], b[MAXP + 1];
        int bad = 0;
        for (int t = 0; t < 4000; t++) {
            int n = rand_str(&seed, a, MAXP, 2 + t % 4), m = rand_str(&seed, b, MAXP, 2 + t % 4);
            if (t % 2) { m = n; memcpy(b, a, (size_t)n + 1); for (int e = (int)(xs32(&seed) % 6u); e > 0 && m > 0; e--) b[xs32(&seed) % (uint32_t)m] = 'z'; }
            int k = (int)(xs32(&seed) % 10u), d = ed_full(a, n, b, m);
            bad += ed_band(a, n, b, m, k) != (d <= k ? d : k + 1);
        }
        CHECK(bad == 0);
        printf("ex6: banded ed 4000 random (k 0..9) == min(d, k+1): bad=%d\n", bad);
        static const char *const CMDS[] = { "reboot", "reset", "status", "stats", "log", "flash", "freq" };
        const char *typed[3] = { "staus", "rset", "flsah" };
        const char *want[3] = { "status", "reset", "flash" };
        for (int q = 0; q < 3; q++) {
            int bestd = 3, besti = -1, n = (int)strlen(typed[q]);
            for (int c = 0; c < 7; c++) {
                int d = ed_band(typed[q], n, CMDS[c], (int)strlen(CMDS[c]), 2);
                if (d < bestd) { bestd = d; besti = c; }
            }
            CHECK(besti >= 0 && strcmp(CMDS[besti], want[q]) == 0);
            printf("ex6: '%s' -> did you mean '%s'? (d=%d)\n", typed[q], besti >= 0 ? CMDS[besti] : "?", bestd);
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
