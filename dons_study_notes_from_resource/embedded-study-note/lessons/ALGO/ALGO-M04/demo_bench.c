/* ALGO-M04 demo_bench — 64×64 격자 BFS 네 가지 구현의 시간·메모리, 그리고 완화 기준 구현이 왜 느린가
 *   V1 바이트 격자 + 범위 검사 + int16 dist/parent/queue    (solution.c Part A, 24 KB 작업 공간)
 *   V2 테두리 센티널(66×66, 바깥 한 겹을 장애물로) → 범위 검사 없음 (dist/parent/queue int16)
 *   V3 1비트 지도 + 방문 비트 + 2비트 방향 + uint16 큐     (solution.c Part C, 9.5 KB 작업 공간)
 *   V4 비트보드 층 BFS: 행 = uint64_t, 층마다 시프트·OR 로 다음 프런티어 (큐 없음, 2 KB) — 거리·면적만, 경로 없음
 *   REF 거리 완화를 바뀌지 않을 때까지 (테스트의 기준 구현) — 한 번만 잼
 * 이 맥(Apple Silicon M-series), -O2, 워밍업 후 101 표본의 중앙값(표본 = 50회 반복 평균). 절대값보다 비율을 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 64
#define CELLS (N * N)
#define CELL(r, c) ((r) * N + (c))
static const int8_t DR[4] = { -1, 0, 1, 0 }, DC[4] = { 0, 1, 0, -1 };

static uint8_t grid[CELLS];                /* 0 빈칸, 1 장애물 */

/* ---------------- V1 ---------------- */
static int16_t v1_dist[CELLS], v1_parent[CELLS], v1_queue[CELLS];
static int v1_bfs(int start) {
    for (int i = 0; i < CELLS; i++) { v1_dist[i] = -1; v1_parent[i] = -1; }
    int head = 0, tail = 0;
    v1_dist[start] = 0; v1_queue[tail++] = (int16_t)start;
    while (head < tail) {
        int u = v1_queue[head++], r = u >> 6, c = u & 63;
        for (int k = 0; k < 4; k++) {
            int nr = r + DR[k], nc = c + DC[k];
            if (nr < 0 || nr >= N || nc < 0 || nc >= N) continue;
            int v = CELL(nr, nc);
            if (grid[v] || v1_dist[v] >= 0) continue;
            v1_dist[v] = (int16_t)(v1_dist[u] + 1); v1_parent[v] = (int16_t)u; v1_queue[tail++] = (int16_t)v;
        }
    }
    return tail;
}

/* ---------------- V2: 센티널 ---------------- */
#define P (N + 2)
static uint8_t pgrid[P * P];
static int16_t v2_dist[P * P], v2_parent[P * P], v2_queue[P * P];
static void v2_build(void) {
    memset(pgrid, 1, sizeof pgrid);                        /* 테두리 포함 전부 장애물로 시작 */
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) pgrid[(r + 1) * P + c + 1] = grid[CELL(r, c)];
}
static int v2_bfs(int start /* 패딩 좌표 id */) {
    static const int off[4] = { -P, 1, P, -1 };
    for (int i = 0; i < P * P; i++) { v2_dist[i] = -1; v2_parent[i] = -1; }
    int head = 0, tail = 0;
    v2_dist[start] = 0; v2_queue[tail++] = (int16_t)start;
    while (head < tail) {
        int u = v2_queue[head++];
        for (int k = 0; k < 4; k++) {
            int v = u + off[k];                                /* 범위 검사 없음: 테두리가 장애물 */
            if (pgrid[v] || v2_dist[v] >= 0) continue;
            v2_dist[v] = (int16_t)(v2_dist[u] + 1); v2_parent[v] = (int16_t)u; v2_queue[tail++] = (int16_t)v;
        }
    }
    return tail;
}

