/* DS-A05 demo_rb_vs_avl.c — RB vs AVL: 높이·회전·메타데이터 변경 수(플랫폼 무관)와 벌크 시간 (관찰용, 체크 아님)
 *
 * AVL 은 DS-M07 과 같은 재귀 알고리즘(부모 포인터 없음, 노드에 높이 저장).
 *   rot   : 단일 회전 수 (이중 회전 = 2)
 *   meta  : 실제로 값이 바뀐 메타데이터 수 — RB 는 색 변경, AVL 은 height 변경. 조기 종료하는 반복 구현이 쓸 양과 같다.
 *   depth : 모든 키의 깊이 평균(루트 = 1) = 성공한 검색의 평균 비교 노드 수
 * 시간은 벌크 평균이다 (키마다 측정하지 않음 — 타이머 눈금 41.7 ns 때문). 5 회 실행의 중앙값과 [최소, 최대].
 * build: cc -std=c11 -O2 demo_rb_vs_avl.c -o drv && ./drv
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static uint64_t g_rot, g_meta;
#define RB_ROT() (g_rot++)
#define RB_RECOLOR() (g_meta++)
#define RB_API __attribute__((unused)) static
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define RB_RED   0u
#define RB_BLACK 1u
#define RB_COLOR_MASK ((uintptr_t)1)
struct rb_node { uintptr_t parent_color; struct rb_node *rb_right, *rb_left; };
struct rb_root { struct rb_node *rb_node; };
_Static_assert(_Alignof(struct rb_node) >= 2, "bit 0 of a node address must be free for the colour");
#define RB_CLEAR_NODE(n) ((n)->parent_color = (uintptr_t)(n))
#define RB_EMPTY_NODE(n) ((n)->parent_color == (uintptr_t)(n))

static struct rb_node *rb_parent(const struct rb_node *n) { return (struct rb_node *)(n->parent_color & ~RB_COLOR_MASK); }
static unsigned rb_color(const struct rb_node *n) { return (unsigned)(n->parent_color & RB_COLOR_MASK); }
static bool rb_is_red(const struct rb_node *n) { return rb_color(n) == RB_RED; }
static bool rb_is_black(const struct rb_node *n) { return rb_color(n) == RB_BLACK; }
static void rb_set_parent(struct rb_node *n, struct rb_node *p) { n->parent_color = (uintptr_t)p | (n->parent_color & RB_COLOR_MASK); }
static void rb_set_black(struct rb_node *n) { if (rb_is_red(n)) RB_RECOLOR(); n->parent_color |= RB_COLOR_MASK; }
static void rb_set_red(struct rb_node *n) { if (rb_is_black(n)) RB_RECOLOR(); n->parent_color &= ~RB_COLOR_MASK; }
static bool rb_red_or_false(const struct rb_node *n) { return n && rb_is_red(n); }
static void rb_change_child(struct rb_node *old, struct rb_node *nw, struct rb_node *parent, struct rb_root *root) {
    if (parent) { if (parent->rb_left == old) parent->rb_left = nw; else parent->rb_right = nw; }
    else root->rb_node = nw;
}
static void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **link) {
    node->parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;
    *link = node;
}
static void rb_rotate_left(struct rb_node *x, struct rb_root *root) {
    struct rb_node *y = x->rb_right, *b = y->rb_left, *p = rb_parent(x);
    x->rb_right = b;
    if (b) rb_set_parent(b, x);
    y->rb_left = x;
    rb_set_parent(y, p);
    rb_set_parent(x, y);
    rb_change_child(x, y, p, root);
    RB_ROT();
}
static void rb_rotate_right(struct rb_node *x, struct rb_root *root) {
    struct rb_node *y = x->rb_left, *b = y->rb_right, *p = rb_parent(x);
    x->rb_left = b;
    if (b) rb_set_parent(b, x);
    y->rb_right = x;
    rb_set_parent(y, p);
    rb_set_parent(x, y);
    rb_change_child(x, y, p, root);
    RB_ROT();
}
RB_API void rb_insert_color(struct rb_node *node, struct rb_root *root) {
    struct rb_node *parent, *gparent, *uncle, *tmp;
    while ((parent = rb_parent(node)) && rb_is_red(parent)) {
        gparent = rb_parent(parent);
        if (parent == gparent->rb_left) {
            uncle = gparent->rb_right;
            if (rb_red_or_false(uncle)) { rb_set_black(parent); rb_set_black(uncle); rb_set_red(gparent); node = gparent; continue; }
            if (node == parent->rb_right) { rb_rotate_left(parent, root); tmp = node; node = parent; parent = tmp; }
            rb_set_black(parent); rb_set_red(gparent); rb_rotate_right(gparent, root);
            break;
        } else {
            uncle = gparent->rb_left;
            if (rb_red_or_false(uncle)) { rb_set_black(parent); rb_set_black(uncle); rb_set_red(gparent); node = gparent; continue; }
            if (node == parent->rb_left) { rb_rotate_right(parent, root); tmp = node; node = parent; parent = tmp; }
            rb_set_black(parent); rb_set_red(gparent); rb_rotate_left(gparent, root);
            break;
        }
    }
    rb_set_black(root->rb_node);
}
RB_API struct rb_node *rb_next(const struct rb_node *node) {
    struct rb_node *parent;
    if (RB_EMPTY_NODE(node)) return NULL;
    if (node->rb_right) { struct rb_node *n = node->rb_right; while (n->rb_left) n = n->rb_left; return n; }
    while ((parent = rb_parent(node)) && node == parent->rb_right) node = parent;
    return parent;
}
static void rb_erase_color(struct rb_node *parent, struct rb_root *root) {
    struct rb_node *node = NULL, *sib;
    for (;;) {
        sib = parent->rb_right;
        if (node != sib) {
            if (rb_is_red(sib)) { rb_set_black(sib); rb_set_red(parent); rb_rotate_left(parent, root); sib = parent->rb_right; }
            if (!rb_red_or_false(sib->rb_left) && !rb_red_or_false(sib->rb_right)) {
                rb_set_red(sib);
                if (rb_is_red(parent)) { rb_set_black(parent); return; }
                node = parent; parent = rb_parent(node);
                if (!parent) return;
                continue;
            }
            if (!rb_red_or_false(sib->rb_right)) { rb_set_black(sib->rb_left); rb_set_red(sib); rb_rotate_right(sib, root); sib = parent->rb_right; }
            if (rb_is_red(parent)) rb_set_red(sib); else rb_set_black(sib);
            rb_set_black(parent); rb_set_black(sib->rb_right); rb_rotate_left(parent, root);
            return;
        } else {
            sib = parent->rb_left;
            if (rb_is_red(sib)) { rb_set_black(sib); rb_set_red(parent); rb_rotate_right(parent, root); sib = parent->rb_left; }
            if (!rb_red_or_false(sib->rb_left) && !rb_red_or_false(sib->rb_right)) {
                rb_set_red(sib);
                if (rb_is_red(parent)) { rb_set_black(parent); return; }
                node = parent; parent = rb_parent(node);
                if (!parent) return;
                continue;
            }
            if (!rb_red_or_false(sib->rb_left)) { rb_set_black(sib->rb_right); rb_set_red(sib); rb_rotate_left(sib, root); sib = parent->rb_left; }
            if (rb_is_red(parent)) rb_set_red(sib); else rb_set_black(sib);
            rb_set_black(parent); rb_set_black(sib->rb_left); rb_rotate_right(parent, root);
            return;
        }
    }
}
RB_API void rb_erase(struct rb_node *node, struct rb_root *root) {
    struct rb_node *child, *fix_parent;
    bool removed_black;
    if (!node->rb_left || !node->rb_right) {
        struct rb_node *parent = rb_parent(node);
        child = node->rb_left ? node->rb_left : node->rb_right;
        removed_black = rb_is_black(node);
        rb_change_child(node, child, parent, root);
        if (child) rb_set_parent(child, parent);
        fix_parent = parent;
    } else {
        struct rb_node *succ = node->rb_right;
        while (succ->rb_left) succ = succ->rb_left;
        child = succ->rb_right;
        removed_black = rb_is_black(succ);
        if (succ == node->rb_right) {
            fix_parent = succ;
        } else {
            fix_parent = rb_parent(succ);
            fix_parent->rb_left = child;
            if (child) rb_set_parent(child, fix_parent);
            succ->rb_right = node->rb_right;
            rb_set_parent(succ->rb_right, succ);
        }
        succ->rb_left = node->rb_left;
        rb_set_parent(succ->rb_left, succ);
        rb_change_child(node, succ, rb_parent(node), root);
        succ->parent_color = node->parent_color;
    }
    if (removed_black) {
        if (child) rb_set_black(child);
        else if (fix_parent) rb_erase_color(fix_parent, root);
    }
}


#define MAXN 1000000u
typedef struct { uint32_t key; struct rb_node node; } ritem_t;
static ritem_t g_r[MAXN];

static void r_insert(struct rb_root *t, ritem_t *it) {
    struct rb_node **l = &t->rb_node, *p = NULL;
    while (*l) { p = *l; l = it->key < container_of(p, ritem_t, node)->key ? &p->rb_left : &p->rb_right; }
    rb_link_node(&it->node, p, l);
    rb_insert_color(&it->node, t);
}
static const ritem_t *r_find(const struct rb_root *t, uint32_t k) {
    const struct rb_node *n = t->rb_node;
    while (n) {
        const ritem_t *it = container_of(n, const ritem_t, node);
        if (k < it->key) n = n->rb_left; else if (k > it->key) n = n->rb_right; else return it;
    }
    return NULL;
}
static void r_stats(const struct rb_node *n, int d, int *h, uint64_t *sum) {
    if (!n) return;
    if (d > *h) *h = d;
    *sum += (uint64_t)d;
    r_stats(n->rb_left, d + 1, h, sum);
    r_stats(n->rb_right, d + 1, h, sum);
}

/* ---- AVL (DS-M07 방식) ---- */
typedef struct anode { struct anode *l, *r; uint32_t key; int8_t h; } anode_t;
static anode_t g_a[MAXN];
static anode_t *a_alloc(uint32_t i, uint32_t key) { anode_t *n = &g_a[i]; n->l = n->r = NULL; n->key = key; n->h = 1; return n; }
static int aht(const anode_t *n) { return n ? n->h : 0; }
static void afix(anode_t *n) { int a = aht(n->l), b = aht(n->r); int8_t h = (int8_t)(1 + (a > b ? a : b)); if (h != n->h) { n->h = h; g_meta++; } }
static anode_t *a_rr(anode_t *y) { g_rot++; anode_t *x = y->l; y->l = x->r; x->r = y; afix(y); afix(x); return x; }
static anode_t *a_rl(anode_t *x) { g_rot++; anode_t *y = x->r; x->r = y->l; y->l = x; afix(x); afix(y); return y; }
static anode_t *a_bal(anode_t *n) {
    afix(n);
    int b = aht(n->l) - aht(n->r);
    if (b > 1) { if (aht(n->l->l) < aht(n->l->r)) n->l = a_rl(n->l); return a_rr(n); }
    if (b < -1) { if (aht(n->r->r) < aht(n->r->l)) n->r = a_rr(n->r); return a_rl(n); }
    return n;
}
static anode_t *a_ins(anode_t *n, anode_t *m) {
    if (!n) return m;
    if (m->key < n->key) n->l = a_ins(n->l, m); else n->r = a_ins(n->r, m);
    return a_bal(n);
}
static anode_t *a_detach_min(anode_t *n, anode_t **min) {
    if (!n->l) { *min = n; return n->r; }
    n->l = a_detach_min(n->l, min);
    return a_bal(n);
}
static anode_t *a_del(anode_t *n, uint32_t k) {
    if (k < n->key) n->l = a_del(n->l, k);
    else if (k > n->key) n->r = a_del(n->r, k);
    else {
        if (!n->l || !n->r) return n->l ? n->l : n->r;
        anode_t *min, *rest = a_detach_min(n->r, &min);
        min->l = n->l; min->r = rest; min->h = n->h;
        return a_bal(min);
    }
    return a_bal(n);
}
static const anode_t *a_find(const anode_t *n, uint32_t k) {
    while (n) { if (k < n->key) n = n->l; else if (k > n->key) n = n->r; else return n; }
    return NULL;
}
static void a_stats(const anode_t *n, int d, int *h, uint64_t *sum) {
    if (!n) return;
    if (d > *h) *h = d;
    *sum += (uint64_t)d;
    a_stats(n->l, d + 1, h, sum);
    a_stats(n->r, d + 1, h, sum);
}

