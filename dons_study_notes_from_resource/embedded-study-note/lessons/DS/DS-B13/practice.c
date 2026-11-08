/* DS-B13 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 128

typedef struct Node { int key; struct Node *left, *right; } Node;
typedef struct { Node *root; int count; } Bst;

/* 정적 풀 (본문과 같음) */
static Node g_pool[POOL_N], *g_free;
static int g_used;
static void pool_init(void) {
    for (int i = 0; i < POOL_N - 1; i++) g_pool[i].left = &g_pool[i + 1];
    g_pool[POOL_N - 1].left = NULL;
    g_free = &g_pool[0]; g_used = 0;
}
static Node *node_alloc(int key) {
    Node *n = g_free;
    if (!n) return NULL;
    g_free = n->left; n->key = key; n->left = n->right = NULL; g_used++;
    return n;
}
static void node_free(Node *n) { n->right = NULL; n->left = g_free; g_free = n; g_used--; }

static bool bst_insert(Bst *t, int key) {
    Node **link = &t->root;
    while (*link) {
        if (key == (*link)->key) return false;
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node *n = node_alloc(key);
    if (!n) return false;
    *link = n; t->count++;
    return true;
}
static Node *bst_find(const Bst *t, int key) {
    Node *n = t->root;
    while (n && n->key != key) n = key < n->key ? n->left : n->right;
    return n;
}
static int height(const Node *n) {
    if (!n) return 0;
    int l = height(n->left), r = height(n->right);
    return 1 + (l > r ? l : r);
}
static void reset(Bst *t) { pool_init(); t->root = NULL; t->count = 0; }

/* 연습 1: 재귀 없는 inorder — 정적 스택. "왼쪽 끝까지 push → pop 해서 방문 → 오른쪽으로" */
static int inorder_iter(const Bst *t, int *out) {
    const Node *stack[POOL_N];
    int sp = 0, k = 0;
    const Node *n = t->root;
    while (n || sp > 0) {
        while (n) { stack[sp++] = n; n = n->left; }
        n = stack[--sp];
        out[k++] = n->key;
        n = n->right;
    }
    return k;
}

/* 연습 2: 범위 질의 [lo, hi] — 범위 밖 서브트리는 아예 내려가지 않는다 (가지치기) */
static int g_visited;
static void range_rec(const Node *n, int lo, int hi, int *out, int *k) {
    if (!n) return;
    g_visited++;
    if (lo < n->key) range_rec(n->left, lo, hi, out, k);       /* 왼쪽에 lo 이상이 있을 수 있음 */
    if (lo <= n->key && n->key <= hi) out[(*k)++] = n->key;
    if (n->key < hi) range_rec(n->right, lo, hi, out, k);      /* 오른쪽에 hi 이하가 있을 수 있음 */
}
static int bst_range(const Bst *t, int lo, int hi, int *out) {
    int k = 0;
    g_visited = 0;
    range_rec(t->root, lo, hi, out, &k);
    return k;
}

/* 연습 3: floor — x 이하인 가장 큰 키 (FTL extent map: "LBA x 는 어느 extent 에 속하나?") */
static bool bst_floor(const Bst *t, int x, int *res) {
    const Node *n = t->root;
    bool found = false;
    while (n) {
        if (n->key == x) { *res = x; return true; }
        if (n->key < x) { *res = n->key; found = true; n = n->right; }  /* 후보, 더 큰 후보를 찾아 오른쪽 */
        else n = n->left;
    }
    return found;
}

/* 연습 4: 키 복사 없이 후계자 "노드"를 옮겨 다는 삭제 — 노드 주소가 외부 핸들일 때 필요 */
static bool bst_delete_relink(Bst *t, int key) {
    Node **link = &t->root;
    while (*link && (*link)->key != key)
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    Node *n = *link;
    if (!n) return false;
    if (n->left && n->right) {
        Node **s = &n->right;
        while ((*s)->left) s = &(*s)->left;
        Node *succ = *s;
        *s = succ->right;          /* ① 후계자를 원래 자리에서 떼어낸다 (먼저!) */
        succ->left = n->left;      /* ② n 의 자식을 물려받는다 */
        succ->right = n->right;    /*    (succ 가 n->right 였다면 ①에서 n->right 가 이미 갱신됨) */
        *link = succ;              /* ③ n 자리에 후계자 노드를 단다 */
    } else {
        *link = n->left ? n->left : n->right;
    }
    node_free(n);
    t->count--;
    return true;
}

/* 연습 5: 정렬 배열 → 높이 최소 BST (가운데를 루트로, 재귀 분할) */
static Node *build_balanced(const int *a, int lo, int hi) {   /* [lo, hi) */
    if (lo >= hi) return NULL;
    int mid = lo + (hi - lo) / 2;
    Node *n = node_alloc(a[mid]);
    n->left = build_balanced(a, lo, mid);
    n->right = build_balanced(a, mid + 1, hi);
    return n;
}

static bool same(const int *a, const int *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    Bst t;
    int out[POOL_N];
    static const int K[] = {50, 30, 70, 20, 40, 60, 80, 35, 45, 65};

    /* 연습 1 */
    reset(&t);
    for (int i = 0; i < 10; i++) bst_insert(&t, K[i]);
    static const int IN[] = {20, 30, 35, 40, 45, 50, 60, 65, 70, 80};
    CHECK(inorder_iter(&t, out) == 10 && same(out, IN, 10));
    reset(&t);
    for (int i = 0; i < POOL_N; i++) bst_insert(&t, i);         /* 정렬 삽입 → 높이 128 */
    int n = inorder_iter(&t, out);
    bool sorted = n == POOL_N;
    for (int i = 0; i < n; i++) if (out[i] != i) sorted = false;
    CHECK(sorted && height(t.root) == POOL_N);

    /* 연습 2 */
    reset(&t);
    for (int i = 0; i < 10; i++) bst_insert(&t, K[i]);
    static const int R[] = {35, 40, 45, 50};
    CHECK(bst_range(&t, 33, 52, out) == 4 && same(out, R, 4));
    CHECK(g_visited < t.count);                                  /* 전부 돌지 않았다 */
    CHECK(bst_range(&t, 81, 99, out) == 0);

    /* 연습 3 — extent 시작 LBA 들 */
    reset(&t);
    static const int EXT[] = {400, 100, 0, 250, 700};
    for (int i = 0; i < 5; i++) bst_insert(&t, EXT[i]);
    int f = -1;
    CHECK(bst_floor(&t, 260, &f) && f == 250);
    CHECK(bst_floor(&t, 400, &f) && f == 400);
    CHECK(bst_floor(&t, 9999, &f) && f == 700);
    reset(&t);
    bst_insert(&t, 10);
    CHECK(bst_floor(&t, 5, &f) == false);                        /* 더 작은 키 없음 */

    /* 연습 4 — 노드 주소가 살아남는지 */
    reset(&t);
    for (int i = 0; i < 10; i++) bst_insert(&t, K[i]);
    Node *p35 = bst_find(&t, 35), *p80 = bst_find(&t, 80);
    CHECK(bst_delete_relink(&t, 30));                            /* 후계자 35 가 깊은 곳에 */
    CHECK(bst_find(&t, 35) == p35 && bst_find(&t, 30) == NULL);
    bst_delete_relink(&t, 60);
    CHECK(bst_delete_relink(&t, 70));                            /* 후계자 80 = 바로 오른쪽 자식 */
    CHECK(bst_find(&t, 80) == p80 && p80->left != NULL && p80->left->key == 65);
    CHECK(inorder_iter(&t, out) == 7 && t.count == 7 && g_used == 7);
    static const int IN4[] = {20, 35, 40, 45, 50, 65, 80};
    CHECK(same(out, IN4, 7));

    /* 연습 5 */
    reset(&t);
    int a[127];
    for (int i = 0; i < 127; i++) a[i] = i * 2;
    t.root = build_balanced(a, 0, 127);
    t.count = 127;
    CHECK(height(t.root) == 7 && g_used == 127);                 /* 2^7 - 1 = 127 → 완전 이진 트리 */
    CHECK(inorder_iter(&t, out) == 127 && out[0] == 0 && out[126] == 252);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
