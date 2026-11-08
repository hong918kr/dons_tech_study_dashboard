/* DS-M07 추가 연습 — 한 번에 검증
 * 본문 AVL(정적 풀) + size 필드(연습 4). fix_height 가 높이와 size 를 함께 갱신한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 4096

typedef struct Node {
    struct Node *left, *right;
    int32_t key;
    int32_t size;    /* 서브트리 노드 수 (연습 4: 순위 통계) */
    int8_t  h;
} Node;
typedef struct { Node *root; int count; } Avl;

static Node g_pool[POOL_N], *g_free;
static int  g_used;
static long g_rot;
static void pool_init(void) {
    for (int i = 0; i < POOL_N - 1; i++) g_pool[i].left = &g_pool[i + 1];
    g_pool[POOL_N - 1].left = NULL;
    g_free = &g_pool[0]; g_used = 0;
}
static Node *node_alloc(int32_t key) {
    Node *n = g_free;
    if (!n) return NULL;
    g_free = n->left;
    n->key = key; n->h = 1; n->size = 1; n->left = n->right = NULL; g_used++;
    return n;
}
static void node_free(Node *n) { n->right = NULL; n->left = g_free; g_free = n; g_used--; }
static void avl_reset(Avl *t) { pool_init(); t->root = NULL; t->count = 0; }

/* ---------------- 본문 AVL (size 갱신만 추가) ---------------- */
static int  ht(const Node *n) { return n ? n->h : 0; }
static int  sz(const Node *n) { return n ? n->size : 0; }
static int  imax(int a, int b) { return a > b ? a : b; }
static void fix_height(Node *n) {
    n->h = (int8_t)(1 + imax(ht(n->left), ht(n->right)));
    n->size = 1 + sz(n->left) + sz(n->right);      /* 회전해도 이 한 줄이면 size 가 맞는다 */
}
static int  bf(const Node *n) { return ht(n->left) - ht(n->right); }
static Node *rot_right(Node *y) {
    g_rot++;
    Node *x = y->left; y->left = x->right; x->right = y;
    fix_height(y); fix_height(x);
    return x;
}
static Node *rot_left(Node *x) {
    g_rot++;
    Node *y = x->right; x->right = y->left; y->left = x;
    fix_height(x); fix_height(y);
    return y;
}
static Node *rebalance(Node *n) {
    fix_height(n);
    int b = bf(n);
    if (b > 1)  { if (bf(n->left) < 0)  n->left  = rot_left(n->left);   return rot_right(n); }
    if (b < -1) { if (bf(n->right) > 0) n->right = rot_right(n->right); return rot_left(n); }
    return n;
}
static Node *insert_rec(Node *n, int32_t key, bool *added) {
    if (!n) { Node *m = node_alloc(key); *added = (m != NULL); return m; }
    if (key < n->key)      n->left  = insert_rec(n->left, key, added);
    else if (key > n->key) n->right = insert_rec(n->right, key, added);
    else return n;
    return *added ? rebalance(n) : n;
}
static bool avl_insert(Avl *t, int32_t key) {
    bool added = false;
    t->root = insert_rec(t->root, key, &added);
    if (added) t->count++;
    return added;
}
static Node *detach_min(Node *n, Node **min) {
    if (!n->left) { *min = n; return n->right; }
    n->left = detach_min(n->left, min);
    return rebalance(n);
}
static Node *delete_rec(Node *n, int32_t key, bool *removed) {
    if (!n) return NULL;
    if (key < n->key)      n->left  = delete_rec(n->left, key, removed);
    else if (key > n->key) n->right = delete_rec(n->right, key, removed);
    else {
        *removed = true;
        Node *l = n->left, *r = n->right, *s;
        node_free(n);
        if (!l) return r;
        if (!r) return l;
        r = detach_min(r, &s);
        s->left = l; s->right = r;
        return rebalance(s);
    }
    return *removed ? rebalance(n) : n;
}
static bool avl_delete(Avl *t, int32_t key) {
    bool removed = false;
    t->root = delete_rec(t->root, key, &removed);
    if (removed) t->count--;
    return removed;
}
/* 검사기: 순서 + |bf|<=1 + 저장된 h + 저장된 size */
static int check_rec(const Node *n, int64_t lo, int64_t hi, int *cnt) {
    if (!n) { *cnt = 0; return 0; }
    if (n->key <= lo || n->key >= hi) return -1;
    int cl, cr;
    int l = check_rec(n->left, lo, n->key, &cl), r = check_rec(n->right, n->key, hi, &cr);
    if (l < 0 || r < 0 || l - r > 1 || r - l > 1) return -1;
    *cnt = 1 + cl + cr;
    if (n->size != *cnt) return -1;
    int h = 1 + imax(l, r);
    return n->h == h ? h : -1;
}
static bool avl_check(const Avl *t) {
    int c;
    return check_rec(t->root, (int64_t)INT32_MIN - 1, (int64_t)INT32_MAX + 1, &c) >= 0 && c == t->count;
}
static void pre_rec(const Node *n, int32_t *out, int *k) {
    if (!n) return;
    out[(*k)++] = n->key; pre_rec(n->left, out, k); pre_rec(n->right, out, k);
}
static int preorder(const Node *n, int32_t *out) { int k = 0; pre_rec(n, out, &k); return k; }
static bool same(const int32_t *a, const int32_t *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x;
}

