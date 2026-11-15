/* ALGO-M14 추가 연습 — 한 번에 검증
 *   연습 1  그래프가 트리인가 (LeetCode 261 Graph Valid Tree): 간선 n-1 개 + 사이클 없음
 *   연습 2  링크 장애를 오프라인으로: 장애 순서를 거꾸로 = 링크 추가 (reverse union-find)
 *   연습 3  모든 센서가 처음 연결되는 시각 (LeetCode 1101 류): 타임스탬프 정렬 + comps == 1
 *   연습 4  망 복구에 옮길 케이블 수 (LeetCode 1319 Number of Operations to Make Network Connected)
 *   연습 5  union by rank (uint8_t rank) + 완전 경로 압축(2-pass, 반복문): rank <= floor(log2 n), 노드당 3 B
 * 모든 답은 DSU 를 쓰지 않는 BFS 참조와 대조한다. 고정 시드 xorshift32.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static int rnd(int k) { return (int)(xorshift32() % (uint32_t)k); }

/* ---- 본문과 같은 DSU (이 파일만으로 컴파일되도록 복사) ---- */
#define MAXN 256
#define MAXM 1024
typedef struct { uint16_t parent[MAXN], size[MAXN]; int n, comps; } dsu_t;
typedef struct { uint16_t u, v; int32_t w; } edge_t;

static void dsu_init(dsu_t *d, int n) {
    d->n = n; d->comps = n;
    for (int i = 0; i < n; i++) { d->parent[i] = (uint16_t)i; d->size[i] = 1; }
}
static int dsu_find(dsu_t *d, int x) {
    while (d->parent[x] != x) { d->parent[x] = d->parent[d->parent[x]]; x = d->parent[x]; }
    return x;
}
static bool dsu_union(dsu_t *d, int a, int b) {
    int ra = dsu_find(d, a), rb = dsu_find(d, b);
    if (ra == rb) return false;
    if (d->size[ra] < d->size[rb]) { int t = ra; ra = rb; rb = t; }
    d->parent[rb] = (uint16_t)ra;
    d->size[ra] = (uint16_t)(d->size[ra] + d->size[rb]);
    d->comps--;
    return true;
}

/* ---- 참조: BFS 연결 요소 (인접 행렬, n <= MAXN) ---- */
static bool g_adj[MAXN][MAXN];
static int ref_components(int n, const edge_t *e, int m, const bool *skip, int *label) {
    for (int i = 0; i < n; i++) memset(g_adj[i], 0, (size_t)n);
    for (int k = 0; k < m; k++) if (!skip || !skip[k]) g_adj[e[k].u][e[k].v] = g_adj[e[k].v][e[k].u] = true;
    static int q[MAXN];
    for (int v = 0; v < n; v++) label[v] = -1;
    int c = 0;
    for (int s = 0; s < n; s++) {
        if (label[s] >= 0) continue;
        int qh = 0, qt = 0; q[qt++] = s; label[s] = c;
        while (qh < qt) {
            int v = q[qh++];
            for (int w = 0; w < n; w++) if (g_adj[v][w] && label[w] < 0) { label[w] = c; q[qt++] = w; }
        }
        c++;
    }
    return c;
}

/* ===================== 연습 1: Graph Valid Tree ===================== */
static dsu_t g_d;
static bool valid_tree(int n, const edge_t *e, int m) {
    if (m != n - 1) return false;                      /* 트리 ⇔ 연결 + 간선 n-1 ⇔ 사이클 없음 + 간선 n-1 */
    dsu_init(&g_d, n);
    for (int k = 0; k < m; k++) if (!dsu_union(&g_d, e[k].u, e[k].v)) return false;
    return true;
}

/* ===================== 연습 2: 오프라인 링크 장애 ===================== */
/* 링크 e[0..m) 가 모두 살아 있다가 fail[0..q) 순서로 하나씩 죽는다 (fail 은 서로 다른 링크 인덱스).
 * out[i] = fail[0..i] 가 죽은 직후의 연결 요소 수. DSU 는 삭제를 못 하므로 거꾸로 추가한다. */
static void comps_after_failures(int n, const edge_t *e, int m, const int *fail, int q, int *out) {
    static bool dead[MAXM];
    memset(dead, 0, sizeof(bool) * (size_t)m);
    for (int i = 0; i < q; i++) dead[fail[i]] = true;
    dsu_init(&g_d, n);
    for (int k = 0; k < m; k++) if (!dead[k]) dsu_union(&g_d, e[k].u, e[k].v);
    for (int i = q - 1; i >= 0; i--) {
        out[i] = g_d.comps;                            /* fail[0..i] 가 죽은 상태 */
        dsu_union(&g_d, e[fail[i]].u, e[fail[i]].v);   /* fail[i] 를 되살리면 fail[0..i-1] 만 죽은 상태 */
    }
}

