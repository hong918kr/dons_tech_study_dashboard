/* DS-B07 인덱스 링크 리스트: 정적 노드 풀
 * 노드 64개 정적 배열 + uint8_t next 인덱스 + free list. malloc 없음.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define POOL_N 64u
#define NIL    0xFFu              /* "다음 없음". 255 는 노드 번호로 쓰지 않는다 */

typedef struct {
    uint16_t val;                 /* 페이로드: 예) ADC 샘플, 명령 ID */
    uint8_t  next;                /* 다음 노드의 배열 인덱스 (포인터 아님) */
} inode_t;

typedef struct {
    inode_t node[POOL_N];
    uint8_t free_head;            /* free list 의 첫 노드 (NIL = 풀 고갈) */
    uint8_t free_cnt;
} pool_t;

typedef struct {
    uint8_t head, tail, len;      /* 리스트 헤더도 3바이트 */
} ilist_t;

/* ---------------- Part A: 정적 노드 풀 (free list) ---------------- */
static void pool_init(pool_t *p) {
    for (unsigned i = 0; i < POOL_N; i++)          /* 0→1→2→...→63→NIL */
        p->node[i].next = (uint8_t)((i + 1u < POOL_N) ? i + 1u : NIL);
    p->free_head = 0;
    p->free_cnt = POOL_N;
}
static uint8_t node_alloc(pool_t *p) {
    uint8_t i = p->free_head;
    if (i == NIL) return NIL;                      /* 풀 고갈 */
    p->free_head = p->node[i].next;                /* free list 에서 pop */
    p->node[i].next = NIL;
    p->free_cnt--;
    return i;
}
static void node_free(pool_t *p, uint8_t i) {
    p->node[i].next = p->free_head;                /* free list 에 push (LIFO) */
    p->free_head = i;
    p->free_cnt++;
}

/* ---------------- Part B: 리스트 연산 (풀 하나를 여러 리스트가 공유) ---------------- */
static void list_init(ilist_t *l) { l->head = l->tail = NIL; l->len = 0; }

static bool list_push_front(pool_t *p, ilist_t *l, uint16_t v) {
    uint8_t i = node_alloc(p);
    if (i == NIL) return false;
    p->node[i].val = v;
    p->node[i].next = l->head;
    l->head = i;
    if (l->tail == NIL) l->tail = i;               /* 빈 리스트였으면 tail 도 i */
    l->len++;
    return true;
}
static bool list_push_back(pool_t *p, ilist_t *l, uint16_t v) {
    uint8_t i = node_alloc(p);
    if (i == NIL) return false;
    p->node[i].val = v;
    p->node[i].next = NIL;
    if (l->tail == NIL) l->head = i;               /* 빈 리스트 */
    else p->node[l->tail].next = i;
    l->tail = i;
    l->len++;
    return true;
}
static bool list_pop_front(pool_t *p, ilist_t *l, uint16_t *out) {
    uint8_t i = l->head;
    if (i == NIL) return false;
    *out = p->node[i].val;
    l->head = p->node[i].next;
    if (l->head == NIL) l->tail = NIL;             /* 마지막 노드였다 */
    l->len--;
    node_free(p, i);
    return true;
}
/* 값이 v 인 첫 노드를 떼어 풀에 돌려준다 */
static bool list_remove(pool_t *p, ilist_t *l, uint16_t v) {
    uint8_t prev = NIL;
    for (uint8_t i = l->head; i != NIL; prev = i, i = p->node[i].next) {
        if (p->node[i].val != v) continue;
        if (prev == NIL) l->head = p->node[i].next;
        else p->node[prev].next = p->node[i].next;
        if (l->tail == i) l->tail = prev;          /* 꼬리를 지웠으면 tail 갱신 */
        l->len--;
        node_free(p, i);
        return true;
    }
    return false;
}
/* 순회: 값을 out[] 에 복사하고 개수를 돌려준다 */
static unsigned list_to_array(const pool_t *p, const ilist_t *l, uint16_t *out, unsigned max) {
    unsigned n = 0;
    for (uint8_t i = l->head; i != NIL && n < max; i = p->node[i].next)
        out[n++] = p->node[i].val;
    return n;
}

/* ---------------- Part C: 불변식 검사 + RAM 비교 ---------------- */
/* 모든 노드가 "free list 또는 리스트 중 정확히 한 곳"에 있어야 한다.
 * 범위 밖 인덱스, 사이클, 두 곳에 걸친 노드, 잃어버린 노드(leak), len/tail/free_cnt 불일치 → false */
