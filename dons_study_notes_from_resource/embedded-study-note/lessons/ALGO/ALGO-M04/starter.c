/* ALGO-M04 BFS/DFS: 점유 격자 지도 탐색
 *
 * 로봇 점유 격자(occupancy grid, 0 = 빈칸, 1 = 장애물)에서
 *   Part A  BFS 최단 경로(4-이웃 이동) + parent 배열로 경로 복원
 *   Part B  장애물 덩어리(blob) 개수: 명시적 스택 DFS 와 BFS, 4-연결 / 8-연결
 *   Part C  펌웨어판: 1비트 지도(512 B) + 방문 비트맵 + 2비트 방향 parent 로 도달 면적과 경로
 *
 * 칸 번호: id = r * 64 + c (행 폭을 MAX_C = 64 로 고정 → r = id >> 6, c = id & 63, 나눗셈 없음. ALGO-B11 Q6)
 * 테스트: 손으로 만든 지도 10개(기댓값 표) + LeetCode 200 예제 + 64×64 이름 붙은 지도(뱀 복도, 9개 방 집)
 *         + 1×n 띠 전수(덩어리 k 개인 문자열 수 = C(n+1, 2k)) + 4×4 모든 격자 65536개 전수
 *         + 무작위 격자(xorshift32)를 "바뀌지 않을 때까지 완화"(Bellman 식) 기준 구현과 대조. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_R 64
#define MAX_C 64                       /* 2의 거듭제곱이어야 ROW/COL 이 시프트·마스크가 된다 */
#define MAX_CELLS (MAX_R * MAX_C)      /* 4096 → 칸 id 는 int16_t / uint16_t 에 들어간다 */
#define CELL(r, c) ((int)(r) * MAX_C + (int)(c))
#define ROW(id) ((id) >> 6)
#define COL(id) ((id) & (MAX_C - 1))

typedef struct { int rows, cols; uint8_t cell[MAX_CELLS]; } grid_t;   /* cell[CELL(r,c)]: 0 빈칸, 1 장애물 */

/* 이웃 순서 고정: 위, 오른쪽, 아래, 왼쪽, (8-연결이면) 왼위, 오위, 오아래, 왼아래. 순서가 고정이라 경로가 결정적이다. */
static const int8_t DR[8] = { -1, 0, 1, 0, -1, -1, 1, 1 };
static const int8_t DC[8] = { 0, 1, 0, -1, -1, 1, 1, -1 };
static const int16_t DID[4] = { -MAX_C, 1, MAX_C, -1 };   /* 4방향 id 변화량 (Part C 의 역추적에 쓴다) */

static int valid_id(int rows, int cols, int id) {
    return id >= 0 && id < MAX_CELLS && ROW(id) < rows && COL(id) < cols;
}
static int in_bounds(const grid_t *g, int r, int c) { return r >= 0 && r < g->rows && c >= 0 && c < g->cols; }

/* ======================= Part A: BFS 최단 경로 ======================= */
typedef struct {
    int16_t dist[MAX_CELLS];    /* 시작점에서 이동 수, 미도달 -1 */
    int16_t parent[MAX_CELLS];  /* BFS 트리의 부모 칸 id, 시작점·미도달 -1 */
    int16_t queue[MAX_CELLS];   /* 칸마다 최대 한 번 들어간다 (넣을 때 dist 로 표시) → 링이 필요 없다 */
} bfs_ws_t;                     /* 3 × 8 KB = 24 KB — Part C 에서 줄인다 */

/* start 에서 4-이웃 BFS. dist/parent 를 채우고 도달한 칸 수(start 포함)를 돌려준다.
 * start 가 범위 밖이거나 장애물이면 0 (dist 는 전부 -1). */
int grid_bfs(const grid_t *g, int start, bfs_ws_t *ws) {
    (void)g; (void)start; (void)in_bounds;
    for (int i = 0; i < MAX_CELLS; i++) { ws->dist[i] = -1; ws->parent[i] = -1; }
    return 0; /* TODO: dist[start] = 0, 큐에 넣고, 꺼낸 칸의 4-이웃 중 빈칸·미방문을 넣을 때 dist/parent 를 정한다 */
}