/* ===================== 연습 3: 처음으로 전부 연결되는 시각 ===================== */
typedef struct { uint32_t t_ms; uint16_t a, b; } link_log_t;
static int g_ord[MAXM];
static int earliest_all_connected(int n, const link_log_t *lg, int m, uint32_t *t_out) {
    for (int i = 0; i < m; i++) g_ord[i] = i;            /* 로그는 시간순이 아닐 수 있다 → 삽입 정렬 (m 작음) */
    for (int i = 1; i < m; i++) {
        int x = g_ord[i], j = i;
        while (j > 0 && lg[g_ord[j - 1]].t_ms > lg[x].t_ms) { g_ord[j] = g_ord[j - 1]; j--; }
        g_ord[j] = x;
    }
    dsu_init(&g_d, n);
    if (n <= 1) { *t_out = 0; return 1; }
    for (int i = 0; i < m; i++) {
        const link_log_t *L = &lg[g_ord[i]];
        if (dsu_union(&g_d, L->a, L->b) && g_d.comps == 1) { *t_out = L->t_ms; return 1; }
    }
    return 0;
}

/* ===================== 연습 4: 옮겨야 할 케이블 수 ===================== */
static int cables_to_move(int n, const edge_t *e, int m) {
    if (m < n - 1) return -1;                          /* 연결 그래프는 간선 >= n-1 */
    dsu_init(&g_d, n);
    for (int k = 0; k < m; k++) dsu_union(&g_d, e[k].u, e[k].v);
    return g_d.comps - 1;                              /* 여분 간선 = m - (n - c) >= c - 1 이 보장됨 */
}

/* ===================== 연습 5: union by rank + 완전 경로 압축 ===================== */
typedef struct { uint16_t parent[MAXN]; uint8_t rank[MAXN]; int n, comps; } dsur_t;   /* 노드당 3 B */
static void dsur_init(dsur_t *d, int n) {
    d->n = n; d->comps = n;
    for (int i = 0; i < n; i++) { d->parent[i] = (uint16_t)i; d->rank[i] = 0; }
}
static int dsur_find(dsur_t *d, int x) {
    int r = x;
    while (d->parent[r] != r) r = d->parent[r];        /* 1-pass: 루트 찾기 */
    while (d->parent[x] != r) { int nx = d->parent[x]; d->parent[x] = (uint16_t)r; x = nx; }   /* 2-pass: 전부 루트로 */
    return r;
}
static bool dsur_union(dsur_t *d, int a, int b) {
    int ra = dsur_find(d, a), rb = dsur_find(d, b);
    if (ra == rb) return false;
    if (d->rank[ra] < d->rank[rb]) { int t = ra; ra = rb; rb = t; }
    d->parent[rb] = (uint16_t)ra;
    if (d->rank[ra] == d->rank[rb]) d->rank[ra]++;     /* 같은 rank 끼리일 때만 1 증가 */
    d->comps--;
    return true;
}
static int floor_log2(int n) { int b = -1; while (n) { b++; n >>= 1; } return b; }

