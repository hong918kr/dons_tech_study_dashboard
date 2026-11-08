/* DS-M01 추가 연습 — 한 번에 검증
 * 1 list_cut_position (앞부분 O(1) 잘라내기)  2 우선순위 정렬 삽입 (순회 끝의 pos 활용)
 * 3 멱등 취소 (list_del_init + list_empty)    4 중첩/배열 멤버 container_of
 * 5 list_replace_init (자리 넘겨주기)
 * 리스트 구현은 solution.c 와 같다 (자기완결).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct list_head { struct list_head *next, *prev; };
#define container_of(ptr, type, member)                     \
    ((void)sizeof((ptr) == &((type *)0)->member),           \
     (type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(head, type, member) list_entry((head)->next, type, member)
#define list_next_entry(pos, member) list_entry((pos)->member.next, __typeof__(*(pos)), member)
#define list_for_each_entry(pos, head, member)                    \
    for (pos = list_first_entry(head, __typeof__(*(pos)), member); \
         &pos->member != (head);                                   \
         pos = list_next_entry(pos, member))

static inline void INIT_LIST_HEAD(struct list_head *h) { h->next = h; h->prev = h; }
static inline bool list_empty(const struct list_head *h) { return h->next == h; }
static inline void list_add_between(struct list_head *n, struct list_head *prev, struct list_head *next) {
    next->prev = n; n->next = next; n->prev = prev; prev->next = n;
}
static inline void list_add_tail(struct list_head *n, struct list_head *head) {
    list_add_between(n, head->prev, head);
}
static inline void list_del_entry(struct list_head *e) { e->next->prev = e->prev; e->prev->next = e->next; }
static inline void list_del_init(struct list_head *e) { list_del_entry(e); INIT_LIST_HEAD(e); }

/* 심판: 고리 일관성 + 길이. 노드 주소는 pool 의 link(i) 와 비교로만 식별 */
static int walk(const struct list_head *head, struct list_head *(*link)(int), int npool, int *out, int max) {
    const struct list_head *p = head;
    for (int k = 0;; k++) {
        const struct list_head *nx = p->next;
        if (nx == head) return head->prev == p ? k : -1;
        int idx = -1;
        for (int i = 0; i < npool && idx < 0; i++) if (nx == link(i)) idx = i;
        if (k >= max || idx < 0 || nx->prev != p) return -1;
        out[k] = idx;
        p = nx;
    }
}

static uint32_t rng = 0xDEADBEEFu;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- 연습 1: list_cut_position ---------- */
/* head 의 첫 노드 ~ entry(포함)를 빈 list 로 옮긴다. entry == head 면 아무것도 안 옮김. O(1) */
static void list_cut_position(struct list_head *list, struct list_head *head, struct list_head *entry) {
    INIT_LIST_HEAD(list);
    if (list_empty(head) || entry == head) return;
    struct list_head *new_first = entry->next;
    list->next = head->next;
    list->next->prev = list;
    list->prev = entry;
    entry->next = list;
    head->next = new_first;
    new_first->prev = head;
}

/* ---------- 연습 2: 우선순위 정렬 삽입 ---------- */
struct ptask { int id, prio; struct list_head node; };
/* prio 작을수록 먼저. 같은 prio 는 먼저 온 것이 앞 (FIFO, 안정). 루프가 끝까지 돌면 &pos->node == head */
static void insert_by_prio(struct list_head *head, struct ptask *t) {
    struct ptask *pos;
    list_for_each_entry(pos, head, node)
        if (pos->prio > t->prio) break;
    list_add_tail(&t->node, &pos->node);                  /* pos 바로 앞 (끝까지 갔으면 head 앞 = 꼬리) */
}