/* start → goal 최단 경로를 path[0..len) 에 (start 와 goal 포함) 쓰고 len = 거리 + 1 을 돌려준다.
 * 도달 불가·잘못된 start/goal 이면 -1, cap < len 이면 -2 (path 는 건드리지 않는다). */
int grid_path(const grid_t *g, int start, int goal, bfs_ws_t *ws, int16_t path[], int cap) {
    (void)g; (void)start; (void)goal; (void)ws; (void)path; (void)cap;
    return -1; /* TODO: grid_bfs 후 len = dist[goal] + 1, cap 확인, parent 를 따라 path[len-1] 부터 거꾸로 채운다 */
}

/* ======================= Part B: 장애물 덩어리 ======================= */
/* conn = 4 또는 8 (그 외 -1). label[id] = 1..k (행 우선 순서로 처음 만난 덩어리부터), 빈칸 0. 덩어리 수 k 를 돌려준다. */
int blobs_dfs(const grid_t *g, int conn, int16_t label[], int16_t stack[]) {
    (void)g; (void)conn; (void)stack;
    memset(label, 0, sizeof(int16_t) * MAX_CELLS);
    return 0; /* TODO: 행 우선으로 훑다가 라벨 없는 장애물을 만나면 count++, 명시적 스택으로 덩어리를 칠한다 (push 할 때 표시) */
}

int blobs_bfs(const grid_t *g, int conn, int16_t label[], int16_t queue[]) {
    (void)g; (void)conn; (void)queue;
    memset(label, 0, sizeof(int16_t) * MAX_CELLS);
    return 0; /* TODO: blobs_dfs 와 같고 스택 대신 큐 (head/tail) */
}

/* ======================= Part C: 펌웨어판 (비트 지도 + 2비트 방향) ======================= */
typedef struct { int rows, cols; uint8_t bits[MAX_CELLS / 8]; } occ_map_t;    /* 1 = 장애물, 512 B */
typedef struct {
    uint8_t  seen[MAX_CELLS / 8];   /* 방문 비트맵 512 B */
    uint8_t  dir[MAX_CELLS / 4];    /* 칸마다 2비트: 부모에서 이 칸으로 온 방향 k (0 위 1 오 2 아래 3 왼) 1 KB */
    uint16_t queue[MAX_CELLS];      /* 8 KB */
} plan_ws_t;

static int  bit_get(const uint8_t *b, int i) { return (b[i >> 3] >> (i & 7)) & 1; }
static void bit_set(uint8_t *b, int i) { b[i >> 3] = (uint8_t)(b[i >> 3] | (1u << (i & 7))); }
static int  dir_get(const uint8_t *d, int i) { return (d[i >> 2] >> ((i & 3) * 2)) & 3; }
static void dir_set(uint8_t *d, int i, int k) {
    unsigned sh = (unsigned)(i & 3) * 2;
    d[i >> 2] = (uint8_t)((d[i >> 2] & ~(3u << sh)) | ((unsigned)k << sh));
}

/* start 에서 4-이웃 BFS. seen/dir 를 채우고 발견한 칸 수(start 포함)를 돌려준다.
 * stop_at >= 0 이면 그 칸을 발견하는 순간 멈춘다(조기 종료). start 가 범위 밖/장애물이면 0. */
int occ_bfs(const occ_map_t *m, int start, int stop_at, plan_ws_t *ws) {
    (void)m; (void)start; (void)stop_at; (void)dir_set; (void)bit_get;
    memset(ws->seen, 0, sizeof ws->seen);
    return 0; /* TODO: grid_bfs 와 같은 모양. 방문은 seen 비트, 부모 대신 dir_set(ws->dir, v, k). v == stop_at 이면 즉시 return */
}

/* start 에서 닿는 빈칸 수 (start 포함). start 가 장애물/범위 밖이면 0. */
int occ_reach_area(const occ_map_t *m, int start, plan_ws_t *ws) {
    (void)m; (void)start; (void)ws;
    return 0; /* TODO: occ_bfs 한 줄 */
}

