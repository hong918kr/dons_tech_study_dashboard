/* ALGO-M08 동적 계획법 2D: 편집 거리·LCS·0/1 배낭
 *
 * Part A: 편집 거리(Levenshtein) 2D 표 + 편집 스크립트 역추적, LCS 2D 표 + 역추적
 * Part B: 편집 거리 한 줄(1-row) O(min(n,m)) 공간, 0/1 배낭 2D 표 + 선택 역추적, O(W) 1D 배낭(역방향 루프)
 * Part C: 펌웨어 설정 diff(키를 아는 줄 단위 편집 거리), Flash 예산 모듈 선택(섹터 단위 1D DP + 결정 비트셋 역추적)
 *
 * 테스트: 문자열 그래프 BFS(편집 연산의 정의 그대로)·정렬 정의 재귀·부분수열 전수·부분집합 전수와 대조,
 *         xorshift32 고정 시드 랜덤 + 작은 크기 전수 + 공개 예제(LeetCode 72/1143, kitten/sitting, CLRS, Rosetta Code).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_S      64       /* 문자열 최대 길이 */
#define MAX_LINES  32       /* 설정 파일 최대 줄 수 */
#define KN_MAX_N   32       /* 배낭 최대 아이템 수 */
#define KN2_MAX_W  2048     /* 2D 표 배낭 최대 용량 */
#define K1_MAX_W   131072   /* 1D 배낭 최대 용량(단위 수) */

/* 편집 스크립트 한 칸. k: '=' 같음, '~' 치환, '-' a[i] 삭제, '+' b[j] 삽입.
 * (i, j) = 이 연산 직전의 a, b 위치. */
typedef struct { char k; int16_t i, j; } Op;

static void reverse_ops(Op *ops, int k) {
    for (int l = 0, r = k - 1; l < r; l++, r--) { Op t = ops[l]; ops[l] = ops[r]; ops[r] = t; }
}

/* ---------------- Part A: 2D 표 + 역추적 ---------------- */

static uint16_t g_ed[MAX_S + 1][MAX_S + 1];    /* D[i][j] = a[0..i) → b[0..j) 최소 편집 수 */
static uint16_t g_lcs[MAX_S + 1][MAX_S + 1];   /* L[i][j] = a[0..i), b[0..j) 의 LCS 길이 */

/* 편집 거리 표를 채우고 D[n][m] 을 돌려준다 (삽입·삭제·치환 각 비용 1) */
static int ed_table(const char *a, int n, const char *b, int m) {
    for (int i = 0; i <= n; i++) g_ed[i][0] = (uint16_t)i;      /* a 앞 i 글자 전부 삭제 */
    for (int j = 0; j <= m; j++) g_ed[0][j] = (uint16_t)j;      /* b 앞 j 글자 전부 삽입 */
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            int best = g_ed[i - 1][j - 1] + (a[i - 1] != b[j - 1]);   /* 같음(0) 또는 치환(1) */
            int del = g_ed[i - 1][j] + 1, ins = g_ed[i][j - 1] + 1;
            if (del < best) best = del;
            if (ins < best) best = ins;
            g_ed[i][j] = (uint16_t)best;
        }
    return g_ed[n][m];
}

/* 표를 (n,m) → (0,0) 으로 거슬러 편집 스크립트를 만든다. 우선순위: 대각 → 삭제 → 삽입.
 * ops 는 앞에서부터의 순서로 담는다. 반환 = op 개수 (≤ n + m) */
static int ed_script(const char *a, int n, const char *b, int m, Op *ops) {
    ed_table(a, n, b, m);
    int i = n, j = m, k = 0;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && g_ed[i][j] == g_ed[i - 1][j - 1] + (a[i - 1] != b[j - 1])) {
            i--; j--;
            ops[k++] = (Op){ a[i] == b[j] ? '=' : '~', (int16_t)i, (int16_t)j };
        } else if (i > 0 && g_ed[i][j] == g_ed[i - 1][j] + 1) {
            i--;
            ops[k++] = (Op){ '-', (int16_t)i, (int16_t)j };
        } else {                                                   /* 남은 경우는 삽입뿐 (j > 0) */
            j--;
            ops[k++] = (Op){ '+', (int16_t)i, (int16_t)j };
        }
    }
    reverse_ops(ops, k);
    return k;
}

/* LCS 길이 표 */
static int lcs_table(const char *a, int n, const char *b, int m) {
    for (int i = 0; i <= n; i++) g_lcs[i][0] = 0;
    for (int j = 0; j <= m; j++) g_lcs[0][j] = 0;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            if (a[i - 1] == b[j - 1]) g_lcs[i][j] = (uint16_t)(g_lcs[i - 1][j - 1] + 1);
            else g_lcs[i][j] = g_lcs[i - 1][j] >= g_lcs[i][j - 1] ? g_lcs[i - 1][j] : g_lcs[i][j - 1];
        }
    return g_lcs[n][m];
}

/* LCS 하나를 out 에 (NUL 종료) — 끝에서부터 거슬러 채운다. 반환 = 길이 */
static int lcs_string(const char *a, int n, const char *b, int m, char *out) {
    int len = lcs_table(a, n, b, m);
    int i = n, j = m, k = len;
    out[len] = '\0';
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { out[--k] = a[i - 1]; i--; j--; }
        else if (g_lcs[i - 1][j] >= g_lcs[i][j - 1]) i--;
        else j--;
    }
    return len;
}