/* ---------- 연습 3: 멱등 취소 ---------- */
struct tmr { int id; struct list_head node; };
static bool tmr_pending(const struct tmr *t) { return !list_empty(&t->node); }
static bool tmr_cancel(struct tmr *t) {
    if (!tmr_pending(t)) return false;                    /* 이미 취소됐거나 한 번도 안 걸림 */
    list_del_init(&t->node);
    return true;
}

/* ---------- 연습 4: 중첩 · 배열 멤버 ---------- */
struct desc {
    uint32_t addr, len;
    struct { uint8_t flags; struct list_head link[2]; } q;  /* link[0]: hw 큐, link[1]: 완료 큐 */
};
static struct desc *desc_of_done(struct list_head *p) { return container_of(p, struct desc, q.link[1]); }
static struct desc *desc_of_hw(struct list_head *p) { return container_of(p, struct desc, q.link[0]); }

/* ---------- 연습 5: list_replace_init ---------- */
/* new 가 old 의 자리를 그대로 차지하고, old 는 자기 고리 (어디에도 없음) */
static void list_replace_init(struct list_head *old, struct list_head *new) {
    new->next = old->next;
    new->next->prev = new;
    new->prev = old->prev;
    new->prev->next = new;
    INIT_LIST_HEAD(old);
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NP 64
static struct ptask P[NP];
static struct list_head *plink(int i) { return &P[i].node; }
static struct tmr TM[4];
static struct list_head *tlink(int i) { return &TM[i].node; }
static struct desc D[4];
static struct list_head *dlink1(int i) { return &D[i].q.link[1]; }

static bool eq(const struct list_head *h, struct list_head *(*link)(int), int np, const int *w, int n) {
    int got[NP];
    if (walk(h, link, np, got, NP) != n) return false;
    for (int i = 0; i < n; i++) if (got[i] != w[i]) return false;
    return true;
}

/* 연습 1 랜덤: 길이 0~20 리스트를 k 번째에서 자르기, 배열 모델과 비교 */
static bool random_cut(int rounds) {
    for (int r = 0; r < rounds; r++) {
        struct list_head h, cut;
        INIT_LIST_HEAD(&h);
        int n = (int)(xs32() % 21), model[NP];
        for (int i = 0; i < n; i++) { list_add_tail(&P[i].node, &h); model[i] = i; }
        int k = n ? (int)(xs32() % (uint32_t)(n + 1)) : 0;   /* 0 이면 entry = head */
        struct list_head *entry = k ? &P[k - 1].node : &h;
        list_cut_position(&cut, &h, entry);
        if (!eq(&cut, plink, NP, model, k) || !eq(&h, plink, NP, model + k, n - k)) return false;
    }
    return true;
}
/* 연습 2 랜덤: 무작위 prio 삽입 → 안정 정렬 모델(삽입 정렬)과 비교. 매 삽입 뒤 검사 */
static bool random_prio(int rounds) {
    for (int r = 0; r < rounds; r++) {
        struct list_head h;
        INIT_LIST_HEAD(&h);
        int model[NP], m = 0;
        for (int i = 0; i < NP; i++) {
            P[i].id = i;
            P[i].prio = (int)(xs32() % 8);
            insert_by_prio(&h, &P[i]);
            int k = m;
            while (k > 0 && P[model[k - 1]].prio > P[i].prio) { model[k] = model[k - 1]; k--; }
            model[k] = i;
            m++;
            if (!eq(&h, plink, NP, model, m)) return false;
        }
    }
    return true;
}

int main(void) {
    /* 연습 1 */
    struct list_head h, cut;
    INIT_LIST_HEAD(&h);
    for (int i = 0; i < 5; i++) list_add_tail(&P[i].node, &h);
    list_cut_position(&cut, &h, &P[2].node);
    CHECK(eq(&cut, plink, NP, (int[]){0, 1, 2}, 3) && eq(&h, plink, NP, (int[]){3, 4}, 2));
    list_cut_position(&cut, &h, &h);                       /* entry == head → 빈 cut */
    CHECK(list_empty(&cut) && eq(&h, plink, NP, (int[]){3, 4}, 2));
    list_cut_position(&cut, &h, &P[4].node);               /* 전부 */
    CHECK(eq(&cut, plink, NP, (int[]){3, 4}, 2) && list_empty(&h));
    CHECK(random_cut(2000));

    /* 연습 2 */
    INIT_LIST_HEAD(&h);
    int pr[6] = {3, 1, 3, 0, 1, 7};
    for (int i = 0; i < 6; i++) { P[i].id = i; P[i].prio = pr[i]; insert_by_prio(&h, &P[i]); }
    CHECK(eq(&h, plink, NP, (int[]){3, 1, 4, 0, 2, 5}, 6));   /* 0 | 1 1 | 3 3 | 7, 같은 prio 는 도착 순 */
    CHECK(random_prio(200));

    /* 연습 3 */
    struct list_head timers;
    INIT_LIST_HEAD(&timers);
    for (int i = 0; i < 4; i++) { TM[i].id = i; INIT_LIST_HEAD(&TM[i].node); }
    list_add_tail(&TM[0].node, &timers);
    list_add_tail(&TM[1].node, &timers);
    list_add_tail(&TM[2].node, &timers);
    CHECK(tmr_cancel(&TM[1]) && !tmr_pending(&TM[1]));
    CHECK(!tmr_cancel(&TM[1]));                            /* 두 번째 취소: 아무 일 없음 */
    CHECK(!tmr_cancel(&TM[3]));                            /* 한 번도 안 건 타이머 */
    CHECK(eq(&timers, tlink, 4, (int[]){0, 2}, 2));
    list_add_tail(&TM[1].node, &timers);                   /* 재등록 */
    CHECK(tmr_pending(&TM[1]) && eq(&timers, tlink, 4, (int[]){0, 2, 1}, 3));

    /* 연습 4 */
    struct list_head hwq, doneq;
    INIT_LIST_HEAD(&hwq);
    INIT_LIST_HEAD(&doneq);
    for (int i = 0; i < 4; i++) {
        D[i].addr = 0x20000000u + 0x100u * (uint32_t)i;
        list_add_tail(&D[i].q.link[0], &hwq);
        if (i % 2) list_add_tail(&D[i].q.link[1], &doneq);
    }
    CHECK(eq(&doneq, dlink1, 4, (int[]){1, 3}, 2));
    CHECK(offsetof(struct desc, q.link[1]) == offsetof(struct desc, q.link[0]) + sizeof(struct list_head));
    CHECK(desc_of_done(doneq.next) == &D[1] && desc_of_done(doneq.prev) == &D[3]);
    CHECK(desc_of_hw(hwq.prev) == &D[3] && desc_of_hw(hwq.prev)->addr == 0x20000300u);
    uint32_t s = 0;
    struct desc *d;
    list_for_each_entry(d, &doneq, q.link[1]) s += d->addr;
    CHECK(s == 0x20000100u + 0x20000300u);

    /* 연습 5 */
    INIT_LIST_HEAD(&h);
    for (int i = 0; i < 3; i++) list_add_tail(&P[i].node, &h);
    list_replace_init(&P[1].node, &P[9].node);             /* 가운데 */
    CHECK(eq(&h, plink, NP, (int[]){0, 9, 2}, 3) && list_empty(&P[1].node));
    list_replace_init(&P[2].node, &P[8].node);             /* 꼬리 */
    list_replace_init(&P[0].node, &P[7].node);             /* 머리 */
    CHECK(eq(&h, plink, NP, (int[]){7, 9, 8}, 3));
    struct list_head one;
    INIT_LIST_HEAD(&one);
    list_add_tail(&P[5].node, &one);
    list_replace_init(&P[5].node, &P[6].node);             /* 원소 하나뿐 */
    CHECK(eq(&one, plink, NP, (int[]){6}, 1) && list_empty(&P[5].node));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