/* grid_path 와 같은 계약 (경로도 같은 칸열이어야 한다). parent 대신 2비트 방향을 거꾸로 따라간다. */
int occ_plan(const occ_map_t *m, int start, int goal, plan_ws_t *ws, int16_t path[], int cap) {
    (void)m; (void)start; (void)goal; (void)ws; (void)path; (void)cap; (void)dir_get; (void)DID;
    return -1; /* TODO: occ_bfs(goal 에서 조기 종료) 후 dir 를 거꾸로 따라가며 1차로 길이를 세고, 2차로 뒤에서부터 채운다 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 문자열 지도: '.' 빈칸, '#' 장애물, 'S' 시작(빈칸), 'G' 목표(빈칸). G 가 없으면 goal = start. */
static void load_map(grid_t *g, const char *const rows[], int nrows, int *start, int *goal) {
    memset(g, 0, sizeof *g);
    g->rows = nrows; g->cols = (int)strlen(rows[0]);
    *start = -1; *goal = -1;
    for (int r = 0; r < nrows; r++)
        for (int c = 0; c < g->cols; c++) {
            char ch = rows[r][c];
            g->cell[CELL(r, c)] = (uint8_t)(ch == '#');
            if (ch == 'S') *start = CELL(r, c);
            if (ch == 'G') *goal = CELL(r, c);
        }
    if (*goal < 0) *goal = *start;
}

static void pack_map(const grid_t *g, occ_map_t *m) {
    memset(m, 0, sizeof *m);
    m->rows = g->rows; m->cols = g->cols;
    for (int r = 0; r < g->rows; r++)
        for (int c = 0; c < g->cols; c++)
            if (g->cell[CELL(r, c)]) bit_set(m->bits, CELL(r, c));
}

/* 기준 구현 1: 거리 완화를 바뀌지 않을 때까지 반복 (Bellman 식). O(칸 × 지름) — 느리지만 의심할 곳이 없다. */
#define INF 0x7fffffff
static int ref_dist(const grid_t *g, int start, int32_t d[]) {
    for (int i = 0; i < MAX_CELLS; i++) d[i] = INF;
    if (!valid_id(g->rows, g->cols, start) || g->cell[start]) return 0;
    d[start] = 0;
    int changed = 1, reach = 0;
    while (changed) {
        changed = 0;
        for (int r = 0; r < g->rows; r++)
            for (int c = 0; c < g->cols; c++) {
                int v = CELL(r, c);
                if (g->cell[v]) continue;
                for (int k = 0; k < 4; k++) {
                    int nr = r + DR[k], nc = c + DC[k];
                    if (!in_bounds(g, nr, nc) || g->cell[CELL(nr, nc)] || d[CELL(nr, nc)] == INF) continue;
                    if (d[CELL(nr, nc)] + 1 < d[v]) { d[v] = d[CELL(nr, nc)] + 1; changed = 1; }
                }
            }
    }
    for (int i = 0; i < MAX_CELLS; i++) reach += (d[i] != INF);
    return reach;
}

/* 기준 구현 2: 라벨 전파 — 장애물 칸마다 자기 id 에서 시작해 이웃 최솟값으로 바뀌지 않을 때까지.
 * 끝나면 덩어리 라벨 = 덩어리 안 최소 id = 행 우선 첫 칸 → 1..k 번호로 바꿔 expected[] 에 쓴다. */
static int ref_blobs(const grid_t *g, int conn, int16_t expected[]) {
    static int32_t lab[MAX_CELLS];
    static int16_t idmap[MAX_CELLS];
    for (int i = 0; i < MAX_CELLS; i++) lab[i] = (valid_id(g->rows, g->cols, i) && g->cell[i]) ? i : -1;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int r = 0; r < g->rows; r++)
            for (int c = 0; c < g->cols; c++) {
                int v = CELL(r, c);
                if (lab[v] < 0) continue;
                for (int k = 0; k < conn; k++) {
                    int nr = r + DR[k], nc = c + DC[k];
                    if (!in_bounds(g, nr, nc) || lab[CELL(nr, nc)] < 0) continue;
                    if (lab[CELL(nr, nc)] < lab[v]) { lab[v] = lab[CELL(nr, nc)]; changed = 1; }
                }
            }
    }
    int k = 0;
    for (int i = 0; i < MAX_CELLS; i++) {
        expected[i] = 0;
        if (lab[i] < 0) continue;
        if (lab[i] == i) idmap[i] = (int16_t)++k;
        expected[i] = idmap[lab[i]];             /* lab[i] <= i 이므로 이미 번호가 있다 */
    }
    return k;
}

