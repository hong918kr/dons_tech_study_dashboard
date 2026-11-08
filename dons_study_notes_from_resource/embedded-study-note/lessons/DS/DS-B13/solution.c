/* DS-B13 이진 탐색 트리: 삽입·삭제·순회
 * 정적 노드 풀(malloc 없음) 위의 int 키 BST.
 */
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 128

typedef struct Node {
    int key;
    struct Node *left, *right;
} Node;

/* ---------------- 정적 노드 풀 (주어짐) ---------------- */
static Node  g_pool[POOL_N];
static Node *g_free;   /* free list — 빈 노드는 left 포인터로 줄줄이 연결 */
static int   g_used;   /* 사용 중인 노드 수 (누수 검사용) */

static void pool_init(void) {
    for (int i = 0; i < POOL_N - 1; i++) g_pool[i].left = &g_pool[i + 1];
    g_pool[POOL_N - 1].left = NULL;
    g_free = &g_pool[0];
    g_used = 0;
}
static Node *node_alloc(int key) {
    Node *n = g_free;
    if (!n) return NULL;                 /* 풀 고갈 */
    g_free = n->left;
    n->key = key;
    n->left = n->right = NULL;
    g_used++;
    return n;
}
static void node_free(Node *n) {
    n->right = NULL;
    n->left = g_free;
    g_free = n;
    g_used--;
}

typedef struct { Node *root; int count; } Bst;

/* ---------------- Part A: find / insert / delete ---------------- */
static Node *bst_find(const Bst *t, int key) {
    Node *n = t->root;
    while (n && n->key != key)
        n = key < n->key ? n->left : n->right;
    return n;
}

