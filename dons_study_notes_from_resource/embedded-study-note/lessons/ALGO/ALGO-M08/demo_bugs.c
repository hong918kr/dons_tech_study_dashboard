/* ALGO-M08 자주 하는 실수 재현 — 틀린 버전을 정답과 랜덤 대조해 "얼마나 자주" 틀리는지 센다.
 * 1) 1D 배낭을 앞으로(w 증가) 돌림  2) 편집 거리 기저를 0 으로  3) 한 줄 편집 거리에서 대각값을 안 남김
 * 4) 가치/크기 비율 그리디  5) LCS 역추적 동점 처리(둘 다 맞음 — 다른 답이 나올 뿐)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MS 32
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static uint32_t knap_rev(int n, const uint32_t *s, const uint32_t *v, uint32_t W) {
    uint32_t dp[256] = { 0 };
    for (int i = 0; i < n; i++) for (uint32_t w = W + 1; w-- > s[i]; ) if (dp[w - s[i]] + v[i] > dp[w]) dp[w] = dp[w - s[i]] + v[i];
    return dp[W];
}
static uint32_t knap_fwd(int n, const uint32_t *s, const uint32_t *v, uint32_t W) {     /* 버그: w 증가 */
    uint32_t dp[256] = { 0 };
    for (int i = 0; i < n; i++) for (uint32_t w = s[i]; w <= W; w++) if (dp[w - s[i]] + v[i] > dp[w]) dp[w] = dp[w - s[i]] + v[i];
    return dp[W];
}
static uint32_t knap_unbounded_2d(int n, const uint32_t *s, const uint32_t *v, uint32_t W) {  /* 무제한 배낭 2D 정의 */
    static uint32_t K[16][256];
    for (uint32_t w = 0; w <= W; w++) K[0][w] = 0;
    for (int i = 1; i <= n; i++)
        for (uint32_t w = 0; w <= W; w++) {
            K[i][w] = K[i - 1][w];
            if (s[i - 1] <= w && K[i][w - s[i - 1]] + v[i - 1] > K[i][w]) K[i][w] = K[i][w - s[i - 1]] + v[i - 1];   /* 같은 줄 i */
        }
    return K[n][W];
}
static uint32_t greedy_ratio(int n, const uint32_t *s, const uint32_t *v, uint32_t W) {
    int idx[16];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++)                                   /* v/s 내림차순 (교차곱 비교, 나눗셈 없음) */
        for (int j = i; j > 0 && (uint64_t)v[idx[j]] * s[idx[j - 1]] > (uint64_t)v[idx[j - 1]] * s[idx[j]]; j--) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
    uint32_t cap = W, val = 0;
    for (int k = 0; k < n; k++) if (s[idx[k]] <= cap) { cap -= s[idx[k]]; val += v[idx[k]]; }
    return val;
}

static int ed_ok(const char *a, int n, const char *b, int m, int zero_base) {
    static int D[MS + 1][MS + 1];
    for (int i = 0; i <= n; i++) D[i][0] = zero_base ? 0 : i;
    for (int j = 0; j <= m; j++) D[0][j] = zero_base ? 0 : j;
    for (int i = 1; i <= n; i++) for (int j = 1; j <= m; j++) {
        int best = D[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
        if (D[i - 1][j] + 1 < best) best = D[i - 1][j] + 1;
        if (D[i][j - 1] + 1 < best) best = D[i][j - 1] + 1;
        D[i][j] = best;
    }
    return D[n][m];
}
static int ed_row_nodiag(const char *a, int n, const char *b, int m) {   /* 버그: 대각 = 이미 덮어쓴 row[j-1] */
    int row[MS + 1];
    for (int j = 0; j <= m; j++) row[j] = j;
    for (int i = 1; i <= n; i++) {
        row[0] = i;
        for (int j = 1; j <= m; j++) {
            int best = row[j - 1] + (a[i - 1] != b[j - 1]);          /* 틀림: row[j-1] 은 이미 이번 줄 값 */
            if (row[j] + 1 < best) best = row[j] + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
        }
    }
    return row[m];
}
static void lcs_str(const char *a, int n, const char *b, int m, int prefer_up, char *out) {
    static int L[MS + 1][MS + 1];
    for (int i = 0; i <= n; i++) for (int j = 0; j <= m; j++)
        L[i][j] = (i == 0 || j == 0) ? 0 : a[i - 1] == b[j - 1] ? L[i - 1][j - 1] + 1 : (L[i - 1][j] >= L[i][j - 1] ? L[i - 1][j] : L[i][j - 1]);
    int i = n, j = m, k = L[n][m];
    out[k] = '\0';
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { out[--k] = a[i - 1]; i--; j--; }
        else if (prefer_up ? L[i - 1][j] >= L[i][j - 1] : L[i - 1][j] > L[i][j - 1]) i--;
        else j--;
    }
}
static int rs(uint32_t *seed, char *s, int maxlen, int al) {
    int n = (int)(xs32(seed) % (uint32_t)(maxlen + 1));
    for (int i = 0; i < n; i++) s[i] = (char)('a' + xs32(seed) % (uint32_t)al);
    s[n] = '\0';
    return n;
}