/* ---------------- Part B: 공간 축소, 0/1 배낭 ---------------- */

/* 편집 거리를 배열 한 줄(짧은 쪽 길이 + 1)로. 대각 값은 변수 하나에 잠시 보관 */
static int ed_1row(const char *a, int n, const char *b, int m) {
    if (m > n) { const char *t = a; a = b; b = t; int u = n; n = m; m = u; }   /* d(a,b) = d(b,a) */
    uint16_t row[MAX_S + 1];
    for (int j = 0; j <= m; j++) row[j] = (uint16_t)j;
    for (int i = 1; i <= n; i++) {
        uint16_t diag = row[0];                  /* D[i-1][j-1] */
        row[0] = (uint16_t)i;
        for (int j = 1; j <= m; j++) {
            uint16_t up = row[j];                /* D[i-1][j] (아직 안 덮어씀) */
            int best = diag + (a[i - 1] != b[j - 1]);
            if (up + 1 < best) best = up + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;   /* D[i][j-1] (방금 덮어씀) */
            row[j] = (uint16_t)best;
            diag = up;
        }
    }
    return row[m];
}

static uint32_t g_kn[KN_MAX_N + 1][KN2_MAX_W + 1];   /* K[i][w] = 아이템 0..i-1, 용량 w 의 최대 가치 */

/* 2D 표 0/1 배낭 + 선택 역추적. chosen[i] = 1 이면 아이템 i 선택. W ≤ KN2_MAX_W, n ≤ KN_MAX_N */
static uint32_t knap_2d(int n, const uint32_t *size, const uint32_t *value, uint32_t W, uint8_t *chosen) {
    for (uint32_t w = 0; w <= W; w++) g_kn[0][w] = 0;
    for (int i = 1; i <= n; i++)
        for (uint32_t w = 0; w <= W; w++) {
            uint32_t best = g_kn[i - 1][w];                              /* 안 넣음 */
            if (size[i - 1] <= w) {
                uint32_t t = g_kn[i - 1][w - size[i - 1]] + value[i - 1];   /* 넣음 */
                if (t > best) best = t;
            }
            g_kn[i][w] = best;
        }
    uint32_t w = W;
    for (int i = n; i >= 1; i--) {
        if (g_kn[i][w] != g_kn[i - 1][w]) { chosen[i - 1] = 1; w -= size[i - 1]; }
        else chosen[i - 1] = 0;
    }
    return g_kn[n][W];
}

/* 1D 배낭: dp[w] 하나로. w 를 큰 쪽에서 작은 쪽으로 돌아야 dp[w - s] 가 "아이템 i 전" 값이다 */
static uint32_t knap_1d(int n, const uint32_t *size, const uint32_t *value, uint32_t W) {
    static uint32_t dp[K1_MAX_W + 1];
    for (uint32_t w = 0; w <= W; w++) dp[w] = 0;
    for (int i = 0; i < n; i++) {
        uint32_t s = size[i], v = value[i];
        if (s > W) continue;
        for (uint32_t w = W + 1; w-- > s; ) {        /* w = W, W-1, ..., s */
            uint32_t t = dp[w - s] + v;
            if (t > dp[w]) dp[w] = t;
        }
    }
    return dp[W];
}

/* ---------------- Part C: 설정 diff, Flash 모듈 선택 ---------------- */

#define CD_INF 1000   /* 키가 다른 두 줄은 치환 금지 (삭제 + 삽입 = 2 보다 훨씬 크게) */

/* "key=value" 줄의 키가 같은가 ('=' 앞부분 비교, '=' 없으면 줄 전체가 키) */
static bool same_key(const char *x, const char *y) {
    size_t kx = strcspn(x, "="), ky = strcspn(y, "=");
    return kx == ky && memcmp(x, y, kx) == 0;
}
/* 줄 치환 비용: 같으면 0. key_aware 면 같은 키일 때만 1, 아니면 금지. key_aware 가 아니면 항상 1 */
static int line_cost(const char *x, const char *y, bool key_aware) {
    if (strcmp(x, y) == 0) return 0;
    if (!key_aware) return 1;
    return same_key(x, y) ? 1 : CD_INF;
}

static int g_cd[MAX_LINES + 1][MAX_LINES + 1];

