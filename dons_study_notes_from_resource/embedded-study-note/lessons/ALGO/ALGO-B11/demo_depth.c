/* ALGO-B11 demo: 같은 입력에서 재귀 깊이 · 프레임 바이트 · 명시적 스택/큐 최대 크기를 잰다.
 * 재귀판은 1 GiB 스택 pthread 안에서 돌린다 (안 터지게 하고 "얼마나 썼나"만 본다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O0 demo_depth.c -o demo_depth_O0 && ./demo_depth_O0
 *        cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth_O2 && ./demo_depth_O2
 */
#define _DARWIN_C_SOURCE
#define ALGO_B11_NO_MAIN
#include "solution.c"
#include <pthread.h>

/* 일부러 틀린 버전: pop 할 때 칠한다 (mark-on-pop). 같은 칸이 여러 번 push 된다 */
static int ff_dfs_popmark(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak) {
    istack_t s;
    is_init(&s, buf, cap);
    int n = 0;
    if (from == to || !in_grid(g, r, c) || g->cell[r * g->w + c] != from) { *peak = 0; return 0; }
    (void)is_push(&s, r * g->w + c);
    while (s.top > 0) {
        int32_t p = is_pop(&s);
        if (g->cell[p] != from) continue;             /* 이미 칠해진 칸이 또 나왔다 */
        g->cell[p] = to;
        n++;
        int pr = p / g->w, pc = p % g->w;
        for (int k = 0; k < 4; k++) {
            int nr = pr + DR[k], nc = pc + DC[k];
            if (in_grid(g, nr, nc) && g->cell[nr * g->w + nc] == from)
                if (!is_push(&s, nr * g->w + nc)) { *peak = s.peak; return B11_EOVERFLOW; }
        }
    }
    *peak = s.peak;
    return n;
}

/* 더 흔한 틀린 버전: 이웃을 검사 없이 전부 push, pop 할 때만 검사 (push 총량 <= 4n+1) */
static int ff_dfs_lazy(grid_t *g, int r, int c, uint8_t from, uint8_t to, int32_t *buf, int cap, int *peak) {
    istack_t s;
    is_init(&s, buf, cap);
    int n = 0;
    if (from == to || !in_grid(g, r, c)) { *peak = 0; return 0; }
    (void)is_push(&s, r * g->w + c);
    while (s.top > 0) {
        int32_t p = is_pop(&s);
        if (g->cell[p] != from) continue;
        g->cell[p] = to;
        n++;
        int pr = p / g->w, pc = p % g->w;
        for (int k = 0; k < 4; k++) {
            int nr = pr + DR[k], nc = pc + DC[k];
            if (in_grid(g, nr, nc) && !is_push(&s, nr * g->w + nc)) { *peak = s.peak; return B11_EOVERFLOW; }
        }
    }
    *peak = s.peak;
    return n;
}

