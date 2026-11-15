/* ALGO-B11 추가 연습 한 번에 검증
 *  연습 1: 재귀를 "그대로" 옮기기 — (칸, 다음 방향) 프레임 스택. 방문 순서가 재귀와 똑같고 피크 = 재귀 깊이 - 1
 *  연습 2: Morris 중위 순회 — 보조 메모리 O(1), 끝나면 트리가 원래대로
 *  연습 3: 트리 최대 깊이 세 가지 (재귀 / BFS 레벨 큐 / DFS (노드,깊이) 스택) — 트리에선 큐가 더 크다
 *  연습 4: 작은 큐로도 정확한 flood fill — 넘치면 버리고 격자를 다시 훑는다 (재시드)
 *  연습 5: "pop 할 때만 검사" 버전은 용량 n 스택도 넘친다
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EOVF (-1)
typedef struct { uint8_t *cell; int w, h; } grid_t;
typedef struct { int left, right, key; } tnode_t;
static const int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
static bool in_grid(const grid_t *g, int r, int c) { return r >= 0 && r < g->h && c >= 0 && c < g->w; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static grid_t grid_new(int w, int h) { grid_t g = { calloc((size_t)w * (size_t)h, 1), w, h }; return g; }
static grid_t grid_clone(const grid_t *s) { grid_t g = grid_new(s->w, s->h); memcpy(g.cell, s->cell, (size_t)s->w * (size_t)s->h); return g; }
static bool grid_same(const grid_t *a, const grid_t *b) { return memcmp(a->cell, b->cell, (size_t)a->w * (size_t)a->h) == 0; }
static grid_t random_grid(int maxw, int *r, int *c) {
    int w = 1 + (int)(xs32() % (uint32_t)maxw), h = 1 + (int)(xs32() % (uint32_t)maxw), dens = (int)(xs32() % 4) * 15;
    grid_t g = grid_new(w, h);
    for (int i = 0; i < w * h; i++) g.cell[i] = (int)(xs32() % 100) < dens ? 1 : 0;
    *r = (int)(xs32() % (uint32_t)h); *c = (int)(xs32() % (uint32_t)w);
    return g;
}
static int oracle_fill(grid_t *g, int r, int c, uint8_t to) {        /* 반복 완화: 스택·큐·재귀 없음 */
    uint8_t from = g->cell[r * g->w + c];
    if (from == to) return 0;
    uint8_t *mk = calloc((size_t)g->w * (size_t)g->h, 1);
    mk[r * g->w + c] = 1;
    for (bool ch = true; ch; ) {
        ch = false;
        for (int i = 0; i < g->h; i++)
            for (int j = 0; j < g->w; j++) {
                if (mk[i * g->w + j] || g->cell[i * g->w + j] != from) continue;
                for (int k = 0; k < 4; k++)
                    if (in_grid(g, i + DR[k], j + DC[k]) && mk[(i + DR[k]) * g->w + j + DC[k]]) { mk[i * g->w + j] = 1; ch = true; break; }
            }
    }
    int n = 0;
    for (int i = 0; i < g->w * g->h; i++) if (mk[i]) { g->cell[i] = to; n++; }
    free(mk);
    return n;
}

/* ---------- 연습 1: 재귀판 (방문 순서 기록) vs 프레임 스택판 ---------- */
static int rec_depth, rec_max;
static int ff_rec_order(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *order, int n) {
    if (++rec_depth > rec_max) rec_max = rec_depth;
    if (from != to && in_grid(g, r, c) && g->cell[r * g->w + c] == from) {
        g->cell[r * g->w + c] = to;
        order[n++] = r * g->w + c;
        for (int k = 0; k < 4; k++) n = ff_rec_order(g, r + DR[k], c + DC[k], from, to, order, n);
    }
    rec_depth--;
    return n;
}
/* 프레임 = (칸, 다음에 볼 방향 0..4). 재귀 호출 = push, return = pop. 반환 = 칠한 수, 넘치면 EOVF */
static int ff_frames(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *order,
                     int32_t *cellstk, uint8_t *dirstk, int cap, int *peak) {
    int top = 0, n = 0;
    *peak = 0;
    if (from == to || !in_grid(g, r, c) || g->cell[r * g->w + c] != from) return 0;
    if (cap < 1) return EOVF;
    g->cell[r * g->w + c] = to;
    order[n++] = r * g->w + c;
    cellstk[0] = r * g->w + c; dirstk[0] = 0; top = 1; *peak = 1;
    while (top > 0) {
        int k = dirstk[top - 1];
        if (k == 4) { top--; continue; }             /* 네 방향 다 봤다 = return */
        dirstk[top - 1] = (uint8_t)(k + 1);           /* 돌아오면 다음 방향부터 */
        int p = cellstk[top - 1], nr = p / g->w + DR[k], nc = p % g->w + DC[k];
        if (!in_grid(g, nr, nc) || g->cell[nr * g->w + nc] != from) continue;
        if (top == cap) return EOVF;
        g->cell[nr * g->w + nc] = to;                 /* 재귀판의 "들어가자마자 칠하기" */
        order[n++] = nr * g->w + nc;
        cellstk[top] = nr * g->w + nc; dirstk[top] = 0; top++;
        if (top > *peak) *peak = top;
    }
    return n;
}

