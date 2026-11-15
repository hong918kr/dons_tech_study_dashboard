/* ALGO-M05 추가 연습 — 한 번에 검증
 *   연습 1  가능한 초기화 순서의 개수 (부분집합 비트마스크 DP, n <= 16) — 순열 전수 / Catalan(2×k 격자) 와 대조
 *   연습 2  외계어 사전 (Alien Dictionary): 정렬된 단어 목록 -> 글자 순서 (사전순 최소), 모순이면 실패
 *   연습 3  드라이버 하나가 리셋될 때 다시 초기화할 집합과 순서 (+ 종료 순서 = 역순)
 *   연습 4  병렬 probe 부팅 시간 = 가중 최장 경로(critical path) + 경로 복원
 *   연습 5  DFS 역후위(reverse postorder) 위상 정렬 — 반복형, 사이클이면 -1
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_V 64
#define MAX_E 1024

typedef struct { uint16_t from, to; } edge_t;
typedef struct { uint16_t n, m; uint16_t off[MAX_V + 1]; uint16_t adj[MAX_E]; } graph_t;

static int graph_build(graph_t *g, uint16_t n, const edge_t *e, size_t m) {
    uint16_t cur[MAX_V];
    if (n > MAX_V || m > MAX_E) return -1;
    for (size_t i = 0; i < m; i++) if (e[i].from >= n || e[i].to >= n) return -1;
    g->n = n; g->m = (uint16_t)m;
    memset(g->off, 0, sizeof g->off);
    for (size_t i = 0; i < m; i++) g->off[e[i].from + 1]++;
    for (uint16_t u = 0; u < n; u++) g->off[u + 1] = (uint16_t)(g->off[u + 1] + g->off[u]);
    for (uint16_t u = 0; u < n; u++) cur[u] = g->off[u];
    for (size_t i = 0; i < m; i++) g->adj[cur[e[i].from]++] = e[i].to;
    return 0;
}

/* 사전순 최소 Kahn — 준비 집합이 64개 이하라 uint64_t 비트마스크 하나로 충분 (가장 낮은 1 비트 = 가장 작은 id) */
static int topo_lex(const graph_t *g, uint16_t *order) {
    uint16_t indeg[MAX_V] = { 0 };
    uint64_t ready = 0;
    for (uint16_t i = 0; i < g->m; i++) indeg[g->adj[i]]++;
    for (uint16_t u = 0; u < g->n; u++) if (!indeg[u]) ready |= 1ull << u;
    int k = 0;
    while (ready) {
        uint16_t u = 0;
        while (!(ready >> u & 1u)) u++;
        ready &= ~(1ull << u);
        order[k++] = u;
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++)
            if (--indeg[g->adj[i]] == 0) ready |= 1ull << g->adj[i];
    }
    return k;
}

static int valid_order(const graph_t *g, const uint16_t *order, int k) {   /* 순열 + 모든 간선 앞->뒤 */
    int pos[MAX_V];
    if (k != g->n) return 0;
    for (int v = 0; v < g->n; v++) pos[v] = -1;
    for (int i = 0; i < k; i++) { if (order[i] >= g->n || pos[order[i]] != -1) return 0; pos[order[i]] = i; }
    for (uint16_t u = 0; u < g->n; u++)
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) if (pos[u] >= pos[g->adj[i]]) return 0;
    return 1;
}

/* ---------------- 연습 1: 위상 순서 개수 ---------------- */
/* dp[S] = S 를 "먼저 초기화된 집합" 으로 만드는 순서의 수. dp[S ∪ {v}] += dp[S]  (v 의 선행자 ⊆ S). n <= 16 */
static uint64_t count_topo_orders(const graph_t *g) {
    static uint64_t dp[1u << 16];
    uint32_t pred[16] = { 0 };
    int n = g->n;
    if (n > 16) return 0;
    for (int u = 0; u < n; u++)
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) pred[g->adj[i]] |= 1u << u;
    uint32_t full = (n == 0) ? 0u : (uint32_t)((1u << n) - 1u);
    memset(dp, 0, sizeof(uint64_t) << n);
    dp[0] = 1;
    for (uint32_t S = 0; S < full; S++) {           /* S 보다 큰 수만 갱신하므로 오름차순이 곧 올바른 계산 순서 */
        if (!dp[S]) continue;
        for (int v = 0; v < n; v++)
            if (!(S >> v & 1u) && (pred[v] & ~S) == 0) dp[S | 1u << v] += dp[S];
    }
    return dp[full];
}