static bool pool_check(const pool_t *p, const ilist_t *ls, unsigned nls) {
    uint8_t seen[POOL_N] = {0};
    unsigned total = 0, n = 0;
    for (uint8_t i = p->free_head; i != NIL; i = p->node[i].next) {
        if (i >= POOL_N || seen[i]) return false;  /* 범위 밖 / 사이클 / 공유 */
        seen[i] = 1; n++;
    }
    if (n != p->free_cnt) return false;
    total += n;
    for (unsigned k = 0; k < nls; k++) {
        uint8_t last = NIL;
        n = 0;
        for (uint8_t i = ls[k].head; i != NIL; i = p->node[i].next) {
            if (i >= POOL_N || seen[i]) return false;
            seen[i] = 1; n++; last = i;
        }
        if (n != ls[k].len || last != ls[k].tail) return false;
        total += n;
    }
    return total == POOL_N;                        /* 빠진 노드 = 누수 */
}

/* 비교 대상: 같은 페이로드의 포인터 버전 */
typedef struct pnode { uint16_t val; struct pnode *next; } pnode_t;     /* 이 호스트 */
typedef struct { pnode_t *head, *tail; uint8_t len; } plist_t;
typedef struct { uint16_t val; uint32_t next; } pnode32_t;              /* Cortex-M 모델: 포인터 4B */
typedef struct { uint32_t head, tail; uint8_t len; } plist32_t;

