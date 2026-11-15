/* ALGO-A01 A* 경로 계획: 휴리스틱과 장애물 팽창
 *
 * 8방향 점유 격자(최대 64×64, 0 = 빈칸, 1 = 장애물)에서 정수 비용 10(직선) / 14(대각선) 최단 경로.
 *   Part A  이동 규칙(모서리 자르기 금지) + 휴리스틱(0 / 맨해튼 / 옥타일) + A* (indexed min-PQ, 닫힌 집합, 재개방 없음)
 *           → 다익스트라(= h ≡ 0)·"완화 반복" 기준 구현과 비용 대조, 확장 노드 집합 비교
 *   Part B  맨해튼은 8방향에서 허용 불가 → 다익스트라보다 비싼 경로를 내는 지도, 동점 처리(큰 g 우선) 효과,
 *           일관성 ⇒ 닫힌 칸 개선 0회
 *   Part C  로봇 반경 r 칸 장애물 팽창(유클리드 원판, 행 구간 찍기) → 팽창 지도에서 A* → ASCII 출력
 * 모든 테스트는 결정적 (고정 지도 + xorshift32 시드). 알고리즘 안에서 malloc 없음 (정적 배열).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXR 64
#define MAXC 64
#define MAXN (MAXR * MAXC)
#define C_ST 10                 /* 직선 한 칸 */
#define C_DG 14                 /* 대각선 한 칸: 10·√2 = 14.142… 의 정수 근사 → "10/14 거리"에 대한 최적 */
#define INF INT32_MAX
#define MAX_INFL 96             /* 팽창 반경 상한 (64×64 지도의 대각선 ≈ 89 칸이면 충분) */

typedef struct { int rows, cols; uint8_t cell[MAXN]; } grid_t;   /* cell[r*cols + c]: 0 빈칸, 1 장애물 */

/* 이동 방향: k = 0..3 직선 (N S W E), k = 4..7 대각선 (NW NE SW SE) */
static const int DR[8] = {-1, 1, 0, 0, -1, -1, 1, 1};
static const int DC[8] = {0, 0, -1, 1, -1, 1, -1, 1};

static bool cell_free(const grid_t *g, int r, int c) {
    return r >= 0 && r < g->rows && c >= 0 && c < g->cols && g->cell[r * g->cols + c] == 0;
}

/* ======================= Part A — 이동 규칙과 휴리스틱 ======================= */
/* TODO 1: (r,c) 에서 방향 k 로 한 칸 갈 수 있나? 도착 칸이 지도 안의 빈칸이어야 하고,
 * 대각선(k ≥ 4)은 스쳐 가는 두 직교 이웃 (r+DR[k], c) 와 (r, c+DC[k]) 가 **둘 다** 빈칸일 때만 허용
 * (모서리 자르기 금지 — 두 장애물 사이로 비집고 지나가기도 당연히 금지). (r,c) 는 빈칸이라고 가정. */
static bool move_ok(const grid_t *g, int r, int c, int k) {
    int nr = r + DR[k], nc = c + DC[k];
    if (!cell_free(g, nr, nc)) return false;
    if (k >= 4 && (!cell_free(g, nr, c) || !cell_free(g, r, nc))) return false;
    return true;
}

typedef enum { H_ZERO, H_MANHATTAN, H_OCTILE } heur_t;

/* TODO 2: (r,c) 에서 목표 (gr,gc) 까지의 추정 비용. dx = |Δ행|, dy = |Δ열|.
 *   H_ZERO       0                               → A* 가 다익스트라가 된다
 *   H_MANHATTAN  10·(dx + dy)                    (4방향 격자의 정확한 거리 — 8방향에선 과대평가)
 *   H_OCTILE     10·max(dx,dy) + 4·min(dx,dy)    (장애물 없는 8방향 10/14 격자의 정확한 거리) */
static int32_t heur(heur_t h, int r, int c, int gr, int gc) {
    int dx = r > gr ? r - gr : gr - r, dy = c > gc ? c - gc : gc - c;
    switch (h) {
    case H_MANHATTAN: return C_ST * (dx + dy);
    case H_OCTILE:    return dx > dy ? C_ST * dx + (C_DG - C_ST) * dy : C_ST * dy + (C_DG - C_ST) * dx;
    default:          return 0;
    }
}

/* ======================= A* 작업 공간 + indexed min-PQ (DS-M11 에서 자세히) ======================= */
typedef enum { TB_ID, TB_HIGH_G, TB_LOW_G } tie_t;   /* f 동점일 때 누구를 먼저 꺼내나 */

