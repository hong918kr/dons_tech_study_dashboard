/* DS-M07 AVL 트리: 회전과 균형 유지
 * 정적 노드 풀(malloc 없음) 위의 int32 키 AVL 트리.
 * Part A: 회전 4종 + 삽입 + 균형 검사기 / Part B: 삭제(후계자 노드 재연결) /
 * Part C: 순차 쓰기 익스텐트 맵(floor 조회) + 정렬 삽입 높이 측정 (AVL vs 일반 BST)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 4096

typedef struct Node {
    struct Node *left, *right;
    int32_t  key;
    uint32_t val;
    int8_t   h;      /* 노드 개수 기준 높이: 잎 1, 빈 트리 0 (DS-B13 과 같은 규약) */
} Node;

/* ---------------- 정적 노드 풀 (주어짐, DS-B13 과 같음) ---------------- */
static Node  g_pool[POOL_N];
static Node *g_free;
static int   g_used;

static void pool_init(void) {
    for (int i = 0; i < POOL_N - 1; i++) g_pool[i].left = &g_pool[i + 1];
    g_pool[POOL_N - 1].left = NULL;
    g_free = &g_pool[0];
    g_used = 0;
}
static Node *node_alloc(int32_t key, uint32_t val) {
    Node *n = g_free;
    if (!n) return NULL;
    g_free = n->left;
    n->key = key; n->val = val; n->h = 1;
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

typedef struct { Node *root; int count; } Avl;

static long g_rot;   /* 단일 회전 횟수 (측정용). 이중 회전 = 2 */

/* ---------------- 도우미 (주어짐) ---------------- */
static int  ht(const Node *n) { return n ? n->h : 0; }
static int  imax(int a, int b) { return a > b ? a : b; }
static void fix_height(Node *n) { n->h = (int8_t)(1 + imax(ht(n->left), ht(n->right))); }
static int  bf(const Node *n) { return ht(n->left) - ht(n->right); }   /* 균형 인자 */

/* ---------------- Part A: 회전 4종 + 삽입 + 검사기 ---------------- */
/*      y            x
 *     / \          / \
 *    x   C   →    A   y
 *   / \              / \
 *  A   B            B   C        새 서브트리 루트(x)를 반환 */
static Node *rot_right(Node *y) {
    g_rot++;
    Node *x = y->left;
    y->left = x->right;          /* B 를 y 의 왼쪽으로 */
    x->right = y;
    fix_height(y);               /* y 가 아래로 내려갔으니 먼저 */
    fix_height(x);
    return x;
}
static Node *rot_left(Node *x) {
    g_rot++;
    Node *y = x->right;
    x->right = y->left;          /* B 를 x 의 오른쪽으로 */
    y->left = x;
    fix_height(x);
    fix_height(y);
    return y;
}

/* 전제: n 의 두 서브트리는 AVL 이고 높이 차는 최대 2.
 * n 의 높이를 갱신하고 |bf| == 2 면 LL/LR/RR/RL 회전으로 고친 뒤 새 서브트리 루트를 반환. */
static Node *rebalance(Node *n) {
    fix_height(n);
    int b = bf(n);
    if (b > 1) {                                  /* 왼쪽이 2 더 높다 */
        if (bf(n->left) < 0)                      /* LR: 자식이 반대로 기움 → 먼저 자식을 왼쪽 회전 */
            n->left = rot_left(n->left);
        return rot_right(n);                      /* LL (자식 bf 0 도 여기 — 삭제에서만 생김) */
    }
    if (b < -1) {                                 /* 오른쪽이 2 더 높다 */
        if (bf(n->right) > 0)                     /* RL */
            n->right = rot_right(n->right);
        return rot_left(n);                       /* RR */
    }
    return n;
}

/* 서브트리 n 에 key 를 넣고 새 서브트리 루트를 반환. 새 노드가 생겼으면 *added = true.
 * 중복 키 / 풀 고갈이면 트리를 바꾸지 않는다. */
static Node *insert_rec(Node *n, int32_t key, uint32_t val, bool *added) {
    if (!n) {
        Node *m = node_alloc(key, val);
        *added = (m != NULL);
        return m;                                 /* 풀 고갈이면 NULL — 빈 링크 그대로 */
    }
    if (key < n->key)      n->left  = insert_rec(n->left, key, val, added);
    else if (key > n->key) n->right = insert_rec(n->right, key, val, added);
    else                   return n;              /* 중복 */
    return *added ? rebalance(n) : n;             /* 돌아오는 길에 경로를 따라 고친다 */
}
static bool avl_insert(Avl *t, int32_t key, uint32_t val) {   /* (주어짐) */
    bool added = false;
    t->root = insert_rec(t->root, key, val, &added);
    if (added) t->count++;
    return added;
}
static Node *avl_find(const Avl *t, int32_t key) {           /* (주어짐 — DS-B13) */
    Node *n = t->root;
    while (n && n->key != key) n = key < n->key ? n->left : n->right;
    return n;
}

/* 서브트리가 ① (lo, hi) 범위의 BST ② 저장된 h 가 실제 높이 ③ 모든 노드 |bf| <= 1
 * 를 만족하면 실제 높이를, 하나라도 어기면 -1 을 반환. */
static int check_rec(const Node *n, int64_t lo, int64_t hi) {
    if (!n) return 0;
    if (n->key <= lo || n->key >= hi) return -1;            /* ① 순서 */
    int l = check_rec(n->left, lo, n->key);
    int r = check_rec(n->right, n->key, hi);
    if (l < 0 || r < 0) return -1;
    if (l - r > 1 || r - l > 1) return -1;                  /* ③ 균형 */
    int h = 1 + imax(l, r);
    return n->h == h ? h : -1;                              /* ② 저장된 높이 */
}
static int count_nodes(const Node *n) { return n ? 1 + count_nodes(n->left) + count_nodes(n->right) : 0; }
static bool avl_check(const Avl *t) {                       /* (주어짐) */
    return check_rec(t->root, (int64_t)INT32_MIN - 1, (int64_t)INT32_MAX + 1) >= 0
        && count_nodes(t->root) == t->count;
}

/* ---------------- Part B: 삭제 ---------------- */
/* 서브트리 n(비어 있지 않음)에서 최소 노드를 떼어 *min 에 넘기고, 균형 맞춘 새 루트를 반환. */
static Node *detach_min(Node *n, Node **min) {
    if (!n->left) {                               /* n 이 최소: 오른쪽 서브트리가 자리를 잇는다 */
        *min = n;
        return n->right;
    }
    n->left = detach_min(n->left, min);
    return rebalance(n);
}
/* key 를 지우고 새 서브트리 루트를 반환. 지웠으면 *removed = true.
 * 자식 2개면 후계자 노드를 "떼어 와서 그 자리에 연결" (키 복사 X → 다른 노드 포인터 유지). */
static Node *delete_rec(Node *n, int32_t key, bool *removed) {
    if (!n) return NULL;                          /* 없는 키 */
    if (key < n->key)      n->left  = delete_rec(n->left, key, removed);
    else if (key > n->key) n->right = delete_rec(n->right, key, removed);
    else {
        *removed = true;
        Node *l = n->left, *r = n->right;
        node_free(n);
        if (!l) return r;                         /* 자식 0개 또는 오른쪽 1개 */
        if (!r) return l;                         /* 왼쪽 1개 */
        Node *s;
        r = detach_min(r, &s);                    /* 후계자 s 를 오른쪽 서브트리에서 떼고 */
        s->left = l;                              /* n 자리에 s 를 앉힌다 */
        s->right = r;
        return rebalance(s);
    }
    return *removed ? rebalance(n) : n;
}
static bool avl_delete(Avl *t, int32_t key) {                 /* (주어짐) */
    bool removed = false;
    t->root = delete_rec(t->root, key, &removed);
    if (removed) t->count--;
    return removed;
}

/* ---------------- Part C: 익스텐트 맵 (key = 시작 LBA, val = 길이) ---------------- */
/* key 이하인 가장 큰 키를 가진 노드 (없으면 NULL) — DS-B13 연습 3 의 floor */
static const Node *avl_floor(const Avl *t, int32_t key) {
    const Node *n = t->root, *best = NULL;
    while (n) {
        if (n->key == key) return n;
        if (n->key < key) { best = n; n = n->right; }   /* 후보. 더 큰 후보는 오른쪽에 */
        else n = n->left;
    }
    return best;
}
/* lba 를 덮는 익스텐트 [start, start+len) 이 있으면 *start 에 시작 LBA 를 쓰고 true */
static bool extent_lookup(const Avl *t, int32_t lba, int32_t *start) {
    const Node *e = avl_floor(t, lba);
    if (!e || (int64_t)lba >= (int64_t)e->key + e->val) return false;   /* 틈(gap) */
    *start = e->key;
    return true;
}

/* ---------------- 측정용 비교 대상: 균형 없는 BST (주어짐, DS-B13) ---------------- */
static bool bst_insert_plain(Avl *t, int32_t key) {
    Node **link = &t->root;
    while (*link) {
        if (key == (*link)->key) return false;
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node *m = node_alloc(key, 0);
    if (!m) return false;
    *link = m; t->count++;
    return true;
}
static int tree_height(const Node *n) {   /* 저장된 h 를 믿지 않고 실제로 잰다 */
    if (!n) return 0;
    return 1 + imax(tree_height(n->left), tree_height(n->right));
}

/* ---------------- 테스트 도우미 (주어짐) ---------------- */
static void avl_reset(Avl *t) { pool_init(); t->root = NULL; t->count = 0; }
static void pre_rec(const Node *n, int32_t *out, int *k) {
    if (!n) return;
    out[(*k)++] = n->key; pre_rec(n->left, out, k); pre_rec(n->right, out, k);
}
static int preorder(const Node *n, int32_t *out) { int k = 0; pre_rec(n, out, &k); return k; }
static void in_rec(const Node *n, int32_t *out, int *k) {
    if (!n) return;
    in_rec(n->left, out, k); out[(*k)++] = n->key; in_rec(n->right, out, k);
}
static int inorder(const Node *n, int32_t *out) { int k = 0; in_rec(n, out, &k); return k; }
static bool same(const int32_t *a, const int32_t *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static void shuffle(int32_t *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(xorshift32() % (uint32_t)(i + 1));
        int32_t tmp = a[i]; a[i] = a[j]; a[j] = tmp;
    }
}
/* 노드 n 개 AVL 의 최악 높이 = N(h) <= n 인 최대 h. N(h) = 1 + N(h-1) + N(h-2) (최소 노드 수) */
static int avl_worst_height(int n) {
    long a = 0, b = 1;   /* N(0), N(1) */
    int h = 0;
    while (b <= n) { long c = 1 + b + a; a = b; b = c; h++; }
    return h;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int32_t g_out[POOL_N], g_want[POOL_N];

int main(void) {
    Avl t;
    int32_t *out = g_out;

    /* ===== Part A — 회전 4종: 키 3개로 각 경우를 만든다. 결과는 모두 20(10,30) ===== */
    static const struct { const char *name; int32_t k[3]; long rots; } R4[] = {
        {"LL", {30, 20, 10}, 1}, {"RR", {10, 20, 30}, 1},
        {"LR", {30, 10, 20}, 2}, {"RL", {10, 30, 20}, 2},
    };
    static const int32_t P3[] = {20, 10, 30};
    for (int c = 0; c < 4; c++) {
        avl_reset(&t); g_rot = 0;
        bool ok = true;
        for (int i = 0; i < 3; i++) ok = avl_insert(&t, R4[c].k[i], 0) && avl_check(&t) && ok;
        int n = preorder(t.root, out);
        printf("  [A] %s  insert %d,%d,%d -> rotations=%ld height=%d\n", R4[c].name,
               R4[c].k[0], R4[c].k[1], R4[c].k[2], g_rot, ht(t.root));
        CHECK(ok && n == 3 && same(out, P3, 3) && g_rot == R4[c].rots && ht(t.root) == 2);
    }

    /* Part A — 1..7 정렬 삽입 → 완전 이진 트리 (일반 BST 라면 높이 7 의 사슬) */
    avl_reset(&t); g_rot = 0;
    bool ok7 = true;
    for (int32_t k = 1; k <= 7; k++) ok7 = avl_insert(&t, k, (uint32_t)k * 10) && avl_check(&t) && ok7;
    static const int32_t P7[] = {4, 2, 1, 3, 6, 5, 7};
    printf("  [A] insert 1..7 -> rotations=%ld height=%d\n", g_rot, ht(t.root));
    CHECK(ok7 && preorder(t.root, out) == 7 && same(out, P7, 7) && ht(t.root) == 3);
    CHECK(avl_insert(&t, 4, 999) == false && t.count == 7 && avl_find(&t, 4)->val == 40);  /* 중복 */
    CHECK(avl_find(&t, 5) != NULL && avl_find(&t, 8) == NULL);

    /* Part A — 검사기가 나쁜 트리를 잡는가 (손으로 만든 노드) */
    Node a1 = {0}, a2 = {0}, a3 = {0}, a4 = {0};
    a1.key = 10; a2.key = 20; a3.key = 30;                         /* 10 → 20 → 30 오른쪽 사슬 */
    a1.right = &a2; a2.right = &a3; a3.h = 1; a2.h = 2; a1.h = 3;   /* 높이는 정확, bf(10) = -2 */
    Avl bad = {&a1, 3};
    CHECK(avl_check(&bad) == false);
    a2.key = 5; a2.right = NULL; a2.h = 1;
    a1.left = &a2; a1.right = &a3; a1.h = 3;                       /* 10(5, 30): 모양 OK, 저장된 h 만 틀림 */
    CHECK(avl_check(&bad) == false);
    a1.h = 2;
    CHECK(avl_check(&bad) == true);                                /* 고치면 통과 */
    a4.key = 12; a4.h = 1; a2.right = &a4; a2.h = 2; a1.h = 3; bad.count = 4;  /* 12 가 10 의 왼쪽에! */
    CHECK(avl_check(&bad) == false);

    /* ===== Part B — 삭제 ===== */
    /* 50,20,60,10,30,70,5,35 는 회전 없이 들어간다. 70 을 지우면 50 의 bf = +2, 왼쪽 자식 20 의 bf = 0 */
    static const int32_t KB[] = {50, 20, 60, 10, 30, 70, 5, 35};
    avl_reset(&t);
    for (int i = 0; i < 8; i++) avl_insert(&t, KB[i], 0);
    CHECK(t.count == 8 && avl_check(&t) && ht(t.root) == 4);
    g_rot = 0;
    CHECK(avl_delete(&t, 70) && avl_check(&t));
    static const int32_t D1[] = {20, 10, 5, 50, 30, 35, 60};      /* 단일 회전이어야 함 (이중 회전이면 깨짐) */
    printf("  [B] delete 70 (child bf 0) -> rotations=%ld\n", g_rot);
    CHECK(preorder(t.root, out) == 7 && same(out, D1, 7) && g_rot == 1);
    Node *p30 = avl_find(&t, 30);
    CHECK(avl_delete(&t, 20) && avl_check(&t));                     /* 자식 2개 — 후계자 30 */
    static const int32_t D2[] = {30, 10, 5, 50, 35, 60};
    CHECK(preorder(t.root, out) == 6 && same(out, D2, 6));
    CHECK(p30 && avl_find(&t, 30) == p30 && t.root == p30);            /* 후계자 노드가 그대로 옮겨 감 */
    CHECK(avl_delete(&t, 99) == false && t.count == 6);            /* 없는 키 */
    static const int32_t DEL_ALL[] = {5, 60, 30, 10, 50, 35};
    bool ok_all = true;
    for (int i = 0; i < 6; i++) ok_all = avl_delete(&t, DEL_ALL[i]) && avl_check(&t) && ok_all;
    CHECK(ok_all && t.root == NULL && t.count == 0 && g_used == 0); /* 풀 누수 없음 */

    /* Part B — 랜덤 삽입·삭제 vs 참조 모델 (bool 배열), 매 연산 후 검사기 */
    enum { KEYS = 300, OPS = 60000 };
    static bool ref[KEYS];
    int bad_ops = 0, max_n = 0, max_h = 0;
    long max_rot_ins = 0, max_rot_del = 0;
    avl_reset(&t);
    for (int op = 0; op < OPS; op++) {
        uint32_t r = xorshift32();
        int32_t key = (int32_t)(r % KEYS);
        long before = g_rot;
        if (r & 0x10000u) {
            if (avl_insert(&t, key, (uint32_t)key) == ref[key]) bad_ops++;   /* 없던 키만 true */
            ref[key] = true;
            if (g_rot - before > max_rot_ins) max_rot_ins = g_rot - before;
        } else {
            if (avl_delete(&t, key) != ref[key]) bad_ops++;                 /* 있던 키만 true */
            ref[key] = false;
            if (g_rot - before > max_rot_del) max_rot_del = g_rot - before;
        }
        int m = 0;
        for (int k = 0; k < KEYS; k++) if (ref[k]) g_want[m++] = k;
        int n = inorder(t.root, out);
        if (n != m || !same(out, g_want, m) || !avl_check(&t) || g_used != m) bad_ops++;
        if (m > max_n) max_n = m;
        if (ht(t.root) > max_h) max_h = ht(t.root);
    }
    printf("  [B] random ops=%d max_size=%d max_height=%d (worst AVL for %d = %d) "
           "max_rot/insert=%ld max_rot/delete=%ld bad_ops=%d\n",
           OPS, max_n, max_h, max_n, avl_worst_height(max_n), max_rot_ins, max_rot_del, bad_ops);
    CHECK(max_n > 100 && bad_ops == 0 && max_rot_ins <= 2 && max_h <= avl_worst_height(max_n));

    /* ===== Part C — 순차 쓰기 익스텐트 맵 ===== */
    /* 호스트가 순차로 쓴다 → 익스텐트 시작 LBA 가 단조 증가 = 정렬 삽입 (일반 BST 의 최악) */
    enum { EXT = 3000, LBA_SPACE = EXT * 20 };
    static int32_t ref_start[LBA_SPACE];          /* 참조 모델: LBA → 덮는 익스텐트의 시작 (-1 = 틈) */
    static int32_t ext_start[EXT];
    for (int i = 0; i < LBA_SPACE; i++) ref_start[i] = -1;
    avl_reset(&t);
    int32_t lba = 0;
    bool ok_ins = true;
    for (int i = 0; i < EXT; i++) {
        uint32_t len = 1 + xorshift32() % 16;
        lba += (int32_t)(xorshift32() % 4);       /* 0~3 칸 틈 */
        ext_start[i] = lba;
        ok_ins = avl_insert(&t, lba, len) && ok_ins;
        for (uint32_t j = 0; j < len; j++) ref_start[lba + (int32_t)j] = lba;
        lba += (int32_t)len;
    }
    int hC = ht(t.root), minC = 0;
    while ((1 << minC) - 1 < EXT) minC++;
    printf("  [C] %d sequential extents (sorted keys): height=%d (min %d, worst AVL %d)\n",
           EXT, hC, minC, avl_worst_height(EXT));
    CHECK(ok_ins && avl_check(&t) && hC <= avl_worst_height(EXT));
    int miss = 0, mapped = 0;
    for (int32_t x = 0; x < lba + 5; x++) {
        int32_t s = -7;
        bool hit = extent_lookup(&t, x, &s);
        int32_t want = x < LBA_SPACE ? ref_start[x] : -1;
        if (hit != (want >= 0) || (hit && s != want)) miss++;
        mapped += hit;
    }
    CHECK(miss == 0 && mapped > 0);
    /* 무작위 TRIM: 익스텐트 절반 삭제 후 다시 전수 조회 */
    shuffle(ext_start, EXT);
    bool ok_trim = true;
    for (int i = 0; i < EXT / 2; i++) {
        int32_t s = ext_start[i];
        const Node *e = avl_find(&t, s);
        uint32_t len = e ? e->val : 0;
        ok_trim = avl_delete(&t, s) && ok_trim;
        for (uint32_t j = 0; j < len; j++) ref_start[s + (int32_t)j] = -1;
    }
    miss = 0;
    for (int32_t x = 0; x < lba + 5; x++) {
        int32_t s = -7;
        bool hit = extent_lookup(&t, x, &s);
        int32_t want = x < LBA_SPACE ? ref_start[x] : -1;
        if (hit != (want >= 0) || (hit && s != want)) miss++;
    }
    printf("  [C] after TRIM of %d extents: count=%d height=%d lookup_mismatch=%d\n",
           EXT / 2, t.count, ht(t.root), miss);
    CHECK(ok_trim && miss == 0 && avl_check(&t) && t.count == EXT - EXT / 2);
    CHECK(avl_floor(&t, -1) == NULL);                              /* 맨 앞보다 작은 키 */

    /* Part C — 풀 고갈: 4096 개를 꽉 채우면 다음 삽입은 거절되고 트리는 멀쩡 */
    avl_reset(&t);
    for (int32_t k = 0; k < POOL_N; k++) avl_insert(&t, k, 0);
    CHECK(t.count == POOL_N && avl_insert(&t, POOL_N, 0) == false && avl_check(&t));

    /* Part C — 측정: 정렬 삽입 높이, AVL vs 일반 BST */
    printf("  [C]     n | min | AVL worst | AVL asc | AVL desc | AVL rand(avg) | BST asc | BST rand(avg) | rotations(asc)\n");
    static const int NS[] = {15, 127, 1023, 4095};
    static int32_t perm[POOL_N];
    int h_asc_4095 = 0;
    for (int s = 0; s < 4; s++) {
        int n = NS[s], minh = 0;
        while ((1 << minh) - 1 < n) minh++;
        avl_reset(&t); g_rot = 0;
        for (int32_t k = 0; k < n; k++) avl_insert(&t, k, 0);
        int h_asc = tree_height(t.root);
        long rot_asc = g_rot;
        bool v_asc = avl_check(&t);
        avl_reset(&t);
        for (int32_t k = n - 1; k >= 0; k--) avl_insert(&t, k, 0);
        int h_desc = tree_height(t.root);
        avl_reset(&t);
        for (int32_t k = 0; k < n; k++) bst_insert_plain(&t, k);
        int b_asc = tree_height(t.root);
        enum { TRIALS = 20 };
        int sum_a = 0, sum_b = 0;
        for (int tr = 0; tr < TRIALS; tr++) {
            for (int i = 0; i < n; i++) perm[i] = i;
            shuffle(perm, n);
            avl_reset(&t);
            for (int i = 0; i < n; i++) avl_insert(&t, perm[i], 0);
            sum_a += tree_height(t.root);
            avl_reset(&t);
            for (int i = 0; i < n; i++) bst_insert_plain(&t, perm[i]);
            sum_b += tree_height(t.root);
        }
        printf("  [C] %5d | %3d | %9d | %7d | %8d | %13.1f | %7d | %13.1f | %ld\n",
               n, minh, avl_worst_height(n), h_asc, h_desc, sum_a / (double)TRIALS,
               b_asc, sum_b / (double)TRIALS, rot_asc);
        CHECK(v_asc && b_asc == n);
        if (n == 4095) h_asc_4095 = h_asc;
    }
    CHECK(h_asc_4095 == 12);   /* 2^12 - 1 개를 정렬 삽입하면 완전 이진 트리 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
