/* ALGO-A03 추가 연습 — 한 번에 검증 (자기 완결: Held-Karp 를 짧게 다시 넣었다)
 *   연습 1  절반 이하 메모리: uint16 비용 + "last 비트 빼고 접기" 인덱스 + 부모 표 없이 역추적 (5 MiB → 1 MiB)
 *   연습 2  시작점 자유(open, 어디서 시작해도 됨) = 비용 0 인 가상 홈
 *   연습 3  프로브 2개: 점을 나눠 맡아 동시에 움직일 때 makespan = min_S max(A(S), B(S^c))
 *   연습 4  LeetCode 847 Shortest Path Visiting All Nodes: (mask, node) BFS = 메트릭 폐포 위의 TSP
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN 16
#define FULLMASK(n) ((1u << (n)) - 1u)
typedef uint32_t cost_t;
#define COST_INF UINT32_MAX

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { int n; cost_t d[MAXN + 1][MAXN + 1]; } tsp_t;   /* 노드 n = 홈 */
static inline cost_t cadd(cost_t a, cost_t b) { return (a >= COST_INF - b) ? COST_INF : a + b; }

/* ---------- 기준: 본문과 같은 uint32 Held-Karp (부모 표 포함, open 만) ---------- */
static cost_t  g_dp[1u << MAXN][MAXN];
static uint8_t g_par[1u << MAXN][MAXN];
static void hk_fill(const tsp_t *t) {
    int n = t->n;
    uint32_t full = FULLMASK(n);
    for (uint32_t m = 1; m <= full; m++)
        for (int j = 0; j < n; j++) {
            if (!((m >> j) & 1u)) { g_dp[m][j] = COST_INF; continue; }
            uint32_t pm = m ^ (1u << j);
            if (pm == 0) { g_dp[m][j] = t->d[n][j]; g_par[m][j] = 0xFF; continue; }
            cost_t best = COST_INF; uint8_t arg = 0xFF;
            for (int i = 0; i < n; i++)
                if ((pm >> i) & 1u) {
                    cost_t c = cadd(g_dp[pm][i], t->d[i][j]);
                    if (c < best) { best = c; arg = (uint8_t)i; }
                }
            g_dp[m][j] = best; g_par[m][j] = arg;
        }
}
static cost_t hk_open(const tsp_t *t, uint8_t *order) {
    int n = t->n;
    if (n == 0) return 0;
    hk_fill(t);
    uint32_t m = FULLMASK(n);
    int last = 0;
    for (int j = 1; j < n; j++) if (g_dp[m][j] < g_dp[m][last]) last = j;
    cost_t c = g_dp[m][last];
    for (int k = n - 1, j = last; k >= 0; k--) { order[k] = (uint8_t)j; int p = g_par[m][j]; m ^= 1u << j; j = p; }
    return c;
}
static cost_t tour_open(const tsp_t *t, const uint8_t *o) {
    cost_t c = 0; int cur = t->n;
    for (int k = 0; k < t->n; k++) { c = cadd(c, t->d[cur][o[k]]); cur = o[k]; }
    return c;
}
static bool is_perm(const uint8_t *o, int n) {
    uint32_t seen = 0;
    for (int k = 0; k < n; k++) { if (o[k] >= n || ((seen >> o[k]) & 1u)) return false; seen |= 1u << o[k]; }
    return true;
}

/* ================= 연습 1 — uint16 + 접힌 인덱스 + 부모 표 없음 ================= */
/* (mask, j), j ∈ mask 인 상태만 저장: mask 에서 j 비트를 빼고 위쪽 비트를 한 칸 내려 (n-1) 비트 수로 접는다.
 * 칸 수 n·2^(n-1) (원래 2^n·n 의 절반), 칸당 2 B → n=16 에서 1 MiB. 부모는 저장하지 않고
 * dp[mask][j] == dp[mask\{j}][i] + d[i][j] 인 i 를 다시 찾아 역추적 (O(n²) 추가 시간). */