/* 경로 검사: 시작/끝, 모든 칸이 범위 안 빈칸, 연속 칸이 4-이웃 */
static int path_ok(const grid_t *g, const int16_t *p, int len, int start, int goal) {
    if (len < 1 || p[0] != start || p[len - 1] != goal) return 0;
    for (int i = 0; i < len; i++) if (!valid_id(g->rows, g->cols, p[i]) || g->cell[p[i]]) return 0;
    for (int i = 1; i < len; i++) {
        int dr = ROW(p[i]) - ROW(p[i - 1]), dc = COL(p[i]) - COL(p[i - 1]);
        if (dr * dr + dc * dc != 1) return 0;
    }
    return 1;
}

static grid_t g_grid;
static occ_map_t g_occ;
static bfs_ws_t g_ws;
static plan_ws_t g_pws;
static int16_t g_path[MAX_CELLS], g_path2[MAX_CELLS], g_lab[MAX_CELLS], g_lab2[MAX_CELLS], g_exp[MAX_CELLS], g_aux[MAX_CELLS];
static int32_t g_ref[MAX_CELLS];

/* 한 격자에 대해 모든 함수를 기준 구현과 대조. 틀린 항목 수를 돌려준다. */
static int check_grid(const grid_t *g, int start, int goal) {
    int bad = 0;
    int reach = ref_dist(g, start, g_ref);
    int got = grid_bfs(g, start, &g_ws);
    if (got != reach) bad++;
    for (int i = 0; i < MAX_CELLS; i++) {
        int want = (g_ref[i] == INF) ? -1 : g_ref[i];
        if (g_ws.dist[i] != want) { bad++; break; }
    }
    int want_len = (valid_id(g->rows, g->cols, goal) && g_ref[goal] != INF) ? g_ref[goal] + 1 : -1;
    int len = grid_path(g, start, goal, &g_ws, g_path, MAX_CELLS);
    if (len != want_len || (len > 0 && !path_ok(g, g_path, len, start, goal))) bad++;
    pack_map(g, &g_occ);
    if (occ_reach_area(&g_occ, start, &g_pws) != reach) bad++;
    int len2 = occ_plan(&g_occ, start, goal, &g_pws, g_path2, MAX_CELLS);
    if (len2 != len || (len > 0 && memcmp(g_path, g_path2, sizeof(int16_t) * (size_t)len) != 0)) bad++;
    for (int conn = 4; conn <= 8; conn += 4) {
        int k = ref_blobs(g, conn, g_exp);
        if (blobs_dfs(g, conn, g_lab, g_aux) != k || memcmp(g_lab, g_exp, sizeof g_lab) != 0) bad++;
        if (blobs_bfs(g, conn, g_lab2, g_aux) != k || memcmp(g_lab2, g_exp, sizeof g_lab2) != 0) bad++;
    }
    return bad;
}

/* ---- 손으로 만든 지도 10개: 기댓값은 손으로 센 값 (풀이 5장 표에 같은 숫자) ---- */
typedef struct {
    const char *name;
    int nrows;
    const char *map[6];
    int dist;     /* S→G 최소 이동 수, 도달 불가 -1 (G 없으면 G = S) */
    int reach;    /* S 에서 4-이웃으로 닿는 빈칸 수 (S 포함) */
    int blobs4, blobs8;
} hand_map_t;