typedef struct {
    int32_t g[MAXN], f[MAXN];   /* g = 시작에서 온 비용, f = g + h */
    int16_t parent[MAXN];       /* 직전 칸 번호, -1 = 없음 */
    int16_t pos[MAXN];          /* 힙 안 위치, -1 = 힙 밖 */
    int16_t heap[MAXN];
    uint8_t closed[MAXN];
    int hn;
    tie_t tb;
    int expanded;               /* 닫힌 칸 수 (꺼내서 이웃을 본 칸, 목표 포함) */
    int pushes, decreases, max_open;
    int closed_improve;         /* 이미 닫힌 칸의 g 를 더 줄이는 간선을 만난 횟수 (재개방은 안 함) */
} astar_ws_t;

/* TODO 3 (Part B): 힙 순서. a 를 b 보다 먼저 꺼내야 하면 true.
 * 1순위 f 작은 것. f 가 같으면 w->tb 에 따라  TB_HIGH_G: g 큰 것 먼저(= h 작은 것, 목표 쪽)
 * TB_LOW_G: g 작은 것 먼저   TB_ID: g 는 안 봄.  마지막 동점은 칸 번호 작은 것 (출력이 결정적이도록). */
static bool node_before(const astar_ws_t *w, int a, int b) {
    if (w->f[a] != w->f[b]) return w->f[a] < w->f[b];
    if (w->tb == TB_HIGH_G && w->g[a] != w->g[b]) return w->g[a] > w->g[b];
    if (w->tb == TB_LOW_G && w->g[a] != w->g[b]) return w->g[a] < w->g[b];
    return a < b;
}

static void pq_swap(astar_ws_t *w, int i, int j) {
    int16_t t = w->heap[i]; w->heap[i] = w->heap[j]; w->heap[j] = t;
    w->pos[w->heap[i]] = (int16_t)i; w->pos[w->heap[j]] = (int16_t)j;
}
static void pq_up(astar_ws_t *w, int i) {
    while (i > 0 && node_before(w, w->heap[i], w->heap[(i - 1) / 2])) { pq_swap(w, i, (i - 1) / 2); i = (i - 1) / 2; }
}
static void pq_down(astar_ws_t *w, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < w->hn && node_before(w, w->heap[l], w->heap[m])) m = l;
        if (r < w->hn && node_before(w, w->heap[r], w->heap[m])) m = r;
        if (m == i) return;
        pq_swap(w, i, m); i = m;
    }
}
static void pq_push(astar_ws_t *w, int v) {
    w->heap[w->hn] = (int16_t)v; w->pos[v] = (int16_t)w->hn; w->hn++;
    pq_up(w, w->hn - 1);
    w->pushes++;
    if (w->hn > w->max_open) w->max_open = w->hn;
}
static int pq_pop(astar_ws_t *w) {
    int v = w->heap[0];
    w->hn--;
    if (w->hn > 0) { w->heap[0] = w->heap[w->hn]; w->pos[w->heap[0]] = 0; pq_down(w, 0); }
    w->pos[v] = -1;
    return v;
}

static void ws_reset(astar_ws_t *w, int n, tie_t tb) {
    for (int i = 0; i < n; i++) { w->g[i] = INF; w->f[i] = INF; w->parent[i] = -1; w->pos[i] = -1; w->closed[i] = 0; }
    w->hn = 0; w->tb = tb;
    w->expanded = w->pushes = w->decreases = w->max_open = w->closed_improve = 0;
}

/* TODO 4: A*. 반환 = 최단 비용, 도달 불가 / 시작·목표가 막힘·지도 밖 = -1.
 * 열린 집합 = 힙(decrease-key), 꺼낸 칸은 closed. 목표를 **꺼낼 때** 끝낸다 (넣을 때가 아님).
 * 닫힌 칸은 다시 열지 않는다: 그 g 를 줄이는 간선을 보면 closed_improve++ 만 하고 넘어간다.
 * w->parent 로 경로를 복원할 수 있어야 하고, 통계(expanded, decreases)를 채운다 (pushes/max_open 은 pq_push 가). */
