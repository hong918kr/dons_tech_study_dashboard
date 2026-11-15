/* ALGO-M04 추가 연습 — 한 번에 검증
 *   연습 1  최단 경로 개수 (BFS 층 순서로 ways 누적) — 빈 격자 C(R+C-2, R-1) + 작은 격자 전수 열거와 대조
 *   연습 2  다중 시작점 BFS: 가장 가까운 충전 도크까지 거리 (LeetCode 286 Walls and Gates / 542 01 Matrix 꼴)
 *   연습 3  8방향 이동, 모서리 자르기(corner cutting) 금지 — 빈 격자 체비셰프 거리 + 완화 기준 구현
 *   연습 4  0-1 BFS: 장애물을 최소 몇 개 부수면 목표에 닿나 (덱) — 가중 완화(Bellman 식) 기준 구현
 *   연습 5  닫힌 빈 공간(pocket): 테두리에 닿지 않는 빈칸 4-연결 요소 수 (LeetCode 130 Surrounded Regions 꼴)
 * 칸 id = r * 64 + c (solution.c 와 같은 규칙). 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_R 64
#define MAX_C 64
#define MAX_CELLS (MAX_R * MAX_C)
#define CELL(r, c) ((int)(r) * MAX_C + (int)(c))
#define ROW(id) ((id) >> 6)
#define COL(id) ((id) & (MAX_C - 1))

typedef struct { int rows, cols; uint8_t cell[MAX_CELLS]; } grid_t;
static const int8_t DR[8] = { -1, 0, 1, 0, -1, -1, 1, 1 };
static const int8_t DC[8] = { 0, 1, 0, -1, -1, 1, 1, -1 };
static int in_bounds(const grid_t *g, int r, int c) { return r >= 0 && r < g->rows && c >= 0 && c < g->cols; }
static int free_at(const grid_t *g, int r, int c) { return in_bounds(g, r, c) && !g->cell[CELL(r, c)]; }

/* ---------- 연습 1: 최단 경로 개수 ---------- */
/* ways[v] = start 에서 v 까지 최단 경로 수. dist 층 순서(= 큐 순서)로 처리하면 u 를 꺼낼 때 ways[u] 는 확정이다:
 * u 로 들어오는 최단 간선은 모두 dist[u]-1 층에서 오고, 그 층은 u 보다 먼저 전부 꺼내졌다. */
int bfs_count_paths(const grid_t *g, int start, int16_t dist[], uint64_t ways[], int16_t queue[]) {
    for (int i = 0; i < MAX_CELLS; i++) { dist[i] = -1; ways[i] = 0; }
    if (!free_at(g, ROW(start), COL(start))) return 0;
    int head = 0, tail = 0;
    dist[start] = 0; ways[start] = 1; queue[tail++] = (int16_t)start;
    while (head < tail) {
        int u = queue[head++];
        for (int k = 0; k < 4; k++) {
            int nr = ROW(u) + DR[k], nc = COL(u) + DC[k];
            if (!free_at(g, nr, nc)) continue;
            int v = CELL(nr, nc);
            if (dist[v] < 0) { dist[v] = (int16_t)(dist[u] + 1); queue[tail++] = (int16_t)v; }
            if (dist[v] == dist[u] + 1) ways[v] += ways[u];     /* 발견 여부와 상관없이 다음 층이면 더한다 */
        }
    }
    return tail;
}