/* 줄 단위 가중 편집 거리 + 스크립트. 반환 = 총 비용, *nops = 스크립트 길이. 우선순위 대각 → 삭제 → 삽입 */
static int cfg_diff(const char *const *o, int n, const char *const *w, int m, bool key_aware, Op *ops, int *nops) {
    for (int i = 0; i <= n; i++) g_cd[i][0] = i;
    for (int j = 0; j <= m; j++) g_cd[0][j] = j;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            int best = g_cd[i - 1][j - 1] + line_cost(o[i - 1], w[j - 1], key_aware);
            if (g_cd[i - 1][j] + 1 < best) best = g_cd[i - 1][j] + 1;
            if (g_cd[i][j - 1] + 1 < best) best = g_cd[i][j - 1] + 1;
            g_cd[i][j] = best;
        }
    int i = n, j = m, k = 0;
    while (i > 0 || j > 0) {
        int c = (i > 0 && j > 0) ? line_cost(o[i - 1], w[j - 1], key_aware) : CD_INF;
        if (i > 0 && j > 0 && g_cd[i][j] == g_cd[i - 1][j - 1] + c) {
            i--; j--;
            ops[k++] = (Op){ c == 0 ? '=' : '~', (int16_t)i, (int16_t)j };
        } else if (i > 0 && g_cd[i][j] == g_cd[i - 1][j] + 1) {
            i--;
            ops[k++] = (Op){ '-', (int16_t)i, (int16_t)j };
        } else {
            j--;
            ops[k++] = (Op){ '+', (int16_t)i, (int16_t)j };
        }
    }
    reverse_ops(ops, k);
    *nops = k;
    return g_cd[n][m];
}

typedef struct { const char *name; uint32_t size; uint32_t value; } Module;

static uint32_t g_dp1[K1_MAX_W + 1];
static uint8_t  g_take[KN_MAX_N][K1_MAX_W / 8 + 1];   /* 결정 비트: 아이템 i 를 용량 w 에서 넣었나 */

/* Flash 예산 budget 바이트 안에서 가치 합 최대. 모듈은 gran 바이트 경계(섹터)에 정렬해 배치하므로
 * 크기를 ceil(size / gran) 단위로 본다. 1D dp + 결정 비트셋(n·W 비트)으로 역추적.
 * chosen[i] = 선택 여부, *used = 선택 모듈의 정렬된 크기 합(바이트). 반환 = 최대 가치 */
static uint32_t flash_select(const Module *md, int n, uint32_t budget, uint32_t gran,
                             uint8_t *chosen, uint32_t *used) {
    uint32_t W = budget / gran;
    uint32_t s[KN_MAX_N];
    for (uint32_t w = 0; w <= W; w++) g_dp1[w] = 0;
    for (int i = 0; i < n; i++) {
        s[i] = md[i].size / gran + (md[i].size % gran != 0);
        memset(g_take[i], 0, W / 8 + 1);
        if (s[i] > W) continue;
        for (uint32_t w = W + 1; w-- > s[i]; ) {
            uint32_t t = g_dp1[w - s[i]] + md[i].value;
            if (t > g_dp1[w]) { g_dp1[w] = t; g_take[i][w >> 3] |= (uint8_t)(1u << (w & 7)); }
        }
    }
    uint32_t w = W, u = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (g_take[i][w >> 3] >> (w & 7) & 1u) { chosen[i] = 1; w -= s[i]; u += s[i] * gran; }
        else chosen[i] = 0;
    }
    *used = u;
    return g_dp1[W];
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int rand_str(uint32_t *seed, char *s, int maxlen, int alpha) {
    int n = (int)(xs32(seed) % (uint32_t)(maxlen + 1));
    for (int i = 0; i < n; i++) s[i] = (char)('a' + xs32(seed) % (uint32_t)alpha);
    s[n] = '\0';
    return n;
}

/* 기준 1: 편집 연산의 정의 그대로 — {a,b} 문자열(길이 ≤ 6) 그래프에서 BFS.
 * 정점 = 문자열, 간선 = 한 글자 삭제 / 삽입 / (allow_sub 면) 치환. id = 2^L - 1 + 비트값 */
#define BFS_L 6
#define BFS_N 127
static int str_id(const char *s, int L) { int v = 0; for (int p = 0; p < L; p++) if (s[p] == 'b') v |= 1 << p; return (1 << L) - 1 + v; }
static int id_str(int id, char *s) {
    int L = 0;
    while ((1 << (L + 1)) - 1 <= id) L++;
    int v = id - ((1 << L) - 1);
    for (int p = 0; p < L; p++) s[p] = (v >> p & 1) ? 'b' : 'a';
    s[L] = '\0';
    return L;
}
static void bfs_dist(int src, bool allow_sub, int *dist) {
    int q[BFS_N], qh = 0, qt = 0;
    for (int v = 0; v < BFS_N; v++) dist[v] = -1;
    dist[src] = 0; q[qt++] = src;
    while (qh < qt) {
        int u = q[qh++];
        char s[BFS_L + 2], t[BFS_L + 2];
        int L = id_str(u, s);
        int nb[64], k = 0;
        for (int p = 0; p < L; p++) {                                 /* 삭제 */
            int tl = 0;
            for (int r = 0; r < L; r++) if (r != p) t[tl++] = s[r];
            nb[k++] = str_id(t, tl);
        }
        if (L < BFS_L)
            for (int p = 0; p <= L; p++)                              /* 삽입 */
                for (char c = 'a'; c <= 'b'; c++) {
                    int tl = 0;
                    for (int r = 0; r < p; r++) t[tl++] = s[r];
                    t[tl++] = c;
                    for (int r = p; r < L; r++) t[tl++] = s[r];
                    nb[k++] = str_id(t, tl);
                }
        if (allow_sub)
            for (int p = 0; p < L; p++) {                             /* 치환 */
                memcpy(t, s, (size_t)L);
                t[p] = s[p] == 'a' ? 'b' : 'a';
                nb[k++] = str_id(t, L);
            }
        for (int e = 0; e < k; e++)
            if (dist[nb[e]] < 0) { dist[nb[e]] = dist[u] + 1; q[qt++] = nb[e]; }
    }
}

