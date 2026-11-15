/* ALGO-A01 추가 연습 — 한 번에 검증
 *   연습 1  10/14 근사의 대가: 10/14 최적 경로의 "진짜 길이"(1, √2) vs 진짜 최적, 상한 10√2/14.
 *           + 8방향 격자 경로 자체가 직선보다 얼마나 긴가 (옥타일/유클리드 최대 = √(4−2√2))
 *   연습 2  거리 변환(squared EDT, Felzenszwalb–Huttenlocher 하한 포락선)으로 팽창 = d² ≤ r², 구간 찍기와 대조
 *   연습 3  가중 A* (f = g + w·h, 옥타일, 재개방 없음): 비용 ≤ w·최적, 확장 노드 수
 *   연습 4  여러 목표(충전 도크 k개): h = min 옥타일 (일관적) → 가장 가까운 도크까지 최단 비용
 * 자기완결 파일 (solution.c 를 include 하지 않음). 결정적 (xorshift32 고정 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXR 64
#define MAXC 64
#define MAXN (MAXR * MAXC)
#define INF INT32_MAX

typedef struct { int rows, cols; uint8_t cell[MAXN]; } grid_t;
static const int DR[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
static const int DC[8] = {0, 0, -1, 1, -1, 1, -1, 1};

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static bool cell_free(const grid_t *g, int r, int c) {
    return r >= 0 && r < g->rows && c >= 0 && c < g->cols && g->cell[r * g->cols + c] == 0;
}
static bool move_ok(const grid_t *g, int r, int c, int k) {
    int nr = r + DR[k], nc = c + DC[k];
    if (!cell_free(g, nr, nc)) return false;
    return k < 4 || (cell_free(g, nr, c) && cell_free(g, r, nc));
}
static int32_t octile(int r, int c, int gr, int gc) {
    int dx = r > gr ? r - gr : gr - r, dy = c > gc ? c - gc : gc - c;
    return dx > dy ? 10 * dx + 4 * dy : 10 * dy + 4 * dx;
}

static void grid_random(grid_t *g, uint32_t *seed, int rows, int cols) {
    g->rows = rows; g->cols = cols;
    int dens = (int)(xs32(seed) % 36);
    for (int i = 0; i < rows * cols; i++) g->cell[i] = (int)(xs32(seed) % 100) < dens;
}

/* ---- 일반화한 A*: 목표 집합(is_goal), 휴리스틱 표(hv[]), 가중치 w = wnum/wden. 동점은 g 큰 것 → 칸 번호 ---- */
typedef struct {
    int32_t g[MAXN]; int64_t f[MAXN];
    int16_t parent[MAXN], pos[MAXN], heap[MAXN];
    uint8_t closed[MAXN];
    int hn, expanded;
} ws_t;
static bool before(const ws_t *w, int a, int b) {
    if (w->f[a] != w->f[b]) return w->f[a] < w->f[b];
    if (w->g[a] != w->g[b]) return w->g[a] > w->g[b];
    return a < b;
}
static void sw(ws_t *w, int i, int j) {
    int16_t t = w->heap[i]; w->heap[i] = w->heap[j]; w->heap[j] = t;
    w->pos[w->heap[i]] = (int16_t)i; w->pos[w->heap[j]] = (int16_t)j;
}
static void up(ws_t *w, int i) { while (i > 0 && before(w, w->heap[i], w->heap[(i - 1) / 2])) { sw(w, i, (i - 1) / 2); i = (i - 1) / 2; } }
static void down(ws_t *w, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < w->hn && before(w, w->heap[l], w->heap[m])) m = l;
        if (r < w->hn && before(w, w->heap[r], w->heap[m])) m = r;
        if (m == i) return;
        sw(w, i, m); i = m;
    }
}
/* 반환: 처음 꺼낸 목표 칸까지의 비용 (*hit = 그 칸), 없으면 -1 */
static int astar_gen(const grid_t *g, int s, const uint8_t *is_goal, const int32_t *hv, int wnum, int wden, ws_t *w, int *hit) {
    int n = g->rows * g->cols;
    for (int i = 0; i < n; i++) { w->g[i] = INF; w->parent[i] = -1; w->pos[i] = -1; w->closed[i] = 0; }
    w->hn = 0; w->expanded = 0; *hit = -1;
    if (g->cell[s]) return -1;
    w->g[s] = 0; w->f[s] = (int64_t)wnum * hv[s];
    w->heap[0] = (int16_t)s; w->pos[s] = 0; w->hn = 1;
    while (w->hn > 0) {
        int u = w->heap[0];
        w->hn--;
        if (w->hn > 0) { w->heap[0] = w->heap[w->hn]; w->pos[w->heap[0]] = 0; down(w, 0); }
        w->pos[u] = -1; w->closed[u] = 1; w->expanded++;
        if (is_goal[u]) { *hit = u; return w->g[u]; }
        int ur = u / g->cols, uc = u % g->cols;
        for (int k = 0; k < 8; k++) {
            if (!move_ok(g, ur, uc, k)) continue;
            int v = (ur + DR[k]) * g->cols + uc + DC[k];
            int32_t nd = w->g[u] + (k < 4 ? 10 : 14);
            if (nd >= w->g[v] || w->closed[v]) continue;          /* 재개방 없음 */
            w->g[v] = nd; w->parent[v] = (int16_t)u;
            w->f[v] = (int64_t)wden * nd + (int64_t)wnum * hv[v];
            if (w->pos[v] < 0) { w->heap[w->hn] = (int16_t)v; w->pos[v] = (int16_t)w->hn; w->hn++; up(w, w->hn - 1); }
            else up(w, w->pos[v]);
        }
    }
    return -1;
}