/* ---------- 연습 2: 다중 시작점 BFS ---------- */
/* 모든 도크를 거리 0 으로 큐에 먼저 넣는다 = 가상의 슈퍼 소스에서 거리 1 인 칸들. src[v] = v 에 처음 닿은 도크 번호. */
int nearest_dock(const grid_t *g, const int16_t docks[], int nd, int16_t dist[], int16_t src[], int16_t queue[]) {
    for (int i = 0; i < MAX_CELLS; i++) { dist[i] = -1; src[i] = -1; }
    int head = 0, tail = 0;
    for (int d = 0; d < nd; d++) {
        int s = docks[d];
        if (!free_at(g, ROW(s), COL(s)) || dist[s] == 0) continue;   /* 막힌 도크·중복 도크는 건너뜀 */
        dist[s] = 0; src[s] = (int16_t)d; queue[tail++] = (int16_t)s;
    }
    while (head < tail) {
        int u = queue[head++];
        for (int k = 0; k < 4; k++) {
            int nr = ROW(u) + DR[k], nc = COL(u) + DC[k];
            if (!free_at(g, nr, nc) || dist[CELL(nr, nc)] >= 0) continue;
            int v = CELL(nr, nc);
            dist[v] = (int16_t)(dist[u] + 1); src[v] = src[u]; queue[tail++] = (int16_t)v;
        }
    }
    return tail;
}

/* ---------- 연습 3: 8방향, 모서리 자르기 금지 ---------- */
/* 대각선 (dr, dc) 이동은 두 직교 이웃 (r+dr, c) 와 (r, c+dc) 가 모두 빈칸일 때만 허용 (로봇 몸체가 모서리를 긁지 않게). */
static int move8_ok(const grid_t *g, int r, int c, int k) {
    int nr = r + DR[k], nc = c + DC[k];
    if (!free_at(g, nr, nc)) return 0;
    if (k >= 4 && (!free_at(g, nr, c) || !free_at(g, r, nc))) return 0;
    return 1;
}
int bfs8_dist(const grid_t *g, int start, int16_t dist[], int16_t queue[]) {
    for (int i = 0; i < MAX_CELLS; i++) dist[i] = -1;
    if (!free_at(g, ROW(start), COL(start))) return 0;
    int head = 0, tail = 0;
    dist[start] = 0; queue[tail++] = (int16_t)start;
    while (head < tail) {
        int u = queue[head++];
        for (int k = 0; k < 8; k++) {
            if (!move8_ok(g, ROW(u), COL(u), k)) continue;
            int v = CELL(ROW(u) + DR[k], COL(u) + DC[k]);
            if (dist[v] >= 0) continue;
            dist[v] = (int16_t)(dist[u] + 1); queue[tail++] = (int16_t)v;
        }
    }
    return tail;
}

/* ---------- 연습 4: 0-1 BFS (장애물 부수기 최소 개수) ---------- */
/* 칸 v 에 들어가는 비용 = cell[v] (빈칸 0, 장애물 1). 비용 0 간선은 덱 앞, 1 은 뒤.
 * 덱 불변식: 덱 안 거리값이 앞에서 뒤로 비내림차순이고 max - min <= 1 → 꺼내는 순서가 다익스트라와 같다.
 * 칸은 여러 번 들어갈 수 있다(거리가 줄 때마다) → 꺼낼 때 done 확인. push 총량 <= 1 + 4V. */
#define DQ_CAP (2 * (4 * MAX_CELLS + 1) + 1)
int min_breaks(const grid_t *g, int start, int goal, int16_t cost[], int16_t dq[]) {
    static uint8_t done[MAX_CELLS];
    for (int i = 0; i < MAX_CELLS; i++) { cost[i] = INT16_MAX; done[i] = 0; }
    if (!in_bounds(g, ROW(start), COL(start)) || !in_bounds(g, ROW(goal), COL(goal))) return -1;
    int head = DQ_CAP / 2, tail = DQ_CAP / 2;          /* 가운데서 시작: 앞으로도 뒤로도 최대 4V+1 칸 */
    cost[start] = 0;                                    /* 시작 칸 자체는 비용 0 으로 본다 (이미 서 있다) */
    dq[tail++] = (int16_t)start;
    while (head < tail) {
        int u = dq[head++];
        if (done[u]) continue;
        done[u] = 1;
        if (u == goal) return cost[u];
        for (int k = 0; k < 4; k++) {
            int nr = ROW(u) + DR[k], nc = COL(u) + DC[k];
            if (!in_bounds(g, nr, nc)) continue;
            int v = CELL(nr, nc), w = g->cell[v];
            if (cost[u] + w < cost[v]) {
                cost[v] = (int16_t)(cost[u] + w);
                if (w == 0) dq[--head] = (int16_t)v; else dq[tail++] = (int16_t)v;
            }
        }
    }
    return -1;   /* 범위 안이면 항상 닿으니 오지 않는다 */
}

