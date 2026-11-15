/* demo_measure.c — 팬아웃·높이·채움률·접촉 페이지 수 (시간 측정 없음, 결정적 작업량)
 * B+ 트리는 solution.c 를 그대로 재사용한다. AVL (DS-M07 과 같은 규칙) 은 대조용으로 여기에 최소 구현.
 * build: cc -std=c11 -O2 demo_measure.c -o dm && ./dm
 */
#define main solution_main
#include "solution.c"
#undef main
#include <stdlib.h>

#define DEMO_PAGES 65536u       /* 256 MB BSS */
_Alignas(4096) static page_t g_big[DEMO_PAGES];

/* ---- 대조군: 인덱스 기반 AVL ---- */
#define AV_MAX 2000002u
static uint32_t av_key[AV_MAX], av_l[AV_MAX], av_r[AV_MAX];
static uint8_t  av_h[AV_MAX];
static uint32_t av_n;
static int av_hg(uint32_t x) { return x ? av_h[x] : 0; }
static void av_up(uint32_t x) { int a = av_hg(av_l[x]), b = av_hg(av_r[x]); av_h[x] = (uint8_t)((a > b ? a : b) + 1); }
static uint32_t av_rr(uint32_t y) { uint32_t x = av_l[y]; av_l[y] = av_r[x]; av_r[x] = y; av_up(y); av_up(x); return x; }
static uint32_t av_rl(uint32_t x) { uint32_t y = av_r[x]; av_r[x] = av_l[y]; av_l[y] = x; av_up(x); av_up(y); return y; }
static uint32_t av_ins(uint32_t x, uint32_t k) {
    if (!x) { uint32_t n = ++av_n; av_key[n] = k; av_l[n] = av_r[n] = 0; av_h[n] = 1; return n; }
    if (k < av_key[x]) av_l[x] = av_ins(av_l[x], k);
    else if (k > av_key[x]) av_r[x] = av_ins(av_r[x], k);
    else return x;
    av_up(x);
    int b = av_hg(av_l[x]) - av_hg(av_r[x]);
    if (b > 1) { if (k > av_key[av_l[x]]) av_l[x] = av_rl(av_l[x]); return av_rr(x); }
    if (b < -1) { if (k < av_key[av_r[x]]) av_r[x] = av_rr(av_r[x]); return av_rl(x); }
    return x;
}

typedef struct { uint32_t leaf_pages, node_pages; uint64_t leaf_keys, node_keys; } fill_t;
static void fill_rec(const bt_t *t, uint32_t id, uint32_t depth, fill_t *f) {
    const page_t *p = &t->pg[id];
    if (depth == t->height) { f->leaf_pages++; f->leaf_keys += p->h.n; return; }
    f->node_pages++; f->node_keys += p->h.n;
    for (uint32_t i = 0; i <= p->h.n; i++) fill_rec(t, p->node.child[i], depth + 1u, f);
}
static fill_t bt_fill(const bt_t *t) { fill_t f = { 0, 0, 0, 0 }; fill_rec(t, t->root, 1u, &f); return f; }

static void report_row(const char *label, uint64_t n, const bt_t *t) {
    fill_t f = bt_fill(t);
    double lf = 100.0 * (double)f.leaf_keys / ((double)f.leaf_pages * LEAF_MAX);
    double nf = f.node_pages ? 100.0 * (double)f.node_keys / ((double)f.node_pages * NODE_MAX) : 0.0;
    printf("| %s | %llu | %u | %u | %u | %.1f | %.1f | %.2f |\n", label, (unsigned long long)n, t->height, f.leaf_pages, f.node_pages,
           lf, nf, (double)(f.leaf_pages + f.node_pages) * PAGE_SIZE / (double)n);
}