static uint16_t g_dp16[MAXN << (MAXN - 1)];
static inline uint32_t fold(uint32_t m, int j) { return ((m >> (j + 1)) << j) | (m & ((1u << j) - 1u)); }
/* 반환: 최적 비용, 또는 -1 (비용 상한이 uint16 에 안 들어감 → 거절). */
static int32_t hk16_open(const tsp_t *t, uint8_t *order) {
    int n = t->n;
    if (n == 0) return 0;
    cost_t maxe = 0;
    for (int i = 0; i <= n; i++) for (int j = 0; j <= n; j++) if (t->d[i][j] > maxe) maxe = t->d[i][j];
    if ((uint64_t)maxe * (uint64_t)n > 0xFFFEu) return -1;          /* 경로 = 간선 n 개 → 합 <= n·max < 0xFFFF */
    uint32_t half = 1u << (n - 1), full = FULLMASK(n);
    for (uint32_t m = 1; m <= full; m++)
        for (int j = 0; j < n; j++) {
            if (!((m >> j) & 1u)) continue;
            uint32_t pm = m ^ (1u << j);
            uint32_t best = 0xFFFFu;
            if (pm == 0) best = t->d[n][j];
            else
                for (int i = 0; i < n; i++)
                    if ((pm >> i) & 1u) {
                        uint32_t c = (uint32_t)g_dp16[(uint32_t)i * half + fold(pm, i)] + t->d[i][j];
                        if (c < best) best = c;
                    }
            g_dp16[(uint32_t)j * half + fold(m, j)] = (uint16_t)best;
        }
    int last = 0;
    for (int j = 1; j < n; j++)
        if (g_dp16[(uint32_t)j * half + fold(full, j)] < g_dp16[(uint32_t)last * half + fold(full, last)]) last = j;
    uint32_t m = full, cur = g_dp16[(uint32_t)last * half + fold(full, last)];
    int32_t answer = (int32_t)cur;
    for (int k = n - 1, j = last; k >= 0; k--) {
        order[k] = (uint8_t)j;
        uint32_t pm = m ^ (1u << j);
        if (pm == 0) break;
        int pick = -1;
        for (int i = 0; i < n && pick < 0; i++)
            if (((pm >> i) & 1u) && (uint32_t)g_dp16[(uint32_t)i * half + fold(pm, i)] + t->d[i][j] == cur) pick = i;
        if (pick < 0) return -1;                                      /* 일어나면 안 됨 */
        cur = g_dp16[(uint32_t)pick * half + fold(pm, pick)];
        m = pm; j = pick;
    }
    return answer;
}

/* ================= 연습 2 — 시작점 자유 ================= */
/* 가상 홈 H 에서 모든 점으로 0, 모든 점에서 H 로 0. open 최적 = 시작·끝 자유인 최단 해밀턴 경로. */
static cost_t free_start_open(const tsp_t *pts_only, uint8_t *order) {
    static tsp_t t;
    t = *pts_only;
    for (int j = 0; j <= t.n; j++) { t.d[t.n][j] = 0; t.d[j][t.n] = 0; }
    return hk_open(&t, order);
}

/* ================= 연습 3 — 프로브 2개, makespan ================= */
/* best[mask] = 홈에서 mask 를 전부 찍는 최소 open 비용 (어디서 끝나든). DP 한 번이 모든 mask 의 답을 준다. */
static cost_t g_bestA[1u << MAXN], g_bestB[1u << MAXN];
static void best_all(const tsp_t *t, cost_t *best) {
    hk_fill(t);
    best[0] = 0;
    for (uint32_t m = 1; m <= FULLMASK(t->n); m++) {
        cost_t b = COST_INF;
        for (int j = 0; j < t->n; j++) if (((m >> j) & 1u) && g_dp[m][j] < b) b = g_dp[m][j];
        best[m] = b;
    }
}
/* tA / tB: 같은 점, 홈 행(d[n][*])만 다르다. 반환 makespan, *maskA = 프로브 A 가 맡는 점들. */
static cost_t two_probe(const tsp_t *tA, const tsp_t *tB, uint32_t *maskA) {
    best_all(tA, g_bestA);
    best_all(tB, g_bestB);
    uint32_t full = FULLMASK(tA->n);
    cost_t best = COST_INF;
    for (uint32_t m = 0; m <= full; m++) {
        cost_t a = g_bestA[m], b = g_bestB[full ^ m], c = a > b ? a : b;
        if (c < best) { best = c; *maskA = m; }
    }
    return best;
}

/* ================= 연습 4 — LeetCode 847 (무가중 그래프, 재방문 허용, 시작·끝 자유) ================= */
#define G_MAXN 12
static uint16_t g_seen[1u << G_MAXN];                       /* g_seen[mask] 의 비트 v = (mask, v) 방문함 */
static uint32_t g_q[(1u << G_MAXN) * G_MAXN];
static int shortest_visit_all(const uint16_t *adj, int n) {
    if (n <= 1) return 0;
    uint32_t full = FULLMASK(n), qh = 0, qt = 0;
    memset(g_seen, 0, sizeof(uint16_t) << n);
    for (int v = 0; v < n; v++) { g_seen[1u << v] |= (uint16_t)(1u << v); g_q[qt++] = (1u << v) << 4 | (uint32_t)v; }
    for (int dist = 0; qh < qt; dist++) {                    /* 층 단위 BFS */
        uint32_t end = qt;
        for (; qh < end; qh++) {
            uint32_t m = g_q[qh] >> 4; int v = (int)(g_q[qh] & 15u);
            if (m == full) return dist;
            for (int w = 0; w < n; w++)
                if ((adj[v] >> w) & 1u) {
                    uint32_t m2 = m | (1u << w);
                    if (!((g_seen[m2] >> w) & 1u)) { g_seen[m2] |= (uint16_t)(1u << w); g_q[qt++] = m2 << 4 | (uint32_t)w; }
                }
        }
    }
    return -1;                                                /* 연결 안 됨 */
}