/* ---------- 연습 5: 닫힌 빈 공간 ---------- */
/* 1) 테두리 빈칸에서 flood → "열린" 표시  2) 남은 빈칸을 4-연결 요소로 센다. label: 열린 빈칸 -1, 닫힌 공간 1..k, 장애물 0 */
static void fill4(const grid_t *g, int s, int16_t mark, int16_t label[], int16_t stack[]) {
    int sp = 0;
    label[s] = mark; stack[sp++] = (int16_t)s;
    while (sp > 0) {
        int u = stack[--sp];
        for (int k = 0; k < 4; k++) {
            int nr = ROW(u) + DR[k], nc = COL(u) + DC[k];
            if (!free_at(g, nr, nc) || label[CELL(nr, nc)]) continue;
            label[CELL(nr, nc)] = mark; stack[sp++] = (int16_t)CELL(nr, nc);
        }
    }
}
int closed_pockets(const grid_t *g, int16_t label[], int16_t stack[]) {
    memset(label, 0, sizeof(int16_t) * MAX_CELLS);
    for (int r = 0; r < g->rows; r++)
        for (int c = 0; c < g->cols; c++) {
            int border = (r == 0 || c == 0 || r == g->rows - 1 || c == g->cols - 1);
            if (border && !g->cell[CELL(r, c)] && !label[CELL(r, c)]) fill4(g, CELL(r, c), -1, label, stack);
        }
    int k = 0;
    for (int r = 1; r < g->rows - 1; r++)
        for (int c = 1; c < g->cols - 1; c++)
            if (!g->cell[CELL(r, c)] && !label[CELL(r, c)]) fill4(g, CELL(r, c), (int16_t)++k, label, stack);
    return k;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

static void load_map(grid_t *g, const char *const rows[], int nrows) {
    memset(g, 0, sizeof *g);
    g->rows = nrows; g->cols = (int)strlen(rows[0]);
    for (int r = 0; r < nrows; r++)
        for (int c = 0; c < g->cols; c++) g->cell[CELL(r, c)] = (uint8_t)(rows[r][c] == '#');
}
static void random_grid(grid_t *g, int maxr, int maxc, uint32_t maxdens) {
    memset(g, 0, sizeof *g);
    g->rows = 1 + (int)(xorshift32() % (uint32_t)maxr);
    g->cols = 1 + (int)(xorshift32() % (uint32_t)maxc);
    uint32_t dens = xorshift32() % (maxdens + 1);
    for (int r = 0; r < g->rows; r++)
        for (int c = 0; c < g->cols; c++) g->cell[CELL(r, c)] = (uint8_t)(xorshift32() % 100 < dens);
}
static int random_cell(const grid_t *g) { return CELL(xorshift32() % (uint32_t)g->rows, xorshift32() % (uint32_t)g->cols); }

/* 기준: 일반 완화. 이동 규칙 conn8 (0 = 4방향, 1 = 8방향 모서리 금지), 진입 비용 = weighted ? cell[v] : 1 (장애물 통과는 weighted 일 때만). */
#define INF 0x7fffffff
static void ref_relax(const grid_t *g, int start, int conn8, int weighted, int32_t d[]) {
    for (int i = 0; i < MAX_CELLS; i++) d[i] = INF;
    if (!in_bounds(g, ROW(start), COL(start)) || (!weighted && g->cell[start])) return;
    d[start] = 0;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int r = 0; r < g->rows; r++)
            for (int c = 0; c < g->cols; c++) {
                int u = CELL(r, c);
                if (d[u] == INF) continue;
                for (int k = 0; k < (conn8 ? 8 : 4); k++) {
                    int nr = r + DR[k], nc = c + DC[k];
                    if (!in_bounds(g, nr, nc)) continue;
                    if (!weighted && (conn8 ? !move8_ok(g, r, c, k) : g->cell[CELL(nr, nc)])) continue;
                    int32_t nd = d[u] + (weighted ? g->cell[CELL(nr, nc)] : 1);
                    if (nd < d[CELL(nr, nc)]) { d[CELL(nr, nc)] = nd; changed = 1; }
                }
            }
    }
}