/* 기준 2: 정렬(alignment) 정의 그대로의 재귀 — 마지막 열이 (삭제 | 삽입 | 맞춤) 중 하나. 표 없음, 지수 시간 */
static int ref_ed(const char *a, int n, const char *b, int m) {
    if (n == 0) return m;
    if (m == 0) return n;
    int best = ref_ed(a, n - 1, b, m) + 1;
    int t = ref_ed(a, n, b, m - 1) + 1;
    if (t < best) best = t;
    t = ref_ed(a, n - 1, b, m - 1) + (a[n - 1] != b[m - 1]);
    return t < best ? t : best;
}

/* 기준 3: a 의 부분수열 2^n 개를 전부 만들어 b 의 부분수열인지 확인 */
static bool is_subseq(const char *s, int k, const char *b, int m) {
    int j = 0;
    for (int i = 0; i < k; i++) { while (j < m && b[j] != s[i]) j++; if (j == m) return false; j++; }
    return true;
}
static int ref_lcs(const char *a, int n, const char *b, int m) {
    int best = 0;
    char sub[MAX_S + 1];
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        int k = 0;
        for (int i = 0; i < n; i++) if (mask >> i & 1u) sub[k++] = a[i];
        if (k > best && is_subseq(sub, k, b, m)) best = k;
    }
    return best;
}

/* 스크립트 검증: 순서대로 적용하면 a 를 다 쓰고 b 를 정확히 만든다 + 비용 계산 */
static bool script_ok(const char *a, int n, const char *b, int m, const Op *ops, int k, int *cost) {
    int i = 0, j = 0, c = 0;
    for (int t = 0; t < k; t++) {
        Op o = ops[t];
        if (o.i != i || o.j != j) return false;
        if (o.k == '=') { if (i >= n || j >= m || a[i] != b[j]) return false; i++; j++; }
        else if (o.k == '~') { if (i >= n || j >= m || a[i] == b[j]) return false; i++; j++; c++; }
        else if (o.k == '-') { if (i >= n) return false; i++; c++; }
        else if (o.k == '+') { if (j >= m) return false; j++; c++; }
        else return false;
    }
    *cost = c;
    return i == n && j == m;
}

static void print_alignment(const char *a, const char *b, const Op *ops, int k) {
    char la[2 * MAX_S + 2], lb[2 * MAX_S + 2], lo[2 * MAX_S + 2];
    for (int t = 0; t < k; t++) {
        la[t] = ops[t].k == '+' ? '-' : a[ops[t].i];
        lb[t] = ops[t].k == '-' ? '-' : b[ops[t].j];
        lo[t] = ops[t].k == '=' ? '|' : ops[t].k == '~' ? 'S' : ops[t].k == '-' ? 'D' : 'I';
    }
    la[k] = lb[k] = lo[k] = '\0';
    printf("  a  : %s\n  b  : %s\n  op : %s\n", la, lb, lo);
}

/* 줄 diff 기준: 정의 그대로의 가중 재귀 */
static int ref_cd(const char *const *o, int n, const char *const *w, int m, bool ka) {
    if (n == 0) return m;
    if (m == 0) return n;
    int best = ref_cd(o, n - 1, w, m, ka) + 1;
    int t = ref_cd(o, n, w, m - 1, ka) + 1;
    if (t < best) best = t;
    t = ref_cd(o, n - 1, w, m - 1, ka) + line_cost(o[n - 1], w[m - 1], ka);
    return t < best ? t : best;
}
static bool cfg_script_ok(const char *const *o, int n, const char *const *w, int m, bool ka,
                          const Op *ops, int k, int *cost) {
    int i = 0, j = 0, c = 0;
    for (int t = 0; t < k; t++) {
        Op p = ops[t];
        if (p.i != i || p.j != j) return false;
        if (p.k == '=' || p.k == '~') {
            if (i >= n || j >= m) return false;
            int lc = line_cost(o[i], w[j], ka);
            if ((p.k == '=') != (lc == 0) || lc >= CD_INF) return false;
            c += lc; i++; j++;
        } else if (p.k == '-') { if (i >= n) return false; i++; c++; }
        else if (p.k == '+') { if (j >= m) return false; j++; c++; }
        else return false;
    }
    *cost = c;
    return i == n && j == m;
}
static void print_cfg_diff(const char *const *o, const char *const *w, const Op *ops, int k) {
    for (int t = 0; t < k; t++) {
        Op p = ops[t];
        if (p.k == '=') printf("    %s\n", o[p.i]);
        else if (p.k == '~') printf("  ~ %s  ->  %s\n", o[p.i], w[p.j]);
        else if (p.k == '-') printf("  - %s\n", o[p.i]);
        else printf("  + %s\n", w[p.j]);
    }
}