/* ---- 공통 ---- */
static uint64_t g_s;
static uint32_t rnd32(void) { uint64_t x = g_s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; g_s = x; return (uint32_t)(x >> 16); }
static uint32_t g_ins[MAXN], g_del[MAXN];
static void shuffle(uint32_t *a, uint32_t n) { for (uint32_t i = n; i > 1; i--) { uint32_t j = rnd32() % i, t = a[i-1]; a[i-1] = a[j]; a[j] = t; } }
static void mk_order(uint32_t n, bool sorted) {
    for (uint32_t i = 0; i < n; i++) g_ins[i] = g_del[i] = i;
    if (!sorted) { shuffle(g_ins, n); shuffle(g_del, n); }
}
typedef struct { int h; double depth, ins_rot, ins_meta, del_rot, del_meta; uint64_t ins_rot_max, del_rot_max, ins_meta_max, del_meta_max; } result_t;

static result_t run_rb(uint32_t n) {
    result_t R; memset(&R, 0, sizeof R);
    struct rb_root t = { NULL };
    uint64_t sr = 0, sm = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t r0 = g_rot, m0 = g_meta;
        ritem_t *it = &g_r[g_ins[i]];
        it->key = g_ins[i];
        r_insert(&t, it);
        uint64_t dr = g_rot - r0, dm = g_meta - m0;
        sr += dr; sm += dm;
        if (dr > R.ins_rot_max) R.ins_rot_max = dr;
        if (dm > R.ins_meta_max) R.ins_meta_max = dm;
    }
    R.ins_rot = (double)sr / n; R.ins_meta = (double)sm / n;
    uint64_t sum = 0; r_stats(t.rb_node, 1, &R.h, &sum); R.depth = (double)sum / n;
    sr = sm = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t r0 = g_rot, m0 = g_meta;
        rb_erase(&g_r[g_del[i]].node, &t);
        uint64_t dr = g_rot - r0, dm = g_meta - m0;
        sr += dr; sm += dm;
        if (dr > R.del_rot_max) R.del_rot_max = dr;
        if (dm > R.del_meta_max) R.del_meta_max = dm;
    }
    R.del_rot = (double)sr / n; R.del_meta = (double)sm / n;
    return R;
}
static result_t run_avl(uint32_t n) {
    result_t R; memset(&R, 0, sizeof R);
    anode_t *t = NULL;
    uint64_t sr = 0, sm = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t r0 = g_rot, m0 = g_meta;
        t = a_ins(t, a_alloc(g_ins[i], g_ins[i]));
        uint64_t dr = g_rot - r0, dm = g_meta - m0;
        sr += dr; sm += dm;
        if (dr > R.ins_rot_max) R.ins_rot_max = dr;
        if (dm > R.ins_meta_max) R.ins_meta_max = dm;
    }
    R.ins_rot = (double)sr / n; R.ins_meta = (double)sm / n;
    uint64_t sum = 0; a_stats(t, 1, &R.h, &sum); R.depth = (double)sum / n;
    sr = sm = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t r0 = g_rot, m0 = g_meta;
        t = a_del(t, g_del[i]);
        uint64_t dr = g_rot - r0, dm = g_meta - m0;
        sr += dr; sm += dm;
        if (dr > R.del_rot_max) R.del_rot_max = dr;
        if (dm > R.del_meta_max) R.del_meta_max = dm;
    }
    R.del_rot = (double)sr / n; R.del_meta = (double)sm / n;
    return R;
}
static void show(const char *tree, const char *pat, uint32_t n, result_t r, double bound) {
    printf("%-3s %-6s %8u | %3d %6.2f %6.1f | %5.3f %2llu %6.3f %3llu | %5.3f %2llu %6.3f %3llu\n",
           tree, pat, n, r.h, r.depth, bound,
           r.ins_rot, (unsigned long long)r.ins_rot_max, r.ins_meta, (unsigned long long)r.ins_meta_max,
           r.del_rot, (unsigned long long)r.del_rot_max, r.del_meta, (unsigned long long)r.del_meta_max);
}

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_UPTIME_RAW, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
#define REP 5
static volatile uintptr_t g_sink;

