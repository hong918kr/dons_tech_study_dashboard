/* ALGO-B11 재귀와 스택 깊이: 재귀를 반복으로
 *
 * Part A: 재귀 flood fill (4-이웃) + 재귀 트리 순회 (전위/중위/후위), 재귀 깊이 계측
 * Part B: 명시적 스택 (호출자가 준 고정 용량 배열) 반복 버전 — 넘치면 B11_EOVERFLOW (UB 아님)
 * Part C: 점유 격자에서 BFS 큐 (링 버퍼) flood fill — RAM 예산, 용량 경계, 카나리 검사
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 * 측정/크래시 데모는 demo_depth.c, demo_overflow.c (이 파일을 #include 해서 쓴다)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B11_EOVERFLOW (-1)                 /* 보조 스택/큐가 꽉 찼다 (격자는 일부만 칠해진 상태) */

typedef struct { uint8_t *cell; int w, h; } grid_t;     /* 행 우선: cell[r * w + c] */
typedef struct { int left, right, key; } tnode_t;       /* 자식 없음 = -1, 노드는 배열 풀 (malloc 없음) */
enum { ORD_PRE, ORD_IN, ORD_POST };

static const int DR[4] = { -1, 1, 0, 0 };   /* 이웃 순서 고정: 위, 아래, 왼쪽, 오른쪽 */
static const int DC[4] = { 0, 0, -1, 1 };

/* 재귀 깊이 계측 (주어짐). depth = 지금 스택에 쌓인 프레임 수, 로컬 주소로 바이트도 잰다 */
typedef struct { int depth, max_depth; uintptr_t sp_first, sp_min; } depth_t;
static void depth_reset(depth_t *d) { memset(d, 0, sizeof *d); }
static inline void depth_enter(depth_t *d, const void *probe) {
    uintptr_t a = (uintptr_t)probe;
    if (d->sp_first == 0) d->sp_first = d->sp_min = a;
    if (a < d->sp_min) d->sp_min = a;                 /* 스택은 아래(낮은 주소)로 자란다 */
    if (++d->depth > d->max_depth) d->max_depth = d->depth;
}
static inline void depth_leave(depth_t *d) { d->depth--; }

/* 고정 용량 스택 / 링 버퍼 큐 (주어짐). 꽉 차면 false — 절대 buf[cap] 을 쓰지 않는다 */
typedef struct { int32_t *buf; int cap, top, peak; } istack_t;
static void is_init(istack_t *s, int32_t *buf, int cap) { s->buf = buf; s->cap = cap; s->top = s->peak = 0; }
static bool is_push(istack_t *s, int32_t x) {
    if (s->top >= s->cap) return false;
    s->buf[s->top++] = x;
    if (s->top > s->peak) s->peak = s->top;
    return true;
}
static int32_t is_pop(istack_t *s) { return s->buf[--s->top]; }
static int32_t is_peek(const istack_t *s) { return s->buf[s->top - 1]; }

typedef struct { int32_t *buf; int cap, head, count, peak; } iqueue_t;
static void iq_init(iqueue_t *q, int32_t *buf, int cap) { q->buf = buf; q->cap = cap; q->head = q->count = q->peak = 0; }
static bool iq_push(iqueue_t *q, int32_t x) {
    if (q->count >= q->cap) return false;
    int tail = q->head + q->count;
    if (tail >= q->cap) tail -= q->cap;
    q->buf[tail] = x;
    if (++q->count > q->peak) q->peak = q->count;
    return true;
}
static int32_t iq_pop(iqueue_t *q) {
    int32_t x = q->buf[q->head];
    if (++q->head == q->cap) q->head = 0;
    q->count--;
    return x;
}

static bool in_grid(const grid_t *g, int r, int c) { return r >= 0 && r < g->h && c >= 0 && c < g->w; }

