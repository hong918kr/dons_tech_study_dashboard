/* DS-A05 침입형 레드블랙 트리 (Linux rbtree 스타일)
 *
 * struct rb_node 를 객체 안에 박아 넣고(intrusive), 색은 부모 포인터의 최하위 비트에 숨긴다.
 * 트리 코드는 키를 모른다: 위치 찾기는 사용자 코드(item_insert)가 하고, 균형 복구만 라이브러리가 한다.
 *   Part A  색+부모 인코딩, 회전, rb_insert_color(3 케이스), first/last/next/prev, 불변식 검사기
 *   Part B  rb_erase(후계자 재연결) + rb_erase_color(4 케이스), 전수 순열 + 랜덤 100만 회 vs 참조 모델
 *   Part C  타임아웃 큐: cached leftmost, 32비트 틱 랩어라운드, 동률 FIFO, cancel, 주기 타이머, 처리량 제한
 *
 * 체크는 전부 결정적이다 (고정 시드 xorshift32, 시간 측정 없음). 측정은 demo_*.c.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define RB_HELPER __attribute__((unused))

/* ======================= 노드와 인코딩 ======================= */
#define RB_RED   0u      /* 빨강이 0 이라서 rb_link_node 는 부모 포인터를 그대로 저장하면 된다 */
#define RB_BLACK 1u
#define RB_COLOR_MASK ((uintptr_t)1)

/* parent_color = 부모 주소 | 색(bit 0). 노드 주소의 bit 0 이 항상 0 이어야 성립한다.
 * (Linux 필드 이름은 __rb_parent_color 지만, 이중 밑줄 식별자는 C 표준상 구현 예약이라 여기서는 뺐다.) */
struct rb_node {
    uintptr_t parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};
struct rb_root { struct rb_node *rb_node; };

_Static_assert(_Alignof(struct rb_node) >= 2, "bit 0 of a node address must be free for the colour");
_Static_assert(sizeof(uintptr_t) == sizeof(struct rb_node *), "parent_color must hold a whole pointer");

/* 트리 밖의 노드 표시: 부모를 자기 자신으로 (실제 트리에서는 절대 생기지 않는 값). 색 비트 0 도 함께 정해진다. */
#define RB_CLEAR_NODE(n) ((n)->parent_color = (uintptr_t)(n))
#define RB_EMPTY_NODE(n) ((n)->parent_color == (uintptr_t)(n))

/* 측정용 카운터: [0] = 노드가 왼쪽 자식인 쪽, [1] = 거울상(오른쪽). 케이스 번호는 1부터. 제품 코드에서는 지운다. */
static struct {
    uint64_t rot;
    uint64_t ins_case[2][4];
    uint64_t del_case[2][5];
} g_st;

/* ======================= Part A — 인코딩 ======================= */
RB_HELPER static struct rb_node *rb_parent(const struct rb_node *n) {
    /* TODO: parent_color 에서 색 비트를 지운 값을 포인터로 */
    (void)n;
    return NULL;
}
RB_HELPER static unsigned rb_color(const struct rb_node *n) { (void)n; return RB_BLACK; /* TODO: bit 0 */ }
/* 색은 그대로 두고 부모만 바꾼다 */
RB_HELPER static void rb_set_parent(struct rb_node *n, struct rb_node *p) {
    /* TODO: 새 부모 주소 | 기존 색 비트 */
    (void)n; (void)p;
}
RB_HELPER static void rb_set_parent_color(struct rb_node *n, struct rb_node *p, unsigned color) {
    /* TODO: 부모 주소 | color */
    (void)n; (void)p; (void)color;
}
RB_HELPER static bool rb_is_red(const struct rb_node *n)   { return rb_color(n) == RB_RED; }
RB_HELPER static bool rb_is_black(const struct rb_node *n) { return rb_color(n) == RB_BLACK; }
RB_HELPER static void rb_set_black(struct rb_node *n) { n->parent_color |= RB_COLOR_MASK; }
RB_HELPER static void rb_set_red(struct rb_node *n)   { n->parent_color &= ~RB_COLOR_MASK; }
/* NULL 자식(NIL)은 검정이다 — 케이스 판정을 한 줄로 만드는 도우미 */
RB_HELPER static bool rb_red_or_false(const struct rb_node *n) { return n && rb_is_red(n); }

/* 부모의 자식 링크(또는 루트)를 old → nw 로 바꾼다 */
RB_HELPER static void rb_change_child(struct rb_node *old, struct rb_node *nw, struct rb_node *parent, struct rb_root *root) {
    if (parent) {
        if (parent->rb_left == old) parent->rb_left = nw;
        else parent->rb_right = nw;
    } else {
        root->rb_node = nw;
    }
}

/* 호출자가 찾은 빈 링크에 새 노드를 빨강으로 붙인다 (RB_RED == 0 이므로 색 비트를 따로 안 쓴다) */
RB_HELPER static void rb_link_node(struct rb_node *node, struct rb_node *parent, struct rb_node **link) {
    node->parent_color = (uintptr_t)parent;
    node->rb_left = node->rb_right = NULL;
    *link = node;
}

/* ======================= Part A — 회전 ======================= */
/*      x                y
 *     / \              / \
 *    A   y     →      x   C      색은 건드리지 않는다. x->rb_right 는 NULL 이 아니어야 한다.
 *       / \          / \
 *      B   C        A   B
 */
RB_HELPER static void rb_rotate_left(struct rb_node *x, struct rb_root *root) {
    /* TODO: y = x->rb_right, b = y->rb_left, p = rb_parent(x).
     *       x->rb_right = b (b 가 있으면 b 의 부모 = x), y->rb_left = x,
     *       y 의 부모 = p, x 의 부모 = y (색은 유지 → rb_set_parent), p 의 자식 링크 교체.
     *       끝에 g_st.rot++ */
    (void)x; (void)root;
}
RB_HELPER static void rb_rotate_right(struct rb_node *x, struct rb_root *root) {
    /* TODO: rb_rotate_left 의 거울상 */
    (void)x; (void)root;
}