static void ram_report(void) {
    printf("== RAM 비교 (POOL_N=%u, payload uint16_t) ==\n", POOL_N);
    printf("%-30s %7s %8s %7s\n", "layout", "node B", "pool B", "head B");
    printf("%-30s %7zu %8zu %7zu\n", "index  (uint8_t next)",
           sizeof(inode_t), sizeof(inode_t) * POOL_N, sizeof(ilist_t));
    printf("%-30s %7zu %8zu %7zu\n", "pointer (this host)",
           sizeof(pnode_t), sizeof(pnode_t) * POOL_N, sizeof(plist_t));
    printf("%-30s %7zu %8zu %7zu\n", "pointer (32-bit model)",
           sizeof(pnode32_t), sizeof(pnode32_t) * POOL_N, sizeof(plist32_t));
    printf("sizeof(pool_t) = %zu (노드 + free_head + free_cnt)\n", sizeof(pool_t));
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2026u;                     /* 고정 시드 xorshift32 → 재현 가능 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static bool same(const pool_t *p, const ilist_t *l, const uint16_t *exp, unsigned n) {
    uint16_t buf[POOL_N];
    unsigned got = list_to_array(p, l, buf, POOL_N);
    if (got != n || l->len != n) return false;
    for (unsigned i = 0; i < n; i++) if (buf[i] != exp[i]) return false;
    return true;
}

static pool_t P;

int main(void) {
    /* Part A: 풀 */
    pool_init(&P);
    CHECK(P.free_cnt == 64 && P.free_head == 0);
    uint8_t got[POOL_N + 1], seen[POOL_N] = {0};
    bool distinct = true;
    for (unsigned k = 0; k < POOL_N; k++) {
        got[k] = node_alloc(&P);
        if (got[k] >= POOL_N || seen[got[k]]) distinct = false;
        else seen[got[k]] = 1;
    }
    CHECK(distinct);                               /* 64개 모두 서로 다른 유효 인덱스 */
    CHECK(node_alloc(&P) == NIL);                  /* 65번째는 실패 */
    CHECK(P.free_cnt == 0);
    node_free(&P, 10);
    node_free(&P, 20);
    CHECK(P.free_cnt == 2);
    CHECK(node_alloc(&P) == 20);                   /* LIFO: 마지막에 반납한 것부터 */
    CHECK(node_alloc(&P) == 10);

    /* Part B: 리스트 */
    pool_init(&P);
    ilist_t L[3];
    for (unsigned k = 0; k < 3; k++) list_init(&L[k]);
    list_push_back(&P, &L[0], 1);
    list_push_back(&P, &L[0], 2);
    list_push_back(&P, &L[0], 3);
    list_push_front(&P, &L[0], 0);
    CHECK(same(&P, &L[0], (const uint16_t[]){0, 1, 2, 3}, 4));
    CHECK(L[0].tail != NIL && P.node[L[0].tail].val == 3);
    CHECK(list_remove(&P, &L[0], 2) == true);
    CHECK(same(&P, &L[0], (const uint16_t[]){0, 1, 3}, 3));
    CHECK(list_remove(&P, &L[0], 3) == true);     /* 꼬리 삭제 → tail 갱신 필요 */
    list_push_back(&P, &L[0], 9);
    CHECK(same(&P, &L[0], (const uint16_t[]){0, 1, 9}, 3));
    CHECK(list_remove(&P, &L[0], 42) == false);
    uint16_t v = 0xFFFF;
    CHECK(list_pop_front(&P, &L[0], &v) == true && v == 0);
    CHECK(P.free_cnt == POOL_N - 2);
    unsigned pushed = 0;
    while (list_push_back(&P, &L[1], (uint16_t)pushed)) pushed++;
    CHECK(pushed == POOL_N - 2);                   /* 풀이 빌 때까지 들어가고 멈춘다 */
    CHECK(list_push_front(&P, &L[2], 7) == false);

    /* Part C: 불변식 검사 */
    CHECK(pool_check(&P, L, 3) == true);
    while (list_pop_front(&P, &L[1], &v)) {}
    CHECK(pool_check(&P, L, 3) == true && P.free_cnt == POOL_N - 2);
    bool cycle_caught = false;
    if (L[0].tail != NIL) {
        uint8_t save = P.node[L[0].tail].next;
        P.node[L[0].tail].next = L[0].head;        /* 일부러 사이클을 만든다 */
        cycle_caught = (pool_check(&P, L, 3) == false);   /* 무한 루프 없이 false */
        P.node[L[0].tail].next = save;
    }
    CHECK(cycle_caught);
    uint8_t leak = node_alloc(&P);                 /* 할당하고 어디에도 안 붙임 = 누수 */
    CHECK(pool_check(&P, L, 3) == false);
    node_free(&P, leak);
    CHECK(pool_check(&P, L, 3) == true);

    /* 랜덤 스트레스: 리스트 3개 + 참조 모델(평범한 배열) 비교 */
    pool_init(&P);
    for (unsigned k = 0; k < 3; k++) list_init(&L[k]);
    uint16_t ref[3][POOL_N];
    unsigned rn[3] = {0, 0, 0}, mismatch = 0, badcheck = 0;
    for (unsigned step = 0; step < 20000; step++) {
        unsigned k = xs32() % 3, op = xs32() % 4;
        uint16_t x = (uint16_t)(xs32() % 50);
        bool room = rn[0] + rn[1] + rn[2] < POOL_N;
        if (op == 0) {                             /* push_front */
            if (list_push_front(&P, &L[k], x) != room) mismatch++;
            if (room) { for (unsigned j = rn[k]; j > 0; j--) ref[k][j] = ref[k][j - 1]; ref[k][0] = x; rn[k]++; }
        } else if (op == 1) {                      /* push_back */
            if (list_push_back(&P, &L[k], x) != room) mismatch++;
            if (room) ref[k][rn[k]++] = x;
        } else if (op == 2) {                      /* pop_front */
            uint16_t o = 0;
            bool ok = list_pop_front(&P, &L[k], &o);
            if (ok != (rn[k] > 0) || (ok && o != ref[k][0])) mismatch++;
            if (rn[k] > 0) { for (unsigned j = 1; j < rn[k]; j++) ref[k][j - 1] = ref[k][j]; rn[k]--; }
        } else {                                   /* remove(x) */
            unsigned j = 0;
            while (j < rn[k] && ref[k][j] != x) j++;
            if (list_remove(&P, &L[k], x) != (j < rn[k])) mismatch++;
            if (j < rn[k]) { for (; j + 1 < rn[k]; j++) ref[k][j] = ref[k][j + 1]; rn[k]--; }
        }
        if (!same(&P, &L[k], ref[k], rn[k])) mismatch++;
        if (!pool_check(&P, L, 3)) badcheck++;
    }
    CHECK(mismatch == 0);
    CHECK(badcheck == 0);

    /* RAM 비교: 실제 sizeof 를 출력하고 크기 관계만 검사 */
    ram_report();
    CHECK(sizeof(inode_t) < sizeof(pnode32_t));
    CHECK(sizeof(ilist_t) < sizeof(plist32_t));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