static const hand_map_t HAND[10] = {
    { "M1 one cell",       1, { "S" },                                              0,  1, 0, 0 },
    { "M2 corridor",       1, { "S...G" },                                          4,  5, 0, 0 },
    { "M3 wall with gap",  4, { "S.#..", "..#..", ".....", "..#.G" },               7, 17, 2, 2 },
    { "M4 goal enclosed",  4, { "S...", ".###", ".#G#", ".###" },                  -1,  7, 1, 1 },
    { "M5 diagonal seal",  4, { "#.#.", ".#.#", "#.#.", "S..G" },                   3,  6, 6, 1 },
    { "M6 serpentine",     5, { "S#....", ".#.##.", ".#..#.", ".##.#.", "....#G" }, 19, 20, 2, 2 },
    { "M7 room + pillar",  4, { "S.....", ".##...", ".##...", ".....G" },           8, 20, 1, 1 },
    { "M8 LC200 ex2",      4, { "##S..", "##...", "..#..", "G..##" },              -1,  8, 3, 1 },
    { "M9 LC200 ex1",      4, { "####.", "##G#.", "##...", "S...." },               4, 11, 1, 1 },
    { "M10 checkerboard",  4, { "S#.#", "#.#.", ".#.#", "#.#G" },                  -1,  1, 8, 1 },
};