/* 기준: 완화 반복 (1 출발점 → 모든 칸, 10/14) */
static void ref_dist(const grid_t *g, int src, int32_t *dist) {
    int n = g->rows * g->cols;
    for (int i = 0; i < n; i++) dist[i] = INF;
    if (!g->cell[src]) dist[src] = 0;
    for (int changed = 1, pass = 0; changed; pass++) {
        changed = 0;
        for (int j = 0; j < n; j++) {
            int u = (pass & 1) ? n - 1 - j : j;
            if (dist[u] == INF) continue;
            for (int k = 0; k < 8; k++)
                if (move_ok(g, u / g->cols, u % g->cols, k)) {
                    int v = u + DR[k] * g->cols + DC[k];
                    int32_t nd = dist[u] + (k < 4 ? 10 : 14);
                    if (nd < dist[v]) { dist[v] = nd; changed = 1; }
                }
        }
    }
}

/* ======================= 연습 1 — 진짜 길이 (1, √2) ======================= */
/* double 가중치 다익스트라 (O(V²) 배열판 — 힙 없이, 기준으로 충분) */
static void dijkstra_real(const grid_t *g, int s, double *d) {
    static uint8_t done[MAXN];
    int n = g->rows * g->cols;
    for (int i = 0; i < n; i++) { d[i] = INFINITY; done[i] = 0; }
    d[s] = 0;
    for (;;) {
        int u = -1;
        for (int i = 0; i < n; i++) if (!done[i] && d[i] < INFINITY && (u < 0 || d[i] < d[u])) u = i;
        if (u < 0) return;
        done[u] = 1;
        for (int k = 0; k < 8; k++)
            if (move_ok(g, u / g->cols, u % g->cols, k)) {
                int v = u + DR[k] * g->cols + DC[k];
                double nd = d[u] + (k < 4 ? 1.0 : sqrt(2.0));
                if (nd < d[v]) d[v] = nd;
            }
    }
}
/* 10/14 경로의 진짜 길이 */
static double real_len(const ws_t *w, const grid_t *g, int t) {
    double L = 0;
    for (int v = t; w->parent[v] >= 0; v = w->parent[v]) {
        int u = w->parent[v], dr = v / g->cols - u / g->cols, dc = v % g->cols - u % g->cols;
        L += (dr != 0 && dc != 0) ? sqrt(2.0) : 1.0;
    }
    return L;
}