/* ======================= Part A — 삽입 후 균형 복구 =======================
 * 진입: node 는 빨강으로 이미 연결돼 있다. 위반은 "node 와 부모가 둘 다 빨강" 하나뿐일 수 있다. */
RB_HELPER static void rb_insert_color(struct rb_node *node, struct rb_root *root) {
    /* TODO: 부모가 있고 빨강인 동안 반복. 조부모 기준으로 왼쪽/오른쪽(거울상)을 나눈다.
     *   Case 1 삼촌 빨강: 부모·삼촌 검정, 조부모 빨강, node = 조부모, 계속
     *   Case 2 삼촌 검정 + 꺾임: 부모에서 회전, node 와 parent 교환 (Case 3 로 이어짐)
     *   Case 3 삼촌 검정 + 일직선: 부모 검정, 조부모 빨강, 조부모에서 반대로 회전, break
     *   마지막에 루트를 검정으로.
     *   카운터: g_st.ins_case[side][케이스]++ (side 0 = 부모가 왼쪽 자식, 1 = 거울상) */
    (void)node; (void)root;
}

/* ======================= Part A — 순회 ======================= */
static struct rb_node *rb_first(const struct rb_root *root) {
    (void)root; return NULL;   /* TODO: 왼쪽 끝 */
}
static struct rb_node *rb_last(const struct rb_root *root) {
    (void)root; return NULL;   /* TODO: 오른쪽 끝 */
}
/* in-order 다음 노드. 스택 없이 부모 포인터로만 걷는다. 트리 밖 노드(RB_EMPTY_NODE)면 NULL. */
static struct rb_node *rb_next(const struct rb_node *node) {
    /* TODO: 오른쪽 자식이 있으면 그 서브트리의 왼쪽 끝.
     *       없으면 "내가 왼쪽 자식인 첫 조상"까지 올라가 그 조상을 반환 (없으면 NULL). */
    (void)node; return NULL;
}
static struct rb_node *rb_prev(const struct rb_node *node) {
    (void)node; return NULL;   /* TODO: rb_next 의 거울상 */
}

/* ======================= Part A — 불변식 검사기 =======================
 * 트리 코드는 키를 모르므로 key 함수를 받는다. allow_dup = 동률 허용 (Part C 타이머). */
typedef enum {
    RB_OK = 0, RB_ERR_ROOT_RED, RB_ERR_RED_RED, RB_ERR_BLACK_HEIGHT, RB_ERR_ORDER, RB_ERR_PARENT, RB_ERR_COUNT
} rb_err_t;
typedef int64_t (*rb_key_fn)(const struct rb_node *, const void *ctx);
typedef struct {
    rb_key_fn key; const void *ctx; bool allow_dup;
    size_t limit, seen;
    bool have_prev; int64_t prev;
    rb_err_t err;
} rb_vstate;

/* n 서브트리의 검정 높이(NIL 제외, n 포함)를 반환. 위반이면 v->err 를 정하고 -1. */
RB_HELPER static int rb_vrec(rb_vstate *v, const struct rb_node *n, const struct rb_node *parent) {
    /* TODO: NIL 이면 0.
     *  ① ++seen > limit → RB_ERR_COUNT (사이클 방어)   ② rb_parent(n) != parent → RB_ERR_PARENT
     *  ③ n 과 parent 가 둘 다 빨강 → RB_ERR_RED_RED
     *  ④ 왼쪽 재귀 → key 를 직전 in-order 키와 비교 (RB_ERR_ORDER, allow_dup 이면 동률 허용) → 오른쪽 재귀
     *  ⑤ 좌우 검정 높이가 다르면 RB_ERR_BLACK_HEIGHT.  반환 = 자식 높이 + (n 이 검정이면 1) */
    (void)v; (void)n; (void)parent;
    return 0;
}
static rb_err_t rb_validate(const struct rb_root *root, size_t expect_n, rb_key_fn key, const void *ctx, bool allow_dup) {
    /* TODO: 루트가 빨강이면 RB_ERR_ROOT_RED. rb_vstate 초기화 (limit = expect_n) → rb_vrec →
     *       음수면 v.err, 아니면 seen == expect_n 인지 (아니면 RB_ERR_COUNT) */
    (void)root; (void)expect_n; (void)key; (void)ctx; (void)allow_dup;
    return RB_OK;
}

/* ======================= 사용자 코드: 키가 있는 객체 ======================= */
typedef struct item {
    int32_t key;
    uint32_t seq;
    struct rb_node node;     /* 일부러 맨 앞에 두지 않았다 → container_of 가 필요하다 (DS-M01) */
} item_t;
_Static_assert(offsetof(item_t, node) != 0, "node must not be first");
_Static_assert(_Alignof(item_t) >= _Alignof(struct rb_node), "a packed container would break the colour bit");

static int64_t item_key(const struct rb_node *n, const void *ctx) {
    (void)ctx;
    return container_of(n, const item_t, node)->key;
}

/* 위치 찾기 + rb_link_node + rb_insert_color. 중복 키면 트리를 바꾸지 않고 false. */
static bool item_insert(struct rb_root *root, item_t *it) {
    /* TODO: link = &root->rb_node 에서 내려가며 parent 를 기억한다 (같은 키면 false).
     *       빈 링크에 rb_link_node → rb_insert_color → true */
    (void)root; (void)it;
    return false;
}
static item_t *item_search(const struct rb_root *root, int32_t key) {       /* (주어짐) */
    const struct rb_node *n = root->rb_node;
    for (unsigned guard = 0; n && guard < 1000000u; guard++) {
        item_t *it = container_of(n, item_t, node);
        if (key < it->key)      n = n->rb_left;
        else if (key > it->key) n = n->rb_right;
        else                    return it;
    }
    return NULL;
}