/* ---------- 브루트포스 기준들 ---------- */
static uint64_t bf_best;
static void bf_free(const tsp_t *t, uint32_t used, int last, int depth, uint64_t acc) {   /* 시작 자유 */
    if (depth == t->n) { if (acc < bf_best) bf_best = acc; return; }
    for (int j = 0; j < t->n; j++)
        if (!((used >> j) & 1u)) bf_free(t, used | (1u << j), j, depth + 1, acc + (last < 0 ? 0 : t->d[last][j]));
}
static uint64_t bf_two(const tsp_t *tA, const tsp_t *tB) {   /* 순열 × 나누는 위치: 앞 k 개는 A, 나머지는 B */
    int n = tA->n, p[MAXN];
    for (int i = 0; i < n; i++) p[i] = i;
    uint64_t best = UINT64_MAX;
    for (;;) {
        for (int k = 0; k <= n; k++) {
            uint64_t a = 0, b = 0; int cur = n;
            for (int i = 0; i < k; i++) { a += tA->d[cur][p[i]]; cur = p[i]; }
            cur = n;
            for (int i = k; i < n; i++) { b += tB->d[cur][p[i]]; cur = p[i]; }
            uint64_t c = a > b ? a : b;
            if (c < best) best = c;
        }
        int i = n - 2;                                        /* 다음 순열 (사전순) */
        while (i >= 0 && p[i] > p[i + 1]) i--;
        if (i < 0) break;
        int j = n - 1;
        while (p[j] < p[i]) j--;
        int x = p[i]; p[i] = p[j]; p[j] = x;
        for (int l = i + 1, r = n - 1; l < r; l++, r--) { x = p[l]; p[l] = p[r]; p[r] = x; }
    }
    return best;
}
static cost_t cheb(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int32_t dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    return (cost_t)(dx > dy ? dx : dy);
}
static void random_board(tsp_t *t, int n, uint32_t *s, int32_t W, int32_t Hh, int32_t hx, int32_t hy) {
    int32_t x[MAXN + 1], y[MAXN + 1];
    memset(t, 0, sizeof *t);
    t->n = n;
    for (int i = 0; i < n; i++) { x[i] = (int32_t)(xs32(s) % (uint32_t)W); y[i] = (int32_t)(xs32(s) % (uint32_t)Hh); }
    x[n] = hx; y[n] = hy;
    for (int i = 0; i <= n; i++) for (int j = 0; j <= n; j++) t->d[i][j] = cheb(x[i], y[i], x[j], y[j]);
}

