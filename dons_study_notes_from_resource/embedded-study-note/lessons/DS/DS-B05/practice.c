/* DS-B05 추가 연습 — 한 번에 검증
 * 일반 빌드:  cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 * ASan 빌드:  cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined practice.c && ./a.out
 * 해제 후 사용 데모: 위 ASan 빌드에 -DDEMO_UAF 를 붙이면 use-after-poison 리포트가 난다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ASan 이 켜져 있을 때만 수동 poison API 를 쓴다 (clang: __has_feature, gcc: __SANITIZE_ADDRESS__) */
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define HAVE_ASAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#  define HAVE_ASAN 1
#endif
#ifdef HAVE_ASAN
#  include <sanitizer/asan_interface.h>
#else
#  define ASAN_POISON_MEMORY_REGION(a, s)   ((void)(a), (void)(s))
#  define ASAN_UNPOISON_MEMORY_REGION(a, s) ((void)(a), (void)(s))
#endif

typedef struct Node {
    int val;
    struct Node *next;
} Node;

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

/* 연습 1: 값 v 를 모두 삭제, 지운 개수 반환. 지웠으면 pp 를 전진시키지 않는다. */
static int remove_all(Node **head, int v) {
    int cnt = 0;
    Node **pp = head;
    while (*pp) {
        if ((*pp)->val == v) { Node *x = *pp; *pp = x->next; x->next = NULL; cnt++; }
        else pp = &(*pp)->next;
    }
    return cnt;
}

/* 연습 2: 끝에서 k 번째(1-based) 노드 삭제. 한 번 순회 — lead 를 k 칸 먼저 보낸다. */
static Node *remove_nth_from_end(Node **head, int k) {
    Node *lead = *head;
    for (int i = 0; i < k; i++) {
        if (!lead) return NULL;                    /* k > 길이 */
        lead = lead->next;
    }
    Node **pp = head;                              /* lead 와 k 칸 간격 유지 */
    while (lead) { lead = lead->next; pp = &(*pp)->next; }
    Node *x = *pp;
    *pp = x->next;
    x->next = NULL;
    return x;
}

/* 연습 3: 정렬된 두 리스트 병합 (노드 재사용, 새 할당 없음). 같은 값이면 a 먼저 → 안정. */
static Node *merge_sorted(Node *a, Node *b) {
    Node *head = NULL, **tail = &head;             /* dummy 노드 대신 꼬리 링크 주소 */
    while (a && b) {
        Node **pick = (a->val <= b->val) ? &a : &b;
        *tail = *pick;
        tail = &(*pick)->next;
        *pick = (*pick)->next;
    }
    *tail = a ? a : b;
    return head;
}

/* 연습 4: O(1) 추가 메모리 회문 검사 — 중간 찾기 → 뒷절반 뒤집기 → 비교 → 원상 복구 */
static Node *reverse_iter(Node *head) {
    Node *prev = NULL;
    while (head) { Node *next = head->next; head->next = prev; prev = head; head = next; }
    return prev;
}
static bool is_palindrome(Node *head) {
    if (!head || !head->next) return true;
    Node *slow = head, *fast = head;               /* slow = 앞절반의 마지막 */
    while (fast->next && fast->next->next) { slow = slow->next; fast = fast->next->next; }
    Node *back = reverse_iter(slow->next);
    bool ok = true;
    for (Node *p = head, *q = back; q; p = p->next, q = q->next)
        if (p->val != q->val) { ok = false; break; }
    slow->next = reverse_iter(back);               /* 호출자 리스트를 망가뜨리지 않는다 */
    return ok;
}

/* 연습 5: free list 풀 + ASan 수동 poison — 해제된 노드를 건드리면 ASan 이 잡는다 */
static Node fpool[8];
static Node *free_head;
static void fpool_init(void) {
    free_head = NULL;
    for (int i = 7; i >= 0; i--) {                 /* 0 번이 맨 앞에 오도록 거꾸로 */
        fpool[i].next = free_head;
        free_head = &fpool[i];
        ASAN_POISON_MEMORY_REGION(&fpool[i], sizeof(Node));
    }
}
static Node *fnode_alloc(int v) {
    Node *n = free_head;
    if (!n) return NULL;
    ASAN_UNPOISON_MEMORY_REGION(n, sizeof(Node));
    free_head = n->next;
    n->val = v;
    n->next = NULL;
    return n;
}
static void fnode_free(Node *n) {
    n->next = free_head;                           /* LIFO: 방금 해제한 것이 다음 할당 */
    free_head = n;
    ASAN_POISON_MEMORY_REGION(n, sizeof(Node));
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    pool_reset();
    Node *h = list_build((int[]){7, 7, 1, 7, 2, 7}, 6);
    CHECK(remove_all(&h, 7) == 4 && list_eq(h, (int[]){1, 2}, 2));
    CHECK(remove_all(&h, 9) == 0 && list_eq(h, (int[]){1, 2}, 2));

    /* 연습 2 */
    pool_reset();
    h = list_build((int[]){1, 2, 3, 4, 5}, 5);
    Node *x = remove_nth_from_end(&h, 2);
    CHECK(x != NULL && x->val == 4 && list_eq(h, (int[]){1, 2, 3, 5}, 4));
    x = remove_nth_from_end(&h, 4);                /* 끝에서 4번째 = head */
    CHECK(x != NULL && x->val == 1 && list_eq(h, (int[]){2, 3, 5}, 3));
    CHECK(remove_nth_from_end(&h, 9) == NULL && list_eq(h, (int[]){2, 3, 5}, 3));

    /* 연습 3 */
    pool_reset();
    Node *a = list_build((int[]){1, 4, 4, 9}, 4);
    Node *b = list_build((int[]){2, 4, 10}, 3);
    Node *b4 = b->next;
    h = merge_sorted(a, b);
    CHECK(list_eq(h, (int[]){1, 2, 4, 4, 4, 9, 10}, 7));
    CHECK(h->next->next->next->next == b4);        /* a 의 4 두 개 다음에 b 의 4 → 안정 */
    CHECK(merge_sorted(NULL, NULL) == NULL);

    /* 연습 4 */
    pool_reset();
    h = list_build((int[]){1, 2, 3, 2, 1}, 5);
    CHECK(is_palindrome(h) && list_eq(h, (int[]){1, 2, 3, 2, 1}, 5));
    pool_reset();
    h = list_build((int[]){1, 2, 2, 1}, 4);
    CHECK(is_palindrome(h));
    pool_reset();
    h = list_build((int[]){1, 2, 3, 1}, 4);
    CHECK(!is_palindrome(h) && list_eq(h, (int[]){1, 2, 3, 1}, 4));

    /* 연습 5 */
    fpool_init();
    Node *p[9];
    for (int i = 0; i < 9; i++) p[i] = fnode_alloc(i);
    CHECK(p[0] == &fpool[0] && p[7] == &fpool[7] && p[8] == NULL);   /* 8개 소진 → NULL */
    fnode_free(p[3]);
    fnode_free(p[5]);
    CHECK(fnode_alloc(50) == &fpool[5] && fnode_alloc(30) == &fpool[3]);
#ifdef DEMO_UAF
    fnode_free(p[1]);
    printf("use after free: %d\n", p[1]->val);     /* ASan: use-after-poison */
#endif

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