static uint64_t brute_count(const graph_t *g) {     /* n <= 8: 모든 순열 */
    uint16_t p[8]; int n = g->n; uint64_t c = 0;
    for (int i = 0; i < n; i++) p[i] = (uint16_t)i;
    for (;;) {
        c += (uint64_t)valid_order(g, p, n);
        int i = n - 2;
        while (i >= 0 && p[i] > p[i + 1]) i--;
        if (i < 0) return c;
        int j = n - 1;
        while (p[j] < p[i]) j--;
        uint16_t t = p[i]; p[i] = p[j]; p[j] = t;
        for (int a = i + 1, b = n - 1; a < b; a++, b--) { t = p[a]; p[a] = p[b]; p[b] = t; }
    }
}

/* ---------------- 연습 2: 외계어 사전 ---------------- */
/* words 는 미지의 알파벳 순서로 정렬돼 있다. 등장한 글자(a-z)의 순서를 out 에 (NUL 종료), 모순이면 -1.
 * 인접한 두 단어의 첫 다른 글자에서만 간선이 생긴다. 다른 글자가 없는데 앞 단어가 더 길면("abc" 다음 "ab") 모순 */
static int alien_order(const char *const *words, int nw, char *out) {
    edge_t e[26 * 26]; size_t m = 0;
    uint8_t seen[26] = { 0 }, has_e[26][26] = { { 0 } };
    for (int i = 0; i < nw; i++) for (const char *p = words[i]; *p; p++) seen[*p - 'a'] = 1;
    for (int i = 0; i + 1 < nw; i++) {
        const char *a = words[i], *b = words[i + 1];
        size_t j = 0;
        while (a[j] && b[j] && a[j] == b[j]) j++;
        if (a[j] && b[j]) {
            int u = a[j] - 'a', v = b[j] - 'a';
            if (!has_e[u][v]) { has_e[u][v] = 1; e[m].from = (uint16_t)u; e[m].to = (uint16_t)v; m++; }
        } else if (a[j] && !b[j]) return -1;       /* b 가 a 의 진짜 접두사인데 뒤에 옴 */
    }
    graph_t g; uint16_t order[26];
    graph_build(&g, 26, e, m);
    if (topo_lex(&g, order) != 26) return -1;      /* 글자 사이 순환 */
    int k = 0;
    for (int i = 0; i < 26; i++) if (seen[order[i]]) out[k++] = (char)('a' + order[i]);
    out[k] = '\0';
    return k;
}

/* ---------------- 연습 3: 리셋 후 재초기화 집합 ---------------- */
/* x 와 x 에서 도달 가능한 모든 드라이버를, 부팅 때의 순서(사전순 최소 위상 순서)를 유지한 채 out 에. 반환 = 개수, 사이클이면 -1 */
static int reinit_set(const graph_t *g, uint16_t x, uint16_t *out) {
    uint8_t mark[MAX_V] = { 0 };
    uint16_t st[MAX_V], order[MAX_V]; int top = 0;
    st[top++] = x; mark[x] = 1;
    while (top) {                                   /* 반복형 DFS 로 도달 집합 표시 */
        uint16_t u = st[--top];
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++)
            if (!mark[g->adj[i]]) { mark[g->adj[i]] = 1; st[top++] = g->adj[i]; }
    }
    if (topo_lex(g, order) != g->n) return -1;
    int k = 0;
    for (int i = 0; i < g->n; i++) if (mark[order[i]]) out[k++] = order[i];
    return k;
}

/* ---------------- 연습 4: 병렬 probe 의 부팅 시간 ---------------- */
/* 각 드라이버 초기화에 ms[v] 가 걸리고 워커는 무한(비동기 probe). finish[v] = ms[v] + max(finish[선행자]).
 * 반환 = max finish, path[0..*plen) = 임계 경로 (소스 -> 끝) */