/* 전수 열거: 길이가 정확히 L 인 단순 경로 수 (맨해튼 하한으로 가지치기 — 가지치기는 "닿을 수 없는" 가지만 자른다) */
static uint8_t g_on[MAX_CELLS];
static uint64_t enum_paths(const grid_t *g, int u, int goal, int left) {
    int md = (ROW(u) > ROW(goal) ? ROW(u) - ROW(goal) : ROW(goal) - ROW(u)) + (COL(u) > COL(goal) ? COL(u) - COL(goal) : COL(goal) - COL(u));
    if (md > left) return 0;
    if (left == 0) return u == goal;
    uint64_t s = 0;
    g_on[u] = 1;
    for (int k = 0; k < 4; k++) {
        int nr = ROW(u) + DR[k], nc = COL(u) + DC[k];
        if (free_at(g, nr, nc) && !g_on[CELL(nr, nc)]) s += enum_paths(g, CELL(nr, nc), goal, left - 1);
    }
    g_on[u] = 0;
    return s;
}

static grid_t g_grid;
static int16_t g_dist[MAX_CELLS], g_src[MAX_CELLS], g_q[MAX_CELLS], g_lab[MAX_CELLS], g_d1[MAX_CELLS];
static int16_t g_dq[DQ_CAP];
static uint64_t g_ways[MAX_CELLS];
static int32_t g_ref[MAX_CELLS];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        static uint64_t C[67][67];                           /* 파스칼 삼각형, C(66, 33) ≈ 7.2e18 < 2^64 */
        for (int n = 0; n <= 66; n++) { C[n][0] = 1; for (int k = 1; k <= n; k++) C[n][k] = C[n - 1][k - 1] + (k <= n - 1 ? C[n - 1][k] : 0); }
        unsigned long bad = 0, cases = 0;
        for (int R = 1; R <= 34; R++)
            for (int Cc = 1; Cc <= 34; Cc++) {
                memset(&g_grid, 0, sizeof g_grid); g_grid.rows = R; g_grid.cols = Cc;
                bfs_count_paths(&g_grid, CELL(0, 0), g_dist, g_ways, g_q);
                cases++;
                if (g_ways[CELL(R - 1, Cc - 1)] != C[R + Cc - 2][R - 1]) bad++;
            }
        memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 34; g_grid.cols = 34;
        bfs_count_paths(&g_grid, CELL(0, 0), g_dist, g_ways, g_q);
        printf("연습 1: empty RxC (R,C<=34) %lu cases bad=%lu, 34x34 corner ways=%llu\n",
               cases, bad, (unsigned long long)g_ways[CELL(33, 33)]);
        CHECK(bad == 0 && g_ways[CELL(33, 33)] == 7219428434016265740ull);   /* C(66,33) */
        unsigned long bad2 = 0, tested = 0;
        for (int t = 0; t < 600; t++) {
            random_grid(&g_grid, 5, 5, 35);
            int s = random_cell(&g_grid), e = random_cell(&g_grid);
            g_grid.cell[s] = 0;
            bfs_count_paths(&g_grid, s, g_dist, g_ways, g_q);
            if (g_dist[e] < 0) { if (g_ways[e] != 0) bad2++; continue; }
            tested++;
            if (g_ways[e] != enum_paths(&g_grid, s, e, g_dist[e])) bad2++;
        }
        printf("연습 1: 600 random grids <=5x5, %lu reachable pairs enumerated, bad=%lu\n", tested, bad2);
        CHECK(bad2 == 0);
        static const char *const m7[] = { "S.....", ".##...", ".##...", ".....G" };   /* 본문 M7 */
        load_map(&g_grid, m7, 4);
        bfs_count_paths(&g_grid, CELL(0, 0), g_dist, g_ways, g_q);
        printf("연습 1: M7 room+pillar shortest paths = %llu\n", (unsigned long long)g_ways[CELL(3, 5)]);
        CHECK(g_ways[CELL(3, 5)] == 11);   /* 기둥 오른쪽: (0,3) 까지 1가지 × C(5,2) = 10, 왼쪽: 1 */
    }

    /* ---- 연습 2 ---- */
    {
        unsigned long bad = 0;
        for (int t = 0; t < 400; t++) {
            random_grid(&g_grid, 24, 24, 40);
            int16_t docks[5];
            int nd = 1 + (int)(xorshift32() % 5);
            for (int d = 0; d < nd; d++) docks[d] = (int16_t)random_cell(&g_grid);
            nearest_dock(&g_grid, docks, nd, g_dist, g_src, g_q);
            /* 기준: 도크마다 단일 시작 BFS(완화) 의 최솟값 */
            static int32_t refd[5][MAX_CELLS];
            for (int d = 0; d < nd; d++) ref_relax(&g_grid, docks[d], 0, 0, refd[d]);
            for (int i = 0; i < MAX_CELLS; i++) {
                int32_t best = INF;
                for (int d = 0; d < nd; d++) if (refd[d][i] < best) best = refd[d][i];
                int want = best == INF ? -1 : best;
                if (g_dist[i] != want) { bad++; break; }
                if (want >= 0 && refd[g_src[i]][i] != want) { bad++; break; }   /* src 가 가리키는 도크가 실제로 가장 가깝다 */
            }
        }
        printf("연습 2: 400 random grids, 1-5 docks, bad=%lu\n", bad);
        CHECK(bad == 0);
        static const char *const hall[] = { "D...#...D" };
        load_map(&g_grid, hall, 1);
        int16_t d2[2] = { CELL(0, 0), CELL(0, 8) };
        nearest_dock(&g_grid, d2, 2, g_dist, g_src, g_q);
        CHECK(g_dist[CELL(0, 3)] == 3 && g_src[CELL(0, 3)] == 0 && g_dist[CELL(0, 5)] == 3 && g_src[CELL(0, 5)] == 1);
    }

    /* ---- 연습 3 ---- */
    {
        unsigned long bad = 0;
        memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 64; g_grid.cols = 64;
        bfs8_dist(&g_grid, CELL(10, 20), g_dist, g_q);
        for (int r = 0; r < 64; r++)
            for (int c = 0; c < 64; c++) {
                int dr = r > 10 ? r - 10 : 10 - r, dc = c > 20 ? c - 20 : 20 - c;
                if (g_dist[CELL(r, c)] != (dr > dc ? dr : dc)) bad++;          /* 체비셰프 거리 */
            }
        for (int t = 0; t < 600; t++) {
            random_grid(&g_grid, 24, 24, 45);
            int s = random_cell(&g_grid);
            bfs8_dist(&g_grid, s, g_dist, g_q);
            ref_relax(&g_grid, s, 1, 0, g_ref);
            for (int i = 0; i < MAX_CELLS; i++)
                if (g_dist[i] != (g_ref[i] == INF ? -1 : g_ref[i])) { bad++; break; }
        }
        printf("연습 3: empty 64x64 Chebyshev + 600 random grids, bad=%lu\n", bad);
        CHECK(bad == 0);
        static const char *const m5[] = { "#.#.", ".#.#", "#.#.", "S..G" };     /* 본문 M5: 대각선 틈은 지날 수 없다 */
        load_map(&g_grid, m5, 4);
        bfs8_dist(&g_grid, CELL(3, 0), g_dist, g_q);
        CHECK(g_dist[CELL(3, 3)] == 3 && g_dist[CELL(1, 2)] == -1 && g_dist[CELL(2, 1)] == 2);   /* (3,0)→(2,1) 대각선은 (2,0)# 때문에 금지 */
    }

    /* ---- 연습 4 ---- */
    {
        unsigned long bad = 0;
        for (int t = 0; t < 600; t++) {
            random_grid(&g_grid, 24, 24, 70);
            int s = random_cell(&g_grid), e = random_cell(&g_grid);
            int got = min_breaks(&g_grid, s, e, g_d1, g_dq);
            ref_relax(&g_grid, s, 0, 1, g_ref);
            if (got != g_ref[e]) bad++;
        }
        printf("연습 4: 600 random grids (density up to 70%%), bad=%lu\n", bad);
        CHECK(bad == 0);
        static const char *const m4[] = { "S...", ".###", ".#G#", ".###" };     /* 본문 M4: 고리를 한 번 부순다 */
        static const char *const m10[] = { "S#.#", "#.#.", ".#.#", "#.#G" };    /* 본문 M10: 6걸음 중 3칸이 장애물 */
        load_map(&g_grid, m4, 4);
        int a = min_breaks(&g_grid, CELL(0, 0), CELL(2, 2), g_d1, g_dq);
        load_map(&g_grid, m10, 4);
        int b = min_breaks(&g_grid, CELL(0, 0), CELL(3, 3), g_d1, g_dq);
        printf("연습 4: M4 breaks=%d, M10 breaks=%d\n", a, b);
        CHECK(a == 1 && b == 3);
    }

    /* ---- 연습 5 ---- */
    {
        unsigned long bad = 0;
        for (int t = 0; t < 600; t++) {
            random_grid(&g_grid, 24, 24, 55);
            int got = closed_pockets(&g_grid, g_lab, g_q);
            /* 기준: 빈칸 라벨 전파(4-연결, 최소 id) 후 테두리에 닿는 요소를 뺀다 */
            static int32_t lab[MAX_CELLS];
            static uint8_t touches[MAX_CELLS];
            for (int i = 0; i < MAX_CELLS; i++) { lab[i] = (in_bounds(&g_grid, ROW(i), COL(i)) && !g_grid.cell[i]) ? i : -1; touches[i] = 0; }
            for (int changed = 1; changed;) {
                changed = 0;
                for (int i = 0; i < MAX_CELLS; i++) {
                    if (lab[i] < 0) continue;
                    for (int k = 0; k < 4; k++) {
                        int nr = ROW(i) + DR[k], nc = COL(i) + DC[k];
                        if (free_at(&g_grid, nr, nc) && lab[CELL(nr, nc)] < lab[i]) { lab[i] = lab[CELL(nr, nc)]; changed = 1; }
                    }
                }
            }
            int roots = 0;
            for (int i = 0; i < MAX_CELLS; i++)
                if (lab[i] >= 0 && (ROW(i) == 0 || COL(i) == 0 || ROW(i) == g_grid.rows - 1 || COL(i) == g_grid.cols - 1)) touches[lab[i]] = 1;
            for (int i = 0; i < MAX_CELLS; i++) roots += (lab[i] == i && !touches[i]);
            if (got != roots) bad++;
        }
        printf("연습 5: 600 random grids, bad=%lu\n", bad);
        CHECK(bad == 0);
        static const char *const m4[] = { "S...", ".###", ".#G#", ".###" };
        static const char *const m5[] = { "#.#.", ".#.#", "#.#.", "S..G" };
        static const char *const m8[] = { "##S..", "##...", "..#..", "G..##" };
        load_map(&g_grid, m4, 4); int p4 = closed_pockets(&g_grid, g_lab, g_q);
        load_map(&g_grid, m5, 4); int p5 = closed_pockets(&g_grid, g_lab, g_q);
        load_map(&g_grid, m8, 4); int p8 = closed_pockets(&g_grid, g_lab, g_q);
        printf("연습 5: closed pockets M4=%d M5=%d M8=%d\n", p4, p5, p8);
        CHECK(p4 == 1 && p5 == 1 && p8 == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
