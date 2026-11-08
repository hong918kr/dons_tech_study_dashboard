/* DS-B06 이중 연결 리스트와 Sentinel 노드
 * 원형(circular) 이중 연결 리스트. sentinel(더미 head)이 항상 있어서 NULL 검사가 없다.
 * 노드는 정적 배열에서만 — malloc 금지.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct DNode {
    struct DNode *prev, *next;
    int val;
} DNode;

typedef struct {
    DNode head;   /* sentinel: 값 없음. head.next = 첫 노드, head.prev = 마지막 노드 */
    size_t len;
} DList;

/* ---------------- Part A: 초기화와 삽입 ---------------- */
/* 빈 리스트 = sentinel 이 자기 자신을 가리키는 고리 */
static void dl_init(DList *l) {
    l->head.next = l->head.prev = &l->head;
    l->len = 0;
}
static bool dl_empty(const DList *l) {
    return l->head.next == &l->head;
}
/* 모든 삽입의 공통 핵심: prev 와 next 사이에 n 을 끼운다 (포인터 4개) */
static void insert_between(DNode *n, DNode *prev, DNode *next) {
    n->prev = prev;                                   /* ① ② 새 노드의 두 팔 */
    n->next = next;
    prev->next = n;                                   /* ③ ④ 이웃이 새 노드를 가리킴 */
    next->prev = n;
}
static void dl_add_head(DList *l, DNode *n) {
    insert_between(n, &l->head, l->head.next);
    l->len++;
}
static void dl_add_tail(DList *l, DNode *n) {
    insert_between(n, l->head.prev, &l->head);
    l->len++;
}

/* ---------------- Part B: 삭제와 splice ---------------- */
/* O(1) 삭제. 떼어낸 노드의 prev/next 는 NULL 로 (재사용 실수를 빨리 드러내기 위해). */
static void dl_remove(DList *l, DNode *n) {
    n->prev->next = n->next;                          /* 이웃끼리 서로 잇는다 — if 없음 */
    n->next->prev = n->prev;
    n->next = n->prev = NULL;
    l->len--;
}
/* src 의 노드 전부를 dst 의 꼬리에 O(1) 로 옮긴다. src 는 빈 리스트가 된다. */
static void dl_splice_tail(DList *dst, DList *src) {
    if (dl_empty(src)) return;                        /* 빈 src 는 sentinel 만 있으므로 반드시 거른다 */
    DNode *first = src->head.next, *last = src->head.prev, *at = dst->head.prev;
    at->next = first;                                 /* dst 의 꼬리 → src 의 첫 노드 */
    first->prev = at;
    last->next = &dst->head;                          /* src 의 마지막 → dst 의 sentinel */
    dst->head.prev = last;
    dst->len += src->len;
    dl_init(src);
}

/* ---------------- Part C: 불변식 검사 ---------------- */
/* 모든 링크 p → p->next 에 대해 p->next->prev == p, NULL 없음,
 * sentinel 로 돌아올 때까지 센 노드 수 == len. 망가진 고리에서도 반드시 멈춘다. */
static bool dl_check(const DList *l) {
    const DNode *s = &l->head, *p = s;
    size_t count = 0;
    do {
        const DNode *nx = p->next;
        if (!nx || nx->prev != p) return false;       /* 링크마다 양방향 일치 */
        p = nx;
        if (p != s && ++count > l->len) return false; /* len 보다 많이 돌면 고리가 샌 것 → 멈춤 */
    } while (p != s);
    return count == l->len;
}

/* ---------------- 테스트 도우미 ---------------- */
/* 앞/뒤 방향으로 값을 꺼낸다. 망가진 리스트에서도 max 에서 멈춘다. */
static int dl_dump(const DList *l, int *out, int max, bool backward) {
    int k = 0;
    const DNode *s = &l->head;
    for (const DNode *p = backward ? s->prev : s->next; p && p != s && k < max;
         p = backward ? p->prev : p->next)
        out[k++] = p->val;
    return k;
}
static bool dl_eq(const DList *l, const int *a, int n) {
    int f[64], b[64];
    if (dl_dump(l, f, 64, false) != n || dl_dump(l, b, 64, true) != n || l->len != (size_t)n)
        return false;
    for (int i = 0; i < n; i++)
        if (f[i] != a[i] || b[n - 1 - i] != a[i]) return false;
    return true;
}

/* 고정 시드 xorshift32 — 랜덤 테스트를 재현 가능하게 */
static uint32_t rng = 0xC0FFEEu;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define NPOOL 32
static DNode rpool[NPOOL];
static int where[NPOOL];            /* -1: 어느 리스트에도 없음, 0: A, 1: B */
static int model[2][NPOOL], mlen[2]; /* 모델: 각 리스트의 노드 번호 순서 */
static long op_count[4];             /* add_head, add_tail, remove, splice */
static int max_len;

static bool matches_model(const DList *l, int w) {
    const DNode *p = l->head.next;
    for (int i = 0; i < mlen[w]; i++, p = p->next)
        if (p != &rpool[model[w][i]]) return false;
    return p == &l->head && l->len == (size_t)mlen[w];
}