/* ---------------- V3: 비트 지도 + 2비트 방향 ---------------- */
static uint8_t occ[CELLS / 8], seen[CELLS / 8], dir2[CELLS / 4];
static uint16_t v3_queue[CELLS];
static int  bget(const uint8_t *b, int i) { return (b[i >> 3] >> (i & 7)) & 1; }
static void bset(uint8_t *b, int i) { b[i >> 3] = (uint8_t)(b[i >> 3] | (1u << (i & 7))); }
static void v3_build(void) { memset(occ, 0, sizeof occ); for (int i = 0; i < CELLS; i++) if (grid[i]) bset(occ, i); }
static int v3_bfs(int start) {
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    bset(seen, start); v3_queue[tail++] = (uint16_t)start;
    while (head < tail) {
        int u = v3_queue[head++], r = u >> 6, c = u & 63;
        for (int k = 0; k < 4; k++) {
            int nr = r + DR[k], nc = c + DC[k];
            if (nr < 0 || nr >= N || nc < 0 || nc >= N) continue;
            int v = CELL(nr, nc);
            if (bget(occ, v) || bget(seen, v)) continue;
            bset(seen, v);
            unsigned sh = (unsigned)(v & 3) * 2;
            dir2[v >> 2] = (uint8_t)((dir2[v >> 2] & ~(3u << sh)) | ((unsigned)k << sh));
            v3_queue[tail++] = (uint16_t)v;
        }
    }
    return tail;
}

/* ---------------- V4: 비트보드 층 BFS ---------------- */
static uint64_t freem[N], vis[N], fr[N], nx[N];
static void v4_build(void) {
    for (int r = 0; r < N; r++) { freem[r] = 0; for (int c = 0; c < N; c++) if (!grid[CELL(r, c)]) freem[r] |= 1ull << c; }
}
/* 반환: 도달 면적. *gdist = goal 까지 층 수(못 닿으면 -1), *layers = 층 개수 */
static int v4_bfs(int start, int goal, int *gdist, int *layers) {
    memset(vis, 0, sizeof vis); memset(fr, 0, sizeof fr);
    fr[start >> 6] = vis[start >> 6] = 1ull << (start & 63);
    int d = 0, L = 1;
    *gdist = (start == goal) ? 0 : -1;
    for (;;) {
        uint64_t any = 0;
        for (int r = 0; r < N; r++) {
            uint64_t m = (fr[r] << 1) | (fr[r] >> 1) | (r ? fr[r - 1] : 0) | (r + 1 < N ? fr[r + 1] : 0);
            nx[r] = m & freem[r] & ~vis[r];
            any |= nx[r];
        }
        if (!any) break;
        d++; L++;
        for (int r = 0; r < N; r++) { vis[r] |= nx[r]; fr[r] = nx[r]; }
        if (*gdist < 0 && ((vis[goal >> 6] >> (goal & 63)) & 1)) *gdist = d;
    }
    int area = 0;
    for (int r = 0; r < N; r++) area += __builtin_popcountll(vis[r]);
    *layers = L;
    return area;
}

/* ---------------- REF: 완화 반복 ---------------- */
static int32_t refd[CELLS];
static int ref_relax(int start, int *sweeps) {
    for (int i = 0; i < CELLS; i++) refd[i] = INT32_MAX;
    refd[start] = 0;
    int changed = 1; *sweeps = 0;
    while (changed) {
        changed = 0; (*sweeps)++;
        for (int v = 0; v < CELLS; v++) {
            if (grid[v]) continue;
            int r = v >> 6, c = v & 63;
            for (int k = 0; k < 4; k++) {
                int nr = r + DR[k], nc = c + DC[k];
                if (nr < 0 || nr >= N || nc < 0 || nc >= N) continue;
                int u = CELL(nr, nc);
                if (grid[u] || refd[u] == INT32_MAX) continue;
                if (refd[u] + 1 < refd[v]) { refd[v] = refd[u] + 1; changed = 1; }
            }
        }
    }
    int area = 0;
    for (int i = 0; i < CELLS; i++) area += refd[i] != INT32_MAX;
    return area;
}

/* ---------------- 지도 ---------------- */
static uint32_t rng = 0xC0FFEEu;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void map_open(void) { memset(grid, 0, sizeof grid); }
static void map_random30(void) {
    rng = 0xC0FFEEu;
    for (int i = 0; i < CELLS; i++) grid[i] = (uint8_t)(xs() % 100 < 30);
    grid[CELL(32, 32)] = grid[CELL(63, 63)] = 0;           /* 시작·목표 칸만 비운다 */
}
static void map_serpentine(void) {
    memset(grid, 0, sizeof grid);
    for (int r = 1; r < N; r += 2) { for (int c = 0; c < N; c++) grid[CELL(r, c)] = 1; if (r < 63) grid[CELL(r, ((r / 2) % 2 == 0) ? 63 : 0)] = 0; }
}
static void map_house(void) {
    memset(grid, 0, sizeof grid);
    for (int i = 0; i < N; i++) grid[CELL(21, i)] = grid[CELL(42, i)] = grid[CELL(i, 21)] = grid[CELL(i, 42)] = 1;
    static const int door[4] = { 9, 10, 52, 53 };
    for (int j = 0; j < 4; j++) { grid[CELL(21, door[j])] = grid[CELL(42, door[j])] = grid[CELL(door[j], 21)] = grid[CELL(door[j], 42)] = 0; }
}

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static volatile int sink;