/* ---------------- 연습 1: 최소 AVL 트리 (Fibonacci tree) ---------------- */
/* 높이 h 인 AVL 중 노드가 가장 적은 트리: 왼쪽 = 높이 h-1 최소 트리, 오른쪽 = 높이 h-2 최소 트리.
 * 키는 inorder 순서로 1, 2, 3, ... 을 매긴다. */
static Node *build_fib(int h, int32_t *next) {
    if (h <= 0) return NULL;
    Node *l = build_fib(h - 1, next);
    Node *n = node_alloc((*next)++);
    n->left = l;
    n->right = build_fib(h - 2, next);
    fix_height(n);
    return n;
}

/* ---------------- 연습 3: 재귀 없는 삽입 (링크 경로 스택) ---------------- */
#define AVL_MAX_H 48     /* int32 키 → 노드 ≤ 2^32 → AVL 높이 < 1.4404·log2(2^32+2) − 0.328 ≈ 45.8 */
static bool avl_insert_iter(Avl *t, int32_t key) {
    Node **path[AVL_MAX_H];          /* 루트에서 내려온 "링크의 주소" — 회전 결과를 그 링크에 다시 쓴다 */
    int depth = 0;
    Node **link = &t->root;
    while (*link) {
        if (key == (*link)->key) return false;
        path[depth++] = link;
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node *m = node_alloc(key);
    if (!m) return false;
    *link = m;
    t->count++;
    while (depth > 0) {              /* 아래에서 위로 */
        Node **p = path[--depth];
        *p = rebalance(*p);
    }
    return true;
}

/* ---------------- 연습 4: 순위 통계 (select / rank) ---------------- */
/* k 번째로 작은 키 (0-based). 없으면 NULL */
static const Node *avl_select(const Avl *t, int k) {
    const Node *n = t->root;
    while (n) {
        int ls = sz(n->left);
        if (k < ls) n = n->left;
        else if (k == ls) return n;
        else { k -= ls + 1; n = n->right; }
    }
    return NULL;
}
/* key 보다 작은 키의 개수 */
static int avl_rank(const Avl *t, int32_t key) {
    const Node *n = t->root;
    int r = 0;
    while (n) {
        if (key <= n->key) n = n->left;
        else { r += sz(n->left) + 1; n = n->right; }
    }
    return r;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int32_t g_a[POOL_N], g_b[POOL_N];

int main(void) {
    Avl t;

    /* 연습 1 — N(h) = F(h+2) - 1, 모든 내부 노드 bf = +1, 그래도 AVL */
    printf("[1]  h | N(h) min nodes | F(h+2)-1 | check\n");
    long F[24] = {0, 1};
    for (int i = 2; i < 24; i++) F[i] = F[i - 1] + F[i - 2];
    bool ok1 = true;
    for (int h = 1; h <= 16; h++) {
        avl_reset(&t);
        int32_t next = 1;
        t.root = build_fib(h, &next);
        t.count = next - 1;
        bool v = avl_check(&t);
        if (h == 1 || h == 2 || h == 3 || h == 4 || h == 8 || h == 12 || h == 16)
            printf("[1] %2d | %14d | %8ld | %s\n", h, t.count, F[h + 2] - 1, v ? "ok" : "BAD");
        ok1 = ok1 && v && t.count == F[h + 2] - 1;
    }
    CHECK(ok1);

    /* 연습 2 — Fibonacci tree 에서 삭제 한 번에 회전이 몇 번까지 일어나나 */
    printf("[2]  h | nodes | max rotations in one delete (key) | after: height\n");
    long worst16 = 0;
    for (int h = 4; h <= 16; h += 4) {
        int nodes = (int)(F[h + 2] - 1);
        long best = -1; int32_t best_key = 0; int after_h = 0;
        for (int32_t k = 1; k <= nodes; k++) {         /* 키마다 새로 만들어 하나만 지워 본다 */
            avl_reset(&t);
            int32_t next = 1;
            t.root = build_fib(h, &next); t.count = nodes;
            g_rot = 0;
            bool ok = avl_delete(&t, k) && avl_check(&t);
            if (!ok) { best = -999; break; }
            if (g_rot > best) { best = g_rot; best_key = k; after_h = ht(t.root); }
        }
        printf("[2] %2d | %5d | %8ld single rotations (key %d) | %d\n", h, nodes, best, best_key, after_h);
        if (h == 16) worst16 = best;
    }
    CHECK(worst16 >= 6);    /* 삽입은 최대 2 (이중 회전 1번), 삭제는 경로 길이에 비례 */

    /* 연습 3 — 재귀 없는 삽입이 재귀 버전과 같은 모양을 만드는가 */
    bool ok3 = true;
    for (int trial = 0; trial < 30; trial++) {
        Avl a, b;
        pool_init();                                   /* 풀 하나를 두 트리가 나눠 쓴다 */
        a.root = b.root = NULL; a.count = b.count = 0;
        for (int i = 0; i < 1000; i++) {
            int32_t key = (int32_t)(xorshift32() % 3000u);
            bool ra = avl_insert(&a, key), rb = avl_insert_iter(&b, key);
            if (ra != rb) ok3 = false;
        }
        int na = preorder(a.root, g_a), nb = preorder(b.root, g_b);
        ok3 = ok3 && na == nb && same(g_a, g_b, na) && avl_check(&a) && avl_check(&b);
    }
    avl_reset(&t);
    for (int32_t k = 0; k < 4095; k++) avl_insert_iter(&t, k);
    printf("[3] iterative insert: 30 random trials same preorder as recursive=%s, 0..4094 sorted -> height %d\n",
           ok3 ? "yes" : "NO", ht(t.root));
    CHECK(ok3 && ht(t.root) == 12 && avl_check(&t));

    /* 연습 4 — select / rank vs 정렬 참조 모델, 랜덤 삽입·삭제 20000 번 */
    enum { KEYS = 2000 };
    static bool ref[KEYS];
    int bad = 0;
    avl_reset(&t);
    for (int op = 0; op < 20000; op++) {
        uint32_t r = xorshift32();
        int32_t key = (int32_t)(r % KEYS);
        if (r & 0x100000u) { avl_insert(&t, key); ref[key] = true; }
        else               { avl_delete(&t, key); ref[key] = false; }
        if (op % 50 == 0) {                            /* 50 번마다 전수 대조 (O(KEYS)) */
            int m = 0;
            for (int k = 0; k < KEYS; k++) {
                if (avl_rank(&t, k) != m) bad++;       /* rank(k) = k 보다 작은 키 수 */
                if (ref[k]) {
                    const Node *s = avl_select(&t, m);
                    if (!s || s->key != k) bad++;
                    m++;
                }
            }
            if (avl_select(&t, m) != NULL || !avl_check(&t) || m != t.count) bad++;
        }
    }
    printf("[4] select/rank vs sorted reference: size=%d bad=%d\n", t.count, bad);
    CHECK(bad == 0 && t.count > 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
