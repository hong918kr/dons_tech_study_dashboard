/* DS-B05 단일 연결 리스트: 삽입·삭제·뒤집기·사이클 검출
 * 노드는 정적 배열(pool)에서만 꺼낸다 — malloc 금지.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Node {
    int val;
    struct Node *next;
} Node;

/* ---------------- 정적 노드 풀 (malloc 대신) ---------------- */
#define POOL_SIZE 64
static Node pool[POOL_SIZE];
static int pool_used;

static void pool_reset(void) { pool_used = 0; }
static Node *node_new(int v) {
    if (pool_used >= POOL_SIZE) return NULL;
    Node *n = &pool[pool_used++];
    n->val = v;
    n->next = NULL;
    return n;
}

/* ---------------- Part A: 삽입과 삭제 ---------------- */
/* n 을 맨 앞에 붙인다. head 자체가 바뀌므로 Node ** 를 받는다. */
static void push_front(Node **head, Node *n) {
    (void)head; (void)n; /* TODO */
}

/* 오름차순 리스트에 n 을 끼워 넣는다. 같은 값이 있으면 그 뒤에 (안정 삽입).
 * 이중 포인터 pp 로 "고쳐 쓸 링크"를 따라가면 head 특별 처리가 사라진다. */
static void insert_sorted(Node **head, Node *n) {
    (void)head; (void)n; /* TODO */
}

/* 값이 v 인 첫 노드를 리스트에서 떼어 반환한다 (next = NULL 로 정리). 없으면 NULL. */
static Node *remove_value(Node **head, int v) {
    (void)head; (void)v;
    return NULL; /* TODO: Node **pp 기법 */
}

/* ---------------- Part B: 뒤집기 ---------------- */
/* 반복: prev / cur / next 세 포인터. 새 head 를 반환. */
static Node *reverse_iter(Node *head) {
    return head; /* TODO */
}

/* 재귀: "나머지를 뒤집고, 내 다음 노드가 나를 가리키게". 새 head 를 반환. */
static Node *reverse_rec(Node *head) {
    return head; /* TODO */
}

/* ---------------- Part C: 두 포인터 (Floyd) ---------------- */
/* slow 는 1칸, fast 는 2칸. 만나면 사이클. */
static bool has_cycle(const Node *head) {
    (void)head;
    return false; /* TODO */
}

/* 사이클이 시작되는 노드 (없으면 NULL). 만난 뒤 한쪽을 head 로 되돌려 1칸씩. */
static Node *cycle_entry(Node *head) {
    (void)head;
    return NULL; /* TODO */
}

/* 중간 노드. 길이가 짝수면 두 번째 중간 (1,2,3,4 → 3). 빈 리스트는 NULL. */
static Node *middle_node(Node *head) {
    (void)head;
    return NULL; /* TODO */
}

/* ---------------- 테스트 도우미 (사이클이 있어도 멈추도록 max 제한) ---------------- */
static Node *list_build(const int *a, int n) {
    Node *head = NULL, **tail = &head;
    for (int i = 0; i < n; i++) { *tail = node_new(a[i]); tail = &(*tail)->next; }
    return head;
}
static bool list_eq(const Node *h, const int *a, int n) {
    int k = 0;
    for (; h && k <= POOL_SIZE; h = h->next, k++)
        if (k >= n || h->val != a[k]) return false;
    return k == n;
}
static Node *nth(Node *h, int i) { while (h && i--) h = h->next; return h; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A: push_front / insert_sorted / remove_value */
    pool_reset();
    Node *h = NULL;
    push_front(&h, node_new(3));
    push_front(&h, node_new(2));
    push_front(&h, node_new(1));
    CHECK(list_eq(h, (int[]){1, 2, 3}, 3));

    pool_reset();
    h = NULL;
    insert_sorted(&h, node_new(5));                 /* 빈 리스트 */
    insert_sorted(&h, node_new(1));                 /* 맨 앞 */
    insert_sorted(&h, node_new(9));                 /* 맨 뒤 */
    Node *a = nth(h, 1);                            /* 기존 5 */
    Node *b = node_new(5);
    insert_sorted(&h, b);                           /* 같은 값 → 기존 5 뒤 */
    CHECK(list_eq(h, (int[]){1, 5, 5, 9}, 4));
    CHECK(a != NULL && a->next == b);

    Node *x = remove_value(&h, 1);                  /* head 삭제 */
    CHECK(x != NULL && x->val == 1 && x->next == NULL);
    CHECK(list_eq(h, (int[]){5, 5, 9}, 3));
    x = remove_value(&h, 9);                        /* tail 삭제 */
    CHECK(x != NULL && x->val == 9);
    CHECK(remove_value(&h, 7) == NULL);             /* 없는 값 */
    CHECK(list_eq(h, (int[]){5, 5}, 2));
    CHECK(remove_value(&h, 5) == a);                /* 첫 번째 5 부터 */
    CHECK(remove_value(&h, 5) == b && h == NULL);   /* 마지막 노드 → 빈 리스트 */

    /* Part B: 뒤집기 */
    CHECK(reverse_iter(NULL) == NULL && reverse_rec(NULL) == NULL);
    pool_reset();
    h = list_build((int[]){1, 2, 3, 4, 5}, 5);
    Node *old_head = h;
    h = reverse_iter(h);
    CHECK(list_eq(h, (int[]){5, 4, 3, 2, 1}, 5));
    CHECK(old_head->next == NULL);                  /* 옛 head 는 이제 tail */
    h = reverse_rec(h);
    CHECK(list_eq(h, (int[]){1, 2, 3, 4, 5}, 5));

    pool_reset();
    int big[POOL_SIZE], rbig[POOL_SIZE];
    for (int i = 0; i < POOL_SIZE; i++) { big[i] = i; rbig[i] = POOL_SIZE - 1 - i; }
    h = list_build(big, POOL_SIZE);                 /* 풀 전체 64개 */
    h = reverse_iter(h);
    CHECK(list_eq(h, rbig, POOL_SIZE));
    h = reverse_rec(h);
    CHECK(list_eq(h, big, POOL_SIZE));

    /* Part C: Floyd 사이클 검출 / 중간 노드 */
    CHECK(has_cycle(NULL) == false);
    pool_reset();
    Node *self = node_new(42);
    self->next = self;                              /* 자기 자신을 가리키는 1-노드 사이클 */
    CHECK(has_cycle(self) == true && cycle_entry(self) == self);

    pool_reset();
    h = list_build((int[]){1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 10);
    CHECK(has_cycle(h) == false && cycle_entry(h) == NULL);
    nth(h, 9)->next = nth(h, 3);                    /* 10 → 4 로 고리 */
    CHECK(has_cycle(h) == true);
    CHECK(cycle_entry(h) == &pool[3]);
    pool[9].next = h;                               /* 10 → 1 : 전체가 고리 */
    CHECK(cycle_entry(h) == h);

    CHECK(middle_node(NULL) == NULL);
    pool_reset();
    h = list_build((int[]){1}, 1);
    CHECK(middle_node(h) == h);
    pool_reset();
    h = list_build((int[]){1, 2}, 2);
    CHECK(middle_node(h) != NULL && middle_node(h)->val == 2);
    pool_reset();
    h = list_build((int[]){1, 2, 3, 4, 5}, 5);
    CHECK(middle_node(h) != NULL && middle_node(h)->val == 3);
    pool_reset();
    h = list_build((int[]){1, 2, 3, 4, 5, 6}, 6);
    CHECK(middle_node(h) != NULL && middle_node(h)->val == 4);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