/* ---------- 트리 공통 ---------- */
static int bst_insert(tnode_t *t, int *cnt, int root, int key) {
    int v = (*cnt)++;
    t[v].left = t[v].right = -1; t[v].key = key;
    if (root == -1) return v;
    for (int u = root; ; ) {
        int *slot = key < t[u].key ? &t[u].left : &t[u].right;
        if (*slot == -1) { *slot = v; return root; }
        u = *slot;
    }
}
static int height_by_index(const tnode_t *t, int root, int nn) {     /* 부모 번호 < 자식 번호 인 트리 전용 */
    if (root == -1) return 0;
    int *dep = malloc(sizeof(int) * (size_t)nn), best = 0;
    dep[root] = 1;
    for (int v = 0; v < nn; v++) {
        if (dep[v] > best) best = dep[v];
        if (t[v].left != -1) dep[t[v].left] = dep[v] + 1;
        if (t[v].right != -1) dep[t[v].right] = dep[v] + 1;
    }
    free(dep);
    return best;
}
static int inorder_rec(const tnode_t *t, int v, int *out, int n) {
    if (v == -1) return n;
    n = inorder_rec(t, t[v].left, out, n);
    out[n++] = t[v].key;
    return inorder_rec(t, t[v].right, out, n);
}

/* ---------- 연습 2: Morris 중위 순회 ---------- */
static int inorder_morris(tnode_t *t, int root, int *out) {
    int n = 0, cur = root;
    while (cur != -1) {
        if (t[cur].left == -1) { out[n++] = t[cur].key; cur = t[cur].right; continue; }
        int pre = t[cur].left;                        /* 왼쪽 부분 트리의 가장 오른쪽 = 중위 선행자 */
        while (t[pre].right != -1 && t[pre].right != cur) pre = t[pre].right;
        if (t[pre].right == -1) { t[pre].right = cur; cur = t[cur].left; }      /* 임시 실 (thread) */
        else { t[pre].right = -1; out[n++] = t[cur].key; cur = t[cur].right; }  /* 실 제거 = 복구 */
    }
    return n;
}

/* ---------- 연습 3: 최대 깊이 세 가지 ---------- */
static int maxdepth_rec(const tnode_t *t, int v) {
    if (v == -1) return 0;
    int a = maxdepth_rec(t, t[v].left), b = maxdepth_rec(t, t[v].right);
    return 1 + (a > b ? a : b);
}
static int maxdepth_bfs(const tnode_t *t, int root, int32_t *q, int cap, int *peak) {  /* 레벨 단위 */
    int head = 0, count = 0, depth = 0;
    *peak = 0;
    if (root == -1) return 0;
    q[0] = root; count = 1; *peak = 1;
    while (count > 0) {
        depth++;
        for (int lvl = count; lvl > 0; lvl--) {
            int v = q[head]; head = (head + 1) % cap; count--;
            int ch[2] = { t[v].left, t[v].right };
            for (int k = 0; k < 2; k++) {
                if (ch[k] == -1) continue;
                if (count == cap) return EOVF;
                q[(head + count) % cap] = ch[k];
                if (++count > *peak) *peak = count;
            }
        }
    }
    return depth;
}
static int maxdepth_dfs(const tnode_t *t, int root, int32_t *nodes, int32_t *deps, int cap, int *peak) {
    int top = 0, best = 0;
    *peak = 0;
    if (root == -1) return 0;
    nodes[0] = root; deps[0] = 1; top = 1; *peak = 1;
    while (top > 0) {
        top--;
        int v = nodes[top], d = deps[top];
        if (d > best) best = d;
        int ch[2] = { t[v].right, t[v].left };
        for (int k = 0; k < 2; k++) {
            if (ch[k] == -1) continue;
            if (top == cap) return EOVF;
            nodes[top] = ch[k]; deps[top] = d + 1; top++;
            if (top > *peak) *peak = top;
        }
    }
    return best;
}

