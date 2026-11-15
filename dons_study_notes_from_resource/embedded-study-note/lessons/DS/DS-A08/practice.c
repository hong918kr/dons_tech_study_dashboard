/* DS-A08 추가 연습 — solution.c 의 트리를 그대로 쓰고 그 위에 네 가지를 만든다.
 *   연습 1  역방향 범위 스캔 (prev 사슬)
 *   연습 2  병합 임계값(merge threshold)과 분할-병합 진동
 *   연습 3  맨 끝 삽입 최적화 (fast-path append) — 오른쪽 끝 리프는 1 키로 시작해도 되게 완화
 *   연습 4  높이 상한/하한: 최소 채움 불변식에서 나오는 최악 높이 공식
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---- 연습 1: [lo, hi] 를 키 내림차순으로 ---- */
static uint32_t bt_rscan(bt_t *t, uint32_t hi, uint32_t lo, uint32_t *ks, uint32_t *vs, uint32_t max) {
    t->op++;
    if (lo > hi || max == 0u) return 0u;
    page_t *l = pg_r(t, bt_descend(t, hi, NULL));
    uint32_t i = ub_u32(l->leaf.key, l->h.n, hi), cnt = 0u;
    for (;;) {
        while (i > 0u) {
            i--;
            if (l->leaf.key[i] < lo || cnt == max) return cnt;
            ks[cnt] = l->leaf.key[i];
            vs[cnt] = l->leaf.val[i];
            cnt++;
        }
        if (l->h.prev == PID_NIL) return cnt;
        l = pg_r(t, l->h.prev);
        i = l->h.n;
    }
}

/* ---- 연습 3: 맨 끝 삽입 fast path ---- */
static int bt_append(bt_t *t, uint32_t key, uint32_t val) {
    step_t path[BT_MAX_H];
    t->op++;
    uint32_t lid = t->root;
    for (uint32_t d = 0; d + 1u < t->height; d++) {          /* 오른쪽 끝으로만 */
        page_t *p = pg_r(t, lid);
        path[d].pid = lid;
        path[d].idx = p->h.n;
        lid = p->node.child[p->h.n];
    }
    page_t *l = pg_r(t, lid);
    if (l->h.n > 0u && key <= l->leaf.key[l->h.n - 1u]) return BT_NOSPACE;   /* 호출자 계약 위반 */
    if (l->h.n < t->leaf_cap) {
        pg_w(t, lid);
        l->leaf.key[l->h.n] = key;
        l->leaf.val[l->h.n] = val;
        l->h.n++;
        t->nkeys++;
        return BT_NEW;
    }
    uint32_t need = 1u;
    int d = (int)t->height - 2;
    while (d >= 0 && pg_r(t, path[d].pid)->h.n == t->node_cap) { need++; d--; }
    if (d < 0) need++;
    if (t->free_cnt < need) return BT_NOSPACE;
    /* 핵심: 절반을 옮기지 않는다. 새 리프에 새 키 하나만. 왼쪽은 꽉 찬 채로 남는다. */
    uint32_t rid = pg_alloc(t, PK_LEAF);
    page_t *r = pg_w(t, rid);
    pg_w(t, lid);
    r->leaf.key[0] = key;
    r->leaf.val[0] = val;
    r->h.n = 1u;
    r->h.prev = lid;
    r->h.next = PID_NIL;
    l->h.next = rid;
    t->nkeys++;
    t->split_leaf++;
    uint32_t up = key;
    for (d = (int)t->height - 2; d >= 0; d--) {
        uint32_t pos = path[d].idx;
        page_t *p = pg_w(t, path[d].pid);
        if (p->h.n < t->node_cap) {
            arr_ins(p->node.key, p->h.n, pos, up);
            arr_ins(p->node.child, p->h.n + 1u, pos + 1u, rid);
            p->h.n++;
            return BT_NEW;
        }
        rid = node_split_insert(t, path[d].pid, pos, up, rid, &up);
    }
    uint32_t nr = pg_alloc(t, PK_NODE);
    page_t *nroot = pg_w(t, nr);
    nroot->h.n = 1u;
    nroot->node.key[0] = up;
    nroot->node.child[0] = t->root;
    nroot->node.child[1] = rid;
    t->root = nr;
    t->height++;
    return BT_NEW;
}