static int astar(const grid_t *g, int sr, int sc, int gr, int gc, heur_t h, tie_t tb, astar_ws_t *w) {
    ws_reset(w, g->rows * g->cols, tb);
    if (!cell_free(g, sr, sc) || !cell_free(g, gr, gc)) return -1;
    int s = sr * g->cols + sc, t = gr * g->cols + gc;
    w->g[s] = 0; w->f[s] = heur(h, sr, sc, gr, gc);
    pq_push(w, s);
    while (w->hn > 0) {
        int u = pq_pop(w);
        w->closed[u] = 1; w->expanded++;
        if (u == t) return w->g[u];
        int ur = u / g->cols, uc = u % g->cols;
        for (int k = 0; k < 8; k++) {
            if (!move_ok(g, ur, uc, k)) continue;
            int vr = ur + DR[k], vc = uc + DC[k], v = vr * g->cols + vc;
            int32_t nd = w->g[u] + (k < 4 ? C_ST : C_DG);
            if (nd >= w->g[v]) continue;
            if (w->closed[v]) { w->closed_improve++; continue; }      /* 재개방 없음 */
            w->g[v] = nd; w->parent[v] = (int16_t)u; w->f[v] = nd + heur(h, vr, vc, gr, gc);
            if (w->pos[v] < 0) pq_push(w, v);
            else { pq_up(w, w->pos[v]); w->decreases++; }
        }
    }
    return -1;
}

/* 목표 칸에서 parent 를 따라 시작까지 → out[0..n-1] = 시작..목표 칸 번호. 반환 n, 경로 없음/공간 부족 = -1 */
static int path_extract(const astar_ws_t *w, int goal, int *out, int max) {
    int n = 0;
    for (int v = goal; v >= 0; v = w->parent[v]) if (++n > max) return -1;
    for (int v = goal, i = n - 1; v >= 0; v = w->parent[v]) out[i--] = v;
    return n;
}

/* ======================= Part C — 장애물 팽창 ======================= */
/* TODO 5: dst = src 를 로봇 반경 r 칸만큼 부풀린 지도. 어떤 장애물 칸 (a,b) 와의 중심 거리²
 * (x−a)² + (y−b)² ≤ r² 인 칸 (x,y) 는 1 (유클리드 원판). r ≤ 0 이면 그대로. r > MAX_INFL 이면 MAX_INFL 로.
 * 방법: |dy| = 0..r 마다 반폭 hw = ⌊√(r² − dy²)⌋ 를 정수로 한 번 구해 두고, 장애물마다 행 a+dy 의 구간
 * [b−hw, b+hw] 를 지도 안으로 잘라 memset. src 와 dst 는 다른 버퍼. */
static void inflate(const grid_t *src, grid_t *dst, int r) {
    *dst = *src;
    if (r <= 0) return;
    if (r > MAX_INFL) r = MAX_INFL;
    int hw[MAX_INFL + 1];
    for (int dy = 0; dy <= r; dy++) {
        int rem = r * r - dy * dy, x = 0;
        while ((x + 1) * (x + 1) <= rem) x++;
        hw[dy] = x;
    }
    for (int a = 0; a < src->rows; a++)
        for (int b = 0; b < src->cols; b++) {
            if (!src->cell[a * src->cols + b]) continue;
            for (int dy = -r; dy <= r; dy++) {
                int rr = a + dy;
                if (rr < 0 || rr >= src->rows) continue;
                int w0 = hw[dy < 0 ? -dy : dy];
                int c0 = b - w0 < 0 ? 0 : b - w0, c1 = b + w0 >= src->cols ? src->cols - 1 : b + w0;
                memset(&dst->cell[rr * src->cols + c0], 1, (size_t)(c1 - c0 + 1));
            }
        }
}

/* 지도 + (팽창 지도) + 경로 → ASCII. '#' 원래 장애물, '+' 팽창으로 막힌 칸, '*' 경로, 'S' 'G', '.' 빈칸 */
static void render(FILE *fp, const grid_t *raw, const grid_t *infl, const int *path, int n) {
    static char buf[MAXR][MAXC + 1];
    for (int r = 0; r < raw->rows; r++) {
        for (int c = 0; c < raw->cols; c++) {
            int i = r * raw->cols + c;
            buf[r][c] = raw->cell[i] ? '#' : (infl && infl->cell[i]) ? '+' : '.';
        }
        buf[r][raw->cols] = 0;
    }
    for (int i = 0; i < n; i++) buf[path[i] / raw->cols][path[i] % raw->cols] = i == 0 ? 'S' : i == n - 1 ? 'G' : '*';
    for (int r = 0; r < raw->rows; r++) fprintf(fp, "  %s\n", buf[r]);
}