/* ---------- 연습 4: 작은 큐 + 재시드 스캔 ---------- */
#define TEMP 0xFFu      /* 진행 중 표시. 격자에 원래 없는 값이어야 한다 */
static int ff_bfs_bounded(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *q, int cap, int *rescans) {
    *rescans = 0;
    if (from == to || from == TEMP || to == TEMP || !in_grid(g, r, c) || g->cell[r * g->w + c] != from) return 0;
    if (cap < 1) return EOVF;
    int head = 0, count = 0;
    g->cell[r * g->w + c] = TEMP;
    q[0] = r * g->w + c; count = 1;
    bool dropped = false;
    for (;;) {
        while (count > 0) {
            int p = q[head]; head = (head + 1) % cap; count--;
            for (int k = 0; k < 4; k++) {
                int nr = p / g->w + DR[k], nc = p % g->w + DC[k];
                if (!in_grid(g, nr, nc) || g->cell[nr * g->w + nc] != from) continue;
                if (count == cap) { dropped = true; continue; }       /* 버린다 — 칠하지 않으니 나중에 다시 찾힌다 */
                g->cell[nr * g->w + nc] = TEMP;
                q[(head + count) % cap] = nr * g->w + nc; count++;
            }
        }
        if (!dropped) break;
        dropped = false;
        (*rescans)++;
        for (int i = 0; i < g->w * g->h && !dropped; i++) {          /* TEMP 옆의 from 칸 = 버려진 경계 */
            if (g->cell[i] != from) continue;
            int ir = i / g->w, ic = i % g->w;
            for (int k = 0; k < 4; k++) {
                int nr = ir + DR[k], nc = ic + DC[k];
                if (!in_grid(g, nr, nc) || g->cell[nr * g->w + nc] != TEMP) continue;
                if (count == cap) { dropped = true; break; }        /* 큐가 또 찼다 → 한 바퀴 더 */
                g->cell[i] = TEMP;
                q[(head + count) % cap] = i; count++;
                break;
            }
        }
    }
    int n = 0;
    for (int i = 0; i < g->w * g->h; i++) if (g->cell[i] == TEMP) { g->cell[i] = to; n++; }
    return n;
}

/* ---------- 연습 5: pop 할 때만 검사 (흔한 버그) ---------- */
static int ff_dfs_lazy(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak) {
    int top = 0, n = 0;
    *peak = 0;
    if (from == to || !in_grid(g, r, c) || cap < 1) return 0;
    buf[top++] = r * g->w + c; *peak = 1;
    while (top > 0) {
        int p = buf[--top];
        if (g->cell[p] != from) continue;
        g->cell[p] = to; n++;
        for (int k = 0; k < 4; k++) {
            int nr = p / g->w + DR[k], nc = p % g->w + DC[k];
            if (!in_grid(g, nr, nc)) continue;
            if (top == cap) return EOVF;
            buf[top++] = nr * g->w + nc;
            if (top > *peak) *peak = top;
        }
    }
    return n;
}