/* 리프 채움률 (사슬을 따라) */
static double leaf_fill(const bt_t *t, uint32_t *leaves) {
    uint32_t id = leftmost(t), n = 0;
    uint64_t keys = 0;
    while (id != PID_NIL) { keys += t->pg[id].h.n; n++; id = t->pg[id].h.next; }
    *leaves = n;
    return 100.0 * (double)keys / ((double)n * t->leaf_cap);
}

/* ---- 연습 4: 높이의 하한/상한 ---- */
/* 높이 h 인 트리의 최소 키 수: h==1 은 0, h>=2 는 2 * leaf_min * (node_min+1)^(h-2) */
static uint64_t min_keys(const bt_t *t, uint32_t h) {
    if (h <= 1u) return 0u;
    uint64_t m = 2u * (uint64_t)t->leaf_min;
    for (uint32_t i = 2u; i < h; i++) {
        m *= t->node_min + 1u;
        if (m > (1ull << 40)) break;
    }
    return m;
}
/* 최악 높이: n 키가 가질 수 있는 가장 큰 h */
static uint32_t height_max(const bt_t *t, uint64_t n) {
    uint32_t h = 1u;
    while (h < 32u && min_keys(t, h + 1u) <= n) h++;
    return h;
}
/* 최소 높이: leaf_cap * (node_cap+1)^(h-1) >= n 인 가장 작은 h */
static uint32_t height_min(const bt_t *t, uint64_t n) {
    uint64_t cap = t->leaf_cap;
    uint32_t h = 1u;
    while (cap < n) { cap *= t->node_cap + 1u; h++; }
    return h;
}