#define SAMPLES 101
#define REPS 50
static double bench(int which, int start, int pstart, int goal) {
    double s[SAMPLES];
    int gd, L;
    for (int w = 0; w < 20; w++) sink += which == 1 ? v1_bfs(start) : which == 2 ? v2_bfs(pstart) : which == 3 ? v3_bfs(start) : v4_bfs(start, goal, &gd, &L);
    for (int i = 0; i < SAMPLES; i++) {
        double t0 = now_ns();
        for (int j = 0; j < REPS; j++)
            sink += which == 1 ? v1_bfs(start) : which == 2 ? v2_bfs(pstart) : which == 3 ? v3_bfs(start) : v4_bfs(start, goal, &gd, &L);
        s[i] = (now_ns() - t0) / REPS;
    }
    qsort(s, SAMPLES, sizeof s[0], cmpd);
    return s[SAMPLES / 2];
}

int main(void) {
    struct { const char *name; void (*build)(void); int start, goal; } maps[] = {
        { "open 64x64",      map_open,       CELL(0, 0), CELL(63, 63) },
        { "random 30%",      map_random30,   CELL(32, 32), CELL(63, 63) },
        { "serpentine",      map_serpentine, CELL(0, 0), CELL(62, 0) },
        { "house (9 rooms)", map_house,      CELL(1, 1), CELL(62, 62) },
    };
    printf("work memory: V1 %zu B, V2 %zu B, V3 %zu B (+ map %zu B), V4 %zu B\n",
           sizeof v1_dist + sizeof v1_parent + sizeof v1_queue,
           sizeof v2_dist + sizeof v2_parent + sizeof v2_queue,
           sizeof seen + sizeof dir2 + sizeof v3_queue, sizeof occ,
           sizeof freem + sizeof vis + sizeof fr + sizeof nx);
    printf("%-16s %5s %5s %6s | %8s %8s %8s %8s | %9s %6s\n",
           "map", "reach", "dist", "layers", "V1 us", "V2 us", "V3 us", "V4 us", "REF us", "sweeps");
    for (size_t m = 0; m < sizeof maps / sizeof maps[0]; m++) {
        maps[m].build(); v2_build(); v3_build(); v4_build();
        int s = maps[m].start, g = maps[m].goal, ps = ((s >> 6) + 1) * P + (s & 63) + 1, pg = ((g >> 6) + 1) * P + (g & 63) + 1;
        int a1 = v1_bfs(s), d1 = v1_dist[g];
        int a2 = v2_bfs(ps), d2 = v2_dist[pg];
        int a3 = v3_bfs(s);
        int gd, L, a4 = v4_bfs(s, g, &gd, &L);
        int sweeps; double t0 = now_ns(); int ar = ref_relax(s, &sweeps); double tref = now_ns() - t0;
        int dr = refd[g] == INT32_MAX ? -1 : refd[g];
        if (a1 != a2 || a1 != a3 || a1 != a4 || a1 != ar || d1 != d2 || d1 != gd || d1 != dr) {
            printf("MISMATCH on %s: %d %d %d %d %d / %d %d %d %d\n", maps[m].name, a1, a2, a3, a4, ar, d1, d2, gd, dr);
            return 1;
        }
        double t1 = bench(1, s, ps, g), t2 = bench(2, s, ps, g), t3 = bench(3, s, ps, g), t4 = bench(4, s, ps, g);
        printf("%-16s %5d %5d %6d | %8.2f %8.2f %8.2f %8.2f | %9.1f %6d\n",
               maps[m].name, a1, d1, L, t1 / 1e3, t2 / 1e3, t3 / 1e3, t4 / 1e3, tref / 1e3, sweeps);
    }
    printf("(all variants agree on reach and dist for every map)\n");
    return 0;
}
