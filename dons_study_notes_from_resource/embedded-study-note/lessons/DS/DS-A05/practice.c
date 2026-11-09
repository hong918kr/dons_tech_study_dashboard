/* DS-A05 추가 연습 — 한 번에 검증
 * 트리 본체는 solution.c 와 같은 알고리즘이다 (이 파일만으로 빌드되도록 다시 실었다).
 *   연습 1  가장 키 큰 RB 트리를 직접 만들어 높이 상한이 실제로 닿는지 본다
 *   연습 2  rb_replace_node: 재균형 없이 객체 교체 O(1)
 *   연습 3  rb_first_postorder / rb_next_postorder: 재귀·재균형 없이 전체 해제
 *   연습 4  한 객체, 두 트리 (id 색인 + 만료 시각 색인)
 *   연습 5  lower_bound + 구간 순회, rb_next 전체 이동 수 ≤ 2(n-1)
 *   연습 6  packed 컨테이너를 컴파일 시점에 거절하는 매크로
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
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

static struct rb_node *rb_first(const struct rb_root *r) { struct rb_node *n = r->rb_node; if (n) while (n->rb_left) n = n->rb_left; return n; }

typedef struct item { int32_t key; uint32_t tag; struct rb_node node; } item_t;
#define item_of(n) container_of(n, item_t, node)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static bool item_insert(struct rb_root *root, item_t *it) {
    struct rb_node **link = &root->rb_node, *parent = NULL;
    while (*link) {
        parent = *link;
        int32_t k = item_of(parent)->key;
        if (it->key < k)      link = &parent->rb_left;
        else if (it->key > k) link = &parent->rb_right;
        else                  return false;
    }
    rb_link_node(&it->node, parent, link);
    rb_insert_color(&it->node, root);
    return true;
}
/* 검사기: 검정 높이 반환, 위반이면 -1. RB 불변식 + BST 순서 + 부모 링크 + 개수. */
static int vrec(const struct rb_node *n, const struct rb_node *p, int64_t *prev, size_t *seen, int64_t (*key)(const struct rb_node *)) {
    if (!n) return 0;
    if (rb_parent(n) != p || (p && rb_is_red(n) && rb_is_red(p))) return -1;
    (*seen)++;
    int l = vrec(n->rb_left, n, prev, seen, key);
    if (l < 0 || key(n) <= *prev) return -1;
    *prev = key(n);
    int r = vrec(n->rb_right, n, prev, seen, key);
    if (r < 0 || l != r) return -1;
    return l + (rb_is_black(n) ? 1 : 0);
}
static bool valid(const struct rb_root *t, size_t n, int64_t (*key)(const struct rb_node *)) {
    int64_t prev = INT64_MIN; size_t seen = 0;
    if (t->rb_node && rb_is_red(t->rb_node)) return false;
    return vrec(t->rb_node, NULL, &prev, &seen, key) >= 0 && seen == n;
}
static int64_t item_k(const struct rb_node *n) { return item_of(n)->key; }
static int height(const struct rb_node *n) {
    if (!n) return 0;
    int l = height(n->rb_left), r = height(n->rb_right);
    return 1 + (l > r ? l : r);
}

/* ---------- 연습 1: 가장 키 큰 트리 ---------- */
#define TALL_MAX 4096
static item_t g_t[TALL_MAX];
static int g_tn, g_tkey;
static struct rb_node *mk(unsigned c, struct rb_node *l, struct rb_node *r, int key) {
    item_t *it = &g_t[g_tn++];
    it->key = key;
    struct rb_node *n = &it->node;
    n->parent_color = c; n->rb_left = l; n->rb_right = r;
    if (l) rb_set_parent(l, n);
    if (r) rb_set_parent(r, n);
    return n;
}
static struct rb_node *perfect_black(int b) {           /* 검정 높이 b 의 완전 검정 트리: 2^b - 1 개 */
    if (b == 0) return NULL;
    struct rb_node *l = perfect_black(b - 1);
    int k = g_tkey++;
    struct rb_node *r = perfect_black(b - 1);
    return mk(RB_BLACK, l, r, k);
}
static struct rb_node *tall(int b) {                    /* 왼쪽 척추가 검-빨 교대, 나머지는 최소 노드 */
    if (b == 0) return NULL;
    struct rb_node *ll = tall(b - 1);
    int rk = g_tkey++;
    struct rb_node *lr = perfect_black(b - 1);
    struct rb_node *red = mk(RB_RED, ll, lr, rk);
    int k = g_tkey++;
    struct rb_node *r = perfect_black(b - 1);
    return mk(RB_BLACK, red, r, k);
}

/* ---------- 연습 2: rb_replace_node ---------- */
static void rb_replace_node(struct rb_node *victim, struct rb_node *nw, struct rb_root *root) {
    struct rb_node *parent = rb_parent(victim);
    *nw = *victim;                                      /* 부모+색, 양쪽 자식을 그대로 복사 */
    if (victim->rb_left)  rb_set_parent(victim->rb_left, nw);
    if (victim->rb_right) rb_set_parent(victim->rb_right, nw);
    rb_change_child(victim, nw, parent, root);
}