/* ======================= 연습 2 — 거리 변환 ======================= */
/* 1D: f[0..n-1] (INF 가능) → d[q] = min_p ((q−p)² + f[p]). 포물선 하한 포락선, O(n) */
#define BIG 1000000000LL
static void edt_1d(const int64_t *f, int n, int64_t *d) {
    int v[MAXR > MAXC ? MAXR : MAXC], k = -1;
    double z[(MAXR > MAXC ? MAXR : MAXC) + 1];
    for (int q = 0; q < n; q++) {
        if (f[q] >= BIG) continue;                     /* 장애물이 없는 칸은 포물선 없음 */
        double s = 0;
        while (k >= 0) {
            int p = v[k];
            s = ((double)(f[q] + (int64_t)q * q) - (double)(f[p] + (int64_t)p * p)) / (2.0 * (q - p));
            if (s <= z[k]) k--; else break;
        }
        k++; v[k] = q; z[k] = k == 0 ? -INFINITY : s; z[k + 1] = INFINITY;
    }
    for (int q = 0, j = 0; q < n; q++) {
        if (k < 0) { d[q] = BIG; continue; }
        while (z[j + 1] < q) j++;
        d[q] = (int64_t)(q - v[j]) * (q - v[j]) + f[v[j]];
    }
}
/* 2D squared EDT: 칸마다 가장 가까운 장애물까지 거리² (장애물 없으면 BIG) — 열 방향, 그다음 행 방향 */
static void edt_2d(const grid_t *g, int64_t *D) {
    int64_t f[MAXR > MAXC ? MAXR : MAXC], d[MAXR > MAXC ? MAXR : MAXC];
    for (int i = 0; i < g->rows * g->cols; i++) D[i] = g->cell[i] ? 0 : BIG;
    for (int c = 0; c < g->cols; c++) {
        for (int r = 0; r < g->rows; r++) f[r] = D[r * g->cols + c];
        edt_1d(f, g->rows, d);
        for (int r = 0; r < g->rows; r++) D[r * g->cols + c] = d[r];
    }
    for (int r = 0; r < g->rows; r++) {
        for (int c = 0; c < g->cols; c++) f[c] = D[r * g->cols + c];
        edt_1d(f, g->cols, d);
        for (int c = 0; c < g->cols; c++) D[r * g->cols + c] = d[c];
    }
}
/* 본문의 구간 찍기 팽창 (기준으로 복사) */
static void inflate_stamp(const grid_t *src, grid_t *dst, int r) {
    *dst = *src;
    if (r <= 0) return;
    int hw[MAXR + MAXC];
    for (int dy = 0; dy <= r; dy++) { int rem = r * r - dy * dy, x = 0; while ((x + 1) * (x + 1) <= rem) x++; hw[dy] = x; }
    for (int a = 0; a < src->rows; a++)
        for (int b = 0; b < src->cols; b++) {
            if (!src->cell[a * src->cols + b]) continue;
            for (int dy = -r; dy <= r; dy++) {
                int rr = a + dy; if (rr < 0 || rr >= src->rows) continue;
                int w0 = hw[dy < 0 ? -dy : dy], c0 = b - w0 < 0 ? 0 : b - w0, c1 = b + w0 >= src->cols ? src->cols - 1 : b + w0;
                memset(&dst->cell[rr * src->cols + c0], 1, (size_t)(c1 - c0 + 1));
            }
        }
}