/* ---------------- Part A: 재귀 버전 ---------------- */
/* (r,c) 와 4-이웃으로 이어진 from 색 칸을 전부 to 로 칠한다. 반환 = 칠한 칸 수.
 * from == to 이면 0 (아니면 무한 재귀). 범위 밖·다른 색이면 0. 모든 호출에서 depth_enter/leave. */
int ff_rec(grid_t *g, int r, int c, uint8_t from, uint8_t to, depth_t *d) {
    int n = 0;
    depth_enter(d, &n);
    /* TODO: from != to 이고 범위 안이고 색이 from 이면 → 칠하고 n = 1,
     *       4-이웃을 DR/DC 순서대로 재귀 호출해 n 에 더한다 (+ 한 줄로 쓰지 말 것) */
    (void)g; (void)r; (void)c; (void)from; (void)to; (void)in_grid;
    depth_leave(d);
    return n;
}

/* 노드 v 를 뿌리로 하는 부분 트리를 order 순서로 out[n..] 에 key 로 적는다. 반환 = 새 n.
 * v == -1 (빈 트리) 호출도 프레임 하나로 센다. */
int trav_rec(const tnode_t *t, int v, int order, int *out, int n, depth_t *d) {
    depth_enter(d, &n);
    /* TODO: v != -1 이면 order 에 맞춰 (전위/중위/후위) key 를 out[n++] 에 적고 왼쪽·오른쪽 재귀 */
    (void)t; (void)v; (void)order; (void)out;
    depth_leave(d);
    return n;
}

/* ---------------- Part B: 명시적 스택 반복 버전 ---------------- */
/* ff_rec 과 같은 결과. 보조 스택 = buf[0..cap), 칸 번호 r*w+c 를 넣는다.
 * push 할 때 칠한다 (mark-on-push) → 한 칸은 최대 한 번만 스택에 들어간다.
 * 반환 = 칠한 칸 수, 스택이 모자라면 B11_EOVERFLOW. *peak = 스택 최대 사용 엔트리 수. */
int ff_dfs_iter(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak) {
    istack_t s;
    is_init(&s, buf, cap);
    /* TODO: 시드를 push 하며 칠하고, pop → 4-이웃 중 from 인 칸을 push 하며 칠한다.
     *       is_push 가 false 면 B11_EOVERFLOW. 끝나면 *peak = s.peak */
    (void)g; (void)r; (void)c; (void)from; (void)to; (void)is_push; (void)is_pop;
    *peak = s.peak;
    return 0;
}

/* trav_rec 과 똑같은 순서로 out 에 적는다. 보조 스택 = buf[0..cap) (노드 번호).
 * 반환 = 적은 개수, 스택이 모자라면 B11_EOVERFLOW. *peak = 스택 최대 사용량. */
int trav_iter(const tnode_t *t, int root, int order, int *out, int32_t *buf, int cap, int *peak) {
    istack_t s;
    is_init(&s, buf, cap);
    /* TODO: 전위 = 꺼내며 방문 (오른쪽 먼저 push). 중위/후위 = 왼쪽 끝까지 push 하며 내려가고,
     *       후위는 "마지막으로 방문한 노드(last)"로 오른쪽을 이미 했는지 판단한다 */
    (void)t; (void)root; (void)order; (void)out; (void)is_peek;
    *peak = s.peak;
    return 0;
}

/* ---------------- Part C: BFS 큐 flood fill (점유 격자) ---------------- */
/* ff_dfs_iter 와 같은 계약, 보조 구조만 링 버퍼 큐 (용량 cap). enqueue 할 때 칠한다. */
int ff_bfs(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak) {
    iqueue_t q;
    iq_init(&q, buf, cap);
    /* TODO: ff_dfs_iter 와 같다. 스택 대신 큐 (iq_push / iq_pop), enqueue 할 때 칠한다 */
    (void)g; (void)r; (void)c; (void)from; (void)to; (void)iq_push; (void)iq_pop;
    *peak = q.peak;
    return 0;
}