/* ======================= Part B — 삭제 =======================
 * 이중 검정(double black) 복구. 진입: parent 의 한쪽(처음엔 NULL 자리)이 검정 하나가 모자란다. */
RB_HELPER static void rb_erase_color(struct rb_node *parent, struct rb_root *root) {
    /* TODO: node = NULL 로 시작. 반복:
     *  sibling = parent->rb_right; node != sibling 이면 node 는 왼쪽 자식, 아니면 거울상(sibling = parent->rb_left).
     *  Case 1 형제 빨강: 형제 검정, 부모 빨강, 부모에서 회전, 형제 다시 읽기 (Case 2/3/4 로 이어짐)
     *  Case 2 조카 둘 다 검정: 형제 빨강. 부모가 빨강이면 부모를 검정으로 하고 끝.
     *         아니면 node = parent, parent = 그 부모로 올라간다 (없으면 끝)
     *  Case 3 먼 조카 검정 (가까운 조카 빨강): 가까운 조카 검정, 형제 빨강, 형제에서 반대로 회전, 형제 다시 읽기
     *  Case 4 먼 조카 빨강: 형제 = 부모 색, 부모 검정, 먼 조카 검정, 부모에서 회전, 끝
     *  카운터: g_st.del_case[side][케이스]++ */
    (void)parent; (void)root;
}

/* node 를 트리에서 뺀다. 키를 복사하지 않고 후계자 노드를 node 자리로 재연결한다
 * → 다른 코드가 들고 있는 객체 포인터가 전부 유효하게 남는다. */
static void rb_erase(struct rb_node *node, struct rb_root *root) {
    /* TODO:
     *  자식 0~1 개: child = 있는 자식 (또는 NULL), removed_black = node 가 검정, 부모 링크를 child 로 교체,
     *               child 가 있으면 child 의 부모 갱신, fix_parent = node 의 부모.
     *  자식 2 개: succ = 오른쪽 서브트리의 왼쪽 끝, child = succ->rb_right, removed_black = succ 가 검정.
     *     succ 가 node 의 오른쪽 자식이면 fix_parent = succ.
     *     아니면 fix_parent = succ 의 부모, 그 왼쪽에 child 를 달고, succ->rb_right = node->rb_right (부모도).
     *     succ->rb_left = node->rb_left (부모도), node 의 부모 링크를 succ 로, succ->parent_color = node->parent_color.
     *  removed_black 이면: child 가 있으면 검정으로 칠하고 끝, 없으면 fix_parent 가 있을 때 rb_erase_color. */
    (void)node; (void)root;
}

/* ======================= 하네스: 문자열 ↔ 트리 =======================
 * 형식: "(" [왼쪽] 키 색 [오른쪽] ")", 색은 r / B.  예: "((10r)20B(30r))".  학생 코드를 쓰지 않고 원시 필드만 본다. */
#define FX_MAX 64
static item_t g_fx[FX_MAX];
static size_t g_fxn;
static const char *g_p;

static item_t *fx_new(int key) {
    item_t *it = &g_fx[g_fxn++];
    memset(it, 0, sizeof *it);
    it->key = key;
    RB_CLEAR_NODE(&it->node);
    return it;
}
static struct rb_node *fx_parse(void) {
    if (*g_p != '(') return NULL;
    g_p++;
    struct rb_node *l = fx_parse();
    int key = 0;
    while (*g_p >= '0' && *g_p <= '9') key = key * 10 + (*g_p++ - '0');
    unsigned color = (*g_p++ == 'B') ? RB_BLACK : RB_RED;
    struct rb_node *r = fx_parse();
    g_p++;                                  /* ')' */
    item_t *it = fx_new(key);
    struct rb_node *n = &it->node;
    n->parent_color = color;                /* 부모는 위 프레임이 채운다 */
    n->rb_left = l;
    n->rb_right = r;
    if (l) l->parent_color = (uintptr_t)n | (l->parent_color & RB_COLOR_MASK);
    if (r) r->parent_color = (uintptr_t)n | (r->parent_color & RB_COLOR_MASK);
    return n;
}
static void fx_load(struct rb_root *root, const char *s) {
    g_fxn = 0;
    g_p = s;
    root->rb_node = fx_parse();
}
static item_t *fx_find(int key) {
    for (size_t i = 0; i < g_fxn; i++) if (g_fx[i].key == key) return &g_fx[i];
    return NULL;
}
static size_t dump_rec(const struct rb_node *n, char *b, size_t cap, size_t i, int depth) {
    if (!n || depth > 40 || i + 16 >= cap) return i;
    b[i++] = '(';
    i = dump_rec(n->rb_left, b, cap, i, depth + 1);
    i += (size_t)snprintf(b + i, cap - i, "%d%c", (int)container_of(n, const item_t, node)->key,
                          (n->parent_color & RB_COLOR_MASK) ? 'B' : 'r');
    i = dump_rec(n->rb_right, b, cap, i, depth + 1);
    b[i++] = ')';
    b[i] = '\0';
    return i;
}
static bool dump_is(const struct rb_root *root, const char *want) {
    static char buf[512];
    buf[0] = '\0';
    dump_rec(root->rb_node, buf, sizeof buf, 0, 0);
    if (strcmp(buf, want) == 0) return true;
    printf("  got  %s\n  want %s\n", buf, want);
    return false;
}
static rb_err_t fx_validate(const char *s, size_t expect_n) {
    struct rb_root r;
    fx_load(&r, s);
    return rb_validate(&r, expect_n, item_key, NULL, false);
}
static size_t fx_count(void) { return g_fxn; }