int main(void) {
    uint32_t seed = 0xB06B06u;
    /* 1) 앞으로 도는 1D 배낭 */
    {
        const uint32_t s[1] = { 2 }, v[1] = { 3 };
        printf("[1] one item (size 2, value 3), W = 6: reverse loop = %u, forward loop = %u\n",
               knap_rev(1, s, v, 6), knap_fwd(1, s, v, 6));
        uint32_t S[16], V[16];
        int wrong = 0, eq_unb = 0;
        for (int t = 0; t < 10000; t++) {
            int n = 1 + (int)(xs32(&seed) % 10u);
            uint32_t W = xs32(&seed) % 101u;
            for (int i = 0; i < n; i++) { S[i] = 1 + xs32(&seed) % 20u; V[i] = 1 + xs32(&seed) % 100u; }
            uint32_t f = knap_fwd(n, S, V, W);
            wrong += f != knap_rev(n, S, V, W);
            eq_unb += f == knap_unbounded_2d(n, S, V, W);
        }
        printf("    10000 random (n<=10, W<=100): forward wrong %d, forward == unbounded-knapsack %d/10000\n", wrong, eq_unb);
    }
    /* 2) 3) 편집 거리 */
    {
        char a[MS + 1], b[MS + 1];
        printf("[2] base D[i][0]=D[0][j]=0: ed(\"abc\",\"\") = %d (should be %d), ed(\"xabcx\",\"abc\") = %d (should be %d)\n",
               ed_ok("abc", 3, "", 0, 1), ed_ok("abc", 3, "", 0, 0), ed_ok("xabcx", 5, "abc", 3, 1), ed_ok("xabcx", 5, "abc", 3, 0));
        int w2 = 0, w3 = 0;
        for (int t = 0; t < 10000; t++) {
            int n = rs(&seed, a, MS, 4), m = rs(&seed, b, MS, 4);
            int d = ed_ok(a, n, b, m, 0);
            w2 += ed_ok(a, n, b, m, 1) != d;
            w3 += ed_row_nodiag(a, n, b, m) != d;
        }
        printf("    10000 random (len<=32, |S|=4): zero-base wrong %d, 1-row without saved diag wrong %d\n", w2, w3);
        printf("[3] 1-row without diag: ed(\"kitten\",\"sitting\") = %d (should be 3)\n", ed_row_nodiag("kitten", 6, "sitting", 7));
    }
    /* 4) 비율 그리디 */
    {
        const uint32_t s3[3] = { 10, 20, 30 }, v3[3] = { 60, 100, 120 };
        printf("[4] ratio greedy on {10:60, 20:100, 30:120}, W=50: greedy %u, optimum %u\n",
               greedy_ratio(3, s3, v3, 50), knap_rev(3, s3, v3, 50));
        uint32_t S[16], V[16];
        int sub = 0;
        double worst = 1.0, worst_fix = 1.0;
        for (int t = 0; t < 10000; t++) {
            int n = 1 + (int)(xs32(&seed) % 10u);
            uint32_t W = 1 + xs32(&seed) % 100u;
            for (int i = 0; i < n; i++) { S[i] = 1 + xs32(&seed) % 60u; V[i] = 1 + xs32(&seed) % 100u; }
            uint32_t opt = knap_rev(n, S, V, W), g = greedy_ratio(n, S, V, W), best1 = 0;
            for (int i = 0; i < n; i++) if (S[i] <= W && V[i] > best1) best1 = V[i];
            uint32_t gf = g > best1 ? g : best1;                   /* max(그리디, 가장 비싼 단일 아이템) */
            if (opt == 0) continue;
            sub += g < opt;
            if ((double)g / opt < worst) worst = (double)g / opt;
            if ((double)gf / opt < worst_fix) worst_fix = (double)gf / opt;
        }
        printf("    10000 random (n<=10): greedy suboptimal %d, worst greedy/opt %.3f, worst max(greedy,best single)/opt %.3f\n",
               sub, worst, worst_fix);
    }
    /* 5) LCS 동점 처리 */
    {
        char a[MS + 1], b[MS + 1], u[MS + 1], l[MS + 1];
        lcs_str("ABCBDAB", 7, "BDCABA", 6, 1, u);
        lcs_str("ABCBDAB", 7, "BDCABA", 6, 0, l);
        printf("[5] LCS(ABCBDAB, BDCABA): prefer-up %s, prefer-left %s (both length 4)\n", u, l);
        int diff = 0, samelen = 0;
        for (int t = 0; t < 10000; t++) {
            int n = rs(&seed, a, MS, 4), m = rs(&seed, b, MS, 4);
            lcs_str(a, n, b, m, 1, u); lcs_str(a, n, b, m, 0, l);
            diff += strcmp(u, l) != 0;
            samelen += strlen(u) == strlen(l);
        }
        printf("    10000 random (len<=32, |S|=4): different LCS strings %d, same length %d/10000\n", diff, samelen);
    }
    return 0;
}