/* ops 번 랜덤 연산, 매 연산 뒤 dl_check + 모델 비교. 첫 실패에서 false. */
static bool random_test(int ops) {
    DList L[2] = {0};
    dl_init(&L[0]);
    dl_init(&L[1]);
    for (int i = 0; i < NPOOL; i++) { where[i] = -1; rpool[i].val = i; }
    for (int step = 0; step < ops; step++) {
        uint32_t r = xs32();
        int w = (int)(r >> 8) & 1, op = (int)(r % 8);
        if (op < 4) {                                 /* 0-1 add_head, 2-3 add_tail */
            int i = (int)((r >> 16) % NPOOL), tries = 0;
            while (where[i] != -1 && tries++ < NPOOL) i = (i + 1) % NPOOL;
            if (where[i] != -1) continue;             /* 풀이 다 찼다 */
            if (op < 2) {
                dl_add_head(&L[w], &rpool[i]);
                for (int k = mlen[w]; k > 0; k--) model[w][k] = model[w][k - 1];
                model[w][0] = i;
                op_count[0]++;
            } else {
                dl_add_tail(&L[w], &rpool[i]);
                model[w][mlen[w]] = i;
                op_count[1]++;
            }
            mlen[w]++;
            where[i] = w;
        } else if (op < 7) {                          /* 4-6 remove */
            if (mlen[w] == 0) continue;
            int k = (int)((r >> 16) % (uint32_t)mlen[w]), i = model[w][k];
            dl_remove(&L[w], &rpool[i]);
            for (; k < mlen[w] - 1; k++) model[w][k] = model[w][k + 1];
            mlen[w]--;
            where[i] = -1;
            op_count[2]++;
        } else {                                      /* 7 splice: L[1-w] → L[w] 꼬리 */
            int s = 1 - w;
            dl_splice_tail(&L[w], &L[s]);
            for (int k = 0; k < mlen[s]; k++) { model[w][mlen[w] + k] = model[s][k]; where[model[s][k]] = w; }
            mlen[w] += mlen[s];
            mlen[s] = 0;
            op_count[3]++;
        }
        if (mlen[w] > max_len) max_len = mlen[w];
        if (!dl_check(&L[0]) || !dl_check(&L[1]) || !matches_model(&L[0], 0) || !matches_model(&L[1], 1)) {
            printf("  random_test: step %d op %d 에서 불일치\n", step, op);
            return false;
        }
    }
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    DNode n[8] = {0};                                     /* prev/next 도 0 으로 — 미초기화 읽기는 UB */
    for (int i = 0; i < 8; i++) n[i].val = i;

    /* Part A */
    DList l = {0};
    dl_init(&l);
    CHECK(dl_empty(&l) && l.len == 0);
    CHECK(l.head.next == &l.head && l.head.prev == &l.head);
    dl_add_tail(&l, &n[1]);
    dl_add_tail(&l, &n[2]);
    dl_add_tail(&l, &n[3]);
    dl_add_head(&l, &n[0]);
    CHECK(dl_eq(&l, (int[]){0, 1, 2, 3}, 4));            /* 앞으로도, 뒤로도 */
    CHECK(!dl_empty(&l) && l.head.next == &n[0] && l.head.prev == &n[3]);

    /* Part B: 가운데 / 맨 앞 / 맨 뒤 삭제가 전부 같은 코드 */
    dl_remove(&l, &n[2]);
    CHECK(dl_eq(&l, (int[]){0, 1, 3}, 3));
    CHECK(n[2].next == NULL && n[2].prev == NULL);
    dl_remove(&l, &n[0]);
    dl_remove(&l, &n[3]);
    CHECK(dl_eq(&l, (int[]){1}, 1));
    dl_remove(&l, &n[1]);
    CHECK(dl_empty(&l) && l.head.next == &l.head && l.head.prev == &l.head);
    dl_add_head(&l, &n[2]);                               /* 떼어낸 노드 재사용 */
    CHECK(dl_eq(&l, (int[]){2}, 1));

    DList a = {0}, b = {0}, e = {0};
    dl_init(&a); dl_init(&b); dl_init(&e);
    dl_add_tail(&a, &n[4]); dl_add_tail(&a, &n[5]);
    dl_add_tail(&b, &n[6]); dl_add_tail(&b, &n[7]);
    dl_splice_tail(&a, &e);                               /* 빈 src → 변화 없음 */
    CHECK(dl_eq(&a, (int[]){4, 5}, 2));
    dl_splice_tail(&a, &b);
    CHECK(dl_eq(&a, (int[]){4, 5, 6, 7}, 4) && dl_empty(&b) && b.len == 0);
    dl_splice_tail(&e, &a);                               /* 빈 dst ← 전체 */
    CHECK(dl_eq(&e, (int[]){4, 5, 6, 7}, 4) && dl_empty(&a));

    /* Part C: 정상 리스트는 통과, 망가진 리스트는 잡아낸다 */
    CHECK(dl_check(&e) && dl_check(&a) && dl_check(&l));
    n[6].prev = &e.head;                                  /* 5 ← 6 링크를 망가뜨림 */
    CHECK(!dl_check(&e));
    n[6].prev = &n[5];
    CHECK(dl_check(&e));
    e.len = 5;                                            /* len 불일치 */
    CHECK(!dl_check(&e));
    e.len = 4;
    n[7].next = &n[5];                                    /* sentinel 을 거치지 않는 고리 */
    CHECK(!dl_check(&e));                                 /* 무한 루프 없이 false */
    n[7].next = &e.head;
    n[5].next = NULL;
    CHECK(!dl_check(&e));
    n[5].next = &n[6];
    CHECK(dl_check(&e));

    CHECK(random_test(20000));
    printf("random: add_head %ld, add_tail %ld, remove %ld, splice %ld, max len %d\n",
           op_count[0], op_count[1], op_count[2], op_count[3], max_len);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