#ifndef ALGO_B11_NO_MAIN
/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

enum { FREE = 0, WALL = 1, DONE = 2 };

static grid_t grid_new(int w, int h) { grid_t g = { calloc((size_t)w * (size_t)h, 1), w, h }; return g; }
static grid_t grid_clone(const grid_t *s) {
    grid_t g = grid_new(s->w, s->h);
    memcpy(g.cell, s->cell, (size_t)s->w * (size_t)s->h);
    return g;
}
static bool grid_same(const grid_t *a, const grid_t *b) { return memcmp(a->cell, b->cell, (size_t)a->w * (size_t)a->h) == 0; }
static int count_color(const grid_t *g, uint8_t col) {
    int n = 0;
    for (int i = 0; i < g->w * g->h; i++) n += g->cell[i] == col;
    return n;
}
/* '#'=WALL '.'=FREE 'R'=FREE + 로봇 위치 */
static grid_t grid_from(const char *const *rows, int h, int *rr, int *rc) {
    grid_t g = grid_new((int)strlen(rows[0]), h);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < g.w; c++) {
            g.cell[r * g.w + c] = rows[r][c] == '#' ? WALL : FREE;
            if (rows[r][c] == 'R' && rr) { *rr = r; *rc = c; }
        }
    return g;
}
/* 뱀 모양 복도: 짝수 행 전부 FREE, 홀수 행은 벽 + 끝 한 칸만 열림 (오른쪽/왼쪽 번갈아) */
static grid_t serpentine(int w, int h) {
    grid_t g = grid_new(w, h);
    for (int r = 1; r < h; r += 2) {
        for (int c = 0; c < w; c++) g.cell[r * w + c] = WALL;
        g.cell[r * w + ((r / 2) % 2 == 0 ? w - 1 : 0)] = FREE;
    }
    return g;
}
/* 기준 구현: 재귀도 스택도 큐도 없이 "표시된 이웃이 있으면 표시"를 변화가 없을 때까지 반복 */
static int oracle_fill(grid_t *g, int r, int c, uint8_t to) {
    uint8_t from = g->cell[r * g->w + c];
    if (from == to) return 0;
    uint8_t *mk = calloc((size_t)g->w * (size_t)g->h, 1);
    mk[r * g->w + c] = 1;
    for (bool changed = true; changed; ) {
        changed = false;
        for (int i = 0; i < g->h; i++)
            for (int j = 0; j < g->w; j++) {
                if (mk[i * g->w + j] || g->cell[i * g->w + j] != from) continue;
                for (int k = 0; k < 4; k++) {
                    int ni = i + DR[k], nj = j + DC[k];
                    if (in_grid(g, ni, nj) && mk[ni * g->w + nj]) { mk[i * g->w + j] = 1; changed = true; break; }
                }
            }
    }
    int n = 0;
    for (int i = 0; i < g->w * g->h; i++) if (mk[i]) { g->cell[i] = to; n++; }
    free(mk);
    return n;
}