int main(void) {
    /* 연습 1 */
    {
        int bad_order = 0, bad_peak = 0, cells = 0;
        int32_t *o1 = malloc(sizeof(int32_t) * 1024), *o2 = malloc(sizeof(int32_t) * 1024), *cs = malloc(sizeof(int32_t) * 1024);
        uint8_t *ds = malloc(1024);
        for (int it = 0; it < 300; it++) {
            int r, c;
            grid_t g = random_grid(32, &r, &c), g2 = grid_clone(&g);
            uint8_t from = g.cell[r * g.w + c];
            rec_depth = rec_max = 0;
            int a = ff_rec_order(&g, r, c, from, 2, o1, 0), pk;
            int b = ff_frames(&g2, r, c, from, 2, o2, cs, ds, 1024, &pk);
            cells += a;
            bad_order += !(a == b && memcmp(o1, o2, sizeof(int32_t) * (size_t)a) == 0 && grid_same(&g, &g2));
            bad_peak += !(pk == rec_max - 1);
            free(g.cell); free(g2.cell);
        }
        printf("ex1 frames: 300 grids, %d cells, order mismatches=%d, peak!=depth-1: %d\n", cells, bad_order, bad_peak);
        CHECK(cells > 0 && bad_order == 0);
        CHECK(bad_peak == 0);
        grid_t g = grid_new(8, 8);
        int pk;
        CHECK(ff_frames(&g, 0, 0, 0, 2, o2, cs, ds, 10, &pk) == EOVF && pk == 10);
        free(g.cell); free(o1); free(o2); free(cs); free(ds);
    }
    /* 연습 2 · 3 */
    {
        int bad_m = 0, bad_restore = 0, bad_d = 0;
        tnode_t *t = malloc(sizeof(tnode_t) * 1100), *t0 = malloc(sizeof(tnode_t) * 1100);
        int *a = malloc(sizeof(int) * 1100), *b = malloc(sizeof(int) * 1100);
        int32_t *q = malloc(sizeof(int32_t) * 1100), *q2 = malloc(sizeof(int32_t) * 1100);
        for (int it = 0; it < 300; it++) {
            int nn = (int)(xs32() % 300), cnt = 0, root = -1;
            for (int i = 0; i < nn; i++) root = bst_insert(t, &cnt, root, it % 7 == 6 ? i : (int)(xs32() % 100000));
            memcpy(t0, t, sizeof(tnode_t) * (size_t)nn);
            int na = inorder_rec(t, root, a, 0), nb = inorder_morris(t, root, b);
            bad_m += !(na == nn && nb == nn && memcmp(a, b, sizeof(int) * (size_t)nn) == 0);
            bad_restore += memcmp(t, t0, sizeof(tnode_t) * (size_t)nn) != 0;
            int h = height_by_index(t, root, nn), p1, p2;
            bad_d += !(maxdepth_rec(t, root) == h && maxdepth_bfs(t, root, q, 1100, &p1) == h &&
                       maxdepth_dfs(t, root, q, q2, 1100, &p2) == h && p2 <= h + 1);
        }
        printf("ex2 morris: 300 BSTs, mismatches=%d, not restored=%d; ex3 depth mismatches=%d\n", bad_m, bad_restore, bad_d);
        CHECK(bad_m == 0);
        CHECK(bad_restore == 0);
        CHECK(bad_d == 0);
        enum { NP = 1023 };                            /* 완전 이진 트리 높이 10: 큐는 폭, 스택은 높이 */
        for (int i = 0; i < NP; i++) { t[i].key = i; t[i].left = 2 * i + 1 < NP ? 2 * i + 1 : -1; t[i].right = 2 * i + 2 < NP ? 2 * i + 2 : -1; }
        int p1, p2;
        int d1 = maxdepth_bfs(t, 0, q, 1100, &p1), d2 = maxdepth_dfs(t, 0, q, q2, 1100, &p2);
        printf("ex3 perfect tree 1023 nodes: depth %d, bfs queue peak=%d, dfs stack peak=%d\n", d1, p1, p2);
        CHECK(d1 == 10 && d2 == 10 && p1 == 512 && p2 == 10);
        CHECK(maxdepth_bfs(t, 0, q, 100, &p1) == EOVF);
        free(t); free(t0); free(a); free(b); free(q); free(q2);
    }
    /* 연습 4 */
    {
        int bad = 0, total_rescans = 0;
        int32_t q[8];
        for (int it = 0; it < 400; it++) {
            int r, c, rs;
            grid_t g = random_grid(20, &r, &c), go = grid_clone(&g);
            uint8_t from = g.cell[r * g.w + c];
            int cap = 1 + it % 8;
            int no = oracle_fill(&go, r, c, 2), nb = ff_bfs_bounded(&g, r, c, from, 2, q, cap, &rs);
            bad += !(nb == no && grid_same(&g, &go));
            total_rescans += rs;
            free(g.cell); free(go.cell);
        }
        grid_t f = grid_new(64, 64);
        int rs, n = ff_bfs_bounded(&f, 32, 32, 0, 2, q, 8, &rs);
        printf("ex4 bounded: 400 random grids (cap 1..8), mismatches=%d, rescans total=%d; open 64x64 cap 8: filled %d, rescans %d\n",
               bad, total_rescans, n, rs);
        CHECK(bad == 0 && total_rescans > 0);
        CHECK(n == 4096 && rs > 0);
        free(f.cell);
    }
    /* 연습 5 */
    {
        grid_t g = grid_new(64, 64);                   /* 뱀 복도: 칸 2080 */
        for (int r = 1; r < 64; r += 2) { for (int c = 0; c < 64; c++) g.cell[r * 64 + c] = 1; g.cell[r * 64 + ((r / 2) % 2 == 0 ? 63 : 0)] = 0; }
        int cells = 0;
        for (int i = 0; i < 4096; i++) cells += g.cell[i] == 0;
        int32_t *buf = malloc(sizeof(int32_t) * (4 * 4096 + 1));
        grid_t g2 = grid_clone(&g);
        int pk, pk2;
        int a = ff_dfs_lazy(&g, 0, 0, 0, 2, buf, cells, &pk);
        int b = ff_dfs_lazy(&g2, 0, 0, 0, 2, buf, 4 * cells + 1, &pk2);
        printf("ex5 lazy: serpentine 64x64 (%d cells): cap=n -> %s, cap=4n+1 -> filled %d, peak %d\n",
               cells, a == EOVF ? "OVERFLOW" : "ok", b, pk2);
        CHECK(cells == 2080 && a == EOVF && pk == cells);
        CHECK(b == cells && pk2 > cells && pk2 <= 4 * cells + 1);
        free(g.cell); free(g2.cell); free(buf);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