#ifndef DEMO_NO_MAIN
static uint32_t *g_keys;
int main(void) {
    static bt_t t;
    g_keys = malloc(sizeof(uint32_t) * 12000000u);      /* 측정 쪽 임시 배열. 자료구조 자체에는 malloc 없음 */
    if (!g_keys) return 1;
    printf("page %u B, leaf %u keys, internal %u keys / %u children\n\n", PAGE_SIZE, LEAF_MAX, NODE_MAX, NODE_MAX + 1u);

    /* 표 1: 무작위 삽입 — 높이 vs n */
    printf("Table 1: random insert (xorshift32, 32-bit keys)\n");
    printf("| workload | n | height | leaf pages | internal pages | leaf fill %% | internal fill %% | bytes/key |\n|---|---|---|---|---|---|---|---|\n");
    uint64_t ns[] = { 100, 1000, 10000, 100000, 1000000, 10000000 };
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
        g_rng = 0x1234ABCDu;
        while (t.nkeys < ns[i]) bt_insert(&t, rnd(), 1u);
        report_row("random", ns[i], &t);
        if (bt_check(&t) != BTC_OK) { printf("CHECK FAILED\n"); return 1; }
    }

    /* 표 1b: 같은 무작위 스트림으로 n 만 늘릴 때 채움률이 어떻게 흔들리나 */
    printf("\nTable 1b: leaf fill %% vs n (one tree, same key stream, random keys, leaf cap 510)\n");
    printf("| n | leaf pages | leaf fill %% |\n|---|---|---|\n");
    bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
    g_rng = 0x1234ABCDu;
    for (uint32_t step = 1; step <= 10u; step++) {
        while (t.nkeys < step * 100000u) bt_insert(&t, rnd(), 1u);
        fill_t f1 = bt_fill(&t);
        printf("| %u | %u | %.1f |\n", step * 100000u, f1.leaf_pages, 100.0 * (double)f1.leaf_keys / ((double)f1.leaf_pages * LEAF_MAX));
    }

    printf("\nTable 2: insert order at n=1000000, then churn\n");
    printf("| workload | n | height | leaf pages | internal pages | leaf fill %% | internal fill %% | bytes/key |\n|---|---|---|---|---|---|---|---|\n");
    uint64_t n = 1000000u;
    bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
    for (uint32_t k = 0; k < n; k++) bt_insert(&t, k, 1u);
    report_row("ascending", n, &t);
    bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
    for (uint32_t k = (uint32_t)n; k-- > 0u; ) bt_insert(&t, k, 1u);
    report_row("descending", n, &t);
    bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
    g_rng = 0x1234ABCDu;
    uint32_t cnt = 0;
    while (t.nkeys < n) { uint32_t k = rnd(); if (bt_insert(&t, k, 1u) == BT_NEW) g_keys[cnt++] = k; }
    report_row("random", n, &t);
    /* 무작위 절반 삭제 (삽입 순서 그대로) */
    for (uint32_t i = 0; i < cnt; i += 2u) bt_delete(&t, g_keys[i]);
    report_row("random, then delete every 2nd", t.nkeys, &t);
    /* 다시 다른 키로 채움 */
    for (uint32_t i = 0; i < 500000u; ) { uint32_t k = rnd(); if (bt_insert(&t, k, 1u) == BT_NEW) i++; }
    report_row("... then refill 500k new keys", t.nkeys, &t);
    if (bt_check(&t) != BTC_OK) { printf("CHECK FAILED\n"); return 1; }

    /* 표 3: 연산당 접촉 페이지 (읽기·쓰기, 서로 다른 페이지 기준), n = 1e6 무작위 트리 */
    printf("\nTable 3: distinct pages touched per op (n=1000000 random tree, 200000 ops each)\n");
    bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
    g_rng = 0x1234ABCDu;
    cnt = 0;
    while (t.nkeys < n) { uint32_t k = rnd(); if (bt_insert(&t, k, 1u) == BT_NEW) g_keys[cnt++] = k; }
    printf("| op | read avg | read max | write avg | write max |\n|---|---|---|---|---|\n");
#define MEASURE(label, stmt) do { \
        uint64_t rs = 0, ws = 0, rmax = 0, wmax = 0; \
        for (uint32_t i = 0; i < 200000u; i++) { \
            uint64_t r0 = t.rd, w0 = t.wr; stmt; \
            uint64_t dr = t.rd - r0, dw = t.wr - w0; \
            rs += dr; ws += dw; if (dr > rmax) rmax = dr; if (dw > wmax) wmax = dw; \
        } \
        printf("| %s | %.2f | %llu | %.2f | %llu |\n", label, rs / 200000.0, (unsigned long long)rmax, ws / 200000.0, (unsigned long long)wmax); \
    } while (0)
    uint32_t v;
    MEASURE("search (hit)", bt_search(&t, g_keys[rnd() % cnt], &v));
    MEASURE("insert (new random key)", bt_insert(&t, rnd(), 1u));
    MEASURE("delete (existing key)", bt_delete(&t, g_keys[rnd() % cnt]));
    static uint32_t ks[2048], vs[2048];
    MEASURE("range scan 1000 keys", bt_scan(&t, rnd(), 0xFFFFFFFFu, ks, vs, 1000u));
    printf("| counters after run | splits L%u N%u | merges L%u N%u | redist L%u R%u | collapses %u |\n",
           t.split_leaf, t.split_node, t.merge_leaf, t.merge_node, t.redist_l, t.redist_r, t.collapses);

    /* 표 4: 대조 — AVL 과 "노드 하나 = 페이지 하나로 저장" 시 접촉 수 */
    printf("\nTable 4: B+ tree vs AVL (random keys, avg nodes visited per successful search, 200000 searches)\n");
    printf("| n | B+ height | B+ pages/search | AVL height | AVL nodes/search |\n|---|---|---|---|---|\n");
    uint32_t ms[] = { 1000u, 10000u, 100000u, 1000000u };
    for (size_t i = 0; i < 4u; i++) {
        bt_init(&t, g_big, DEMO_PAGES, LEAF_MAX, NODE_MAX);
        av_n = 0;
        uint32_t avroot = 0;
        g_rng = 0x1234ABCDu;
        cnt = 0;
        while (t.nkeys < ms[i]) {
            uint32_t k = rnd();
            if (bt_insert(&t, k, 1u) == BT_NEW) { g_keys[cnt++] = k; avroot = av_ins(avroot, k); }
        }
        uint64_t avvisit = 0, r0 = t.rd;
        for (uint32_t j = 0; j < 200000u; j++) {
            uint32_t k = g_keys[rnd() % cnt], x = avroot;
            bt_search(&t, k, &v);
            while (x) { avvisit++; if (k == av_key[x]) break; x = k < av_key[x] ? av_l[x] : av_r[x]; }
        }
        printf("| %u | %u | %.2f | %d | %.2f |\n", ms[i], t.height, (double)(t.rd - r0) / 200000.0, av_hg(avroot), (double)avvisit / 200000.0);
    }
    return 0;
}
#endif