/* 시작 트리(올바른 RB 인지도 확인) → key 삽입 → 정확한 모양과 색 비교 → 검사기 통과 */
static bool t_insert(const char *before, int key, const char *after) {
    struct rb_root r;
    fx_load(&r, before);
    size_t n = fx_count();
    if (rb_validate(&r, n, item_key, NULL, false) != RB_OK) { printf("  bad fixture\n"); return false; }
    item_t *it = fx_new(key);
    if (!item_insert(&r, it)) return false;
    return dump_is(&r, after) && rb_validate(&r, n + 1, item_key, NULL, false) == RB_OK;
}
/* 삭제 뒤 살아남은 키마다 "처음과 같은 객체"가 트리에 있어야 한다 (키 복사 구현을 잡는다) */
static bool t_erase(const char *before, int key, const char *after) {
    struct rb_root r;
    fx_load(&r, before);
    size_t n = fx_count();
    if (rb_validate(&r, n, item_key, NULL, false) != RB_OK) { printf("  bad fixture\n"); return false; }
    item_t *victim = fx_find(key);
    if (!victim) return false;
    rb_erase(&victim->node, &r);
    if (!dump_is(&r, after)) return false;
    if (rb_validate(&r, n - 1, item_key, NULL, false) != RB_OK) return false;
    for (size_t i = 0; i < n; i++) {
        if (&g_fx[i] == victim) continue;
        if (item_search(&r, g_fx[i].key) != &g_fx[i]) { printf("  object identity lost: key %d\n", (int)g_fx[i].key); return false; }
    }
    return true;
}

/* ======================= 하네스: 랜덤 / 전수 테스트 ======================= */
static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

#define RKEYS 8192u
static item_t g_ri[RKEYS];
static uint8_t g_rp[RKEYS];

/* 참조 모델(g_rp)과 정방향·역방향 순회를 대조한다 */
static bool r_walk_ok(const struct rb_root *root, uint32_t keys) {
    const struct rb_node *n = rb_first(root);
    for (uint32_t k = 0; k < keys; k++) {
        if (!g_rp[k]) continue;
        if (!n || container_of(n, const item_t, node)->key != (int32_t)k) return false;
        n = rb_next(n);
    }
    if (n) return false;
    n = rb_last(root);
    for (uint32_t k = keys; k-- > 0;) {
        if (!g_rp[k]) continue;
        if (!n || container_of(n, const item_t, node)->key != (int32_t)k) return false;
        n = rb_prev(n);
    }
    return n == NULL;
}

/* ops 회: 앞 1/3 은 삽입 70%, 가운데 50%, 뒤 1/3 은 30%. check_every 회마다 전체 검사. 끝나면 전부 비운다. */
static bool random_vs_model(uint32_t keys, uint32_t ops, uint32_t check_every, uint64_t *max_ins_rot, uint64_t *max_del_rot) {
    struct rb_root root = { NULL };
    size_t count = 0;
    memset(g_rp, 0, sizeof g_rp);
    for (uint32_t k = 0; k < keys; k++) { g_ri[k].key = (int32_t)k; RB_CLEAR_NODE(&g_ri[k].node); }

    for (uint32_t i = 0; i < ops; i++) {
        uint32_t phase = (uint32_t)((uint64_t)i * 3u / ops);
        uint32_t ins_pct = phase == 0 ? 70u : phase == 1 ? 50u : 30u;
        uint32_t k = rnd() % keys;
        bool ins = rnd() % 100u < ins_pct;
        uint64_t rot0 = g_st.rot;
        if (ins) {
            bool added = item_insert(&root, &g_ri[k]);
            if (added == (bool)g_rp[k]) { printf("  op %u: insert result vs model\n", (unsigned)i); return false; }
            if (added) { g_rp[k] = 1; count++; if (g_st.rot - rot0 > *max_ins_rot) *max_ins_rot = g_st.rot - rot0; }
        } else if (g_rp[k]) {
            rb_erase(&g_ri[k].node, &root);
            RB_CLEAR_NODE(&g_ri[k].node);
            g_rp[k] = 0; count--;
            if (g_st.rot - rot0 > *max_del_rot) *max_del_rot = g_st.rot - rot0;
        }
        uint32_t q = rnd() % keys;
        if (item_search(&root, (int32_t)q) != (g_rp[q] ? &g_ri[q] : NULL)) { printf("  op %u: search vs model\n", (unsigned)i); return false; }
        if ((i + 1) % check_every == 0) {
            rb_err_t e = rb_validate(&root, count, item_key, NULL, false);
            if (e != RB_OK || !r_walk_ok(&root, keys)) { printf("  op %u: full check failed (err %d)\n", (unsigned)i, (int)e); return false; }
        }
    }
    for (uint32_t k = 0; k < keys; k++) {
        if (!g_rp[k]) continue;
        rb_erase(&g_ri[k].node, &root);
        RB_CLEAR_NODE(&g_ri[k].node);
        g_rp[k] = 0; count--;
        if (rb_validate(&root, count, item_key, NULL, false) != RB_OK) { printf("  drain: invariant failed at key %u\n", (unsigned)k); return false; }
    }
    return root.rb_node == NULL && count == 0;
}