int main(void) {
    int start, goal;

    /* ---- 손 지도 10개: 기댓값 표 + 기준 구현 대조 ---- */
    for (int i = 0; i < 10; i++) {
        const hand_map_t *h = &HAND[i];
        load_map(&g_grid, h->map, h->nrows, &start, &goal);
        grid_bfs(&g_grid, start, &g_ws);
        int len = grid_path(&g_grid, start, goal, &g_ws, g_path, MAX_CELLS);
        int dist = len < 0 ? -1 : len - 1;
        pack_map(&g_grid, &g_occ);
        int reach = occ_reach_area(&g_occ, start, &g_pws);
        int b4 = blobs_dfs(&g_grid, 4, g_lab, g_aux), b8 = blobs_dfs(&g_grid, 8, g_lab, g_aux);
        int b4q = blobs_bfs(&g_grid, 4, g_lab, g_aux), b8q = blobs_bfs(&g_grid, 8, g_lab, g_aux);
        int ok = dist == h->dist && reach == h->reach && b4 == h->blobs4 && b8 == h->blobs8 && b4q == b4 && b8q == b8;
        printf("%-18s dist=%3d reach=%3d blobs4=%d blobs8=%d  %s\n", h->name, dist, reach, b4, b8, ok ? "ok" : "MISMATCH");
        CHECK(ok && check_grid(&g_grid, start, goal) == 0);
    }
    {   /* M6 은 빈칸 전체가 한 줄 복도라 최단 경로가 유일하다 → 칸열까지 정확히 */
        static const int8_t want[20][2] = { {0,0},{1,0},{2,0},{3,0},{4,0},{4,1},{4,2},{4,3},{3,3},{2,3},
                                            {2,2},{1,2},{0,2},{0,3},{0,4},{0,5},{1,5},{2,5},{3,5},{4,5} };
        load_map(&g_grid, HAND[5].map, HAND[5].nrows, &start, &goal);
        int len = grid_path(&g_grid, start, goal, &g_ws, g_path, MAX_CELLS), same = (len == 20);
        for (int i = 0; same && i < 20; i++) same = (g_path[i] == CELL(want[i][0], want[i][1]));
        CHECK(same);
    }
    {   /* M3 에서 BFS 가 실제로 쓰는 parent 트리 경로 (이웃 순서 위·오·아래·왼 고정) */
        load_map(&g_grid, HAND[2].map, HAND[2].nrows, &start, &goal);
        int len = grid_path(&g_grid, start, goal, &g_ws, g_path, MAX_CELLS);
        printf("M3 path:");
        for (int i = 0; i < len; i++) printf(" (%d,%d)", ROW(g_path[i]), COL(g_path[i]));
        printf("\n");
        CHECK(len == 8 && path_ok(&g_grid, g_path, len, start, goal));
    }

    /* ---- 이름 붙은 경계 사례 ---- */
    {
        load_map(&g_grid, HAND[2].map, HAND[2].nrows, &start, &goal);
        pack_map(&g_grid, &g_occ);
        int wall = CELL(0, 2);                                       /* 장애물 위에서 시작 */
        CHECK(grid_bfs(&g_grid, wall, &g_ws) == 0 && g_ws.dist[start] == -1);
        CHECK(grid_path(&g_grid, wall, goal, &g_ws, g_path, 64) == -1 && occ_reach_area(&g_occ, wall, &g_pws) == 0);
        CHECK(grid_path(&g_grid, start, wall, &g_ws, g_path, 64) == -1 && occ_plan(&g_occ, start, wall, &g_pws, g_path, 64) == -1);
        int outside = CELL(0, 5);                                    /* cols = 5 → 열 5 는 지도 밖 (id 자체는 배열 안) */
        CHECK(grid_bfs(&g_grid, outside, &g_ws) == 0 && grid_path(&g_grid, start, outside, &g_ws, g_path, 64) == -1);
        CHECK(grid_path(&g_grid, start, -1, &g_ws, g_path, 64) == -1 && grid_bfs(&g_grid, MAX_CELLS, &g_ws) == 0);
        g_path[7] = 12345;
        CHECK(grid_path(&g_grid, start, goal, &g_ws, g_path, 7) == -2 && g_path[7] == 12345);   /* cap 부족: 안 쓴다 */
        CHECK(grid_path(&g_grid, start, goal, &g_ws, g_path, 8) == 8);                         /* 딱 맞는 cap */
        CHECK(occ_plan(&g_occ, start, goal, &g_pws, g_path, 7) == -2 && occ_plan(&g_occ, start, goal, &g_pws, g_path, 8) == 8);
        CHECK(blobs_dfs(&g_grid, 6, g_lab, g_aux) == -1 && blobs_bfs(&g_grid, 0, g_lab, g_aux) == -1);
        CHECK(grid_path(&g_grid, start, start, &g_ws, g_path, 1) == 1 && g_path[0] == start);  /* 시작 = 목표 */
    }
    {   /* LeetCode 200 예제를 '#' 로: 섬 개수 1 과 3 (4-연결) */
        static const char *const ex1[] = { "####.", "##.#.", "##...", "....." };
        static const char *const ex2[] = { "##...", "##...", "..#..", "...##" };
        load_map(&g_grid, ex1, 4, &start, &goal);
        CHECK(blobs_dfs(&g_grid, 4, g_lab, g_aux) == 1);
        load_map(&g_grid, ex2, 4, &start, &goal);
        CHECK(blobs_dfs(&g_grid, 4, g_lab, g_aux) == 3 && blobs_bfs(&g_grid, 4, g_lab, g_aux) == 3);
    }

    /* ---- 64×64 이름 붙은 지도 ---- */
    {   /* 빈 지도: 모서리→모서리 126, 도달 4096 / 꽉 찬 지도: 덩어리 1, 도달 0 / 체커보드: 4-연결 2048, 8-연결 1 */
        memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 64; g_grid.cols = 64;
        CHECK(grid_bfs(&g_grid, CELL(0, 0), &g_ws) == 4096 && g_ws.dist[CELL(63, 63)] == 126);
        CHECK(blobs_dfs(&g_grid, 8, g_lab, g_aux) == 0);
        for (int i = 0; i < MAX_CELLS; i++) g_grid.cell[i] = 1;
        CHECK(blobs_dfs(&g_grid, 4, g_lab, g_aux) == 1 && blobs_bfs(&g_grid, 8, g_lab, g_aux) == 1 && grid_bfs(&g_grid, 0, &g_ws) == 0);
        for (int i = 0; i < MAX_CELLS; i++) g_grid.cell[i] = (uint8_t)((ROW(i) + COL(i)) & 1);
        CHECK(blobs_dfs(&g_grid, 4, g_lab, g_aux) == 2048 && blobs_bfs(&g_grid, 4, g_lab, g_aux) == 2048);
        CHECK(blobs_dfs(&g_grid, 8, g_lab, g_aux) == 1 && blobs_bfs(&g_grid, 8, g_lab, g_aux) == 1);
    }
    {   /* 뱀 복도: 홀수 행이 벽, 벽마다 끝에 구멍 하나(오른쪽·왼쪽 번갈아), 63행은 막힌 벽.
         * 빈칸 = 32행 × 64 + 구멍 31 = 2079. (0,0) → (62,0) = 32 × 63 (가로) + 31 × 2 (내려가기) = 2078 */
        memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 64; g_grid.cols = 64;
        for (int r = 1; r < 64; r += 2) {
            for (int c = 0; c < 64; c++) g_grid.cell[CELL(r, c)] = 1;
            if (r < 63) g_grid.cell[CELL(r, ((r / 2) % 2 == 0) ? 63 : 0)] = 0;
        }
        int len = grid_path(&g_grid, CELL(0, 0), CELL(62, 0), &g_ws, g_path, MAX_CELLS);
        pack_map(&g_grid, &g_occ);
        CHECK(len == 2079 && occ_reach_area(&g_occ, CELL(0, 0), &g_pws) == 2079);
        CHECK(occ_plan(&g_occ, CELL(0, 0), CELL(62, 0), &g_pws, g_path2, MAX_CELLS) == 2079 &&
              memcmp(g_path, g_path2, sizeof(int16_t) * 2079) == 0);
        CHECK(blobs_dfs(&g_grid, 4, g_lab, g_aux) == 32);           /* 벽 32줄: 구멍이 있어도 각 줄은 한 덩어리 */
    }
    {   /* Part C 시나리오: 64×64 집, 벽 = 21·42 행과 열, 방 9개. 바깥 8개 방은 2칸 문으로 고리처럼 연결,
         * 가운데 방(20×20 = 400칸)은 문이 모두 닫힘. 빈칸 = 4096 - 252(벽) + 16(문 8개) = 3860,
         * 도크(1,1)에서 도달 = 3860 - 400 = 3460. 가운데 문(21행 31·32열)을 열면 3862 전부. */
        memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 64; g_grid.cols = 64;
        for (int i = 0; i < 64; i++) {
            g_grid.cell[CELL(21, i)] = g_grid.cell[CELL(42, i)] = 1;
            g_grid.cell[CELL(i, 21)] = g_grid.cell[CELL(i, 42)] = 1;
        }
        static const int door[2][2] = { { 9, 10 }, { 52, 53 } };   /* 바깥 구간의 문 위치 */
        for (int w = 0; w < 2; w++) {
            int line = w ? 42 : 21;
            for (int s = 0; s < 2; s++)
                for (int d = 0; d < 2; d++) {
                    g_grid.cell[CELL(line, door[s][d])] = 0;      /* 가로 벽의 문 */
                    g_grid.cell[CELL(door[s][d], line)] = 0;      /* 세로 벽의 문 */
                }
        }
        int dock = CELL(1, 1), freec = 0;
        for (int r = 0; r < 64; r++) for (int c = 0; c < 64; c++) freec += !g_grid.cell[CELL(r, c)];
        pack_map(&g_grid, &g_occ);
        int area = occ_reach_area(&g_occ, dock, &g_pws);
        int closed = occ_plan(&g_occ, dock, CELL(30, 30), &g_pws, g_path, MAX_CELLS);   /* 닫힌 방 안 목표 */
        int len = occ_plan(&g_occ, dock, CELL(62, 62), &g_pws, g_path, MAX_CELLS);
        ref_dist(&g_grid, dock, g_ref);
        int path_good = len == g_ref[CELL(62, 62)] + 1 && path_ok(&g_grid, g_path, len, dock, CELL(62, 62));
        g_grid.cell[CELL(21, 31)] = g_grid.cell[CELL(21, 32)] = 0;   /* 가운데 방 문 열기 */
        pack_map(&g_grid, &g_occ);
        int area2 = occ_reach_area(&g_occ, dock, &g_pws);
        printf("house 64x64: free=%d reach from dock=%d unreachable=%d, path dock->(62,62)=%d moves, door opened: reach=%d\n",
               freec, area, freec - area, len - 1, area2);
        CHECK(freec == 3860 && area == 3460);
        CHECK(closed == -1);
        CHECK(path_good);
        CHECK(area2 == 3862 && check_grid(&g_grid, dock, CELL(30, 30)) == 0);
    }

    /* ---- 1×n 띠 전수: 덩어리(1의 런) k 개인 길이 n 문자열 수 = C(n+1, 2k) ---- */
    {
        unsigned long bad = 0, grids = 0;
        for (int n = 1; n <= 16; n++) {
            unsigned long hist[10] = { 0 };
            for (uint32_t mask = 0; mask < (1u << n); mask++) {
                memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 1; g_grid.cols = n;
                for (int c = 0; c < n; c++) g_grid.cell[c] = (uint8_t)((mask >> c) & 1);
                int k4 = blobs_dfs(&g_grid, 4, g_lab, g_aux), k8 = blobs_bfs(&g_grid, 8, g_lab, g_aux);
                if (k4 != k8 || k4 < 0 || k4 > 9) { bad++; continue; }
                hist[k4]++;
                grids++;
            }
            for (int k = 0; k <= (n + 1) / 2; k++) {      /* C(n+1, 2k) */
                unsigned long cnk = 1;
                for (int j = 1; j <= 2 * k; j++) cnk = cnk * (unsigned long)(n + 1 - 2 * k + j) / (unsigned long)j;
                if (hist[k] != cnk) bad++;
            }
        }
        printf("1xn strips n=1..16: %lu grids, mismatches vs C(n+1,2k)=%lu\n", grids, bad);
        CHECK(bad == 0);
    }

    /* ---- 4×4 모든 격자 65536개 전수: 시작 (0,0), 목표 (3,3) ---- */
    {
        unsigned long bad = 0, reach_hist[17] = { 0 };
        for (uint32_t mask = 0; mask < 65536u; mask++) {
            memset(&g_grid, 0, sizeof g_grid); g_grid.rows = 4; g_grid.cols = 4;
            for (int i = 0; i < 16; i++) g_grid.cell[CELL(i / 4, i % 4)] = (uint8_t)((mask >> i) & 1);
            if (check_grid(&g_grid, CELL(0, 0), CELL(3, 3))) bad++;
            reach_hist[grid_bfs(&g_grid, CELL(0, 0), &g_ws)]++;
        }
        printf("4x4 exhaustive: 65536 grids, bad=%lu, start blocked=%lu, reach=16 (all free)=%lu\n",
               bad, reach_hist[0], reach_hist[16]);
        CHECK(bad == 0);
        CHECK(reach_hist[0] == 32768 && reach_hist[16] == 1);
    }

    /* ---- 무작위 격자: 크기 1..24 × 1..24 (밀도 0~60%) 3000개 + 64×64 (밀도 0~45%) 40개 ---- */
    {
        unsigned long bad = 0, reachable_pairs = 0;
        for (int t = 0; t < 3040; t++) {
            int big = t >= 3000;
            memset(&g_grid, 0, sizeof g_grid);
            g_grid.rows = big ? 64 : 1 + (int)(xorshift32() % 24);
            g_grid.cols = big ? 64 : 1 + (int)(xorshift32() % 24);
            uint32_t dens = big ? (uint32_t)(t - 3000) * 45 / 39 : xorshift32() % 61;   /* % */
            for (int r = 0; r < g_grid.rows; r++)
                for (int c = 0; c < g_grid.cols; c++) g_grid.cell[CELL(r, c)] = (uint8_t)(xorshift32() % 100 < dens);
            start = CELL(xorshift32() % (uint32_t)g_grid.rows, xorshift32() % (uint32_t)g_grid.cols);
            goal  = CELL(xorshift32() % (uint32_t)g_grid.rows, xorshift32() % (uint32_t)g_grid.cols);
            if (t % 5 == 0) g_grid.cell[start] = 0;                  /* 5개 중 1개는 시작을 빈칸으로 강제, 나머지는 장애물일 수도 있다 */
            if (check_grid(&g_grid, start, goal)) bad++;
            if (g_ref[goal] != INF) reachable_pairs++;
        }
        printf("random: 3040 grids (3000 up to 24x24, 40 at 64x64), goal reachable in %lu, bad=%lu\n", reachable_pairs, bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