#ifndef ALGO_A01_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 문자열 지도 → grid. '#' 장애물, 나머지 빈칸. 'S'/'G' 위치를 돌려준다 (없으면 -1) */
static void grid_parse(grid_t *g, const char *const *rows, int nr, int *s, int *t) {
    g->rows = nr; g->cols = (int)strlen(rows[0]);
    *s = *t = -1;
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < g->cols; c++) {
            char ch = rows[r][c];
            g->cell[r * g->cols + c] = ch == '#';
            if (ch == 'S') *s = r * g->cols + c;
            if (ch == 'G') *t = r * g->cols + c;
        }
}

/* ---- 기준 구현 (이동 규칙을 따로 다시 씀): 한 걸음이 합법이면 비용, 아니면 0 ---- */
static int ref_step(const grid_t *g, int r0, int c0, int r1, int c1) {
    int dr = r1 - r0, dc = c1 - c0;
    if (dr < -1 || dr > 1 || dc < -1 || dc > 1 || (dr == 0 && dc == 0)) return 0;
    if (!cell_free(g, r0, c0) || !cell_free(g, r1, c1)) return 0;
    if (dr != 0 && dc != 0) return cell_free(g, r0 + dr, c0) && cell_free(g, r0, c0 + dc) ? C_DG : 0;
    return C_ST;
}

/* "바뀌지 않을 때까지 완화" (벨만-포드식, 정방향·역방향 스윕 번갈아). 힙도 휴리스틱도 없다. */
static void ref_dist(const grid_t *g, int src, int32_t *dist) {
    int n = g->rows * g->cols;
    for (int i = 0; i < n; i++) dist[i] = INF;
    if (!g->cell[src]) dist[src] = 0;
    for (int changed = 1, pass = 0; changed; pass++) {
        changed = 0;
        for (int j = 0; j < n; j++) {
            int u = (pass & 1) ? n - 1 - j : j;
            if (dist[u] == INF) continue;
            int ur = u / g->cols, uc = u % g->cols;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int cost = ref_step(g, ur, uc, ur + dr, uc + dc);
                    if (!cost) continue;
                    int v = (ur + dr) * g->cols + uc + dc;
                    if (dist[u] + cost < dist[v]) { dist[v] = dist[u] + cost; changed = 1; }
                }
        }
    }
}

/* 경로 검증: 시작·목표가 맞고, 모든 걸음이 합법이고, 걸음 비용 합 == cost */
static bool path_ok(const grid_t *g, const int *p, int n, int s, int t, int cost) {
    if (n < 1 || p[0] != s || p[n - 1] != t) return false;
    int sum = 0;
    for (int i = 1; i < n; i++) {
        int c = ref_step(g, p[i - 1] / g->cols, p[i - 1] % g->cols, p[i] / g->cols, p[i] % g->cols);
        if (!c) return false;
        sum += c;
    }
    return sum == cost;
}

/* 무작위 지도: kind 0 = 흩뿌린 장애물 (밀도 0..40%), kind 1 = 벽 조각 (가로/세로 선분) */
static void grid_random(grid_t *g, uint32_t *seed, int rows, int cols, int kind) {
    g->rows = rows; g->cols = cols;
    memset(g->cell, 0, sizeof g->cell);
    if (kind == 0) {
        int dens = (int)(xs32(seed) % 41);
        for (int i = 0; i < rows * cols; i++) g->cell[i] = (int)(xs32(seed) % 100) < dens;
    } else {
        int walls = rows * cols / 25 + 1;
        for (int w = 0; w < walls; w++) {
            int r = (int)(xs32(seed) % (uint32_t)rows), c = (int)(xs32(seed) % (uint32_t)cols);
            int len = 2 + (int)(xs32(seed) % (uint32_t)(rows / 2 + 1)), vert = (int)(xs32(seed) & 1);
            for (int k = 0; k < len; k++) {
                int rr = r + (vert ? k : 0), cc = c + (vert ? 0 : k);
                if (rr < rows && cc < cols) g->cell[rr * cols + cc] = 1;
            }
        }
    }
}

/* 팽창 기준: 칸마다 반경 안 사각 창을 전부 훑어 장애물과의 거리² ≤ r² 인지 */
static void inflate_brute(const grid_t *src, grid_t *dst, int r) {
    *dst = *src;
    for (int x = 0; x < src->rows; x++)
        for (int y = 0; y < src->cols; y++) {
            int hit = 0;
            for (int a = x - r; a <= x + r && !hit; a++)
                for (int b = y - r; b <= y + r && !hit; b++)
                    if (a >= 0 && a < src->rows && b >= 0 && b < src->cols && src->cell[a * src->cols + b]
                        && (a - x) * (a - x) + (b - y) * (b - y) <= r * r) hit = 1;
            dst->cell[x * src->cols + y] = (uint8_t)hit;
        }
}