static uint32_t critical_path(const graph_t *g, const uint32_t *ms, uint16_t *path, int *plen) {
    uint16_t order[MAX_V], best_pred[MAX_V];
    uint32_t start[MAX_V] = { 0 }, finish[MAX_V];
    *plen = 0;
    if (topo_lex(g, order) != g->n || g->n == 0) return 0;
    for (int v = 0; v < g->n; v++) best_pred[v] = UINT16_MAX;
    uint32_t best = 0; uint16_t end = 0;
    for (int k = 0; k < g->n; k++) {
        uint16_t u = order[k];
        finish[u] = start[u] + ms[u];               /* start[u] 는 선행자가 모두 처리되어 확정 */
        if (k == 0 || finish[u] > best) { best = finish[u]; end = u; }
        for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) {
            uint16_t v = g->adj[i];
            if (best_pred[v] == UINT16_MAX || finish[u] > start[v]) { start[v] = finish[u]; best_pred[v] = u; }
        }
    }
    uint16_t rev[MAX_V]; int L = 0;
    for (uint16_t v = end; v != UINT16_MAX; v = best_pred[v]) rev[L++] = v;
    for (int i = 0; i < L; i++) path[i] = rev[L - 1 - i];
    *plen = L;
    return best;
}

static uint32_t brute_longest_from(const graph_t *g, const uint32_t *ms, uint16_t u) {   /* 모든 경로 (지수) */
    uint32_t b = 0;
    for (uint16_t i = g->off[u]; i < g->off[u + 1]; i++) {
        uint32_t t = brute_longest_from(g, ms, g->adj[i]);
        if (t > b) b = t;
    }
    return ms[u] + b;
}