int main(void) {
    bt_t t;
    uint32_t ks[512], vs[512];

    printf("== 연습 1: reverse range scan\n");
    bt_init(&t, g_pool, 1024u, 4u, 4u);
    for (uint32_t k = 0; k < 300u; k++) bt_insert(&t, k * 2u, k + 1u);
    for (uint32_t k = 0; k < 300u; k += 3u) bt_delete(&t, k * 2u);        /* 첫 키가 지워진 리프를 많이 만든다 */
    bool ok = bt_check(&t) == BTC_OK;
    for (uint32_t trial = 0; trial < 2000u; trial++) {
        g_rng = 0x51000000u + trial;
        uint32_t a = rnd() % 620u, b = rnd() % 620u;
        uint32_t hi = a > b ? a : b, lo = a > b ? b : a;
        uint32_t fwd[512], fv[512], rev = bt_rscan(&t, hi, lo, ks, vs, 512u);
        uint32_t f = bt_scan(&t, lo, hi, fwd, fv, 512u);
        if (rev != f) { ok = false; break; }
        for (uint32_t i = 0; i < f; i++) ok &= ks[i] == fwd[f - 1u - i] && vs[i] == fv[f - 1u - i];
    }
    CHECK(ok);
    CHECK(bt_rscan(&t, 10u, 5u, ks, vs, 1u) == 1u && ks[0] == 10u && bt_rscan(&t, 3u, 9u, ks, vs, 8u) == 0u);

    printf("== 연습 2: merge threshold\n");
    /* cap 16: 왼쪽 리프와 오른쪽 리프가 최소(8)에 있도록 만든 뒤 "삭제 2개 + 재삽입 2개" 를 반복 */
    uint64_t wr_tight = 0, wr_loose = 0;
    uint32_t merges_tight = 0, merges_loose = 0, splits_tight = 0, splits_loose = 0;
    for (int mode = 0; mode < 2; mode++) {
        bt_init(&t, g_pool, 16u, 16u, 16u);
        if (mode == 1) t.leaf_min = 4u;               /* 임계값을 절반으로 — 빈 공간을 더 허용 */
        for (uint32_t k = 1u; k <= 17u; k++) bt_insert(&t, k, k);
        /* [1..9] [10..17] → 9 를 지우면 [1..8] [10..17] 둘 다 8 칸 */
        bt_delete(&t, 9u);
        uint64_t w0 = t.wr;
        uint32_t m0 = t.merge_leaf, s0 = t.split_leaf;
        bool cyc = true;
        for (uint32_t c = 0; c < 1000u; c++) {
            cyc &= bt_delete(&t, 8u);
            cyc &= bt_delete(&t, 1u);
            cyc &= bt_insert(&t, 1u, 1u) == BT_NEW;
            cyc &= bt_insert(&t, 8u, 8u) == BT_NEW;
            /* 다음 회전을 같은 상태에서 시작하도록 9 를 넣었다 지운다 */
            cyc &= bt_insert(&t, 9u, 9u) == BT_NEW;
            cyc &= bt_delete(&t, 9u);
        }
        cyc &= bt_check(&t) == BTC_OK;
        CHECK(cyc);
        if (mode == 0) { wr_tight = t.wr - w0; merges_tight = t.merge_leaf - m0; splits_tight = t.split_leaf - s0; }
        else { wr_loose = t.wr - w0; merges_loose = t.merge_leaf - m0; splits_loose = t.split_leaf - s0; }
    }
    printf("  leaf_min 8: merges %u, splits %u, page writes %llu | leaf_min 4: merges %u, splits %u, page writes %llu (1000 cycles)\n",
           merges_tight, splits_tight, (unsigned long long)wr_tight, merges_loose, splits_loose, (unsigned long long)wr_loose);
    CHECK(merges_tight >= 1000u && splits_tight >= 1000u && merges_loose == 0u && splits_loose == 0u && wr_loose < wr_tight);

    printf("== 연습 3: fast-path append\n");
    double fill[2];
    uint32_t leaves[2], heights[2];
    for (int mode = 0; mode < 2; mode++) {
        bt_init(&t, g_pool, POOL_PAGES, LEAF_MAX, NODE_MAX);
        t.leaf_min = 1u;                              /* 오른쪽 끝 리프 1 키 허용: 맨 끝 삽입 전용 트리의 약속 */
        bool good = true;
        for (uint32_t k = 0; k < 200000u; k++)
            good &= (mode == 0 ? bt_insert(&t, k, k + 1u) : bt_append(&t, k, k + 1u)) == BT_NEW;
        uint32_t v;
        for (uint32_t k = 0; k < 200000u; k += 997u) good &= bt_search(&t, k, &v) && v == k + 1u;
        good &= bt_check(&t) == BTC_OK && t.nkeys == 200000u;
        CHECK(good);
        fill[mode] = leaf_fill(&t, &leaves[mode]);
        heights[mode] = t.height;
    }
    printf("  normal insert: fill %.1f%%, %u leaves, height %u | append: fill %.1f%%, %u leaves, height %u\n",
           fill[0], leaves[0], heights[0], fill[1], leaves[1], heights[1]);
    CHECK(fill[0] < 52.0 && fill[1] > 99.0 && leaves[1] * 100u < leaves[0] * 52u);

    printf("== 연습 4: height bounds\n");
    ok = true;
    for (uint32_t cap = 3u; cap <= 8u; cap++) {
        bt_init(&t, g_pool, 1024u, cap, cap);
        g_rng = 0x41000000u + cap;
        for (uint32_t i = 0; i < 20000u; i++) {
            uint32_t k = rnd() % 700u, phase = (i / 2500u) % 2u;
            if ((rnd() % 100u) < (phase ? 25u : 75u)) bt_insert(&t, k, 1u); else bt_delete(&t, k);
            ok &= t.height >= height_min(&t, t.nkeys) && t.height <= height_max(&t, t.nkeys);
        }
    }
    CHECK(ok);
    bt_init(&t, g_pool, 16u, LEAF_MAX, NODE_MAX);
    printf("  cap 510 keys n=1e6: height %u..%u, n=1e9: height %u..%u, n=1e12: height %u..%u\n",
           height_min(&t, 1000000ull), height_max(&t, 1000000ull),
           height_min(&t, 1000000000ull), height_max(&t, 1000000000ull),
           height_min(&t, 1000000000000ull), height_max(&t, 1000000000000ull));
    CHECK(height_min(&t, 1000000ull) == 3u && height_max(&t, 1000000ull) == 3u && height_max(&t, 1000000000ull) == 4u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
