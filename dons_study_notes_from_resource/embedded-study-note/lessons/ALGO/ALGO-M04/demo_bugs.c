/* ALGO-M04 demo_bugs — 격자 BFS 의 흔한 버그 3개를 무작위 격자에서 기준 구현(완화 반복)과 대조
 *   B1 칸 id = r*cols + c (행 폭 = cols) 로 두고 이웃을 id±1 로만 구함 → 행 끝에서 다음 행 첫 칸으로 "순간이동"
 *   B2 방문 표시를 꺼낼(pop) 때 함 → 거리는 맞지만 같은 칸이 큐에 여러 번 들어감 (큐 cap = 칸 수 를 넘는다)
 *   B3 큐 대신 스택(DFS)으로 "처음 발견 = 거리" → 최단이 아닌 거리
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXR 24
#define MAXC 24
#define MAXCELLS (MAXR * MAXC)
static int R, C;
static uint8_t g[MAXCELLS];                 /* 이 데모는 행 폭 = C 인 평평한 배열 */
static const int DR[4] = { -1, 0, 1, 0 }, DC[4] = { 0, 1, 0, -1 };

static uint32_t rng = 0xBADC0DEu;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int ref[MAXCELLS];
static void ref_relax(int s) {
    for (int i = 0; i < R * C; i++) ref[i] = -1;
    ref[s] = 0;
    for (int ch = 1; ch;) {
        ch = 0;
        for (int v = 0; v < R * C; v++) {
            if (g[v]) continue;
            for (int k = 0; k < 4; k++) {
                int nr = v / C + DR[k], nc = v % C + DC[k];
                if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
                int u = nr * C + nc;
                if (g[u] || ref[u] < 0) continue;
                if (ref[v] < 0 || ref[u] + 1 < ref[v]) { ref[v] = ref[u] + 1; ch = 1; }
            }
        }
    }
}

static int d[MAXCELLS], q[4 * MAXCELLS + 1];
static uint8_t seen[MAXCELLS];

/* B1: 행 경계 검사 없이 id±1, id±C (위·아래는 범위만 확인) */
static void bfs_b1(int s) {
    static const int off[4] = { 0, 1, 0, -1 };
    for (int i = 0; i < R * C; i++) d[i] = -1;
    int h = 0, t = 0; d[s] = 0; q[t++] = s;
    while (h < t) {
        int u = q[h++];
        for (int k = 0; k < 4; k++) {
            int v = (k == 0) ? u - C : (k == 2) ? u + C : u + off[k];
            if (v < 0 || v >= R * C || g[v] || d[v] >= 0) continue;   /* 버그: (u%C)==C-1 이면 u+1 은 다음 행 */
            d[v] = d[u] + 1; q[t++] = v;
        }
    }
}
/* B2: pop 할 때 표시 → 반환 = 총 push 수 */
static int bfs_b2(int s) {
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < R * C; i++) d[i] = -1;
    int h = 0, t = 0, pushes = 1; d[s] = 0; q[t++] = s;
    while (h < t) {
        int u = q[h++];
        if (seen[u]) continue;
        seen[u] = 1;
        for (int k = 0; k < 4; k++) {
            int nr = u / C + DR[k], nc = u % C + DC[k];
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
            int v = nr * C + nc;
            if (g[v] || seen[v]) continue;
            if (d[v] < 0 || d[u] + 1 < d[v]) d[v] = d[u] + 1;
            q[t++] = v; pushes++;
        }
    }
    return pushes;
}
/* B3: 스택 */
static void bfs_b3(int s) {
    for (int i = 0; i < R * C; i++) d[i] = -1;
    int sp = 0; d[s] = 0; q[sp++] = s;
    while (sp > 0) {
        int u = q[--sp];
        for (int k = 0; k < 4; k++) {
            int nr = u / C + DR[k], nc = u % C + DC[k];
            if (nr < 0 || nr >= R || nc < 0 || nc >= C) continue;
            int v = nr * C + nc;
            if (g[v] || d[v] >= 0) continue;
            d[v] = d[u] + 1; q[sp++] = v;
        }
    }
}

int main(void) {
    long grids = 0, wrong1 = 0, wrong2 = 0, wrong3 = 0, over2 = 0, maxratio_num = 0, maxratio_den = 1;
    for (int t = 0; t < 2000; t++) {
        R = 2 + (int)(xs() % (MAXR - 1)); C = 2 + (int)(xs() % (MAXC - 1));
        uint32_t dens = xs() % 41;
        for (int i = 0; i < R * C; i++) g[i] = (uint8_t)(xs() % 100 < dens);
        int s = (int)(xs() % (uint32_t)(R * C)); g[s] = 0;
        ref_relax(s);
        int ncell = 0; for (int i = 0; i < R * C; i++) ncell += !g[i];
        grids++;
        bfs_b1(s); if (memcmp(d, ref, sizeof(int) * (size_t)(R * C))) wrong1++;
        int p = bfs_b2(s); if (memcmp(d, ref, sizeof(int) * (size_t)(R * C))) wrong2++;
        if (p > ncell) over2++;
        if ((long)p * maxratio_den > maxratio_num * ncell) { maxratio_num = p; maxratio_den = ncell; }
        bfs_b3(s); if (memcmp(d, ref, sizeof(int) * (size_t)(R * C))) wrong3++;
    }
    printf("%ld random grids (2..24 x 2..24, density 0-40%%)\n", grids);
    printf("B1 id+-1 row wrap   : wrong dist arrays %ld\n", wrong1);
    printf("B2 mark on pop      : wrong dist arrays %ld, pushes > free cells in %ld grids, worst pushes/free = %ld/%ld = %.2f\n",
           wrong2, over2, maxratio_num, maxratio_den, (double)maxratio_num / (double)maxratio_den);
    printf("B3 stack instead    : wrong dist arrays %ld\n", wrong3);

    /* B1 을 눈으로: 2×3, 장애물 없음, (0,2) 에서 시작 → (1,0) 까지 진짜 거리 3, 버그판은 1 */
    R = 2; C = 3; memset(g, 0, sizeof g);
    ref_relax(2); bfs_b1(2);
    printf("B1 2x3 from (0,2): true d(1,0)=%d, buggy d(1,0)=%d\n", ref[3], d[3]);
    /* B3 을 눈으로: 빈 3×3, (0,0) 에서 시작한 거리 표 */
    R = 3; C = 3; memset(g, 0, sizeof g);
    ref_relax(0); bfs_b3(0);
    printf("B3 3x3 from (0,0): true | stack\n");
    for (int r = 0; r < 3; r++)
        printf("  %d %d %d | %d %d %d\n", ref[r * 3], ref[r * 3 + 1], ref[r * 3 + 2], d[r * 3], d[r * 3 + 1], d[r * 3 + 2]);
    return 0;
}