/* ---------- 연습 3: 후위 순회 ---------- */
static struct rb_node *rb_left_deepest(const struct rb_node *n) {
    for (;;) {
        if (n->rb_left)       n = n->rb_left;
        else if (n->rb_right) n = n->rb_right;
        else                  return (struct rb_node *)(uintptr_t)n;
    }
}
static struct rb_node *rb_first_postorder(const struct rb_root *r) { return r->rb_node ? rb_left_deepest(r->rb_node) : NULL; }
static struct rb_node *rb_next_postorder(const struct rb_node *n) {
    struct rb_node *p = rb_parent(n);
    if (p && n == p->rb_left && p->rb_right) return rb_left_deepest(p->rb_right);
    return p;
}

/* ---------- 연습 4: 한 객체, 두 트리 ---------- */
typedef struct conn {
    uint32_t id, deadline;
    struct rb_node by_id, by_deadline;
} conn_t;
#define CONN_N 200
static conn_t g_c[CONN_N];
static bool g_cin[CONN_N];
static int64_t conn_id(const struct rb_node *n) { return container_of(n, conn_t, by_id)->id; }
static int64_t conn_dl(const struct rb_node *n) {               /* 동률을 id 로 깨서 전순서 */
    const conn_t *c = container_of(n, conn_t, by_deadline);
    return ((int64_t)c->deadline << 32) | c->id;
}
static void conn_insert(struct rb_root *ids, struct rb_root *dls, conn_t *c) {
    struct rb_node **l = &ids->rb_node, *p = NULL;
    while (*l) { p = *l; l = c->id < container_of(p, conn_t, by_id)->id ? &p->rb_left : &p->rb_right; }
    rb_link_node(&c->by_id, p, l); rb_insert_color(&c->by_id, ids);
    int64_t k = ((int64_t)c->deadline << 32) | c->id;
    l = &dls->rb_node; p = NULL;
    while (*l) { p = *l; l = k < conn_dl(p) ? &p->rb_left : &p->rb_right; }
    rb_link_node(&c->by_deadline, p, l); rb_insert_color(&c->by_deadline, dls);
}
static conn_t *conn_find(const struct rb_root *ids, uint32_t id) {
    const struct rb_node *n = ids->rb_node;
    while (n) {
        conn_t *c = container_of(n, conn_t, by_id);
        if (id < c->id) n = n->rb_left; else if (id > c->id) n = n->rb_right; else return c;
    }
    return NULL;
}

/* ---------- 연습 5: lower_bound ---------- */
static struct rb_node *rb_lower_bound(const struct rb_root *root, int32_t key) {
    struct rb_node *n = root->rb_node, *best = NULL;
    while (n) {
        if (item_of(n)->key >= key) { best = n; n = n->rb_left; }
        else n = n->rb_right;
    }
    return best;
}

/* ---------- 연습 6: 컨테이너 정렬 검사 ---------- */
#define RB_CONTAINER_OK(T, m) \
    (_Alignof(T) >= _Alignof(struct rb_node) && offsetof(T, m) % _Alignof(struct rb_node) == 0)
struct good_t { char tag; struct rb_node node; };
struct __attribute__((packed)) packed_t { char tag; struct rb_node node; };
_Static_assert(RB_CONTAINER_OK(struct good_t, node), "natural layout is fine");
_Static_assert(!RB_CONTAINER_OK(struct packed_t, node), "packed layout must be rejected");