static int count_ones(const grid_t *g) {
    int n = 0;
    for (int i = 0; i < g->rows * g->cols; i++) n += g->cell[i];
    return n;
}

/* 맨해튼이 다익스트라보다 비싼 경로를 내는 지도 (Part B, 5. 풀이에 손 계산) */
static const char *const MAP_MANH[] = {
    ".....G",
    "..#...",
    "S.....",
};

/* Part C 창고 지도: 두께 1 벽(열 20)에 좁은 문(3칸, 행 4..6)과 넓은 문(7칸, 행 10..16) */
static const char *const MAP_WAREHOUSE[] = {
    "#########################################",
    "#...................#...................#",
    "#...................#...................#",
    "#...................#...................#",
    "#.......................................#",
    "#....S.............................G....#",
    "#.......................................#",
    "#...................#...................#",
    "#...................#...................#",
    "#........####.......#...................#",
    "#........####...........................#",
    "#.......................................#",
    "#.......................................#",
    "#...........................####........#",
    "#...........................####........#",
    "#.......................................#",
    "#.......................................#",
    "#...................#...................#",
    "#########################################",
};

static grid_t G1, G2;
static astar_ws_t W1, W2;
static int32_t D1[MAXN], D2[MAXN];
static int P1[MAXN];

int main(void) {
    /* ---------- Part A: 손 계산 사례 ---------- */
    {
        grid_t *g = &G1;
        g->rows = g->cols = 1; g->cell[0] = 0;
        CHECK(astar(g, 0, 0, 0, 0, H_OCTILE, TB_HIGH_G, &W1) == 0 && W1.expanded == 1);
        g->rows = g->cols = 10; memset(g->cell, 0, sizeof g->cell);
        CHECK(astar(g, 0, 0, 9, 9, H_OCTILE, TB_HIGH_G, &W1) == 126);              /* 대각선 9번 */
        CHECK(astar(g, 0, 0, 9, 3, H_ZERO, TB_ID, &W1) == 102);                   /* 14·3 + 10·6 */
        CHECK(astar(g, 0, 0, 0, 10, H_OCTILE, TB_ID, &W1) == -1);                 /* 목표가 지도 밖 */
        g->cell[5 * 10 + 5] = 1;
        CHECK(astar(g, 0, 0, 5, 5, H_OCTILE, TB_ID, &W1) == -1 && astar(g, 5, 5, 0, 0, H_OCTILE, TB_ID, &W1) == -1);
        /* 모서리 자르기: 2×2, (0,1) 만 막힘 → 대각선 금지, 20. 둘 다 막힘 → 비집고 지나가기 금지, -1 */
        g->rows = g->cols = 2; memset(g->cell, 0, 4); g->cell[1] = 1;
        CHECK(astar(g, 0, 0, 1, 1, H_OCTILE, TB_ID, &W1) == 20 && astar(g, 1, 1, 0, 0, H_ZERO, TB_ID, &W1) == 20);
        g->cell[2] = 1;
        CHECK(astar(g, 0, 0, 1, 1, H_OCTILE, TB_ID, &W1) == -1);
        /* move_ok 직접: 3×3 가운데에서, (0,1) 만 막힘 → N, NW, NE 금지, 나머지 5개 허용 */
        g->rows = g->cols = 3; memset(g->cell, 0, 9); g->cell[1] = 1;
        int okmask = 0;
        for (int k = 0; k < 8; k++) okmask |= move_ok(g, 1, 1, k) << k;
        CHECK(okmask == 0xCE);                                                   /* S W E SW SE = 비트 1,2,3,6,7 */
        CHECK(heur(H_OCTILE, 0, 0, 3, 7) == 82 && heur(H_MANHATTAN, 0, 0, 3, 7) == 100 && heur(H_ZERO, 0, 0, 3, 7) == 0);
    }
    /* 빈 8×8: 모든 (시작, 목표) 쌍에서 A* 비용 == 옥타일 h == 기준 구현  (옥타일 = 장애물 없는 지도의 정확한 거리) */
    {
        grid_t *g = &G1;
        g->rows = g->cols = 8; memset(g->cell, 0, sizeof g->cell);
        int bad = 0;
        for (int s = 0; s < 64; s++) {
            ref_dist(g, s, D1);
            for (int t = 0; t < 64; t++) {
                int c = astar(g, s / 8, s % 8, t / 8, t % 8, H_OCTILE, TB_HIGH_G, &W1);
                if (c != D1[t] || c != heur(H_OCTILE, s / 8, s % 8, t / 8, t % 8)) bad++;
            }
        }
        CHECK(bad == 0);
    }
    /* ---------- Part A + B: 무작위 지도 대조 ---------- */
    {
        uint32_t seed = 0xA01u;
        int maps = 0, unreach = 0, cost_bad = 0, path_bad = 0, subset_bad = 0, reopen_cons = 0, adm_bad = 0, cons_bad = 0;
        int man_adm_viol = 0, man_cons_viol = 0, man_sub = 0, man_bound_bad = 0, man_reopen_maps = 0, man_cost_bad = 0;
        long exp_d = 0, exp_hi = 0, exp_lo = 0, exp_id = 0, exp_m = 0;
        double worst_ratio = 1.0;
        const tie_t tbs[3] = {TB_HIGH_G, TB_LOW_G, TB_ID};
        for (int it = 0; it < 300; it++) {
            grid_t *g = &G1;
            int rows = 5 + (int)(xs32(&seed) % 36), cols = 5 + (int)(xs32(&seed) % 36);
            grid_random(g, &seed, rows, cols, it & 1);
            int s = (int)(xs32(&seed) % (uint32_t)(rows * cols)), t = (int)(xs32(&seed) % (uint32_t)(rows * cols));
            g->cell[s] = g->cell[t] = 0;
            int sr = s / cols, sc = s % cols, tr = t / cols, tc = t % cols;
            maps++;
            ref_dist(g, s, D1);                       /* 시작에서의 진짜 거리 */
            ref_dist(g, t, D2);                       /* 목표까지의 진짜 거리 h* (이동 규칙이 대칭이라 역방향 = 정방향) */
            int want = D1[t] == INF ? -1 : D1[t];
            if (want < 0) unreach++;
            int cd = astar(g, sr, sc, tr, tc, H_ZERO, TB_ID, &W2);
            if (cd != want) cost_bad++;
            if (W2.closed_improve) reopen_cons++;
            exp_d += W2.expanded;
            for (int m = 0; m < 3; m++) {
                int c = astar(g, sr, sc, tr, tc, H_OCTILE, tbs[m], &W1);
                if (c != want) cost_bad++;
                if (W1.closed_improve) reopen_cons++;
                if (c >= 0) {
                    int n = path_extract(&W1, t, P1, MAXN);
                    if (!path_ok(g, P1, n, s, t, c)) path_bad++;
                }
                /* A*(옥타일)이 닫은 칸 ⊆ 다익스트라가 닫은 칸 (5. 풀이의 증명) */
                for (int v = 0; v < rows * cols; v++) if (W1.closed[v] && !W2.closed[v]) subset_bad++;
                *(m == 0 ? &exp_hi : m == 1 ? &exp_lo : &exp_id) += W1.expanded;
            }
            /* 허용성 h ≤ h*, 일관성 h(u) ≤ c(u,v) + h(v): 옥타일은 0번, 맨해튼은 위반이 나와야 한다 */
            for (int u = 0; u < rows * cols; u++) {
                if (D2[u] == INF) continue;
                int ur = u / cols, uc = u % cols;
                if (heur(H_OCTILE, ur, uc, tr, tc) > D2[u]) adm_bad++;
                if (heur(H_MANHATTAN, ur, uc, tr, tc) > D2[u]) man_adm_viol++;
                for (int k = 0; k < 8; k++) {
                    if (!move_ok(g, ur, uc, k)) continue;
                    int cost = k < 4 ? C_ST : C_DG;
                    if (heur(H_OCTILE, ur, uc, tr, tc) > cost + heur(H_OCTILE, ur + DR[k], uc + DC[k], tr, tc)) cons_bad++;
                    if (heur(H_MANHATTAN, ur, uc, tr, tc) > cost + heur(H_MANHATTAN, ur + DR[k], uc + DC[k], tr, tc)) man_cons_viol++;
                }
            }
            /* 맨해튼 A*: 합법 경로이긴 하지만 최적이 아닐 수 있다. 상한 7·cost ≤ 10·opt (= w 10/7 인 가중 A*) */
            int cm = astar(g, sr, sc, tr, tc, H_MANHATTAN, TB_HIGH_G, &W1);
            exp_m += W1.expanded;
            if ((cm < 0) != (want < 0)) man_cost_bad++;
            if (cm >= 0) {
                int n = path_extract(&W1, t, P1, MAXN);
                if (!path_ok(g, P1, n, s, t, cm) || cm < want) man_cost_bad++;
                if (cm > want) { man_sub++; if ((double)cm / want > worst_ratio) worst_ratio = (double)cm / want; }
                if (7 * cm > 10 * want) man_bound_bad++;
            }
            if (W1.closed_improve) man_reopen_maps++;
        }
        printf("Part A random: %d maps (%d unreachable), cost mismatches=%d, bad paths=%d, closed(A*) not in closed(Dijkstra)=%d,"
               " closed-improve with consistent h=%d\n", maps, unreach, cost_bad, path_bad, subset_bad, reopen_cons);
        printf("  octile: admissibility violations=%d, consistency violations=%d | manhattan: h > h* at %d cells, inconsistent edges=%d\n",
               adm_bad, cons_bad, man_adm_viol, man_cons_viol);
        printf("  expanded total: dijkstra=%ld  octile/high-g=%ld  octile/low-g=%ld  octile/id=%ld  manhattan/high-g=%ld\n",
               exp_d, exp_hi, exp_lo, exp_id, exp_m);
        printf("  manhattan: suboptimal on %d maps, worst cost ratio %.4f (bound 10/7 = 1.4286), maps with closed-improve=%d\n",
               man_sub, worst_ratio, man_reopen_maps);
        CHECK(cost_bad == 0 && unreach > 0);
        CHECK(path_bad == 0);
        CHECK(subset_bad == 0 && exp_hi <= exp_d);
        CHECK(reopen_cons == 0);
        CHECK(adm_bad == 0 && cons_bad == 0);
        CHECK(man_adm_viol > 0 && man_cons_viol > 0);
        CHECK(man_cost_bad == 0 && man_bound_bad == 0);
        CHECK(man_sub > 0);
    }
    /* ---------- Part B: 맨해튼 반례 (손 지도) ---------- */
    {
        int s, t;
        grid_parse(&G1, MAP_MANH, 3, &s, &t);
        int cols = G1.cols;
        int cd = astar(&G1, s / cols, s % cols, t / cols, t % cols, H_ZERO, TB_ID, &W2);
        int co = astar(&G1, s / cols, s % cols, t / cols, t % cols, H_OCTILE, TB_HIGH_G, &W1);
        int cm_lo = astar(&G1, s / cols, s % cols, t / cols, t % cols, H_MANHATTAN, TB_LOW_G, &W1);
        int cm_id = astar(&G1, s / cols, s % cols, t / cols, t % cols, H_MANHATTAN, TB_ID, &W1);
        int cm = astar(&G1, s / cols, s % cols, t / cols, t % cols, H_MANHATTAN, TB_HIGH_G, &W1);
        int n = path_extract(&W1, t, P1, MAXN);
        printf("Part B manhattan counterexample: dijkstra=%d octile=%d manhattan=%d/%d/%d (high-g/low-g/id), expanded %d\n",
               cd, co, cm, cm_lo, cm_id, W1.expanded);
        render(stdout, &G1, NULL, P1, cm >= 0 && n > 0 ? n : 0);
        CHECK(cd == 58 && co == 58 && cm == 64 && cm_lo == 64 && cm_id == 64 && path_ok(&G1, P1, n, s, t, cm));
    }
    /* ---------- Part B: 동점 처리 — 빈 64×64, (0,0) → (63,40) ---------- */
    {
        grid_t *g = &G1;
        g->rows = g->cols = 64; memset(g->cell, 0, sizeof g->cell);
        int ch = astar(g, 0, 0, 63, 40, H_OCTILE, TB_HIGH_G, &W1); int eh = W1.expanded, ph = W1.pushes;
        int cl = astar(g, 0, 0, 63, 40, H_OCTILE, TB_LOW_G, &W1);  int el = W1.expanded, pl = W1.pushes;
        int ci = astar(g, 0, 0, 63, 40, H_OCTILE, TB_ID, &W1);     int ei = W1.expanded, pi = W1.pushes;
        int cd = astar(g, 0, 0, 63, 40, H_ZERO, TB_ID, &W1);       int ed = W1.expanded, pd = W1.pushes;
        printf("Part B ties, empty 64x64 (0,0)->(63,40): cost %d/%d/%d/%d  expanded high-g=%d low-g=%d id=%d dijkstra=%d"
               "  (pushes %d/%d/%d/%d)\n", ch, cl, ci, cd, eh, el, ei, ed, ph, pl, pi, pd);
        CHECK(ch == 790 && cl == 790 && ci == 790 && cd == 790);              /* 10·63 + 4·40 */
        CHECK(eh == 64);                                                     /* 경로 위 칸만 */
        CHECK(el > 10 * eh);
    }
    /* ---------- Part C: 팽창 ---------- */
    {
        grid_t *g = &G1;
        g->rows = g->cols = 21; memset(g->cell, 0, sizeof g->cell);
        g->cell[10 * 21 + 10] = 1;
        int cnt[6];
        for (int r = 0; r <= 5; r++) { inflate(g, &G2, r); cnt[r] = count_ones(&G2); }
        CHECK(cnt[0] == 1 && cnt[1] == 5 && cnt[2] == 13 && cnt[3] == 29 && cnt[4] == 49 && cnt[5] == 81);  /* 원 안 격자점 수 */
        uint32_t seed = 0xC0FFEEu;
        int bad = 0, cases = 0;
        static grid_t B;
        for (int it = 0; it < 120; it++) {
            int rows = 1 + (int)(xs32(&seed) % 64), cols = 1 + (int)(xs32(&seed) % 64);
            grid_random(g, &seed, rows, cols, it & 1);
            if (it % 3 == 0) for (int i = 0; i < rows * cols; i++) if (g->cell[i] && (xs32(&seed) & 7)) g->cell[i] = 0;
            for (int r = 0; r <= 6; r++) {
                inflate(g, &G2, r); inflate_brute(g, &B, r); cases++;
                if (G2.rows != rows || G2.cols != cols || memcmp(G2.cell, B.cell, (size_t)(rows * cols))) bad++;
            }
        }
        printf("Part C inflate vs brute: %d (map, r) cases, mismatches=%d\n", cases, bad);
        CHECK(bad == 0);
        inflate(g, &G2, 1000);                                               /* 거대한 r → 상한으로 자름, 안 터짐 */
        CHECK(count_ones(&G2) == (count_ones(g) ? G2.rows * G2.cols : 0));
    }
    /* ---------- Part C: 창고 시나리오 — 반경별 경로 ---------- */
    {
        int s, t;
        grid_parse(&G1, MAP_WAREHOUSE, (int)(sizeof MAP_WAREHOUSE / sizeof *MAP_WAREHOUSE), &s, &t);
        int cols = G1.cols, cost[6], door[6], clear_bad = 0;
        for (int r = 0; r <= 5; r++) {
            inflate(&G1, &G2, r);
            cost[r] = astar(&G2, s / cols, s % cols, t / cols, t % cols, H_OCTILE, TB_HIGH_G, &W1);
            int n = cost[r] >= 0 ? path_extract(&W1, t, P1, MAXN) : 0;
            door[r] = -1;
            for (int i = 0; i < n; i++) {
                if (P1[i] % cols == 20) door[r] = P1[i] / cols;               /* 벽 열(20)을 지나는 행 */
                /* 여유 거리: 경로 칸에서 모든 원래 장애물까지 거리² > r² */
                for (int j = 0; j < G1.rows * cols; j++)
                    if (G1.cell[j]) {
                        int dr = j / cols - P1[i] / cols, dc = j % cols - P1[i] % cols;
                        if (dr * dr + dc * dc <= r * r) clear_bad++;
                    }
            }
            if (n > 0 && !path_ok(&G2, P1, n, s, t, cost[r])) clear_bad++;
            printf("Part C warehouse r=%d: cost=%d, crosses wall at row %d, expanded=%d, inflated cells=%d\n",
                   r, cost[r], door[r], W1.expanded, count_ones(&G2));
            if (r == 2) render(stdout, &G1, &G2, P1, n);
        }
        CHECK(door[0] == 5 && door[1] == 5 && cost[0] == 300 && cost[1] == 300);   /* r ≤ 1: 좁은 문으로 직진 */
        CHECK(door[2] >= 12 && door[2] <= 14 && door[3] == 13);               /* r = 2, 3: 넓은 문으로 돌아감 */
        CHECK(cost[1] < cost[2] && cost[2] <= cost[3] && cost[4] == -1 && cost[5] == -1);
        CHECK(clear_bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A01_NO_MAIN */