static grid_t G, G2;
static ws_t W;
static int32_t D1[MAXN], HV[MAXN];
static uint8_t GOAL[MAXN];
static double DR1[MAXN];
static int64_t EDT[MAXN];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        uint32_t seed = 0x1414u;
        int maps = 0, longer = 0, bad = 0; double worst = 1;
        for (int it = 0; it < 150; it++) {
            grid_random(&G, &seed, 8 + (int)(xs32(&seed) % 25), 8 + (int)(xs32(&seed) % 25));
            int n = G.rows * G.cols, s = (int)(xs32(&seed) % (uint32_t)n), t = (int)(xs32(&seed) % (uint32_t)n);
            G.cell[s] = G.cell[t] = 0;
            memset(GOAL, 0, (size_t)n); GOAL[t] = 1;
            for (int i = 0; i < n; i++) HV[i] = octile(i / G.cols, i % G.cols, t / G.cols, t % G.cols);
            int hit, c = astar_gen(&G, s, GOAL, HV, 1, 1, &W, &hit);
            if (c < 0) continue;
            maps++;
            dijkstra_real(&G, s, DR1);
            double L = real_len(&W, &G, t), opt = DR1[t];
            if (L < opt - 1e-9) bad++;                                     /* 진짜 최적보다 짧을 수는 없다 */
            if (L > opt + 1e-9) { longer++; if (L / opt > worst) worst = L / opt; }
        }
        double bound = 10 * sqrt(2.0) / 14;
        double emax = 0; int ex = 0, ey = 0;                               /* 빈 지도: 옥타일(진짜 길이)/직선 */
        for (int dx = 0; dx < 64; dx++)
            for (int dy = 1; dy <= dx; dy++) {
                double oct = (dx - dy) + dy * sqrt(2.0), e = sqrt((double)dx * dx + (double)dy * dy);
                if (oct / e > emax) { emax = oct / e; ex = dx; ey = dy; }
            }
        printf("연습 1: %d random maps, 10/14 path really longer on %d, worst %.5f (bound 10*sqrt2/14 = %.5f); "
               "grid vs straight line max %.5f at (%d,%d), sqrt(4-2*sqrt2) = %.5f\n",
               maps, longer, worst, bound, emax, ex, ey, sqrt(4 - 2 * sqrt(2.0)));
        CHECK(maps > 100 && bad == 0 && worst <= bound + 1e-12);
        CHECK(emax <= sqrt(4 - 2 * sqrt(2.0)) + 1e-12 && emax > 1.08);
        /* 만든 지도 (26×37): S(7,0) → T(7,36). U 통로 = 위로 7, 옆으로 36, 아래로 7 (직선 50번: 500, 진짜 50)
         * V 통로 = 대각선 18번 내려갔다 18번 올라옴. 꼭짓점에서 대각선 2번을 직선 2번으로 줄일 수 있어
         * 최선은 대각선 34 + 직선 2 (496, 진짜 34√2 + 2 = 50.083). 10/14 는 V(496 < 500), 진짜로는 U 가 짧다. */
        G.rows = 26; G.cols = 37; memset(G.cell, 1, (size_t)(26 * 37));
        for (int r = 0; r <= 7; r++) G.cell[r * 37 + 0] = G.cell[r * 37 + 36] = 0;
        for (int c = 0; c < 37; c++) G.cell[c] = 0;
        for (int i = 0; i < 36; i++) {                                   /* V: 대각선 한 걸음마다 두 직교 이웃도 판다 */
            int pr = i < 18 ? 7 + i : 7 + 36 - i, pc = i, qr = i < 18 ? pr + 1 : pr - 1, qc = i + 1;
            G.cell[pr * 37 + pc] = G.cell[qr * 37 + qc] = G.cell[qr * 37 + pc] = G.cell[pr * 37 + qc] = 0;
        }
        int s = 7 * 37, t = 7 * 37 + 36, hit;
        memset(GOAL, 0, sizeof GOAL); GOAL[t] = 1;
        for (int i = 0; i < 26 * 37; i++) HV[i] = octile(i / 37, i % 37, 7, 36);
        int c = astar_gen(&G, s, GOAL, HV, 1, 1, &W, &hit);
        dijkstra_real(&G, s, DR1);
        double L = real_len(&W, &G, t);
        printf("  built U/V map: 10/14 cost %d, its real length %.4f, real optimum %.4f, ratio %.5f\n", c, L, DR1[t], L / DR1[t]);
        CHECK(c == 496 && fabs(L - (34 * sqrt(2.0) + 2)) < 1e-9 && fabs(DR1[t] - 50.0) < 1e-9);
    }
    /* ---- 연습 2 ---- */
    {
        uint32_t seed = 0xED7u;
        int bad = 0, cases = 0;
        for (int it = 0; it < 60; it++) {
            int rows = 1 + (int)(xs32(&seed) % 64), cols = 1 + (int)(xs32(&seed) % 64);
            grid_random(&G, &seed, rows, cols);
            if (it % 4 == 0) for (int i = 0; i < rows * cols; i++) G.cell[i] = (xs32(&seed) % 200) == 0;   /* 아주 드문 장애물 */
            if (it == 0) memset(G.cell, 0, (size_t)(rows * cols));                                           /* 장애물 없음 */
            edt_2d(&G, EDT);
            for (int r = 0; r <= 10; r++) {
                inflate_stamp(&G, &G2, r); cases++;
                for (int i = 0; i < rows * cols; i++)
                    if ((EDT[i] <= (int64_t)r * r) != (G2.cell[i] != 0)) { bad++; break; }
            }
        }
        printf("연습 2: EDT threshold vs span stamping: %d (map, r) cases, mismatches=%d\n", cases, bad);
        CHECK(bad == 0);
        G.rows = 1; G.cols = 7; memset(G.cell, 0, 7); G.cell[0] = 1; G.cell[6] = 1;   /* 1×7: 양 끝 장애물 */
        edt_2d(&G, EDT);
        CHECK(EDT[0] == 0 && EDT[1] == 1 && EDT[2] == 4 && EDT[3] == 9 && EDT[4] == 4 && EDT[5] == 1);
    }
    /* ---- 연습 3 ---- */
    {
        const int wn[5] = {1, 5, 3, 2, 3}, wd[5] = {1, 4, 2, 1, 1};       /* w = 1, 1.25, 1.5, 2, 3 */
        long exp[5] = {0}; int sub[5] = {0}, bound_bad = 0, maps = 0;
        double worst[5] = {1, 1, 1, 1, 1};
        uint32_t seed = 0x3A5u;
        for (int it = 0; it < 200; it++) {
            grid_random(&G, &seed, 16 + (int)(xs32(&seed) % 49), 16 + (int)(xs32(&seed) % 49));
            int n = G.rows * G.cols, s = (int)(xs32(&seed) % (uint32_t)n), t = (int)(xs32(&seed) % (uint32_t)n);
            G.cell[s] = G.cell[t] = 0;
            memset(GOAL, 0, (size_t)n); GOAL[t] = 1;
            for (int i = 0; i < n; i++) HV[i] = octile(i / G.cols, i % G.cols, t / G.cols, t % G.cols);
            int hit, opt = astar_gen(&G, s, GOAL, HV, 1, 1, &W, &hit);
            if (opt <= 0) continue;
            maps++;
            for (int k = 0; k < 5; k++) {
                int c = astar_gen(&G, s, GOAL, HV, wn[k], wd[k], &W, &hit);
                exp[k] += W.expanded;
                if (c < opt || (int64_t)c * wd[k] > (int64_t)opt * wn[k]) bound_bad++;
                if (c > opt) { sub[k]++; if ((double)c / opt > worst[k]) worst[k] = (double)c / opt; }
            }
        }
        printf("연습 3: %d maps\n", maps);
        for (int k = 0; k < 5; k++)
            printf("  w=%.2f  expanded mean %7.1f  suboptimal maps %3d  worst ratio %.4f\n",
                   (double)wn[k] / wd[k], (double)exp[k] / maps, sub[k], worst[k]);
        CHECK(maps > 150 && bound_bad == 0 && sub[0] == 0);
        CHECK(exp[4] < exp[0] && sub[4] > 0);
    }
    /* ---- 연습 4 ---- */
    {
        uint32_t seed = 0xD0C4u;
        int bad = 0, maps = 0, fewer = 0;
        static int dock[8];
        for (int it = 0; it < 150; it++) {
            grid_random(&G, &seed, 8 + (int)(xs32(&seed) % 41), 8 + (int)(xs32(&seed) % 41));
            int n = G.rows * G.cols, s = (int)(xs32(&seed) % (uint32_t)n), k = 1 + (int)(xs32(&seed) % 6);
            memset(GOAL, 0, (size_t)n);
            G.cell[s] = 0;
            for (int j = 0; j < k; j++) { dock[j] = (int)(xs32(&seed) % (uint32_t)n); G.cell[dock[j]] = 0; GOAL[dock[j]] = 1; }
            for (int i = 0; i < n; i++) {
                HV[i] = INF;
                for (int j = 0; j < k; j++) {
                    int32_t h = octile(i / G.cols, i % G.cols, dock[j] / G.cols, dock[j] % G.cols);
                    if (h < HV[i]) HV[i] = h;
                }
            }
            int hit, c = astar_gen(&G, s, GOAL, HV, 1, 1, &W, &hit);
            int ea = W.expanded;
            for (int i = 0; i < n; i++) HV[i] = 0;                            /* 다익스트라 (h ≡ 0) */
            int hit0, c0 = astar_gen(&G, s, GOAL, HV, 1, 1, &W, &hit0);
            ref_dist(&G, s, D1);
            int32_t want = INF;
            for (int j = 0; j < k; j++) if (D1[dock[j]] < want) want = D1[dock[j]];
            int wv = want == INF ? -1 : want;
            maps++;
            if (c != wv || c0 != wv || (c >= 0 && (!GOAL[hit] || D1[hit] != want))) bad++;
            if (ea < W.expanded) fewer++;
        }
        printf("연습 4: %d maps, mismatches=%d, A* expanded fewer than Dijkstra on %d maps\n", maps, bad, fewer);
        CHECK(bad == 0 && fewer > 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