/* 키 6개의 삽입 순열 720 × 삭제 순열 720 전부. 매 연산 뒤 검사기와 순회를 돌린다. */
#define PN 6
#define PERMS 720
static int g_perm[PERMS][PN];
static int g_nperm;
static void perm_gen(int *a, int k) {
    if (k == PN) { memcpy(g_perm[g_nperm++], a, sizeof(int) * PN); return; }
    for (int i = k; i < PN; i++) {
        int t = a[k]; a[k] = a[i]; a[i] = t;
        perm_gen(a, k + 1);
        t = a[k]; a[k] = a[i]; a[i] = t;
    }
}
static bool exhaustive(void) {
    item_t it[PN];
    int a[PN];
    for (int i = 0; i < PN; i++) { a[i] = i; it[i].key = i; }
    g_nperm = 0;
    perm_gen(a, 0);
    if (g_nperm != PERMS) return false;
    for (int p = 0; p < PERMS; p++) {
        for (int d = 0; d < PERMS; d++) {
            struct rb_root root = { NULL };
            size_t n = 0;
            for (int i = 0; i < PN; i++) {
                if (!item_insert(&root, &it[g_perm[p][i]])) return false;
                n++;
                if (rb_validate(&root, n, item_key, NULL, false) != RB_OK) return false;
            }
            for (int i = 0; i < PN; i++) {
                rb_erase(&it[g_perm[d][i]].node, &root);
                n--;
                if (rb_validate(&root, n, item_key, NULL, false) != RB_OK) return false;
                size_t walked = 0;
                for (const struct rb_node *w = rb_first(&root); w && walked <= PN; w = rb_next(w)) walked++;
                if (walked != n) return false;
            }
        }
    }
    return true;
}

/* ======================= Part C — 타임아웃 큐 ======================= */
struct rb_root_cached { struct rb_root rb_root; struct rb_node *rb_leftmost; };

RB_HELPER static void rb_insert_color_cached(struct rb_node *node, struct rb_root_cached *root, bool leftmost) {
    (void)node; (void)root; (void)leftmost;   /* TODO: leftmost 면 캐시 갱신 + rb_insert_color */
}
RB_HELPER static void rb_erase_cached(struct rb_node *node, struct rb_root_cached *root) {
    (void)node; (void)root;   /* TODO: node 가 캐시면 rb_next 로 캐시를 옮긴다 — 순서 주의 — 그리고 rb_erase */
}

typedef struct sw_timer {
    struct rb_node node;
    uint32_t expires;        /* 만료 틱 (랩어라운드 있음) */
    uint32_t period;         /* 0 = 원샷. 0 < period < TQ_WINDOW */
    uint32_t seq;            /* 테스트 전용: 등록 순서 */
    uint32_t fired;
} sw_timer_t;

typedef struct timerq {
    struct rb_root_cached tree;
    uint32_t now;
} timerq_t;
typedef void (*tq_fire_fn)(timerq_t *q, sw_timer_t *t, void *arg);

/* 활성 타이머끼리의 거리가 2^31 미만이어야 모듈러 비교가 전순서가 된다 → now 기준 ±2^30 만 받는다 */
#define TQ_WINDOW 0x40000000u

static bool tq_time_before(uint32_t a, uint32_t b) {
    (void)a; (void)b; return false;   /* TODO: 랩어라운드 안전 비교 (a - b 를 부호 있는 값으로) */
}
static void tq_init(timerq_t *q, uint32_t now) { q->tree.rb_root.rb_node = NULL; q->tree.rb_leftmost = NULL; q->now = now; }
static void tq_timer_init(sw_timer_t *t, uint32_t period) { memset(t, 0, sizeof *t); RB_CLEAR_NODE(&t->node); t->period = period; }

/* 등록. 이미 등록돼 있거나 now 기준 창(±TQ_WINDOW) 밖이면 false. 같은 만료 시각이면 먼저 넣은 것이 먼저 나온다. */
static bool tq_add(timerq_t *q, sw_timer_t *t, uint32_t expires) {
    /* TODO: 이미 등록(!RB_EMPTY_NODE)이면 false. d = (int32_t)(expires - now) 가 창 밖이면 false.
     *       내려가며 왼쪽으로만 갔는지(leftmost) 기억한다. 같은 시각은 오른쪽으로.
     *       t->expires 저장 → rb_link_node → rb_insert_color_cached → true */
    (void)q; (void)t; (void)expires;
    return false;
}
/* 취소. O(log n), 검색 없음. 등록돼 있지 않으면 false (여러 번 불러도 안전). */
static bool tq_cancel(timerq_t *q, sw_timer_t *t) {
    (void)q; (void)t; return false;   /* TODO: RB_EMPTY_NODE 면 false, rb_erase_cached, RB_CLEAR_NODE, true */
}
/* 가장 이른 만료 시각 O(1) — tickless idle 이 다음 깨어날 시각을 정할 때 쓴다 */
static bool tq_next_deadline(const timerq_t *q, uint32_t *out) {
    (void)q; (void)out; return false;   /* TODO: 캐시된 leftmost 의 expires */
}
/* now 로 전진하고 만료된 타이머를 최대 budget 개 발화한다. 발화 수 반환.
 * 주기 타이머는 콜백 전에 expires + period 로 재등록한다 (콜백이 자기 자신을 cancel 할 수 있게). */
static uint32_t tq_tick(timerq_t *q, uint32_t now, uint32_t budget, tq_fire_fn fire, void *arg) {
    /* TODO: q->now = now. fired < budget 이고 leftmost 가 있는 동안:
     *       now 가 leftmost 의 expires 보다 이르면 멈춤. 아니면 rb_erase_cached + RB_CLEAR_NODE, fired++, t->fired++,
     *       period 가 있으면 tq_add(q, t, t->expires + period), 그 다음 fire 콜백 (NULL 일 수 있음). */
    (void)q; (void)now; (void)budget; (void)fire; (void)arg;
    return 0;
}

/* ---- 하네스 ---- */
static int64_t tq_key(const struct rb_node *n, const void *ctx) {
    return (int64_t)(int32_t)(container_of(n, const sw_timer_t, node)->expires - *(const uint32_t *)ctx);
}
static bool tq_ok(const timerq_t *q, size_t n) {
    return rb_validate(&q->tree.rb_root, n, tq_key, &q->now, true) == RB_OK
        && q->tree.rb_leftmost == rb_first(&q->tree.rb_root);
}

#define TQN 256u
static sw_timer_t g_tq[TQN];
static struct { bool active; uint32_t expires, seq; } g_tm[TQN];
static uint32_t g_tseq;
static size_t g_tbad;