static void bench(uint32_t n) {
    double ri[REP], rf[REP], re[REP], rs[REP], ai[REP], af[REP], ae[REP];
    for (int rep = 0; rep < REP; rep++) {
        g_s = 0x1234567890ABCDEFull + (uint64_t)rep;
        mk_order(n, false);
        struct rb_root t = { NULL };
        double t0 = now_ns();
        for (uint32_t i = 0; i < n; i++) { ritem_t *it = &g_r[g_ins[i]]; it->key = g_ins[i]; r_insert(&t, it); }
        double t1 = now_ns();
        uintptr_t s = 0;
        for (uint32_t i = 0; i < n; i++) s += (uintptr_t)r_find(&t, g_del[i]);
        double t2 = now_ns();
        for (uint32_t i = 0; i < n; i++) rb_erase(&g_r[g_del[i]].node, &t);
        double t3 = now_ns();
        anode_t *a = NULL;
        double t4 = now_ns();
        for (uint32_t i = 0; i < n; i++) a = a_ins(a, a_alloc(g_ins[i], g_ins[i]));
        double t5 = now_ns();
        for (uint32_t i = 0; i < n; i++) s += (uintptr_t)a_find(a, g_del[i]);
        double t6 = now_ns();
        for (uint32_t i = 0; i < n; i++) a = a_del(a, g_del[i]);
        double t7 = now_ns();
        /* RB 를 다시 만들어 "검색 + 삭제" 를 잰다 — AVL 의 a_del(키로 찾으면서 지움) 과 같은 일 */
        for (uint32_t i = 0; i < n; i++) { ritem_t *it = &g_r[g_ins[i]]; it->key = g_ins[i]; r_insert(&t, it); }
        double t8 = now_ns();
        for (uint32_t i = 0; i < n; i++) rb_erase((struct rb_node *)(uintptr_t)&r_find(&t, g_del[i])->node, &t);
        double t9 = now_ns();
        rs[rep] = (t9 - t8) / n;
        g_sink += s;
        ri[rep] = (t1 - t0) / n; rf[rep] = (t2 - t1) / n; re[rep] = (t3 - t2) / n;
        ai[rep] = (t5 - t4) / n; af[rep] = (t6 - t5) / n; ae[rep] = (t7 - t6) / n;
    }
    struct { const char *name; double *v; } rows[7] = {
        {"RB  insert             ", ri}, {"AVL insert             ", ai}, {"RB  search             ", rf}, {"AVL search             ", af},
        {"RB  erase(ptr, no find)", re}, {"RB  find + erase       ", rs}, {"AVL erase(by key)      ", ae} };
    printf("n=%u (ns/op, median of %d runs [min, max]):\n", (unsigned)n, REP);
    for (int i = 0; i < 7; i++) {
        qsort(rows[i].v, REP, sizeof(double), cmpd);
        printf("  %s %7.1f  [%7.1f, %7.1f]\n", rows[i].name, rows[i].v[REP / 2], rows[i].v[0], rows[i].v[REP - 1]);
    }
}

int main(void) {
    printf("sizeof: RB node+key = %zu, AVL node = %zu (bytes, this 64-bit host)\n", sizeof(ritem_t), sizeof(anode_t));
    printf("tree pat           n | hgt  depth  bound | ins: rot/op max meta/op max | del: rot/op max meta/op max\n");
    static const uint32_t ns[] = { 1000u, 10000u, 100000u, 1000000u };
    for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        uint32_t n = ns[i];
        double rb_b = 2.0 * log2((double)n + 1.0), avl_b = 1.4405 * log2((double)n + 2.0) - 0.3277;
        g_s = 0x1234567890ABCDEFull; mk_order(n, false);
        show("RB", "random", n, run_rb(n), rb_b);
        show("AVL", "random", n, run_avl(n), avl_b);
        mk_order(n, true);
        show("RB", "sorted", n, run_rb(n), rb_b);
        show("AVL", "sorted", n, run_avl(n), avl_b);
    }
    bench(100000u);
    bench(1000000u);
    return 0;
}