/* ---------------- 연습 5: DFS 역후위 위상 정렬 ---------------- */
/* 정점이 BLACK 이 되는 순간(후위)에 뒤에서부터 채운다. GRAY 를 만나면 사이클 -> -1. 반복형, 스택 깊이 <= n */
static int topo_dfs(const graph_t *g, uint16_t *order) {
    uint8_t color[MAX_V] = { 0 };
    uint16_t stk[MAX_V], it[MAX_V];
    int pos = g->n;
    for (uint16_t s = 0; s < g->n; s++) {
        if (color[s]) continue;
        int top = 0; stk[0] = s; it[0] = g->off[s]; color[s] = 1;
        while (top >= 0) {
            uint16_t u = stk[top];
            if (it[top] < g->off[u + 1]) {
                uint16_t v = g->adj[it[top]++];
                if (color[v] == 1) return -1;
                if (color[v] == 0) { top++; stk[top] = v; it[top] = g->off[v]; color[v] = 1; }
            } else { color[u] = 2; order[--pos] = u; top--; }
        }
    }
    return g->n;
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static size_t random_dag(uint16_t n, size_t m, edge_t *e) {
    uint16_t perm[MAX_V];
    for (uint16_t i = 0; i < n; i++) perm[i] = i;
    for (uint16_t i = n; i > 1; i--) { uint16_t j = (uint16_t)(xorshift32() % i), t = perm[i - 1]; perm[i - 1] = perm[j]; perm[j] = t; }
    if (n < 2) return 0;
    for (size_t i = 0; i < m; i++) {
        uint16_t a = (uint16_t)(xorshift32() % n), b = (uint16_t)(xorshift32() % n);
        if (a == b) b = (uint16_t)((a + 1) % n);
        if (a > b) { uint16_t t = a; a = b; b = t; }
        e[i].from = perm[a]; e[i].to = perm[b];
    }
    return m;
}

/* 본문 Part C 의 13 드라이버 그래프 (id = 선언 순서) */
enum { CLOCK, GPIO, DMA, I2C1, SPI1, PMIC, IMU, BARO, MAG, FLASH, FS, FUSION, LOGGER, NDRV };
static const char *const NAMES[NDRV] = { "clock", "gpio", "dma", "i2c1", "spi1", "pmic", "imu", "baro", "mag", "flash", "fs", "fusion", "logger" };
static const edge_t DRV_E[] = {
    {CLOCK,GPIO}, {CLOCK,DMA}, {CLOCK,I2C1}, {GPIO,I2C1}, {CLOCK,SPI1}, {GPIO,SPI1}, {DMA,SPI1}, {I2C1,PMIC},
    {SPI1,IMU}, {PMIC,IMU}, {GPIO,IMU}, {I2C1,BARO}, {PMIC,BARO}, {I2C1,MAG}, {PMIC,MAG}, {SPI1,FLASH},
    {FLASH,FS}, {IMU,FUSION}, {BARO,FUSION}, {MAG,FUSION}, {FS,LOGGER}, {FUSION,LOGGER} };

static void print_names(const char *label, const uint16_t *v, int k) {
    printf("%s", label);
    for (int i = 0; i < k; i++) printf(" %s", NAMES[v[i]]);
    printf("\n");
}

int main(void) {
    static graph_t g;
    static edge_t e[MAX_E];
    uint16_t order[MAX_V], path[MAX_V];

    /* ---- 연습 1 ---- */
    {
        unsigned long bad = 0, cases = 0;
        for (int t = 0; t < 400; t++) {
            uint16_t n = (uint16_t)(xorshift32() % 8);
            size_t m = random_dag(n, n ? xorshift32() % (2u * n + 1) : 0, e);
            if (t % 4 == 3 && m >= 1) { e[m].from = e[0].to; e[m].to = e[0].from; m++; }   /* 가끔 2-사이클 */
            if (graph_build(&g, n, e, m) != 0) bad++;
            cases++;
            if (count_topo_orders(&g) != brute_count(&g)) bad++;
        }
        printf("연습 1: %lu random graphs (n<=7) vs permutation brute force, mismatches=%lu\n", cases, bad);
        CHECK(bad == 0);
        /* 2×k 격자 포셋 (a_i -> a_{i+1}, b_i -> b_{i+1}, a_i -> b_i) 의 선형 확장 수 = Catalan(k) (OEIS A000108) */
        static const uint64_t catalan[9] = { 1, 1, 2, 5, 14, 42, 132, 429, 1430 };
        int cat_ok = 1;
        printf("  2xk grid:");
        for (int k = 1; k <= 8; k++) {
            size_t m = 0;
            for (int i = 0; i < k; i++) {
                if (i + 1 < k) { e[m++] = (edge_t){ (uint16_t)i, (uint16_t)(i + 1) }; e[m++] = (edge_t){ (uint16_t)(k + i), (uint16_t)(k + i + 1) }; }
                e[m++] = (edge_t){ (uint16_t)i, (uint16_t)(k + i) };
            }
            graph_build(&g, (uint16_t)(2 * k), e, m);
            uint64_t c = count_topo_orders(&g);
            printf(" %llu", (unsigned long long)c);
            if (c != catalan[k]) cat_ok = 0;
        }
        printf("  (Catalan 1..8)\n");
        CHECK(cat_ok);
        graph_build(&g, 10, e, 0);
        CHECK(count_topo_orders(&g) == 3628800ull);                 /* 의존성 없음: 10! */
        for (int i = 0; i < 15; i++) e[i] = (edge_t){ (uint16_t)i, (uint16_t)(i + 1) };
        graph_build(&g, 16, e, 15);
        CHECK(count_topo_orders(&g) == 1);                          /* 사슬: 1 */
        graph_build(&g, NDRV, DRV_E, sizeof DRV_E / sizeof DRV_E[0]);
        uint64_t drv = count_topo_orders(&g);
        printf("  13-driver graph: %llu valid init orders\n", (unsigned long long)drv);
        CHECK(drv > 1);
    }

    /* ---- 연습 2 ---- */
    {
        char out[27];
        const char *w1[] = { "wrt", "wrf", "er", "ett", "rftt" };
        const char *w2[] = { "z", "x" };
        const char *w3[] = { "z", "x", "z" };
        const char *w4[] = { "abc", "ab" };
        const char *w5[] = { "ab", "abc", "b" };            /* 접두사가 앞이면 OK. 간선 a->b 하나, c 는 자유 */
        CHECK(alien_order(w1, 5, out) == 5 && strcmp(out, "wertf") == 0);
        CHECK(alien_order(w2, 2, out) == 2 && strcmp(out, "zx") == 0);
        CHECK(alien_order(w3, 3, out) == -1);
        CHECK(alien_order(w4, 2, out) == -1);
        CHECK(alien_order(w5, 3, out) == 3 && strcmp(out, "abc") == 0);
        /* 무작위: 무작위 알파벳 순서로 단어 30개를 만들어 그 순서로 정렬 -> 결과가 모든 인접 쌍과 모순이 없어야 */
        unsigned long bad = 0;
        for (int t = 0; t < 200; t++) {
            char alpha[6]; int rank[26];
            memcpy(alpha, "abcdef", 6);
            for (int i = 6; i > 1; i--) { int j = (int)(xorshift32() % (uint32_t)i); char c = alpha[i - 1]; alpha[i - 1] = alpha[j]; alpha[j] = c; }
            for (int i = 0; i < 6; i++) rank[alpha[i] - 'a'] = i;
            static char buf[30][5]; const char *ws[30];
            for (int i = 0; i < 30; i++) {
                int len = 1 + (int)(xorshift32() % 4);
                for (int c = 0; c < len; c++) buf[i][c] = (char)('a' + xorshift32() % 6);
                buf[i][len] = '\0';
            }
            for (int i = 1; i < 30; i++)                     /* 삽입 정렬 (미지 알파벳 기준) */
                for (int j = i; j > 0; j--) {
                    const char *a = buf[j - 1], *b = buf[j]; int k = 0;
                    while (a[k] && b[k] && a[k] == b[k]) k++;
                    int gt = (a[k] && b[k]) ? rank[a[k] - 'a'] > rank[b[k] - 'a'] : (a[k] != 0);
                    if (!gt) break;
                    char tmp[5]; memcpy(tmp, buf[j - 1], 5); memcpy(buf[j - 1], buf[j], 5); memcpy(buf[j], tmp, 5);
                }
            for (int i = 0; i < 30; i++) ws[i] = buf[i];
            int k = alien_order(ws, 30, out);
            if (k < 0) { bad++; continue; }
            int pos[26]; for (int i = 0; i < 26; i++) pos[i] = -1;
            for (int i = 0; i < k; i++) pos[out[i] - 'a'] = i;
            for (int i = 0; i + 1 < 30; i++) {               /* 결과 순서로 다시 비교해도 정렬돼 있어야 */
                const char *a = ws[i], *b = ws[i + 1]; int j = 0;
                while (a[j] && b[j] && a[j] == b[j]) j++;
                if (a[j] && b[j] && pos[a[j] - 'a'] > pos[b[j] - 'a']) bad++;
            }
        }
        printf("연습 2: 200 random sorted dictionaries, inconsistent results=%lu\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 3 ---- */
    {
        graph_build(&g, NDRV, DRV_E, sizeof DRV_E / sizeof DRV_E[0]);
        int k = reinit_set(&g, I2C1, order);
        print_names("연습 3: reset i2c1 -> reinit:", order, k);
        CHECK(k == 7 && order[0] == I2C1 && order[1] == PMIC && order[k - 1] == LOGGER);
        uint16_t rev[MAX_V];
        for (int i = 0; i < k; i++) rev[i] = order[k - 1 - i];
        print_names("        shutdown (reverse):", rev, k);
        k = reinit_set(&g, LOGGER, order);
        CHECK(k == 1 && order[0] == LOGGER);
        /* 무작위: 도달 집합을 Warshall 로 따로 구해 대조, 순서가 부분 그래프의 위상 순서인지 확인 */
        unsigned long bad = 0;
        for (int t = 0; t < 300; t++) {
            uint16_t n = (uint16_t)(1 + xorshift32() % 40);
            size_t m = random_dag(n, xorshift32() % (2u * n + 1), e);
            graph_build(&g, n, e, m);
            static uint8_t r[MAX_V][MAX_V];
            memset(r, 0, sizeof r);
            for (int v = 0; v < n; v++) r[v][v] = 1;
            for (size_t i = 0; i < m; i++) r[e[i].from][e[i].to] = 1;
            for (int w = 0; w < n; w++) for (int a = 0; a < n; a++) if (r[a][w]) for (int b = 0; b < n; b++) if (r[w][b]) r[a][b] = 1;
            uint16_t x = (uint16_t)(xorshift32() % n);
            k = reinit_set(&g, x, order);
            int cnt = 0, pos[MAX_V];
            for (int v = 0; v < n; v++) { cnt += r[x][v]; pos[v] = -1; }
            if (k != cnt) { bad++; continue; }
            for (int i = 0; i < k; i++) { if (!r[x][order[i]]) bad++; pos[order[i]] = i; }
            for (size_t i = 0; i < m; i++)
                if (pos[e[i].from] >= 0 && pos[e[i].to] >= 0 && pos[e[i].from] >= pos[e[i].to]) bad++;
        }
        printf("연습 3: 300 random DAGs vs Warshall closure, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 4 ---- */
    {
        static const uint32_t ms[NDRV] = { 1, 1, 1, 2, 2, 5, 30, 12, 8, 3, 20, 2, 4 };
        graph_build(&g, NDRV, DRV_E, sizeof DRV_E / sizeof DRV_E[0]);
        int L;
        uint32_t t = critical_path(&g, ms, path, &L), seq = 0;
        for (int v = 0; v < NDRV; v++) seq += ms[v];
        printf("연습 4: parallel boot = %u ms (sequential sum %u ms)\n", t, seq);
        print_names("        critical path:", path, L);
        CHECK(t == 45 && seq == 91);
        CHECK(L == 7 && path[0] == CLOCK && path[1] == GPIO && path[2] == I2C1 && path[3] == PMIC
              && path[4] == IMU && path[5] == FUSION && path[6] == LOGGER);
        unsigned long bad = 0;
        uint32_t w[MAX_V];
        for (int r = 0; r < 300; r++) {
            uint16_t n = (uint16_t)(1 + xorshift32() % 12);
            size_t m = random_dag(n, xorshift32() % (2u * n + 1), e);
            graph_build(&g, n, e, m);
            for (int v = 0; v < n; v++) w[v] = xorshift32() % 50;
            uint32_t ref = 0;
            for (uint16_t v = 0; v < n; v++) { uint32_t x = brute_longest_from(&g, w, v); if (x > ref) ref = x; }
            t = critical_path(&g, w, path, &L);
            uint32_t sum = 0;
            for (int i = 0; i < L; i++) sum += w[path[i]];
            int edges_ok = 1;
            for (int i = 0; i + 1 < L; i++) {
                int f = 0;
                for (uint16_t j = g.off[path[i]]; j < g.off[path[i] + 1]; j++) f |= g.adj[j] == path[i + 1];
                edges_ok &= f;
            }
            if (t != ref || sum != t || !edges_ok || L < 1) bad++;
        }
        printf("연습 4: 300 random weighted DAGs (n<=12) vs all-paths brute force, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 5 ---- */
    {
        const edge_t d[] = { {3,1}, {3,2}, {1,0}, {2,0} };
        graph_build(&g, 4, d, 4);
        int k = topo_dfs(&g, order);
        printf("연습 5: diamond 3->{1,2}->0: dfs order = %u %u %u %u\n", order[0], order[1], order[2], order[3]);
        CHECK(k == 4 && valid_order(&g, order, 4));
        graph_build(&g, NDRV, DRV_E, sizeof DRV_E / sizeof DRV_E[0]);
        k = topo_dfs(&g, order);
        print_names("        13 drivers, dfs order:", order, k);
        CHECK(k == NDRV && valid_order(&g, order, k));
        unsigned long bad = 0;
        for (int t = 0; t < 500; t++) {
            uint16_t n = (uint16_t)(xorshift32() % 65);
            size_t m = random_dag(n, n ? xorshift32() % (3u * n + 1) : 0, e);
            if (t % 2 && n >= 2) { e[m].from = (uint16_t)(xorshift32() % n); e[m].to = (uint16_t)(xorshift32() % n); m++; }  /* 임의 간선 하나 추가: 사이클일 수도 아닐 수도 */
            if (graph_build(&g, n, e, m) != 0) { bad++; continue; }
            uint16_t o2[MAX_V];
            int kd = topo_dfs(&g, order), kk = topo_lex(&g, o2);
            if ((kd == n) != (kk == n)) bad++;                         /* 사이클 판정 일치 */
            else if (kd == n && !valid_order(&g, order, n)) bad++;
        }
        printf("연습 5: 500 random graphs, dfs vs kahn disagreement / invalid=%lu\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
