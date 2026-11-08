/* DS-B07 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 64u
#define NIL    0xFFu

typedef struct { uint16_t val; uint8_t next; } inode_t;
typedef struct { inode_t node[POOL_N]; uint8_t free_head, free_cnt; uint64_t used; } pool_t;
typedef struct { uint8_t head, tail, len; } ilist_t;

static void pool_init(pool_t *p) {
    for (unsigned i = 0; i < POOL_N; i++)
        p->node[i].next = (uint8_t)((i + 1u < POOL_N) ? i + 1u : NIL);
    p->free_head = 0; p->free_cnt = POOL_N; p->used = 0;
}
static uint8_t node_alloc(pool_t *p) {
    uint8_t i = p->free_head;
    if (i == NIL) return NIL;
    p->free_head = p->node[i].next;
    p->node[i].next = NIL;
    p->free_cnt--;
    p->used |= 1ull << i;                       /* 연습 2: 사용 중 표시 */
    return i;
}
/* 연습 2: double free / 범위 밖 인덱스를 거부하는 free */
static bool node_free_safe(pool_t *p, uint8_t i) {
    if (i >= POOL_N || !(p->used & (1ull << i))) return false;   /* 범위 밖 or 이미 free */
    p->used &= ~(1ull << i);
    p->node[i].next = p->free_head;
    p->free_head = i;
    p->free_cnt++;
    return true;
}
static void list_init(ilist_t *l) { l->head = l->tail = NIL; l->len = 0; }
static bool list_push_back(pool_t *p, ilist_t *l, uint16_t v) {
    uint8_t i = node_alloc(p);
    if (i == NIL) return false;
    p->node[i].val = v; p->node[i].next = NIL;
    if (l->tail == NIL) l->head = i; else p->node[l->tail].next = i;
    l->tail = i; l->len++;
    return true;
}
static unsigned list_to_array(const pool_t *p, const ilist_t *l, uint16_t *out, unsigned max) {
    unsigned n = 0;
    for (uint8_t i = l->head; i != NIL && n < max; i = p->node[i].next) out[n++] = p->node[i].val;
    return n;
}

/* 연습 1: 제자리 뒤집기 — 노드는 그대로, next 인덱스만 바꾼다 */
static void list_reverse(pool_t *p, ilist_t *l) {
    uint8_t prev = NIL, cur = l->head;
    l->tail = cur;                               /* 옛 head 가 새 tail */
    while (cur != NIL) {
        uint8_t nxt = p->node[cur].next;
        p->node[cur].next = prev;
        prev = cur;
        cur = nxt;
    }
    l->head = prev;
}

/* 연습 3: SoA(Struct of Arrays) 풀 — 패딩이 사라진다 */
typedef struct {
    uint16_t val[POOL_N];
    uint8_t  next[POOL_N];
    uint8_t  free_head, free_cnt;
} soa_pool_t;
static void soa_init(soa_pool_t *p) {
    for (unsigned i = 0; i < POOL_N; i++) p->next[i] = (uint8_t)((i + 1u < POOL_N) ? i + 1u : NIL);
    p->free_head = 0; p->free_cnt = POOL_N;
}
static uint8_t soa_alloc(soa_pool_t *p) {
    uint8_t i = p->free_head;
    if (i == NIL) return NIL;
    p->free_head = p->next[i]; p->next[i] = NIL; p->free_cnt--;
    return i;
}
static void soa_free(soa_pool_t *p, uint8_t i) { p->next[i] = p->free_head; p->free_head = i; p->free_cnt++; }

/* 연습 4: 정렬 삽입 — 마감 시각(deadline) 오름차순 타이머 리스트. 같은 값은 뒤에 (FIFO 안정성) */
static bool list_insert_sorted(pool_t *p, ilist_t *l, uint16_t v) {
    uint8_t i = node_alloc(p);
    if (i == NIL) return false;
    p->node[i].val = v;
    uint8_t prev = NIL, cur = l->head;
    while (cur != NIL && p->node[cur].val <= v) { prev = cur; cur = p->node[cur].next; }
    p->node[i].next = cur;
    if (prev == NIL) l->head = i; else p->node[prev].next = i;
    if (cur == NIL) l->tail = i;
    l->len++;
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool same(const pool_t *p, const ilist_t *l, const uint16_t *exp, unsigned n) {
    uint16_t buf[POOL_N];
    if (list_to_array(p, l, buf, POOL_N) != n || l->len != n) return false;
    for (unsigned i = 0; i < n; i++) if (buf[i] != exp[i]) return false;
    return true;
}

static pool_t P;
static soa_pool_t S;

int main(void) {
    ilist_t L;

    /* 연습 1 */
    pool_init(&P); list_init(&L);
    for (uint16_t v = 1; v <= 5; v++) list_push_back(&P, &L, v);
    list_reverse(&P, &L);
    CHECK(same(&P, &L, (const uint16_t[]){5, 4, 3, 2, 1}, 5));
    CHECK(P.node[L.tail].val == 1 && P.node[L.tail].next == NIL);
    list_push_back(&P, &L, 0);                   /* tail 이 맞아야 뒤에 붙는다 */
    CHECK(same(&P, &L, (const uint16_t[]){5, 4, 3, 2, 1, 0}, 6));
    ilist_t E; list_init(&E); list_reverse(&P, &E);
    CHECK(E.head == NIL && E.tail == NIL);

    /* 연습 2 */
    pool_init(&P);
    uint8_t a = node_alloc(&P);
    CHECK(node_free_safe(&P, a) == true);
    CHECK(node_free_safe(&P, a) == false);      /* double free 거부 */
    CHECK(node_free_safe(&P, 200) == false);    /* 범위 밖 */
    CHECK(node_free_safe(&P, 5) == false);      /* 한 번도 할당 안 된 노드 */
    CHECK(P.free_cnt == POOL_N);

    /* 연습 3 */
    soa_init(&S);
    unsigned n = 0;
    while (soa_alloc(&S) != NIL) n++;
    CHECK(n == POOL_N && S.free_cnt == 0);
    soa_free(&S, 7);
    CHECK(soa_alloc(&S) == 7);
    printf("AoS inode_t[64] = %zu B, SoA val[]+next[] = %zu B (sizeof(soa_pool_t)=%zu)\n",
           sizeof(inode_t) * POOL_N, sizeof(S.val) + sizeof(S.next), sizeof(soa_pool_t));
    CHECK(sizeof(S.val) + sizeof(S.next) == 192);
    CHECK(sizeof(S.val) + sizeof(S.next) < sizeof(inode_t) * POOL_N);

    /* 연습 4 */
    pool_init(&P); list_init(&L);
    const uint16_t in[] = {30, 10, 20, 10, 50, 5};
    for (unsigned i = 0; i < 6; i++) list_insert_sorted(&P, &L, in[i]);
    CHECK(same(&P, &L, (const uint16_t[]){5, 10, 10, 20, 30, 50}, 6));
    CHECK(P.node[L.tail].val == 50);
    uint8_t first10 = P.node[L.head].next;      /* 같은 10 중 먼저 넣은 것이 앞 (안정) */
    CHECK(first10 == 1);                        /* 두 번째로 할당된 노드(인덱스 1)가 첫 10 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
