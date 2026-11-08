/* DS-B06 추가 연습 — 한 번에 검증
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct DNode {
    struct DNode *prev, *next;
    int val;
} DNode;

typedef struct {
    DNode head;
    size_t len;
} DList;

static void dl_init(DList *l) { l->head.next = l->head.prev = &l->head; l->len = 0; }
static bool dl_empty(const DList *l) { return l->head.next == &l->head; }
static void insert_between(DNode *n, DNode *prev, DNode *next) {
    n->prev = prev; n->next = next; prev->next = n; next->prev = n;
}
static void dl_add_head(DList *l, DNode *n) { insert_between(n, &l->head, l->head.next); l->len++; }
static void dl_add_tail(DList *l, DNode *n) { insert_between(n, l->head.prev, &l->head); l->len++; }
static void dl_remove(DList *l, DNode *n) {
    n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = NULL; l->len--;
}
static bool dl_check(const DList *l) {
    const DNode *s = &l->head, *p = s;
    size_t count = 0;
    do {
        const DNode *nx = p->next;
        if (!nx || nx->prev != p) return false;
        p = nx;
        if (p != s && ++count > l->len) return false;
    } while (p != s);
    return count == l->len;
}
static bool dl_eq(const DList *l, const int *a, int n) {
    if (!dl_check(l) || l->len != (size_t)n) return false;
    const DNode *p = l->head.next;
    for (int i = 0; i < n; i++, p = p->next)
        if (p->val != a[i]) return false;
    return true;
}

/* 연습 1: LRU "touch" — 이미 리스트에 있는 노드를 맨 앞으로. 떼고 붙이면 끝, O(1). */
static void dl_move_to_head(DList *l, DNode *n) {
    if (l->head.next == n) return;                 /* 이미 맨 앞 */
    n->prev->next = n->next;                       /* 떼기 (len 은 그대로) */
    n->next->prev = n->prev;
    insert_between(n, &l->head, l->head.next);
}

/* 연습 2: 제자리 뒤집기 — sentinel 을 포함한 모든 노드의 prev/next 를 맞바꾼다. */
static void dl_reverse(DList *l) {
    DNode *p = &l->head;
    do {
        DNode *t = p->next;
        p->next = p->prev;
        p->prev = t;
        p = t;                                     /* 원래 next 로 전진 */
    } while (p != &l->head);
}

/* 연습 3: 순회 중 삭제 — 다음 노드를 미리 저장해 두는 "safe" 순회 (list_for_each_safe) */
#define dl_for_each_safe(p, tmp, l) \
    for ((p) = (l)->head.next, (tmp) = (p)->next; (p) != &(l)->head; (p) = (tmp), (tmp) = (p)->next)
static int dl_remove_even(DList *l) {
    int cnt = 0;
    DNode *p, *tmp;
    dl_for_each_safe(p, tmp, l)
        if (p->val % 2 == 0) { dl_remove(l, p); cnt++; }
    return cnt;
}

/* 연습 4: at 다음부터 끝까지를 빈 리스트 dst 로 잘라 옮긴다 (list_cut_position 의 친척).
 * 포인터 수정은 O(1) 이지만 len 을 유지하려면 옮긴 개수를 세야 해서 O(k). */
static void dl_split_after(DList *src, DNode *at, DList *dst) {
    if (at->next == &src->head) return;            /* 뒤에 아무것도 없음 */
    size_t k = 0;
    for (DNode *p = at->next; p != &src->head; p = p->next) k++;
    DNode *first = at->next, *last = src->head.prev;
    dst->head.next = first; first->prev = &dst->head;
    dst->head.prev = last;  last->next = &dst->head;
    at->next = &src->head;  src->head.prev = at;
    dst->len = k;
    src->len -= k;
}

/* 연습 5: 왼쪽 회전 k 칸 — 노드를 옮기지 말고 sentinel 하나를 k 칸 뒤로 옮긴다.
 * 원형 리스트에서 "어디가 시작인가"는 sentinel 위치뿐. 탐색 O(k) + 재연결 O(1). */
static void dl_rotate_left(DList *l, size_t k) {
    if (l->len < 2 || (k %= l->len) == 0) return;
    DNode *s = &l->head, *t = s;
    for (size_t i = 0; i < k; i++) t = t->next;    /* t = 회전 후의 꼬리 */
    s->prev->next = s->next;                       /* sentinel 을 고리에서 떼고 */
    s->next->prev = s->prev;
    insert_between(s, t, t->next);                 /* t 뒤에 다시 끼운다 */
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    DNode n[8] = {0};
    for (int i = 0; i < 8; i++) n[i].val = i;
    DList l;

    /* 연습 1 */
    dl_init(&l);
    for (int i = 0; i < 4; i++) dl_add_tail(&l, &n[i]);  /* 0 1 2 3 */
    dl_move_to_head(&l, &n[2]);
    CHECK(dl_eq(&l, (int[]){2, 0, 1, 3}, 4));
    dl_move_to_head(&l, &n[3]);                           /* 꼬리 → 머리 */
    dl_move_to_head(&l, &n[3]);                           /* 이미 머리 */
    CHECK(dl_eq(&l, (int[]){3, 2, 0, 1}, 4));

    /* 연습 2 */
    dl_reverse(&l);
    CHECK(dl_eq(&l, (int[]){1, 0, 2, 3}, 4));
    DList e;
    dl_init(&e);
    dl_reverse(&e);                                       /* 빈 리스트도 안전 */
    CHECK(dl_empty(&e) && dl_check(&e));

    /* 연습 3 */
    dl_init(&l);
    for (int i = 7; i >= 0; i--) dl_add_head(&l, &n[i]); /* 0 1 2 ... 7 */
    CHECK(dl_remove_even(&l) == 4 && dl_eq(&l, (int[]){1, 3, 5, 7}, 4));

    /* 연습 4 */
    DList d;
    dl_init(&d);
    dl_split_after(&l, &n[3], &d);                        /* 1 3 | 5 7 */
    CHECK(dl_eq(&l, (int[]){1, 3}, 2) && dl_eq(&d, (int[]){5, 7}, 2));
    dl_init(&e);
    dl_split_after(&d, &n[7], &e);                        /* 마지막 노드 뒤 → 아무것도 안 옮김 */
    CHECK(dl_eq(&d, (int[]){5, 7}, 2) && dl_empty(&e));

    /* 연습 5 */
    dl_init(&l);
    for (int i = 0; i < 5; i++) dl_add_tail(&l, &n[i]);  /* 0 1 2 3 4 */
    dl_rotate_left(&l, 2);
    CHECK(dl_eq(&l, (int[]){2, 3, 4, 0, 1}, 5));
    dl_rotate_left(&l, 5);                                /* 한 바퀴 → 그대로 */
    CHECK(dl_eq(&l, (int[]){2, 3, 4, 0, 1}, 5));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