static edge_t g_e[MAXM];
static int g_lab[MAXN], g_out[MAXM], g_fail[MAXM];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        const edge_t t1[] = { { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 }, { 1, 4, 0 } };
        const edge_t t2[] = { { 0, 1, 0 }, { 1, 2, 0 }, { 2, 3, 0 }, { 1, 3, 0 }, { 1, 4, 0 } };
        CHECK(valid_tree(5, t1, 4) && !valid_tree(5, t2, 5));       /* LeetCode 261 예제 두 개 */
        CHECK(valid_tree(1, NULL, 0) && !valid_tree(2, NULL, 0));
        unsigned long bad = 0, trees = 0;
        for (int t = 0; t < 3000; t++) {
            int n = 1 + rnd(8), m = rnd(n + 2);
            for (int k = 0; k < m; k++) g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), 0 };
            bool want = (m == n - 1) && ref_components(n, g_e, m, NULL, g_lab) == 1;
            if (want) trees++;
            if (valid_tree(n, g_e, m) != want) bad++;
        }
        printf("연습 1: 3000 graphs (%lu trees), mismatches=%lu\n", trees, bad);
        CHECK(bad == 0 && trees > 100);
    }
    /* ---- 연습 2 ---- */
    {
        unsigned long bad = 0, queries = 0;
        for (int t = 0; t < 200; t++) {
            int n = 2 + rnd(40), m = rnd(2 * n + 1), q = m ? 1 + rnd(m) : 0;
            for (int k = 0; k < m; k++) { g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), 0 }; g_fail[k] = k; }
            for (int k = m - 1; k > 0; k--) { int j = rnd(k + 1), x = g_fail[k]; g_fail[k] = g_fail[j]; g_fail[j] = x; }
            comps_after_failures(n, g_e, m, g_fail, q, g_out);
            static bool skip[MAXM];
            memset(skip, 0, sizeof skip);
            for (int i = 0; i < q; i++) {
                skip[g_fail[i]] = true;
                if (g_out[i] != ref_components(n, g_e, m, skip, g_lab)) bad++;
                queries++;
            }
        }
        printf("연습 2: 200 networks, %lu failure events, mismatches=%lu\n", queries, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 3 ---- */
    {
        const link_log_t ex[] = { { 20190101, 0, 1 }, { 20190104, 3, 4 }, { 20190107, 2, 3 }, { 20190211, 1, 5 },
                                  { 20190224, 2, 4 }, { 20190301, 0, 3 }, { 20190312, 1, 2 }, { 20190322, 4, 5 } };
        uint32_t tt = 0;
        CHECK(earliest_all_connected(6, ex, 8, &tt) == 1 && tt == 20190301);   /* LeetCode 1101 예제 1 */
        unsigned long bad = 0, never = 0;
        static link_log_t lg[MAXM];
        for (int t = 0; t < 300; t++) {
            int n = 1 + rnd(20), m = rnd(3 * n + 1);
            for (int k = 0; k < m; k++) lg[k] = (link_log_t){ (uint32_t)rnd(1000), (uint16_t)rnd(n), (uint16_t)rnd(n) };
            /* 참조: 시각 T 를 0..999 로 올리며 t_ms <= T 인 링크만으로 BFS → 처음 1 요소가 되는 T */
            int want_ok = 0; uint32_t want_t = 0;
            for (uint32_t T = 0; T < 1000 && !want_ok; T++) {
                int k2 = 0;
                for (int k = 0; k < m; k++) if (lg[k].t_ms <= T) g_e[k2++] = (edge_t){ lg[k].a, lg[k].b, 0 };
                if (ref_components(n, g_e, k2, NULL, g_lab) <= 1) { want_ok = 1; want_t = T; }
            }
            if (n <= 1) want_t = 0;
            uint32_t got_t = 0;
            int got_ok = earliest_all_connected(n, lg, m, &got_t);
            if (!want_ok) never++;
            if (got_ok != want_ok || (got_ok && got_t != want_t)) bad++;
        }
        printf("연습 3: 300 link logs (%lu never fully connected), mismatches=%lu\n", never, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 4 ---- */
    {
        const edge_t a[] = { { 0, 1, 0 }, { 0, 2, 0 }, { 1, 2, 0 } };
        const edge_t b[] = { { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 }, { 1, 2, 0 }, { 1, 3, 0 } };
        const edge_t c[] = { { 0, 1, 0 }, { 0, 2, 0 }, { 0, 3, 0 }, { 1, 2, 0 } };
        CHECK(cables_to_move(4, a, 3) == 1 && cables_to_move(6, b, 5) == 2 && cables_to_move(6, c, 4) == -1);
        unsigned long bad = 0;
        for (int t = 0; t < 1000; t++) {
            int n = 1 + rnd(30), m = rnd(2 * n + 1);
            for (int k = 0; k < m; k++) g_e[k] = (edge_t){ (uint16_t)rnd(n), (uint16_t)rnd(n), 0 };
            int c2 = ref_components(n, g_e, m, NULL, g_lab);
            int want = (m < n - 1) ? -1 : c2 - 1;
            if (cables_to_move(n, g_e, m) != want) bad++;
        }
        printf("연습 4: 1000 networks, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 5 ---- */
    {
        static dsur_t r;
        unsigned long bad = 0, bad_rank = 0;
        int max_rank_seen = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + rnd(MAXN), m = rnd(2 * n + 1);
            dsur_init(&r, n); dsu_init(&g_d, n);
            for (int k = 0; k < m; k++) {
                int a = rnd(n), b = rnd(n);
                if (dsur_union(&r, a, b) != dsu_union(&g_d, a, b) || r.comps != g_d.comps) bad++;
            }
            for (int x = 0; x < n; x++) {
                if (r.rank[x] > floor_log2(n)) bad_rank++;
                if (r.rank[x] > max_rank_seen) max_rank_seen = r.rank[x];
                int y = rnd(n);
                if ((dsur_find(&r, x) == dsur_find(&r, y)) != (dsu_find(&g_d, x) == dsu_find(&g_d, y))) bad++;
            }
        }
        dsur_init(&r, 256);                                 /* 2배 짝짓기 → rank 가 정확히 log2 n */
        for (int s = 1; s < 256; s *= 2) for (int i = 0; i + s < 256; i += 2 * s) dsur_union(&r, i, i + s);
        printf("연습 5: rank variant vs size variant mismatches=%lu, rank>log2n=%lu, max rank seen=%d, "
               "doubling n=256 -> root rank %d, bytes/node %zu vs %zu\n",
               bad, bad_rank, max_rank_seen, r.rank[dsur_find(&r, 0)],
               sizeof(uint16_t) + sizeof(uint8_t), 2 * sizeof(uint16_t));
        CHECK(bad == 0 && bad_rank == 0);
        CHECK(r.rank[dsur_find(&r, 0)] == 8 && r.comps == 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