int main(void) {
    /* 연습 1: 0.1 mm 단위 100 x 80 mm 보드 (체비쇼프 간선 <= 1000 → 16·1000 < 65535) */
    {
        uint32_t s = 1u;
        int bad = 0, cases = 0;
        static tsp_t t;
        uint8_t o1[MAXN] = {0}, o2[MAXN] = {0};
        for (int n = 1; n <= 16; n++)
            for (int r = 0; r < (n <= 12 ? 6 : 2); r++) {
                random_board(&t, n, &s, 1001, 801, 0, 0);
                cost_t ref = hk_open(&t, o1);
                int32_t c = hk16_open(&t, o2);
                cases++;
                if (c < 0 || (cost_t)c != ref || !is_perm(o2, n) || tour_open(&t, o2) != ref) bad++;
            }
        printf("ex1: %d boards n=1..16, uint16 folded DP vs uint32 DP mismatches=%d; bytes full=%zu folded=%zu\n",
               cases, bad, sizeof g_dp + sizeof g_par, sizeof g_dp16);
        CHECK(bad == 0);
        CHECK(sizeof g_dp16 * 5 == sizeof g_dp + sizeof g_par);           /* 1 MiB vs 5 MiB */
        random_board(&t, 16, &s, 10001, 8001, 0, 0);                       /* 10 µm 단위 → 상한 초과 → 거절 */
        CHECK(hk16_open(&t, o2) == -1);
        /* 접기 인덱스가 단사인지: n=5 에서 (mask, j∈mask) 80 개가 0..79 를 한 번씩 */
        int hit[5 * 16] = {0}, ok = 1;
        for (uint32_t m = 1; m < 32; m++) for (int j = 0; j < 5; j++) if ((m >> j) & 1u) hit[j * 16 + (int)fold(m, j)]++;
        for (int k = 0; k < 80; k++) ok &= hit[k] == 1;
        CHECK(ok);
    }
    /* 연습 2: 시작점 자유 vs 모든 순열 (n <= 8) */
    {
        uint32_t s = 2u;
        int bad = 0;
        static tsp_t t;
        uint8_t o[MAXN] = {0};
        for (int r = 0; r < 60; r++) {
            int n = 1 + r % 8;
            random_board(&t, n, &s, 3000, 3000, 0, 0);
            bf_best = UINT64_MAX; bf_free(&t, 0, -1, 0, 0);
            cost_t c = free_start_open(&t, o);
            cost_t walk = 0;
            for (int k = 1; k < n; k++) walk += t.d[o[k - 1]][o[k]];
            if (c != bf_best || !is_perm(o, n) || walk != c) bad++;
        }
        CHECK(bad == 0);
        /* 일직선 점 4개: 홈 (0,0) 에서 시작하면 0→3 순서로 30+... 자유 시작이면 양 끝 사이 거리 */
        random_board(&t, 0, &s, 1, 1, 0, 0);
        t.n = 4;
        const int32_t X[4] = {50, 10, 40, 20};
        for (int i = 0; i <= 4; i++) for (int j = 0; j <= 4; j++) t.d[i][j] = cheb(i == 4 ? 100 : X[i], 0, j == 4 ? 100 : X[j], 0);
        CHECK(free_start_open(&t, o) == 40 && hk_open(&t, o) == 90);     /* 자유: 10..50 = 40 / 홈(100) 출발: 100→10 = 90 */
    }
    /* 연습 3: 프로브 2개 makespan vs 순열×분할 브루트포스 (n <= 7) */
    {
        uint32_t s = 3u;
        int bad = 0, split = 0;
        static tsp_t tA, tB;
        for (int r = 0; r < 60; r++) {
            int n = 1 + r % 7;
            random_board(&tA, n, &s, 5000, 4000, 0, 0);
            tB = tA;                                                    /* 같은 점, 홈 행만 다름: DP 는 홈 행이 무엇이든 성립 */
            for (int j = 0; j < n; j++) tB.d[n][j] = xs32(&s) % 6000u;
            uint32_t mA = 0;
            cost_t c = two_probe(&tA, &tB, &mA);
            uint64_t ref = bf_two(&tA, &tB);
            if (c != ref) bad++;
            if (mA != 0 && mA != FULLMASK(n)) split++;
        }
        printf("ex3: 60 instances, makespan mismatches=%d, instances where both probes work=%d\n", bad, split);
        CHECK(bad == 0 && split > 20);
    }
    /* 연습 4: LeetCode 847 — 예제 2개 + 무작위 연결 그래프 vs (Floyd 폐포 + 시작 자유 Held-Karp) */
    {
        const uint16_t star[4] = {0xE, 0x1, 0x1, 0x1};                 /* [[1,2,3],[0],[0],[0]] → 4 */
        const uint16_t ex2[5] = {0x2, 0x15, 0x1A, 0x4, 0x6};           /* [[1],[0,2,4],[1,3,4],[2],[1,2]] → 4 */
        CHECK(shortest_visit_all(star, 4) == 4 && shortest_visit_all(ex2, 5) == 4);
        uint32_t s = 4u;
        int bad = 0;
        static tsp_t t;
        uint8_t o[MAXN] = {0};
        for (int r = 0; r < 80; r++) {
            int n = 1 + r % 10;
            uint16_t adj[G_MAXN] = {0};
            for (int v = 1; v < n; v++) { int u = (int)(xs32(&s) % (uint32_t)v); adj[u] |= (uint16_t)(1u << v); adj[v] |= (uint16_t)(1u << u); }
            for (int e = 0; e < (int)(xs32(&s) % 4u); e++) {
                int u = (int)(xs32(&s) % (uint32_t)n), v = (int)(xs32(&s) % (uint32_t)n);
                if (u != v) { adj[u] |= (uint16_t)(1u << v); adj[v] |= (uint16_t)(1u << u); }
            }
            memset(&t, 0, sizeof t);
            t.n = n;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) t.d[i][j] = i == j ? 0 : (((adj[i] >> j) & 1u) ? 1 : 1000);
            for (int k = 0; k < n; k++) for (int i = 0; i < n; i++) for (int j = 0; j < n; j++)
                if (t.d[i][k] + t.d[k][j] < t.d[i][j]) t.d[i][j] = t.d[i][k] + t.d[k][j];
            if ((int)free_start_open(&t, o) != shortest_visit_all(adj, n)) bad++;
        }
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