/* 모델에서 지금 나와야 할 타이머: 만료된 것 중 (만료 시각, 등록 순서) 최소 */
static int tm_next_due(uint32_t now) {
    int best = -1;
    for (uint32_t i = 0; i < TQN; i++) {
        if (!g_tm[i].active || (int32_t)(now - g_tm[i].expires) < 0) continue;
        if (best < 0) { best = (int)i; continue; }
        int32_t d = (int32_t)(g_tm[i].expires - g_tm[best].expires);
        if (d < 0 || (d == 0 && g_tm[i].seq < g_tm[best].seq)) best = (int)i;
    }
    return best;
}
static void tm_on_fire(timerq_t *q, sw_timer_t *t, void *arg) {
    (void)arg;
    int want = tm_next_due(q->now);
    if (want < 0 || &g_tq[want] != t) g_tbad++;
    if (want >= 0) g_tm[want].active = false;
    else g_tm[t - g_tq].active = false;
}
static bool tq_random(uint32_t ops, uint32_t *wraps) {
    timerq_t q;
    uint32_t now = 0xFFFC0000u;
    size_t active = 0;
    tq_init(&q, now);
    for (uint32_t i = 0; i < TQN; i++) { tq_timer_init(&g_tq[i], 0); g_tm[i].active = false; }
    g_tbad = 0;
    for (uint32_t i = 0; i < ops; i++) {
        uint32_t r = rnd() % 100u, id = rnd() % TQN;
        if (r < 50u) {
            bool was = g_tm[id].active;
            uint32_t exp = q.now + rnd() % 5000u;
            bool ok = tq_add(&q, &g_tq[id], exp);
            if (ok == was) return false;
            if (ok) { g_tm[id].active = true; g_tm[id].expires = exp; g_tm[id].seq = g_tseq++; active++; }
        } else if (r < 70u) {
            bool was = g_tm[id].active;
            if (tq_cancel(&q, &g_tq[id]) != was) return false;
            if (was) { g_tm[id].active = false; active--; }
        } else {
            uint32_t before = q.now, next = q.now + rnd() % 3000u;
            uint32_t n = tq_tick(&q, next, UINT32_MAX, tm_on_fire, NULL);
            active -= n;
            if (next < before) (*wraps)++;
            if (tm_next_due(q.now) >= 0) return false;      /* 만료된 것이 남으면 안 된다 */
        }
        if (g_tbad || !tq_ok(&q, active)) return false;
    }
    return true;
}

static sw_timer_t *g_cancel_target;
static uint32_t g_fire_log[16], g_fire_n;
static void log_fire(timerq_t *q, sw_timer_t *t, void *arg) {
    sw_timer_t *base = arg;
    if (g_fire_n < 16u) g_fire_log[g_fire_n++] = (uint32_t)(t - base);
    if (g_cancel_target) { tq_cancel(q, g_cancel_target); g_cancel_target = NULL; }
}