/* 트리: 배열 풀 + 반복 BST 삽입 (하네스 쪽도 재귀 없음) */
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
static int tree_height(const tnode_t *t, int root, int nn) {   /* 노드 수 기준 높이 (빈 트리 0) */
    int *dep = malloc(sizeof(int) * (size_t)(nn + 1)), best = 0;
    if (root != -1) dep[root] = 1;
    /* BST 삽입 순서상 부모가 자식보다 먼저 만들어지므로 번호 순으로 한 번 훑으면 된다 */
    for (int v = 0; v < nn; v++) {
        if (dep[v] > best) best = dep[v];
        if (t[v].left != -1) dep[t[v].left] = dep[v] + 1;
        if (t[v].right != -1) dep[t[v].right] = dep[v] + 1;
    }
    free(dep);
    return best;
}
/* 같은 키 집합의 두 트리 구조가 같은가 (키 0..nn-1, key → 노드 번호 표로 비교) */
static bool tree_same_shape(const tnode_t *a, int ra, const tnode_t *b, int rb, int nn) {
    if (nn == 0) return ra == -1 && rb == -1;
    int *ia = malloc(sizeof(int) * (size_t)nn), *ib = malloc(sizeof(int) * (size_t)nn);
    for (int v = 0; v < nn; v++) { ia[a[v].key] = v; ib[b[v].key] = v; }
    bool ok = a[ra].key == b[rb].key;
    for (int k = 0; ok && k < nn; k++) {
        const tnode_t *x = &a[ia[k]], *y = &b[ib[k]];
        int xl = x->left == -1 ? -1 : a[x->left].key, yl = y->left == -1 ? -1 : b[y->left].key;
        int xr = x->right == -1 ? -1 : a[x->right].key, yr = y->right == -1 ? -1 : b[y->right].key;
        ok = xl == yl && xr == yr;
    }
    free(ia); free(ib);
    return ok;
}
static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

static const char *const WAREHOUSE[] = {     /* Part C: 창고 점유 격자 (로봇 R, 아래 가운데·오른쪽 구역은 닫혀 있음) */
    "########################",
    "#R.....#.......#.......#",
    "#.####.#.#####.#.#####.#",
    "#.#....#.#...#...#...#.#",
    "#.#.####.#.#.#####.#.#.#",
    "#...#....#.#.......#...#",
    "###.#.####.#########.###",
    "#...#......#.......#...#",
    "#.########.#.#####.#####",
    "#..........#.#...#.#...#",
    "#.##########.#.......#.#",
    "########################",
};
#define WH_H ((int)(sizeof WAREHOUSE / sizeof WAREHOUSE[0]))