/* 새 키면 true. 중복 키이거나 풀이 비었으면 false (트리 불변). */
static bool bst_insert(Bst *t, int key) {
    Node **link = &t->root;              /* "나를 가리키는 포인터"의 주소 */
    while (*link) {
        if (key == (*link)->key) return false;
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node *n = node_alloc(key);
    if (!n) return false;
    *link = n;
    t->count++;
    return true;
}

/* 지웠으면 true, 키가 없으면 false. 자식 0/1/2개 세 경우를 모두 처리. */
static bool bst_delete(Bst *t, int key) {
    Node **link = &t->root;
    while (*link && (*link)->key != key)
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node *n = *link;
    if (!n) return false;
    if (n->left && n->right) {           /* 경우 3: 자식 2개 → 후계자로 바꿔치기 */
        Node **s = &n->right;            /* 오른쪽 서브트리의 최솟값 = 후계자 */
        while ((*s)->left) s = &(*s)->left;
        Node *succ = *s;
        n->key = succ->key;              /* 키만 복사하고 */
        link = s;                        /* 지울 대상을 후계자 노드로 바꾼다 */
        n = succ;                        /* (후계자는 왼쪽 자식이 없다 → 경우 1·2) */
    }
    *link = n->left ? n->left : n->right;  /* 경우 1·2: 자식(또는 NULL)을 부모에 이어붙임 */
    node_free(n);
    t->count--;
    return true;
}

/* ---------------- Part B: 순회 3종 ---------------- */
/* inorder: 재귀 (왼쪽 → 나 → 오른쪽) → 정렬된 순서 */
static void inorder_rec(const Node *n, int *out, int *k) {
    if (!n) return;
    inorder_rec(n->left, out, k);
    out[(*k)++] = n->key;
    inorder_rec(n->right, out, k);
}
static int bst_inorder(const Bst *t, int *out) {
    int k = 0;
    inorder_rec(t->root, out, &k);
    return k;
}

/* preorder: 반복 + 정적 배열 스택 (나 → 왼쪽 → 오른쪽) */
static int bst_preorder(const Bst *t, int *out) {
    const Node *stack[POOL_N];
    int sp = 0, k = 0;
    if (t->root) stack[sp++] = t->root;
    while (sp > 0) {
        const Node *n = stack[--sp];
        out[k++] = n->key;
        if (n->right) stack[sp++] = n->right;  /* 오른쪽을 먼저 넣어야 왼쪽이 먼저 나온다 */
        if (n->left)  stack[sp++] = n->left;
    }
    return k;
}

/* level-order: 정적 배열 큐 (BFS, 위 → 아래, 왼 → 오) */
static int bst_levelorder(const Bst *t, int *out) {
    const Node *q[POOL_N];
    int head = 0, tail = 0, k = 0;
    if (t->root) q[tail++] = t->root;
    while (head < tail) {
        const Node *n = q[head++];
        out[k++] = n->key;
        if (n->left)  q[tail++] = n->left;
        if (n->right) q[tail++] = n->right;
    }
    return k;
}

/* ---------------- Part C: 유효성 검사 (min/max 범위) ---------------- */
/* 모든 노드가 열린 구간 (lo, hi) 안에 있어야 한다. int64_t 로 INT_MIN/INT_MAX 키도 안전. */
static bool valid_rec(const Node *n, int64_t lo, int64_t hi) {
    if (!n) return true;
    if (n->key <= lo || n->key >= hi) return false;
    return valid_rec(n->left, lo, n->key) && valid_rec(n->right, n->key, hi);
}
static bool bst_valid(const Bst *t) {
    return valid_rec(t->root, (int64_t)INT_MIN - 1, (int64_t)INT_MAX + 1);
}

/* ---------------- 도우미 (주어짐) ---------------- */
static int height(const Node *n) {   /* 노드 개수 기준 높이: 빈 트리 0, 노드 1개 1 */
    if (!n) return 0;
    int l = height(n->left), r = height(n->right);
    return 1 + (l > r ? l : r);
}
static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static bool same(const int *a, const int *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
static void build(Bst *t, const int *keys, int n) {
    pool_init();
    t->root = NULL; t->count = 0;
    for (int i = 0; i < n; i++) bst_insert(t, keys[i]);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    Bst t;
    int out[POOL_N];
    static const int K[] = {50, 30, 70, 20, 40, 60, 80, 35, 45, 65};

    /* Part A — find / insert */
    build(&t, K, 10);
    CHECK(t.count == 10 && g_used == 10);
    CHECK(bst_find(&t, 45) != NULL && bst_find(&t, 45)->key == 45);
    CHECK(bst_find(&t, 99) == NULL);
    CHECK(bst_insert(&t, 40) == false && t.count == 10);       /* 중복 거부 */

    /* Part B — 순회 3종 */
    static const int IN[]  = {20, 30, 35, 40, 45, 50, 60, 65, 70, 80};
    static const int PRE[] = {50, 30, 20, 40, 35, 45, 70, 60, 65, 80};
    static const int LVL[] = {50, 30, 70, 20, 40, 60, 80, 35, 45, 65};
    CHECK(bst_inorder(&t, out) == 10 && same(out, IN, 10));
    CHECK(bst_preorder(&t, out) == 10 && same(out, PRE, 10));
    CHECK(bst_levelorder(&t, out) == 10 && same(out, LVL, 10));

    /* Part A — delete 세 경우 (매번 preorder 로 모양까지 확인) */
    CHECK(bst_delete(&t, 30));                                  /* 자식 2개: 후계자 35 */
    static const int D1[] = {50, 35, 20, 40, 45, 70, 60, 65, 80};
    CHECK(bst_preorder(&t, out) == 9 && same(out, D1, 9));
    CHECK(bst_delete(&t, 20));                                  /* 자식 0개 (리프) */
    CHECK(bst_delete(&t, 60));                                  /* 자식 1개 (오른쪽 65) */
    static const int D2[] = {50, 35, 40, 45, 70, 65, 80};
    CHECK(bst_preorder(&t, out) == 7 && same(out, D2, 7));
    CHECK(bst_delete(&t, 50));                                  /* 루트, 자식 2개: 후계자 65 */
    static const int D3[] = {65, 35, 40, 45, 70, 80};
    CHECK(bst_preorder(&t, out) == 6 && same(out, D3, 6));
    bst_insert(&t, 68);
    CHECK(bst_delete(&t, 70));                     /* 후계자(80)가 바로 오른쪽 자식인 경우 */
    static const int D4[] = {65, 35, 40, 45, 80, 68};
    CHECK(bst_preorder(&t, out) == 6 && same(out, D4, 6));
    static const int L4[] = {65, 35, 80, 40, 68, 45};
    CHECK(bst_levelorder(&t, out) == 6 && same(out, L4, 6));
    CHECK(bst_delete(&t, 99) == false && t.count == 6);         /* 없는 키 */
    CHECK(t.count == g_used);                                   /* 풀 누수 없음 */

    /* Part C — 유효성 검사: 지역적으로는 맞지만 전역적으로 틀린 트리 */
    CHECK(bst_valid(&t));
    Node a = {10, NULL, NULL}, b = {5, NULL, NULL}, c = {15, NULL, NULL}, d = {12, NULL, NULL};
    a.left = &b; a.right = &c; b.right = &d;                    /* 12 가 10 의 왼쪽 서브트리에! */
    Bst bad = {&a, 4};
    CHECK(bst_valid(&bad) == false);
    b.right = NULL; c.left = &d;                                /* 올바른 위치로 옮기면 OK */
    CHECK(bst_valid(&bad) == true);
    Node m = {INT_MIN, NULL, NULL}, x = {INT_MAX, &m, NULL};
    Bst edge = {&x, 2};
    CHECK(bst_valid(&edge) == true);                            /* 극단 키도 통과 */

    /* Part C — 랜덤 삽입·삭제 스트레스 (참조 모델과 비교) */
    enum { KEYS = 100, OPS = 5000 };
    bool ref[KEYS] = {false};                                   /* 참조 모델: 키 존재 여부 */
    int want[KEYS], bad_ops = 0, max_n = 0;
    pool_init();
    t.root = NULL; t.count = 0;
    for (int op = 0; op < OPS; op++) {
        uint32_t r = xorshift32();
        int key = (int)(r % KEYS);
        if (r & 0x10000u) {                                     /* 삽입: 없던 키만 true */
            if (bst_insert(&t, key) == ref[key]) bad_ops++;
            ref[key] = true;
        } else {                                                /* 삭제: 있던 키만 true */
            if (bst_delete(&t, key) != ref[key]) bad_ops++;
            ref[key] = false;
        }
        int m = 0;
        for (int k = 0; k < KEYS; k++) if (ref[k]) want[m++] = k;
        int n = bst_inorder(&t, out);                           /* 매 연산 후: 정렬 순서 = 참조 */
        if (n != m || !same(out, want, m) || t.count != m || g_used != m || !bst_valid(&t)) bad_ops++;
        if (m > max_n) max_n = m;
    }
    printf("  random ops=%d  max_size=%d  final_size=%d  bad_ops=%d\n", OPS, max_n, t.count, bad_ops);
    CHECK(max_n > 0 && bad_ops == 0);

    /* 측정 — 삽입 순서가 높이를 결정한다 */
    printf("  N | sorted | random(avg of 200) | min=ceil(log2(N+1))\n");
    static const int NS[] = {15, 63, 127};
    for (int s = 0; s < 3; s++) {
        int n = NS[s], perm[POOL_N], sum = 0, minh = 0;
        while ((1 << minh) - 1 < n) minh++;
        for (int i = 0; i < n; i++) perm[i] = i;
        build(&t, perm, n);
        int hs = height(t.root);
        for (int trial = 0; trial < 200; trial++) {
            for (int i = n - 1; i > 0; i--) {                  /* Fisher–Yates 셔플 */
                int j = (int)(xorshift32() % (uint32_t)(i + 1)), tmp = perm[i];
                perm[i] = perm[j]; perm[j] = tmp;
            }
            build(&t, perm, n);
            sum += height(t.root);
        }
        printf("%3d | %6d | %18.1f | %d\n", n, hs, sum / 200.0, minh);
        if (n == 127) CHECK(hs == 127);                        /* 정렬 삽입 = 연결 리스트 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