/* 배낭 기준: 부분집합 2^n 전수 (Gray 코드로 한 비트씩 바꿔 O(1) 갱신). 최적 부분집합 수도 센다 */
static uint32_t ref_knap(int n, const uint32_t *size, const uint32_t *value, uint32_t W, uint32_t *n_opt) {
    uint64_t ss = 0, vv = 0, best = 0;
    uint32_t cnt = 1, g = 0;                                   /* 빈 집합: 가치 0, 1개 */
    for (uint32_t k = 1; k < (1u << n); k++) {
        int bit = __builtin_ctz(k);                            /* 테스트 쪽이라 builtin 사용 */
        g ^= 1u << bit;
        if (g >> bit & 1u) { ss += size[bit]; vv += value[bit]; } else { ss -= size[bit]; vv -= value[bit]; }
        if (ss <= W) { if (vv > best) { best = vv; cnt = 1; } else if (vv == best) cnt++; }
    }
    if (n_opt) *n_opt = cnt;
    return (uint32_t)best;
}
static bool pick_ok(int n, const uint32_t *size, const uint32_t *value, uint32_t W, const uint8_t *ch, uint32_t want) {
    uint64_t s = 0, v = 0;
    for (int i = 0; i < n; i++) { if (ch[i] > 1) return false; if (ch[i]) { s += size[i]; v += value[i]; } }
    return s <= W && v == want;
}

static const Module MODS[] = {
    { "ble_stack",     38912, 90 }, { "ota_dfu",     26100, 80 }, { "sensor_fusion", 17300, 70 },
    { "tinyml_kws",    30500, 60 }, { "crypto_aes",   9000, 45 }, { "usb_cdc",       12100, 40 },
    { "fat_fs",        20400, 35 }, { "lfs_logging", 14200, 30 }, { "power_mgr",      3900, 25 },
    { "shell_cli",      6000, 15 },
};
#define NMODS ((int)(sizeof MODS / sizeof MODS[0]))

/* Flash 기준: 부분집합 전수 (정렬된 크기 합 ≤ budget) */
static uint32_t ref_flash(const Module *md, int n, uint32_t budget, uint32_t gran) {
    uint32_t best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        uint64_t s = 0, v = 0;
        for (int i = 0; i < n; i++)
            if (mask >> i & 1u) { s += (uint64_t)(md[i].size / gran + (md[i].size % gran != 0)) * gran; v += md[i].value; }
        if (s <= budget && v > best) best = (uint32_t)v;
    }
    return best;
}