static grid_t mk_grid(int w, int h) { grid_t g = { calloc((size_t)w * (size_t)h, 1), w, h }; return g; }
static grid_t mk_serp(int w, int h) {
    grid_t g = mk_grid(w, h);
    for (int r = 1; r < h; r += 2) {
        for (int c = 0; c < w; c++) g.cell[r * w + c] = 1;
        g.cell[r * w + ((r / 2) % 2 == 0 ? w - 1 : 0)] = 0;
    }
    return g;
}
static uint32_t rs = 0x12345678u;
static uint32_t xr(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static grid_t mk_random(int w, int h, int pct) {
    grid_t g = mk_grid(w, h);
    for (int i = 0; i < w * h; i++) g.cell[i] = (int)(xr() % 100) < pct ? 1 : 0;
    g.cell[(h / 2) * w + w / 2] = 0;
    return g;
}

typedef struct { const char *name; grid_t g; int r, c; } gcase_t;
typedef struct { const char *name; tnode_t *t; int root, n, height; } tcase_t;

static gcase_t G[8];
static tcase_t T[3];
static int NG, NT;

static void *run_all(void *arg) {
    (void)arg;
    printf("grid case               cells  rec depth  rec KiB  B/frame | dfs push-mark  dfs pop-mark  dfs lazy  bfs queue  (entries)\n");
    for (int i = 0; i < NG; i++) {
        grid_t *g0 = &G[i].g;
        size_t n = (size_t)g0->w * (size_t)g0->h;
        int32_t *buf = malloc(sizeof(int32_t) * (4 * n + 1));
        grid_t a = mk_grid(g0->w, g0->h), b = mk_grid(g0->w, g0->h), c = mk_grid(g0->w, g0->h), e = mk_grid(g0->w, g0->h), f = mk_grid(g0->w, g0->h);
        memcpy(a.cell, g0->cell, n); memcpy(b.cell, g0->cell, n); memcpy(c.cell, g0->cell, n); memcpy(e.cell, g0->cell, n); memcpy(f.cell, g0->cell, n);
        depth_t d;
        depth_reset(&d);
        int nr = ff_rec(&a, G[i].r, G[i].c, 0, 2, &d);
        int pd, pp, pl, pb;
        int nd = ff_dfs_iter(&b, G[i].r, G[i].c, 0, 2, buf, (int)n, &pd);
        int np = ff_dfs_popmark(&c, G[i].r, G[i].c, 0, 2, buf, (int)(4 * n + 1), &pp);
        int nl = ff_dfs_lazy(&f, G[i].r, G[i].c, 0, 2, buf, (int)(4 * n + 1), &pl);
        int nb = ff_bfs(&e, G[i].r, G[i].c, 0, 2, buf, (int)n, &pb);
        double kib = (double)(d.sp_first - d.sp_min) / 1024.0;
        double bpf = d.max_depth > 1 ? (double)(d.sp_first - d.sp_min) / (d.max_depth - 1) : 0.0;
        printf("%-22s %7d %10d %8.0f %8.1f | %13d %13d %9d %10d  %s\n", G[i].name, nr, d.max_depth, kib, bpf, pd, pp, pl, pb,
               (nr == nd && nr == np && nr == nl && nr == nb) ? "" : "MISMATCH");
        free(buf); free(a.cell); free(b.cell); free(c.cell); free(e.cell); free(f.cell);
    }
    printf("\ntree case               nodes  height  rec depth  rec KiB  B/frame | iter peak pre    in   post\n");
    for (int i = 0; i < NT; i++) {
        int *out = malloc(sizeof(int) * (size_t)T[i].n), pk[3];
        int32_t *buf = malloc(sizeof(int32_t) * (size_t)T[i].n);
        depth_t d;
        depth_reset(&d);
        int a = trav_rec(T[i].t, T[i].root, ORD_IN, out, 0, &d);
        for (int o = 0; o < 3; o++) (void)trav_iter(T[i].t, T[i].root, o, out, buf, T[i].n, &pk[o]);
        double kib = (double)(d.sp_first - d.sp_min) / 1024.0;
        double bpf = (double)(d.sp_first - d.sp_min) / (d.max_depth - 1);
        printf("%-22s %7d %7d %10d %8.0f %8.1f | %14d %5d %6d  %s\n", T[i].name, a, T[i].height, d.max_depth, kib, bpf,
               pk[0], pk[1], pk[2], a == T[i].n ? "" : "MISMATCH");
        free(out); free(buf);
    }
    return NULL;
}

int main(void) {
#ifdef __OPTIMIZE__
    printf("build: optimized (-O1 or higher)\n");
#else
    printf("build: -O0\n");
#endif
    G[NG++] = (gcase_t){ "open 64x64 center", mk_grid(64, 64), 32, 32 };
    G[NG++] = (gcase_t){ "open 256x256 corner", mk_grid(256, 256), 0, 0 };
    G[NG++] = (gcase_t){ "open 256x256 center", mk_grid(256, 256), 128, 128 };
    G[NG++] = (gcase_t){ "random30% 256x256 mid", mk_random(256, 256, 30), 128, 128 };
    G[NG++] = (gcase_t){ "serpentine 256x256", mk_serp(256, 256), 0, 0 };
    G[NG++] = (gcase_t){ "serpentine 1024x1024", mk_serp(1024, 1024), 0, 0 };

    enum { NP = (1 << 20) - 1, NR = 100000, NC = 100000 };
    tnode_t *p = malloc(sizeof(tnode_t) * NP);          /* 완전 이진 트리 (힙 번호) */
    for (int i = 0; i < NP; i++) {
        p[i].key = i;
        p[i].left = 2 * i + 1 < NP ? 2 * i + 1 : -1;
        p[i].right = 2 * i + 2 < NP ? 2 * i + 2 : -1;
    }
    T[NT++] = (tcase_t){ "perfect 2^20-1", p, 0, NP, 20 };
    tnode_t *rb = malloc(sizeof(tnode_t) * NR);         /* 무작위 BST */
    int root = -1, h = 0;
    for (int i = 0; i < NR; i++) {
        int key = (int)(xr() & 0x7fffffff), dep = 1;
        rb[i] = (tnode_t){ -1, -1, key };
        if (root == -1) { root = i; h = 1; continue; }
        for (int u = root; ; dep++) {
            int *slot = key < rb[u].key ? &rb[u].left : &rb[u].right;
            if (*slot == -1) { *slot = i; break; }
            u = *slot;
        }
        if (dep + 1 > h) h = dep + 1;
    }
    T[NT++] = (tcase_t){ "random BST 1e5", rb, root, NR, h };
    tnode_t *ch = malloc(sizeof(tnode_t) * NC);         /* 사슬 (정렬된 순서로 삽입한 BST) */
    for (int i = 0; i < NC; i++) ch[i] = (tnode_t){ i + 1 < NC ? i + 1 : -1, -1, NC - 1 - i };
    T[NT++] = (tcase_t){ "chain 1e5", ch, 0, NC, NC };

    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, (size_t)1 << 30);   /* 1 GiB: 재귀가 끝까지 가게 */
    pthread_t th;
    if (pthread_create(&th, &at, run_all, NULL) != 0) { printf("pthread_create failed\n"); return 1; }
    pthread_join(th, NULL);
    return 0;
}