int main(void) {
    depth_t d;
    int pk;
    int32_t sbuf[64];

    /* ===== Part A: 재귀, 손 계산 ===== */
    {
        grid_t g = grid_new(1, 1);
        depth_reset(&d);
        CHECK(ff_rec(&g, 0, 0, FREE, DONE, &d) == 1 && g.cell[0] == DONE);
        CHECK(d.max_depth == 2 && d.depth == 0);          /* 시드 프레임 + 이웃 검사 프레임 1 */
        free(g.cell);
    }
    {
        grid_t g = grid_new(10, 1);                       /* 1x10 복도, 왼쪽 끝에서 */
        depth_reset(&d);
        CHECK(ff_rec(&g, 0, 0, FREE, DONE, &d) == 10 && count_color(&g, DONE) == 10);
        CHECK(d.max_depth == 11);                         /* 칸 10 + 마지막 bail-out 1 */
        depth_reset(&d);
        CHECK(ff_rec(&g, 0, 3, DONE, DONE, &d) == 0 && count_color(&g, DONE) == 10);  /* from == to */
        free(g.cell);
    }
    {
        static const char *const ring[] = { ".....", ".###.", ".#..#", ".###.", "....." };
        grid_t g = grid_from(ring, 5, NULL, NULL), g2 = grid_clone(&g);
        depth_reset(&d);
        CHECK(ff_rec(&g, 2, 2, FREE, DONE, &d) == 2);     /* 벽 안쪽 두 칸만 */
        CHECK(ff_rec(&g, 0, 0, FREE, DONE, &d) == 15 && count_color(&g, FREE) == 0);
        depth_reset(&d);
        CHECK(ff_rec(&g2, 1, 1, WALL, DONE, &d) == 7);    /* 시드가 벽이면 벽 덩어리. (2,4) 는 대각선 → 제외 */
        free(g.cell); free(g2.cell);
    }
    /*        8
     *      /   \
     *     3     10
     *    / \      \
     *   1   6      14
     *      / \    /
     *     4   7  13          */
    static const int keys[] = { 8, 3, 10, 1, 6, 14, 4, 7, 13 };
    tnode_t T[16];
    int tn = 0, troot = -1;
    for (int i = 0; i < 9; i++) troot = bst_insert(T, &tn, troot, keys[i]);
    static const int exp_ord[3][9] = { { 8, 3, 1, 6, 4, 7, 10, 14, 13 },
                                       { 1, 3, 4, 6, 7, 8, 10, 13, 14 },
                                       { 1, 4, 7, 6, 3, 13, 14, 10, 8 } };
    int out[16];
    for (int o = 0; o < 3; o++) {
        depth_reset(&d);
        CHECK(trav_rec(T, troot, o, out, 0, &d) == 9 && memcmp(out, exp_ord[o], sizeof exp_ord[o]) == 0);
        CHECK(d.max_depth == 5);                          /* 높이 4 + null 프레임 1 */
    }
    depth_reset(&d);
    CHECK(trav_rec(T, -1, ORD_IN, out, 0, &d) == 0 && d.max_depth == 1);

    /* ===== Part B: 명시적 스택, 손 계산 + 경계 ===== */
    {
        grid_t g = grid_new(10, 1);
        CHECK(ff_dfs_iter(&g, 0, 0, FREE, DONE, sbuf, 64, &pk) == 10 && count_color(&g, DONE) == 10 && pk == 1);
        CHECK(ff_dfs_iter(&g, 0, 0, DONE, DONE, sbuf, 64, &pk) == 0 && pk == 0);
        grid_t h = grid_new(3, 3);
        CHECK(ff_dfs_iter(&h, 1, 1, FREE, DONE, sbuf, 0, &pk) == B11_EOVERFLOW && pk == 0);  /* cap 0 */
        CHECK(ff_dfs_iter(&h, 0, 0, FREE, DONE, sbuf, 64, &pk) == 9 && pk == 3);   /* 3x3 모서리: 5. 풀이 추적표 */
        memset(h.cell, FREE, 9);
        CHECK(ff_bfs(&h, 0, 0, FREE, DONE, sbuf, 64, &pk) == 9 && pk == 3);
        free(g.cell); free(h.cell);
    }
    for (int o = 0; o < 3; o++) {
        CHECK(trav_iter(T, troot, o, out, sbuf, 64, &pk) == 9 && memcmp(out, exp_ord[o], sizeof exp_ord[o]) == 0);
        if (o == ORD_POST) CHECK(pk == 4);                /* 후위 스택 = 뿌리까지의 경로 전체 = 높이 */
    }
    CHECK(trav_iter(T, -1, ORD_POST, out, sbuf, 64, &pk) == 0 && pk == 0);

    /* 카나리: 용량이 모자라도 buf[cap] 이후를 절대 안 건드린다 (MPU 가드 영역 흉내) */
    {
        int32_t cbuf[8 + 4];
        for (int i = 0; i < 12; i++) cbuf[i] = (int32_t)0xA5A5A5A5u;
        grid_t g = grid_new(16, 16);
        int rc = ff_dfs_iter(&g, 8, 8, FREE, DONE, cbuf, 8, &pk);
        bool canary = true;
        for (int i = 8; i < 12; i++) canary = canary && cbuf[i] == (int32_t)0xA5A5A5A5u;
        CHECK(rc == B11_EOVERFLOW && pk == 8 && canary);
        for (int i = 0; i < 12; i++) cbuf[i] = (int32_t)0xA5A5A5A5u;
        memset(g.cell, FREE, 256);
        rc = ff_bfs(&g, 8, 8, FREE, DONE, cbuf, 8, &pk);
        canary = true;
        for (int i = 8; i < 12; i++) canary = canary && cbuf[i] == (int32_t)0xA5A5A5A5u;
        CHECK(rc == B11_EOVERFLOW && pk == 8 && canary);
        free(g.cell);
    }

    /* ===== 무작위 격자 300개: 재귀 / 스택 / 큐 / 기준 구현이 칸 단위로 똑같은가 ===== */
    {
        int bad_rec = 0, bad_dfs = 0, bad_bfs = 0, bad_bound = 0, bad_cap = 0, filled_total = 0;
        int32_t *buf = malloc(sizeof(int32_t) * 24 * 24);
        for (int it = 0; it < 300; it++) {
            int w = 1 + (int)(xs32() % 24), h = 1 + (int)(xs32() % 24), dens = (int)(xs32() % 4) * 20;
            grid_t g0 = grid_new(w, h);
            for (int i = 0; i < w * h; i++)               /* 0=FREE 1=WALL, 가끔 2 (이미 칠해진 칸) */
                g0.cell[i] = (uint8_t)((int)(xs32() % 100) < dens ? WALL : (xs32() % 10 == 0 ? DONE : FREE));
            int r = (int)(xs32() % (uint32_t)h), c = (int)(xs32() % (uint32_t)w);
            uint8_t from = g0.cell[r * w + c], to = (uint8_t)(xs32() % 4);   /* to == from 도 나온다 */
            grid_t go = grid_clone(&g0), gr = grid_clone(&g0), gd = grid_clone(&g0), gb = grid_clone(&g0);
            int no = oracle_fill(&go, r, c, to);
            depth_reset(&d);
            int nr = ff_rec(&gr, r, c, from, to, &d);
            int pd = -1, pb = -1;
            int nd = ff_dfs_iter(&gd, r, c, from, to, buf, w * h, &pd);
            int nb = ff_bfs(&gb, r, c, from, to, buf, w * h, &pb);
            filled_total += no;
            bad_rec += !(nr == no && grid_same(&gr, &go));
            bad_dfs += !(nd == no && grid_same(&gd, &go));
            bad_bfs += !(nb == no && grid_same(&gb, &go));
            bad_bound += !(d.max_depth <= no + 1 && pd <= no && pb <= no && d.depth == 0);
            if (no > 0) {                                 /* peak 는 정확한 값: cap=peak 성공, cap=peak-1 실패 */
                grid_t t1 = grid_clone(&g0), t2 = grid_clone(&g0), t3 = grid_clone(&g0), t4 = grid_clone(&g0);
                int x;
                bad_cap += !(ff_dfs_iter(&t1, r, c, from, to, buf, pd, &x) == no && x == pd);
                bad_cap += !(ff_dfs_iter(&t2, r, c, from, to, buf, pd - 1, &x) == B11_EOVERFLOW);
                bad_cap += !(ff_bfs(&t3, r, c, from, to, buf, pb, &x) == no && x == pb);
                bad_cap += !(ff_bfs(&t4, r, c, from, to, buf, pb - 1, &x) == B11_EOVERFLOW);
                free(t1.cell); free(t2.cell); free(t3.cell); free(t4.cell);
            }
            free(g0.cell); free(go.cell); free(gr.cell); free(gd.cell); free(gb.cell);
        }
        free(buf);
        printf("random grids: 300, filled cells %d, mismatches rec=%d dfs=%d bfs=%d, bound=%d, cap-edge=%d\n",
               filled_total, bad_rec, bad_dfs, bad_bfs, bad_bound, bad_cap);
        CHECK(filled_total > 0 && bad_rec == 0);
        CHECK(bad_dfs == 0);
        CHECK(bad_bfs == 0);
        CHECK(bad_bound == 0);
        CHECK(bad_cap == 0);
    }

    /* ===== 무작위 BST 300개: 반복 순회 == 재귀 순회, 중위 == 정렬, 재삽입하면 같은 모양 ===== */
    {
        int bad_in = 0, bad_eq = 0, bad_shape = 0, bad_peak = 0, nodes = 0;
        tnode_t *t = malloc(sizeof(tnode_t) * 256), *t2 = malloc(sizeof(tnode_t) * 256);
        int *perm = malloc(sizeof(int) * 256), *o1 = malloc(sizeof(int) * 256), *o2 = malloc(sizeof(int) * 256);
        int32_t *buf = malloc(sizeof(int32_t) * 256);
        for (int it = 0; it < 300; it++) {
            int nn = (int)(xs32() % 201);
            for (int i = 0; i < nn; i++) perm[i] = i;
            if (it % 10 != 9)                             /* 10번에 1번은 정렬 순서 그대로 = 사슬 (최악) */
                for (int i = nn - 1; i > 0; i--) { int j = (int)(xs32() % (uint32_t)(i + 1)), x = perm[i]; perm[i] = perm[j]; perm[j] = x; }
            int cnt = 0, root = -1;
            for (int i = 0; i < nn; i++) root = bst_insert(t, &cnt, root, perm[i]);
            nodes += nn;
            int ht = tree_height(t, root, nn);
            for (int o = 0; o < 3; o++) {
                depth_reset(&d);
                int a = trav_rec(t, root, o, o1, 0, &d), p = -1;
                int b = trav_iter(t, root, o, o2, buf, 256, &p);
                bad_eq += !(a == nn && b == nn && memcmp(o1, o2, sizeof(int) * (size_t)nn) == 0);
                bad_peak += !(d.max_depth == ht + 1 && p <= d.max_depth && (o != ORD_POST || p == ht));
                if (o == ORD_IN) {
                    memcpy(o2, perm, sizeof(int) * (size_t)nn);
                    qsort(o2, (size_t)nn, sizeof(int), cmp_int);
                    bad_in += memcmp(o1, o2, sizeof(int) * (size_t)nn) != 0;
                } else {                                  /* 전위, 그리고 후위를 뒤집은 것 = "부모가 자식보다 먼저" */
                    int c2 = 0, r2 = -1;
                    for (int i = 0; i < nn; i++) r2 = bst_insert(t2, &c2, r2, o == ORD_PRE ? o1[i] : o1[nn - 1 - i]);
                    bad_shape += !tree_same_shape(t, root, t2, r2, nn);
                }
            }
        }
        printf("random BSTs: 300, nodes %d, mismatches iter!=rec=%d inorder!=sorted=%d shape=%d peak=%d\n",
               nodes, bad_eq, bad_in, bad_shape, bad_peak);
        CHECK(nodes > 0 && bad_eq == 0);
        CHECK(bad_in == 0);
        CHECK(bad_shape == 0);
        CHECK(bad_peak == 0);
        free(t); free(t2); free(perm); free(o1); free(o2); free(buf);
    }

    /* ===== 큰 입력: 재귀라면 위험한 깊이, 반복판은 힙/정적 버퍼만 쓴다 ===== */
    {
        enum { N = 1000000 };                             /* 왼쪽으로만 뻗은 사슬 트리, 높이 10^6 */
        tnode_t *t = malloc(sizeof(tnode_t) * N);
        int *o = malloc(sizeof(int) * N);
        int32_t *buf = malloc(sizeof(int32_t) * N);
        for (int i = 0; i < N; i++) { t[i].key = N - 1 - i; t[i].left = i + 1 < N ? i + 1 : -1; t[i].right = -1; }
        int p1 = -1, p2 = -1, p3 = -1;
        int a = trav_iter(t, 0, ORD_IN, o, buf, N, &p1);
        bool sorted = a == N;
        for (int i = 0; sorted && i < N; i++) sorted = o[i] == i;
        CHECK(sorted && p1 == N);
        CHECK(trav_iter(t, 0, ORD_PRE, o, buf, N, &p2) == N && p2 == 1 && o[0] == N - 1);
        CHECK(trav_iter(t, 0, ORD_POST, o, buf, 1000, &p3) == B11_EOVERFLOW && p3 == 1000);
        free(t); free(o); free(buf);

        grid_t g = serpentine(1000, 1000);                /* 복도 칸 50만 개: 재귀 깊이 50만+1 */
        int path = count_color(&g, FREE), pd = -1, pb = -1;
        int32_t small[16];
        grid_t g2 = grid_clone(&g);
        CHECK(path == 500 * 1000 + 500 && ff_dfs_iter(&g, 0, 0, FREE, DONE, small, 16, &pd) == path);
        CHECK(ff_bfs(&g2, 0, 0, FREE, DONE, small, 16, &pb) == path && pd <= 2 && pb <= 2);
        printf("big inputs: chain tree 1e6 in-order peak=%d pre-order peak=%d; serpentine %d cells: dfs peak=%d bfs peak=%d\n",
               p1, p2, path, pd, pb);
        free(g.cell); free(g2.cell);
    }

    /* ===== Part C: 창고 점유 격자, 정적 큐 64 엔트리 (256 B) ===== */
    {
        static int32_t q[64 + 4];                         /* 64 + 카나리 4 */
        int rr = -1, rc = -1;
        grid_t g = grid_from(WAREHOUSE, WH_H, &rr, &rc), go = grid_clone(&g), gr = grid_clone(&g), gd = grid_clone(&g);
        int free0 = count_color(&g, FREE);
        for (int i = 64; i < 68; i++) q[i] = (int32_t)0xA5A5A5A5u;
        int pb, pd, area = ff_bfs(&g, rr, rc, FREE, DONE, q, 64, &pb);
        int no = oracle_fill(&go, rr, rc, DONE);
        depth_reset(&d);
        int nr = ff_rec(&gr, rr, rc, FREE, DONE, &d);
        int32_t *big = malloc(sizeof(int32_t) * (size_t)free0);
        int nd = ff_dfs_iter(&gd, rr, rc, FREE, DONE, big, free0, &pd);
        printf("warehouse %dx%d: free=%d reachable=%d (sealed %d), rec depth=%d, dfs peak=%d, bfs peak=%d\n",
               g.w, g.h, free0, area, free0 - area, d.max_depth, pd, pb);
        CHECK(area == no && grid_same(&g, &go) && area == 97 && free0 - area == 26);
        CHECK(nr == area && nd == area && grid_same(&gr, &go) && grid_same(&gd, &go));
        CHECK(pb <= 64 && q[64] == (int32_t)0xA5A5A5A5u && q[67] == (int32_t)0xA5A5A5A5u);
        free(g.cell); free(go.cell); free(gr.cell); free(gd.cell); free(big);

        grid_t f = grid_new(64, 64), f2 = grid_clone(&f);  /* 장애물 없는 64x64 바닥, 가운데서 */
        int32_t *fb = malloc(sizeof(int32_t) * 4096);
        int nb2 = ff_bfs(&f, 32, 32, FREE, DONE, fb, 4096, &pb);
        int nd2 = ff_dfs_iter(&f2, 32, 32, FREE, DONE, fb, 4096, &pd);
        printf("open floor 64x64: bfs peak=%d entries (%d B), dfs peak=%d entries (%d B)\n", pb, pb * 4, pd, pd * 4);
        CHECK(nb2 == 4096 && nd2 == 4096 && pb <= 2 * (64 + 64) && pd > pb);
        grid_t f3 = grid_new(64, 64);                     /* 같은 64 엔트리 예산을 넓은 바닥에 쓰면? */
        for (int i = 64; i < 68; i++) q[i] = (int32_t)0xA5A5A5A5u;
        CHECK(ff_bfs(&f3, 32, 32, FREE, DONE, q, 64, &pb) == B11_EOVERFLOW && pb == 64 &&
              q[64] == (int32_t)0xA5A5A5A5u && q[67] == (int32_t)0xA5A5A5A5u);
        free(f3.cell);
        free(f.cell); free(f2.cell); free(fb);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