int main(void) {
    printf("== 연습 1: tallest valid tree, height = 2*bh, n = 2^(bh+1) - 2\n");
    {
        bool ok = true;
        for (int b = 1; b <= 10; b++) {
            g_tn = 0; g_tkey = 0;
            struct rb_root t = { tall(b) };
            int h = height(t.rb_node), n = g_tn, expect = (1 << (b + 1)) - 2;
            bool good = valid(&t, (size_t)n, item_k) && h == 2 * b && n == expect;
            ok = ok && good;
            if (b == 1 || b == 2 || b == 4 || b == 7 || b == 10) printf("bh=%2d  n=%5d  height=%2d\n", b, n, h);
        }
        CHECK(ok);
    }

    printf("== 연습 2: rb_replace_node\n");
    {
        static item_t it[128], nw;
        struct rb_root t = { NULL };
        for (int i = 0; i < 128; i++) { it[i].key = i * 2; RB_CLEAR_NODE(&it[i].node); item_insert(&t, &it[i]); }
        item_t *victim = &it[37];
        struct rb_node *vl = victim->node.rb_left, *vr = victim->node.rb_right;
        unsigned c = rb_color(&victim->node);
        nw.key = victim->key;
        rb_replace_node(&victim->node, &nw.node, &t);
        CHECK(valid(&t, 128, item_k));
        CHECK(nw.node.rb_left == vl && nw.node.rb_right == vr && rb_color(&nw.node) == c);
        CHECK((!vl || rb_parent(vl) == &nw.node) && (!vr || rb_parent(vr) == &nw.node));
        const struct rb_node *n = t.rb_node;
        while (n && item_of(n)->key != nw.key) n = nw.key < item_of(n)->key ? n->rb_left : n->rb_right;
        CHECK(n == &nw.node);
        /* 루트 교체 */
        static item_t nr;
        item_t *rt = item_of(t.rb_node);
        nr.key = rt->key;
        rb_replace_node(t.rb_node, &nr.node, &t);
        CHECK(t.rb_node == &nr.node && rb_parent(t.rb_node) == NULL && valid(&t, 128, item_k));
    }

    printf("== 연습 3: post-order teardown\n");
    {
        static item_t it[1024];
        static uint8_t freed[1024];
        struct rb_root t = { NULL };
        memset(freed, 0, sizeof freed);
        for (int i = 0; i < 1024; i++) { it[i].key = (i * 577) % 1024; RB_CLEAR_NODE(&it[i].node); item_insert(&t, &it[i]); }
        CHECK(valid(&t, 1024, item_k));
        int visited = 0; bool child_first = true;
        struct rb_node *n = rb_first_postorder(&t);
        while (n) {
            struct rb_node *next = rb_next_postorder(n);        /* 해제하기 전에 다음을 구한다 */
            if (n->rb_left  && !freed[item_of(n->rb_left)->key])  child_first = false;
            if (n->rb_right && !freed[item_of(n->rb_right)->key]) child_first = false;
            freed[item_of(n)->key] = 1;
            memset(n, 0xA5, sizeof *n);                         /* 해제 흉내: 내용을 망가뜨린다 */
            visited++;
            n = next;
        }
        CHECK(visited == 1024 && child_first);
        bool all = true;
        for (int i = 0; i < 1024; i++) all = all && freed[i];
        CHECK(all);
    }

    printf("== 연습 4: one object in two trees\n");
    {
        struct rb_root ids = { NULL }, dls = { NULL };
        size_t live = 0;
        bool ok = true;
        memset(g_cin, 0, sizeof g_cin);
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t i = rnd() % CONN_N;
            conn_t *c = &g_c[i];
            if (g_cin[i]) {
                conn_t *f = conn_find(&ids, i + 1000u);
                ok = ok && f == c;
                rb_erase(&c->by_id, &ids);
                rb_erase(&c->by_deadline, &dls);
                g_cin[i] = false; live--;
            } else {
                c->id = i + 1000u;
                c->deadline = rnd() % 64u;
                conn_insert(&ids, &dls, c);
                g_cin[i] = true; live++;
            }
            ok = ok && valid(&ids, live, conn_id) && valid(&dls, live, conn_dl);
        }
        CHECK(ok);
        /* 만료 순서로 걸으면서 by_id 쪽도 같은 객체인지 */
        uint32_t last = 0; bool sorted = true, same = true;
        for (const struct rb_node *n = rb_first(&dls); n; n = rb_next(n)) {
            const conn_t *c = container_of(n, conn_t, by_deadline);
            if (c->deadline < last) sorted = false;
            last = c->deadline;
            if (conn_find(&ids, c->id) != c) same = false;
        }
        CHECK(sorted && same);
    }

    printf("== 연습 5: lower_bound + range walk\n");
    {
        static item_t it[2048];
        static uint8_t in[8192];
        struct rb_root t = { NULL };
        memset(in, 0, sizeof in);
        int n = 0;
        for (int i = 0; i < 2048; i++) {
            it[i].key = (int32_t)(rnd() % 8192u);
            RB_CLEAR_NODE(&it[i].node);
            if (item_insert(&t, &it[i])) { in[it[i].key] = 1; n++; }
        }
        bool ok = true;
        for (int q = 0; q < 3000; q++) {
            int32_t lo = (int32_t)(rnd() % 8192u), hi = lo + (int32_t)(rnd() % 200u);
            long sum = 0, want = 0;
            for (const struct rb_node *w = rb_lower_bound(&t, lo); w && item_of(w)->key < hi; w = rb_next(w)) sum += item_of(w)->key;
            for (int32_t k = lo; k < hi && k < 8192; k++) if (in[k]) want += k;
            ok = ok && sum == want;
        }
        CHECK(ok);
        CHECK(rb_lower_bound(&t, 8192) == NULL && rb_lower_bound(&t, -1) == rb_first(&t));
        /* in-order 전체 이동 수: 간선 하나를 많아야 두 번 지난다 */
        long hops = 0;
        for (const struct rb_node *w = rb_first(&t), *nx; w; w = nx) {
            const struct rb_node *c = w;
            if (c->rb_right) {
                c = c->rb_right; hops++;
                while (c->rb_left) { c = c->rb_left; hops++; }
                nx = c;
            } else {
                const struct rb_node *p;
                while ((p = rb_parent(c)) && c == p->rb_right) { c = p; hops++; }
                if (p) hops++;
                nx = p;
            }
        }
        printf("n=%d full in-order walk: %ld pointer hops, 2(n-1)=%d\n", n, hops, 2 * (n - 1));
        CHECK(hops <= 2L * (n - 1));
    }

    printf("== 연습 6: packed container rejected at compile time\n");
    CHECK(RB_CONTAINER_OK(struct good_t, node));
    CHECK(!RB_CONTAINER_OK(struct packed_t, node));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