int main(void) {
    uint32_t seed = 0xA16008u;
    static Op ops[2 * MAX_S + 2];
    char out[MAX_S + 1];
    int cost = -1;

    /* ===== Part A: 공개 예제 ===== */
    CHECK(ed_table("kitten", 6, "sitting", 7) == 3);        /* Wikipedia Levenshtein */
    CHECK(ed_table("flaw", 4, "lawn", 4) == 2);
    CHECK(ed_table("horse", 5, "ros", 3) == 3);             /* LeetCode 72 예 1 */
    CHECK(ed_table("intention", 9, "execution", 9) == 5);   /* LeetCode 72 예 2 */
    CHECK(ed_table("", 0, "abc", 3) == 3 && ed_table("abc", 3, "", 0) == 3 && ed_table("", 0, "", 0) == 0);
    int k = ed_script("kitten", 6, "sitting", 7, ops);
    CHECK(script_ok("kitten", 6, "sitting", 7, ops, k, &cost) && cost == 3);
    printf("kitten -> sitting, distance %d, script:\n", cost);
    print_alignment("kitten", "sitting", ops, k);
    k = ed_script("intention", 9, "execution", 9, ops);
    CHECK(script_ok("intention", 9, "execution", 9, ops, k, &cost) && cost == 5);
    printf("intention -> execution, distance %d, script:\n", cost);
    print_alignment("intention", "execution", ops, k);

    CHECK(lcs_string("abcde", 5, "ace", 3, out) == 3 && strcmp(out, "ace") == 0);   /* LeetCode 1143 예 1 */
    CHECK(lcs_string("abc", 3, "def", 3, out) == 0 && out[0] == '\0');              /* LeetCode 1143 예 3 */
    CHECK(lcs_string("ABCBDAB", 7, "BDCABA", 6, out) == 4                          /* CLRS 15.4 */
          && is_subseq(out, 4, "ABCBDAB", 7) && is_subseq(out, 4, "BDCABA", 6));
    printf("LCS(ABCBDAB, BDCABA) = %s\n", out);

    /* ===== Part A+B: {a,b} 길이 ≤ 5 모든 쌍 (63 × 63) — BFS 와 전수 대조 ===== */
    {
        static int dfull[BFS_N], dindel[BFS_N];
        int bad_ed = 0, bad_sc = 0, bad_lcs = 0, bad_str = 0, bad_row = 0, pairs = 0;
        char a[BFS_L + 2], b[BFS_L + 2];
        for (int ia = 0; ia < 63; ia++) {
            int n = id_str(ia, a);
            bfs_dist(ia, true, dfull);
            bfs_dist(ia, false, dindel);
            for (int ib = 0; ib < 63; ib++) {
                int m = id_str(ib, b);
                pairs++;
                int d = ed_table(a, n, b, m);
                bad_ed += d != dfull[ib];
                k = ed_script(a, n, b, m, ops);
                bad_sc += !(script_ok(a, n, b, m, ops, k, &cost) && cost == dfull[ib]);
                int L = lcs_string(a, n, b, m, out);
                bad_lcs += n + m - 2 * L != dindel[ib];               /* 삽입·삭제만의 거리 = n + m − 2·LCS */
                bad_str += !((int)strlen(out) == L && is_subseq(out, L, a, n) && is_subseq(out, L, b, m));
                bad_row += ed_1row(a, n, b, m) != dfull[ib];
            }
        }
        printf("exhaustive {a,b} len<=5: %d pairs vs BFS, bad ed=%d script=%d lcs=%d lcs_str=%d 1row=%d\n",
               pairs, bad_ed, bad_sc, bad_lcs, bad_str, bad_row);
        CHECK(bad_ed == 0); CHECK(bad_sc == 0); CHECK(bad_lcs == 0); CHECK(bad_str == 0); CHECK(bad_row == 0);
    }

    /* ===== Part A: 정의 재귀 / 부분수열 전수와 랜덤 대조 ===== */
    {
        char a[MAX_S + 1], b[MAX_S + 1];
        int bad_ed = 0, bad_lcs = 0;
        for (int t = 0; t < 1500; t++) {
            int n = rand_str(&seed, a, 7, 3), m = rand_str(&seed, b, 7, 3);
            bad_ed += ed_table(a, n, b, m) != ref_ed(a, n, b, m);
        }
        for (int t = 0; t < 600; t++) {
            int n = rand_str(&seed, a, 12, 4), m = rand_str(&seed, b, 12, 4);
            bad_lcs += lcs_table(a, n, b, m) != ref_lcs(a, n, b, m);
        }
        printf("random: ed vs definition recursion 1500 (len<=7, |S|=3) bad=%d; lcs vs 2^n brute 600 (len<=12, |S|=4) bad=%d\n",
               bad_ed, bad_lcs);
        CHECK(bad_ed == 0); CHECK(bad_lcs == 0);
    }

    /* ===== Part A+B: 길이 0~64 랜덤 — 스크립트·LCS 문자열 검증, 성질, 1-row 대조 ===== */
    {
        char a[MAX_S + 1], b[MAX_S + 1], c[MAX_S + 1];
        int bad_sc = 0, bad_str = 0, bad_row = 0, bad_prop = 0, bad_tri = 0;
        const int alphas[3] = { 2, 4, 26 };
        for (int t = 0; t < 3000; t++) {
            int al = alphas[t % 3];
            int n = rand_str(&seed, a, MAX_S, al), m = rand_str(&seed, b, MAX_S, al), q = rand_str(&seed, c, MAX_S, al);
            int d = ed_table(a, n, b, m);
            k = ed_script(a, n, b, m, ops);
            bad_sc += !(script_ok(a, n, b, m, ops, k, &cost) && cost == d);
            int L = lcs_string(a, n, b, m, out);
            bad_str += !((int)strlen(out) == L && is_subseq(out, L, a, n) && is_subseq(out, L, b, m));
            bad_row += ed_1row(a, n, b, m) != d || ed_1row(b, m, a, n) != d;
            int mx = n > m ? n : m, lo = mx - L, hi = n + m - 2 * L < mx ? n + m - 2 * L : mx;
            bad_prop += d < lo || d > hi || ed_table(b, m, a, n) != d;           /* max−L ≤ d ≤ min(max, n+m−2L), 대칭 */
            bad_tri += ed_table(a, n, c, q) > d + ed_table(b, m, c, q);          /* 삼각 부등식 */
        }
        printf("random len 0..64 x3000: bad script=%d lcs_str=%d 1row=%d bounds/symmetry=%d triangle=%d\n",
               bad_sc, bad_str, bad_row, bad_prop, bad_tri);
        CHECK(bad_sc == 0); CHECK(bad_str == 0); CHECK(bad_row == 0); CHECK(bad_prop == 0); CHECK(bad_tri == 0);
        CHECK(ed_1row("kitten", 6, "sitting", 7) == 3 && ed_1row("", 0, "", 0) == 0 && ed_1row("abc", 3, "", 0) == 3);
    }

    /* ===== Part B: 0/1 배낭 ===== */
    {
        uint8_t ch[KN_MAX_N];
        const uint32_t s3[3] = { 10, 20, 30 }, v3[3] = { 60, 100, 120 };
        CHECK(knap_2d(3, s3, v3, 50, ch) == 220 && !ch[0] && ch[1] && ch[2]);   /* 고전 예: 100 + 120 */
        CHECK(knap_1d(3, s3, v3, 50) == 220);
        CHECK(knap_2d(3, s3, v3, 0, ch) == 0 && knap_1d(3, s3, v3, 0) == 0);    /* 용량 0 */
        CHECK(knap_2d(0, s3, v3, 50, ch) == 0 && knap_1d(0, s3, v3, 50) == 0);  /* 아이템 0개 */
        CHECK(knap_2d(3, s3, v3, 9, ch) == 0 && knap_1d(3, s3, v3, 9) == 0);    /* 전부 너무 큼 */
        CHECK(knap_2d(3, s3, v3, 30, ch) == 160 && knap_1d(3, s3, v3, 30) == 160);   /* 딱 맞음: 10 + 20 */
        const uint32_t sz0[2] = { 0, 5 }, vz0[2] = { 7, 3 };
        CHECK(knap_2d(2, sz0, vz0, 4, ch) == 7 && ch[0] && !ch[1] && knap_1d(2, sz0, vz0, 4) == 7);  /* 크기 0 아이템 */

        /* Rosetta Code "Knapsack problem/0-1": 22개, W = 400 → 가치 1030, 무게 396 */
        static const uint32_t rs[22] = { 9, 13, 153, 50, 15, 68, 27, 39, 23, 52, 11, 32, 24, 48, 73, 42, 43, 22, 7, 18, 4, 30 };
        static const uint32_t rv[22] = { 150, 35, 200, 160, 60, 45, 60, 40, 30, 10, 70, 30, 15, 10, 40, 70, 75, 80, 20, 12, 50, 10 };
        uint32_t nopt = 0, best = knap_2d(22, rs, rv, 400, ch);
        uint32_t wsum = 0;
        for (int i = 0; i < 22; i++) if (ch[i]) wsum += rs[i];
        CHECK(best == 1030 && wsum == 396 && pick_ok(22, rs, rv, 400, ch, 1030));
        CHECK(knap_1d(22, rs, rv, 400) == 1030);
        CHECK(ref_knap(22, rs, rv, 400, &nopt) == 1030);
        printf("Rosetta 0-1 knapsack: value %u, weight %u, optimal subsets %u (2^22 brute force)\n", best, wsum, nopt);

        /* 작은 인스턴스 전수: n = 3, 크기 0..3, 가치 0..3, W 0..7 (4^3 · 4^3 · 8 = 32768) */
        int inst = 0, bad2 = 0, bad1 = 0, badp = 0;
        for (uint32_t sc = 0; sc < 64; sc++)
            for (uint32_t vc = 0; vc < 64; vc++)
                for (uint32_t W = 0; W < 8; W++) {
                    uint32_t sz[3] = { sc & 3, sc >> 2 & 3, sc >> 4 }, vl[3] = { vc & 3, vc >> 2 & 3, vc >> 4 };
                    uint32_t r = ref_knap(3, sz, vl, W, NULL), x = knap_2d(3, sz, vl, W, ch);
                    inst++;
                    bad2 += x != r;
                    badp += !pick_ok(3, sz, vl, W, ch, r);
                    bad1 += knap_1d(3, sz, vl, W) != r;
                }
        printf("knapsack exhaustive n=3: %d instances, bad 2d=%d pick=%d 1d=%d\n", inst, bad2, badp, bad1);
        CHECK(bad2 == 0); CHECK(badp == 0); CHECK(bad1 == 0);

        /* 랜덤: n ≤ 15, W ≤ 2048 → 부분집합 전수와 / n = 32 → 2D 와 1D 끼리 */
        uint32_t sz[KN_MAX_N], vl[KN_MAX_N];
        bad2 = bad1 = badp = 0;
        for (int t = 0; t < 400; t++) {
            int n = (int)(xs32(&seed) % 16u);
            uint32_t W = xs32(&seed) % (KN2_MAX_W + 1u);
            for (int i = 0; i < n; i++) { sz[i] = (xs32(&seed) % 8u == 0) ? 0 : 1 + xs32(&seed) % 600u; vl[i] = xs32(&seed) % 1001u; }
            uint32_t r = ref_knap(n, sz, vl, W, NULL);
            bad2 += knap_2d(n, sz, vl, W, ch) != r;
            badp += !pick_ok(n, sz, vl, W, ch, r);
            bad1 += knap_1d(n, sz, vl, W) != r;
        }
        int bad21 = 0;
        for (int t = 0; t < 200; t++) {
            uint32_t W = 1 + xs32(&seed) % KN2_MAX_W;
            for (int i = 0; i < KN_MAX_N; i++) { sz[i] = 1 + xs32(&seed) % 300u; vl[i] = 1 + xs32(&seed) % 1000u; }
            uint32_t x = knap_2d(KN_MAX_N, sz, vl, W, ch);
            bad21 += x != knap_1d(KN_MAX_N, sz, vl, W) || !pick_ok(KN_MAX_N, sz, vl, W, ch, x);
        }
        printf("knapsack random: 400 (n<=15) vs brute bad 2d=%d pick=%d 1d=%d; 200 (n=32) 2d vs 1d bad=%d\n",
               bad2, badp, bad1, bad21);
        CHECK(bad2 == 0); CHECK(badp == 0); CHECK(bad1 == 0); CHECK(bad21 == 0);
    }

    /* ===== Part C: 펌웨어 설정 diff ===== */
    {
        static const char *const OLDC[] = { "uart.baud=9600", "uart.parity=none", "imu.rate_hz=100",
                                            "imu.range_g=4", "log.level=info", "radio.channel=11" };
        static const char *const NEWC[] = { "uart.baud=115200", "uart.parity=none", "imu.rate_hz=200",
                                            "log.level=debug", "radio.channel=11", "radio.tx_power=8" };
        int nops = 0;
        char kinds[2 * MAX_LINES + 2];
        int c1 = cfg_diff(OLDC, 6, NEWC, 6, true, ops, &nops);
        for (int t = 0; t < nops; t++) kinds[t] = ops[t].k;
        kinds[nops] = '\0';
        CHECK(c1 == 5 && strcmp(kinds, "~=~-~=+") == 0);
        CHECK(cfg_script_ok(OLDC, 6, NEWC, 6, true, ops, nops, &cost) && cost == 5);
        printf("config diff (key-aware), cost %d, ops %s:\n", c1, kinds);
        print_cfg_diff(OLDC, NEWC, ops, nops);
        int c0 = cfg_diff(OLDC, 6, NEWC, 6, false, ops, &nops);
        for (int t = 0; t < nops; t++) kinds[t] = ops[t].k;
        kinds[nops] = '\0';
        CHECK(cfg_script_ok(OLDC, 6, NEWC, 6, false, ops, nops, &cost) && cost == c0 && c0 == 5);
        printf("config diff (unit cost, key-blind), cost %d, ops %s:\n", c0, kinds);
        print_cfg_diff(OLDC, NEWC, ops, nops);
        CHECK(cfg_diff(OLDC, 0, NEWC, 3, true, ops, &nops) == 3 && nops == 3);   /* 빈 설정 → 전부 삽입 */
        CHECK(cfg_diff(OLDC, 6, OLDC, 6, true, ops, &nops) == 0 && nops == 6);   /* 같은 설정 → 전부 '=' */

        static const char *const POOL[] = { "a=1", "a=2", "b=1", "b=2", "c=1", "c", "d=1=2" };
        const char *o[MAX_LINES], *w[MAX_LINES];
        int bad_ref = 0, bad_sc = 0;
        for (int t = 0; t < 1500; t++) {
            bool ka = t & 1;
            int n = (int)(xs32(&seed) % 7u), m = (int)(xs32(&seed) % 7u);
            for (int i = 0; i < n; i++) o[i] = POOL[xs32(&seed) % 7u];
            for (int j = 0; j < m; j++) w[j] = POOL[xs32(&seed) % 7u];
            int c = cfg_diff(o, n, w, m, ka, ops, &nops);
            bad_ref += c != ref_cd(o, n, w, m, ka);
            bad_sc += !(cfg_script_ok(o, n, w, m, ka, ops, nops, &cost) && cost == c);
        }
        for (int t = 0; t < 1000; t++) {
            bool ka = t & 1;
            int n = (int)(xs32(&seed) % (MAX_LINES + 1u)), m = (int)(xs32(&seed) % (MAX_LINES + 1u));
            for (int i = 0; i < n; i++) o[i] = POOL[xs32(&seed) % 7u];
            for (int j = 0; j < m; j++) w[j] = POOL[xs32(&seed) % 7u];
            int c = cfg_diff(o, n, w, m, ka, ops, &nops);
            bad_sc += !(cfg_script_ok(o, n, w, m, ka, ops, nops, &cost) && cost == c);
        }
        printf("config diff random: 1500 (<=6 lines) vs recursion bad=%d; 2500 scripts (<=32 lines) bad=%d\n",
               bad_ref, bad_sc);
        CHECK(bad_ref == 0); CHECK(bad_sc == 0);
    }

    /* ===== Part C: Flash 예산 모듈 선택 ===== */
    {
        uint8_t ch[KN_MAX_N];
        uint32_t used = 0;
        const uint32_t budget = 128u * 1024u;
        const uint32_t grans[3] = { 4096, 256, 1 };
        for (int g = 0; g < 3; g++) {
            uint32_t v = flash_select(MODS, NMODS, budget, grans[g], ch, &used);
            uint32_t r = ref_flash(MODS, NMODS, budget, grans[g]);
            uint64_t vs = 0, us = 0;
            for (int i = 0; i < NMODS; i++)
                if (ch[i]) { vs += MODS[i].value; us += (uint64_t)(MODS[i].size / grans[g] + (MODS[i].size % grans[g] != 0)) * grans[g]; }
            CHECK(v == r && vs == v && us == used && used <= budget);
            printf("flash budget %u B, gran %4u B: value %u (brute %u), used %u B, modules:", budget, grans[g], v, r, used);
            for (int i = 0; i < NMODS; i++) if (ch[i]) printf(" %s", MODS[i].name);
            printf("\n");
        }
        Module rm[KN_MAX_N];
        int bad = 0;
        const uint32_t gr[4] = { 1, 64, 256, 4096 };
        for (int t = 0; t < 300; t++) {
            int n = (int)(xs32(&seed) % 13u);
            uint32_t gran = gr[t % 4], B = xs32(&seed) % 65537u;
            for (int i = 0; i < n; i++) { rm[i].name = "m"; rm[i].size = xs32(&seed) % 20001u; rm[i].value = xs32(&seed) % 100u; }
            uint32_t v = flash_select(rm, n, B, gran, ch, &used);
            uint64_t vs = 0, us = 0;
            for (int i = 0; i < n; i++)
                if (ch[i]) { vs += rm[i].value; us += (uint64_t)(rm[i].size / gran + (rm[i].size % gran != 0)) * gran; }
            bad += v != ref_flash(rm, n, B, gran) || vs != v || us != used || used > B;
        }
        printf("flash_select random: 300 instances (n<=12, gran 1/64/256/4096) vs brute bad=%d\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