/* ======================= main ======================= */
int main(void) {
    printf("sizeof(struct rb_node) = %zu, offsetof(item_t, node) = %zu\n", sizeof(struct rb_node), offsetof(item_t, node));
    struct rb_root r = { NULL };

    printf("== Part A: encoding\n");
    {
        static struct rb_node p, c;
        rb_set_parent_color(&c, &p, RB_BLACK);
        CHECK(rb_parent(&c) == &p && rb_color(&c) == RB_BLACK);
        rb_set_parent_color(&c, &p, RB_RED);
        CHECK(rb_parent(&c) == &p && rb_color(&c) == RB_RED);
        rb_set_parent_color(&c, &p, RB_BLACK);
        rb_set_parent(&c, NULL);
        CHECK(rb_parent(&c) == NULL && rb_color(&c) == RB_BLACK);   /* rb_set_parent 는 색을 유지 */
    }

    printf("== Part A: validator catches each violation\n");
    CHECK(fx_validate("((10r)20B(30r))", 3) == RB_OK);
    CHECK(fx_validate("((10B)20r(30B))", 3) == RB_ERR_ROOT_RED);
    CHECK(fx_validate("(((5r)10r)20B(30r))", 4) == RB_ERR_RED_RED);
    CHECK(fx_validate("((10B)20B)", 2) == RB_ERR_BLACK_HEIGHT);
    CHECK(fx_validate("((30r)20B(10r))", 3) == RB_ERR_ORDER);
    CHECK(fx_validate("((10B(25r))20B(30B))", 4) == RB_ERR_ORDER);    /* 25 는 10 의 오른쪽으로는 맞지만 20 보다 크다 */
    CHECK(fx_validate("((10r)20B(30r))", 4) == RB_ERR_COUNT);
    CHECK(fx_validate("((10r)20B(30r))", 2) == RB_ERR_COUNT);
    {
        fx_load(&r, "((10r)20B(30r))");
        fx_find(10)->node.parent_color = (uintptr_t)&fx_find(30)->node;          /* 빨강 유지, 부모만 틀림 */
        CHECK(rb_validate(&r, 3, item_key, NULL, false) == RB_ERR_PARENT);
        fx_load(&r, "((10r)20B)");
        fx_find(10)->node.rb_right = &fx_find(20)->node;                         /* 사이클 */
        CHECK(rb_validate(&r, 2, item_key, NULL, false) != RB_OK);
    }

    printf("== Part A: insert fixups (exact shape and colours)\n");
    CHECK(t_insert("", 7, "(7B)"));
    CHECK(t_insert("(20B)", 10, "((10r)20B)"));
    CHECK(t_insert("((10r)20B(30r))", 5, "(((5r)10B)20B(30B))"));                   /* Case 1, 루트까지 */
    CHECK(t_insert("((10r)20B(30r))", 35, "((10B)20B(30B(35r)))"));                 /* Case 1 거울상 */
    CHECK(t_insert("((((10r)20B(25r))30r(40B))50B(70B))", 5,
                   "((((5r)10B)20r(25B))30B((40B)50r(70B)))"));                     /* Case 1 → 위에서 Case 3 */
    CHECK(t_insert("((10r)20B)", 5, "((5r)10B(20r))"));                             /* Case 3 */
    CHECK(t_insert("((10r)20B)", 15, "((10r)15B(20r))"));                           /* Case 2 → 3 */
    CHECK(t_insert("(20B(30r))", 40, "((20r)30B(40r))"));                           /* Case 3 거울상 */
    CHECK(t_insert("(20B(30r))", 25, "((20r)25B(30r))"));                           /* Case 2 → 3 거울상 */
    {
        fx_load(&r, "((10r)20B(30r))");
        item_t *dup = fx_new(10);
        CHECK(!item_insert(&r, dup) && dump_is(&r, "((10r)20B(30r))"));
    }

    printf("== Part A: traversal\n");
    {
        static item_t it[64];
        struct rb_root t = { NULL };
        for (int i = 0; i < 64; i++) { it[i].key = (i * 37) % 64; RB_CLEAR_NODE(&it[i].node); (void)item_insert(&t, &it[i]); }
        int k = 0;
        bool ok = true;
        const struct rb_node *n = rb_first(&t);
        for (; n && k <= 64; n = rb_next(n), k++) ok = ok && container_of(n, const item_t, node)->key == k;
        CHECK(ok && k == 64);
        k = 63; ok = true;
        n = rb_last(&t);
        for (; n && k >= -1; n = rb_prev(n), k--) ok = ok && container_of(n, const item_t, node)->key == k;
        CHECK(ok && k == -1);
        CHECK(rb_first(&(struct rb_root){ NULL }) == NULL && rb_last(&(struct rb_root){ NULL }) == NULL);
        CHECK(rb_validate(&t, 64, item_key, NULL, false) == RB_OK);
        struct rb_node detached;
        RB_CLEAR_NODE(&detached);
        CHECK(rb_next(&detached) == NULL && rb_prev(&detached) == NULL);
    }
    bool a_ok = g_pass == g_total;

    printf("== Part B: erase — node shapes\n");
    int b0 = g_pass;
    CHECK(t_erase("(7B)", 7, ""));
    CHECK(t_erase("((10r)20B(30r))", 10, "(20B(30r))"));                            /* 빨강 잎: 복구 없음 */
    CHECK(t_erase("((10r)20B)", 20, "(10B)"));                                      /* 빨강 자식을 검정으로 */
    CHECK(t_erase("(20B(30r))", 20, "(30B)"));
    CHECK(t_erase("((10B)20B(30B))", 20, "((10r)30B)"));                            /* 후계자 = 오른쪽 자식 */
    CHECK(t_erase("((10B)20B((30B)40r(50B)))", 20, "((10B)30B(40B(50r)))"));        /* 후계자 = 깊은 왼쪽 끝 */
    CHECK(t_erase("((10B)20B((30B(35r))40r(50B)))", 20, "((10B)30B((35B)40r(50B)))")); /* 후계자에 빨강 자식 */
    printf("== Part B: erase fixups (exact shape and colours)\n");
    CHECK(t_erase("((10B)20B((30B)40r(50B)))", 10, "((20B(30r))40B(50B))"));        /* Case 1 → 2 */
    CHECK(t_erase("(((10B)20r(30B))40B(60B))", 10, "((20B(30r))40B(60B))"));        /* Case 2: 부모 빨강, 멈춤 */
    CHECK(t_erase("((10B)20B(30B))", 10, "(20B(30r))"));                            /* Case 2: 루트로 전파 */
    CHECK(t_erase("((10B)20B((30r)40B))", 10, "((20B)30B(40B))"));                  /* Case 3 → 4 */
    CHECK(t_erase("((10B)20B(30B(40r)))", 10, "((20B)30B(40B))"));                  /* Case 4 */
    CHECK(t_erase("(((10B)20r(30B(40r)))50B(70B))", 10, "(((20B)30r(40B))50B(70B))")); /* Case 4: 부모 색을 물려받는다 */
    CHECK(t_erase("(((5B)10r(20B))30B(40B))", 40, "((5B)10B((20r)30B))"));          /* Case 1 → 2 거울상 */
    CHECK(t_erase("((20B)40B((50B)60r(70B)))", 70, "((20B)40B((50r)60B))"));        /* Case 2 거울상 */
    CHECK(t_erase("((10B(20r))30B(40B))", 40, "((10B)20B(30B))"));                  /* Case 3 → 4 거울상 */
    CHECK(t_erase("(((10r)20B)30B(40B))", 40, "((10B)20B(30B))"));                  /* Case 4 거울상 */
    bool b_ok = a_ok && g_pass - b0 == 17;

    printf("== Part B: exhaustive + random vs reference model\n");
    if (b_ok) {
        memset(&g_st, 0, sizeof g_st);
        uint64_t mi = 0, md = 0;
        CHECK(exhaustive());
        CHECK(random_vs_model(32u, 200000u, 1u, &mi, &md));
        uint64_t mi2 = 0, md2 = 0;
        CHECK(random_vs_model(RKEYS, 1000000u, 1000u, &mi2, &md2));
        printf("INFO insert cases L: %llu/%llu/%llu  R: %llu/%llu/%llu\n",
               (unsigned long long)g_st.ins_case[0][1], (unsigned long long)g_st.ins_case[0][2], (unsigned long long)g_st.ins_case[0][3],
               (unsigned long long)g_st.ins_case[1][1], (unsigned long long)g_st.ins_case[1][2], (unsigned long long)g_st.ins_case[1][3]);
        printf("INFO erase  cases L: %llu/%llu/%llu/%llu  R: %llu/%llu/%llu/%llu\n",
               (unsigned long long)g_st.del_case[0][1], (unsigned long long)g_st.del_case[0][2], (unsigned long long)g_st.del_case[0][3], (unsigned long long)g_st.del_case[0][4],
               (unsigned long long)g_st.del_case[1][1], (unsigned long long)g_st.del_case[1][2], (unsigned long long)g_st.del_case[1][3], (unsigned long long)g_st.del_case[1][4]);
        printf("INFO max rotations per op: insert %llu, erase %llu (small n) / insert %llu, erase %llu (1M ops)\n",
               (unsigned long long)mi, (unsigned long long)md, (unsigned long long)mi2, (unsigned long long)md2);
        bool all = true;
        for (int s = 0; s < 2; s++) {
            for (int c = 1; c <= 3; c++) all = all && g_st.ins_case[s][c] > 0;
            for (int c = 1; c <= 4; c++) all = all && g_st.del_case[s][c] > 0;
        }
        CHECK(all);                               /* 모든 케이스가 양쪽 방향으로 실제로 실행됐다 */
        CHECK(mi <= 2u && mi2 <= 2u);             /* 삽입 회전 ≤ 2 */
        CHECK(md <= 3u && md2 <= 3u);             /* 삭제 회전 ≤ 3 */
    } else {
        printf("SKIP exhaustive/random: unit checks failed\n");
    }

    printf("== Part C: timeout queue\n");
    int c0 = g_pass, ct0 = g_total;
    {
        timerq_t q;
        sw_timer_t t[8];
        uint32_t d = 0;
        for (int i = 0; i < 8; i++) tq_timer_init(&t[i], 0);

        CHECK(tq_time_before(0xFFFFFFF0u, 5u) && !tq_time_before(5u, 0xFFFFFFF0u) && !tq_time_before(7u, 7u));

        tq_init(&q, 1000u);
        CHECK(!tq_next_deadline(&q, &d) && tq_tick(&q, 2000u, 10u, NULL, NULL) == 0u);

        tq_init(&q, 100u);
        CHECK(tq_add(&q, &t[0], 300u) && tq_add(&q, &t[1], 200u) && tq_add(&q, &t[2], 400u));
        CHECK(tq_next_deadline(&q, &d) && d == 200u && tq_ok(&q, 3));
        CHECK(!tq_add(&q, &t[0], 500u));                                /* 이미 등록 */
        CHECK(!tq_add(&q, &t[3], 100u + TQ_WINDOW));                    /* 창 밖 */
        CHECK(tq_cancel(&q, &t[1]) && tq_next_deadline(&q, &d) && d == 300u && tq_ok(&q, 2));   /* leftmost 취소 */
        CHECK(!tq_cancel(&q, &t[1]));                                   /* 멱등 */
        CHECK(tq_cancel(&q, &t[2]) && tq_ok(&q, 1) && tq_cancel(&q, &t[0]) && tq_ok(&q, 0) && !tq_next_deadline(&q, &d));

        /* 동률은 등록 순서대로 */
        tq_init(&q, 0u);
        g_fire_n = 0;
        for (int i = 0; i < 5; i++) CHECK(tq_add(&q, &t[i], 50u));
        CHECK(tq_ok(&q, 5));
        CHECK(tq_tick(&q, 49u, 100u, log_fire, t) == 0u && tq_tick(&q, 50u, 100u, log_fire, t) == 5u);
        CHECK(g_fire_n == 5u && g_fire_log[0] == 0u && g_fire_log[1] == 1u && g_fire_log[2] == 2u && g_fire_log[3] == 3u && g_fire_log[4] == 4u);

        /* 랩어라운드 */
        tq_init(&q, 0xFFFFFFF0u);
        g_fire_n = 0;
        CHECK(tq_add(&q, &t[0], 0x00000005u) && tq_add(&q, &t[1], 0xFFFFFFF8u));
        CHECK(tq_next_deadline(&q, &d) && d == 0xFFFFFFF8u);
        CHECK(tq_tick(&q, 0xFFFFFFFFu, 100u, log_fire, t) == 1u && g_fire_log[0] == 1u);
        CHECK(tq_tick(&q, 0x00000006u, 100u, log_fire, t) == 1u && g_fire_log[1] == 0u && tq_ok(&q, 0));

        /* 주기 타이머 + 처리량 제한 */
        tq_timer_init(&t[0], 10u);
        tq_init(&q, 0u);
        CHECK(tq_add(&q, &t[0], 10u));
        CHECK(tq_tick(&q, 95u, 3u, NULL, NULL) == 3u && t[0].fired == 3u && t[0].expires == 40u);
        CHECK(tq_tick(&q, 95u, 100u, NULL, NULL) == 6u && t[0].fired == 9u && t[0].expires == 100u && tq_ok(&q, 1));

        /* 콜백이 같은 틱에 만료될 다른 타이머를 취소 */
        for (int i = 0; i < 8; i++) tq_timer_init(&t[i], 0);
        tq_init(&q, 0u);
        g_fire_n = 0;
        CHECK(tq_add(&q, &t[0], 5u) && tq_add(&q, &t[1], 5u) && tq_add(&q, &t[2], 6u));
        g_cancel_target = &t[1];
        CHECK(tq_tick(&q, 10u, 100u, log_fire, t) == 2u && g_fire_n == 2u && g_fire_log[0] == 0u && g_fire_log[1] == 2u && tq_ok(&q, 0));
        CHECK(RB_EMPTY_NODE(&t[0].node) && RB_EMPTY_NODE(&t[1].node));
    }
    bool c_ok = b_ok && g_pass - c0 == g_total - ct0;
    if (c_ok) {
        uint32_t wraps = 0;
        CHECK(tq_random(100000u, &wraps));
        CHECK(wraps >= 1u);
        printf("INFO timer model: tick counter wrapped %u time(s)\n", (unsigned)wraps);
    } else {
        printf("SKIP timer random: unit checks failed\n");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
