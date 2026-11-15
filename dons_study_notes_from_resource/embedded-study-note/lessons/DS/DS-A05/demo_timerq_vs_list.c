/* DS-A05 demo_timerq_vs_list.c — 정렬 연결 리스트 vs RB(cached leftmost) 타이머 큐 (관찰용, 체크 아님)
 *
 * 정상 상태 n 개의 타이머: "가장 이른 것을 꺼내고 + 새 만료 시각으로 다시 넣기" 반복.
 * 리스트는 앞에서부터 삽입 위치를 찾는다. 만료 시각은 now + uniform(1..D) 으로 뽑는다.
 * 비교 수는 플랫폼 무관이다. 시간은 벌크 평균(pair 당 ns), 5 회 중앙값 [최소, 최대].
 * build: cc -std=c11 -O2 demo_timerq_vs_list.c -o dtl && ./dtl
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define RB_ROT() ((void)0)
#define RB_RECOLOR() ((void)0)
#define RB_API static
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


#define NMAX 4096u
#define PAIRS 400000u
#define DELTA 100000u
static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static bool before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

/* RB + leftmost */
typedef struct { struct rb_node node; uint32_t expires; } rt_t;
static rt_t g_rt[NMAX];
static struct rb_root g_rroot;
static struct rb_node *g_left;
static uint64_t g_rcmp, g_rcmp_max;
static void r_add(rt_t *t, uint32_t e) {
    struct rb_node **l = &g_rroot.rb_node, *p = NULL;
    bool lm = true;
    uint64_t c = 0;
    t->expires = e;
    while (*l) {
        p = *l; c++;
        if (before(e, container_of(p, rt_t, node)->expires)) l = &p->rb_left;
        else { l = &p->rb_right; lm = false; }
    }
    g_rcmp += c; if (c > g_rcmp_max) g_rcmp_max = c;
    rb_link_node(&t->node, p, l);
    if (lm) g_left = &t->node;
    rb_insert_color(&t->node, &g_rroot);
}
static rt_t *r_pop(void) {
    struct rb_node *n = g_left;
    g_left = rb_next(n);
    rb_erase(n, &g_rroot);
    return container_of(n, rt_t, node);
}

/* 정렬 리스트 */
typedef struct lt { struct lt *prev, *next; uint32_t expires; } lt_t;
static lt_t g_lt[NMAX], g_lhead;
static uint64_t g_lcmp, g_lcmp_max;
static void l_add(lt_t *t, uint32_t e) {
    lt_t *p = g_lhead.next;
    uint64_t c = 0;
    t->expires = e;
    while (p != &g_lhead) { c++; if (before(e, p->expires)) break; p = p->next; }
    g_lcmp += c; if (c > g_lcmp_max) g_lcmp_max = c;
    t->next = p; t->prev = p->prev; p->prev->next = t; p->prev = t;
}
static lt_t *l_pop(void) { lt_t *n = g_lhead.next; n->prev->next = n->next; n->next->prev = n->prev; return n; }

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_UPTIME_RAW, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
#define REP 5

static void one(uint32_t n) {
    double tr[REP], tl[REP];
    uint64_t rc = 0, lc = 0, rm = 0, lm = 0;
    uint32_t pairs = PAIRS / n * n / 1u;
    if (pairs > PAIRS) pairs = PAIRS;
    for (int rep = 0; rep < REP; rep++) {
        uint32_t seed = 0x2545F491u + (uint32_t)rep * 2u + 1u;
        g_rng = seed; g_rroot.rb_node = NULL; g_left = NULL; g_rcmp = 0; g_rcmp_max = 0;
        uint32_t now = 1000u;
        for (uint32_t i = 0; i < n; i++) r_add(&g_rt[i], now + 1u + rnd() % DELTA);
        g_rcmp = 0; g_rcmp_max = 0;
        double t0 = now_ns();
        for (uint32_t i = 0; i < pairs; i++) { rt_t *t = r_pop(); now = t->expires; r_add(t, now + 1u + rnd() % DELTA); }
        double t1 = now_ns();
        g_rng = seed; g_lhead.next = g_lhead.prev = &g_lhead; g_lcmp = 0; g_lcmp_max = 0;
        now = 1000u;
        for (uint32_t i = 0; i < n; i++) l_add(&g_lt[i], now + 1u + rnd() % DELTA);
        g_lcmp = 0; g_lcmp_max = 0;
        double t2 = now_ns();
        for (uint32_t i = 0; i < pairs; i++) { lt_t *t = l_pop(); now = t->expires; l_add(t, now + 1u + rnd() % DELTA); }
        double t3 = now_ns();
        tr[rep] = (t1 - t0) / pairs; tl[rep] = (t3 - t2) / pairs;
        rc = g_rcmp; rm = g_rcmp_max; lc = g_lcmp; lm = g_lcmp_max;
    }
    qsort(tr, REP, sizeof(double), cmpd); qsort(tl, REP, sizeof(double), cmpd);
    printf("%5u | %7.1f %5llu | %8.1f %5llu | %7.1f [%6.1f,%6.1f] | %8.1f [%7.1f,%7.1f]\n", (unsigned)n,
           (double)rc / pairs, (unsigned long long)rm, (double)lc / pairs, (unsigned long long)lm,
           tr[REP / 2], tr[0], tr[REP - 1], tl[REP / 2], tl[0], tl[REP - 1]);
}
int main(void) {
    printf("sizeof: RB timer %zu, list timer %zu bytes (64-bit)\n", sizeof(rt_t), sizeof(lt_t));
    printf("    n | RB cmp/add  max | list cmp/add   max |  RB ns/pair [min,max]   | list ns/pair [min,max]\n");
    static const uint32_t ns[] = { 4u, 8u, 16u, 32u, 64u, 128u, 256u, 1024u, 4096u };
    for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; i++) one(ns[i]);
    return 0;
}
