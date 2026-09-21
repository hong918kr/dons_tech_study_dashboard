// 06_linked_list.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 연결 리스트 (Linked List, malloc 없는 노드 풀 포함)  —  Q66~Q75
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 06_linked_list.c -o /tmp/andb_linked_list
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 임베디드 힌트:
//   - 삭제는 이중 포인터(node **) 관용구로 head 특수 케이스를 없앤다.
//   - 중앙/사이클은 slow/fast 포인터, 끝에서 N번째는 two-pointer 간격.
//   - Q75 는 힙 없이: 정적 배열 노드 풀 + free-list 로 O(1) alloc/free.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// (A) Singly Linked List  —  Q66~Q73
// ===========================================================================
typedef struct node {
    int          data;
    struct node *next;
} node_t;

// Q66. 앞쪽 삽입(push_front).  O(1).
//   예: push_front 1,2,3 -> [3 2 1]
void list_push_front(node_t **head, int data) {
    (void)head; (void)data;
    // TODO: 새 노드를 만들어 head 앞에 붙이고 *head 를 갱신
}

// Q67. 뒤쪽 삽입(push_back).  꼬리까지 순회 후 연결.  O(n).
//   예: [3 2 1] push_back 4,5 -> [3 2 1 4 5]
void list_push_back(node_t **head, int data) {
    (void)head; (void)data;
    // TODO: 꼬리의 next 슬롯을 찾아 새 노드 연결 (이중 포인터 권장)
}

// Q68. 특정 값(첫 일치) 삭제.  삭제했으면 true.  O(n).
//   예: delete 3 from [3 2 1 4 5] -> [2 1 4 5], true
bool list_delete_value(node_t **head, int value) {
    (void)head; (void)value;
    // TODO: node** 관용구로 head/중간/꼬리 삭제를 한 코드로 처리
    return false;   // placeholder
}

// Q69. 제자리 뒤집기.  O(n), O(1).
//   예: [2 1 4] -> [4 1 2]
void list_reverse(node_t **head) {
    (void)head;
    // TODO: prev/cur/next 3-포인터로 링크 반전
}

// Q70. 중앙 노드(slow/fast).  짝수면 뒤쪽 중앙 반환.  빈 리스트 -> NULL.
//   예: [1 2 3 4 5] -> 3,  [1 2 3 4] -> 3
node_t *list_find_middle(node_t *head) {
    (void)head;
    // TODO: fast 가 2칸씩 갈 때 slow 는 1칸씩
    return NULL;    // placeholder
}

// Q71. 사이클 탐지(Floyd).  O(n), O(1).
//   예: 무순환 -> false,  꼬리가 중간을 가리킴 -> true
bool list_has_cycle(node_t *head) {
    (void)head;
    // TODO: slow==fast 로 재회하면 사이클
    return false;   // placeholder
}

// Q72. 정렬된 두 리스트 병합(노드 재사용, 입력 소비).  O(n+m).
//   예: [1 3 5] + [2 4 6] -> [1 2 3 4 5 6]
node_t *list_merge_sorted(node_t *a, node_t *b) {
    (void)a; (void)b;
    // TODO: 스택 더미 노드로 head 특수 케이스 제거
    return NULL;    // placeholder
}

// Q73. 끝에서 N번째(n=1 이 마지막).  n 이 길이보다 크면 NULL.  O(n).
//   예: [4 1 2], n=1 -> 2,  n=3 -> 4
node_t *list_nth_from_end(node_t *head, size_t n) {
    (void)head; (void)n;
    // TODO: lead 를 n 칸 앞세운 뒤 함께 이동
    return NULL;    // placeholder
}

// (유틸) 정리 함수 — 제공됨.
void list_free(node_t **head) {
    if (!head) return;
    node_t *cur = *head;
    while (cur) { node_t *nx = cur->next; free(cur); cur = nx; }
    *head = NULL;
}

// ===========================================================================
// (B) Doubly Linked List  —  Q74
// ===========================================================================
typedef struct dnode {
    int           data;
    struct dnode *prev;
    struct dnode *next;
} dnode_t;

// Q74a. 앞쪽 삽입.  기존 head 의 prev 갱신 잊지 말 것.
void dlist_push_front(dnode_t **head, int data) {
    (void)head; (void)data;
    // TODO
}

// Q74b. 뒤쪽 삽입.  새 노드의 prev 를 꼬리로 연결.
void dlist_push_back(dnode_t **head, int data) {
    (void)head; (void)data;
    // TODO
}

// Q74c. 값 삭제(첫 일치).  prev/next 양쪽 재연결이 핵심.
bool dlist_delete_value(dnode_t **head, int value) {
    (void)head; (void)value;
    // TODO
    return false;   // placeholder
}

// Q74d. 제자리 뒤집기.  각 노드 prev/next 를 맞바꾸고 head 를 옛 꼬리로.
void dlist_reverse(dnode_t **head) {
    (void)head;
    // TODO
}

// (유틸) 정리 함수 — 제공됨.
void dlist_free(dnode_t **head) {
    if (!head) return;
    dnode_t *cur = *head;
    while (cur) { dnode_t *nx = cur->next; free(cur); cur = nx; }
    *head = NULL;
}

// ===========================================================================
// (C) malloc 없는 연결 리스트: 정적 노드 풀 + free-list  —  Q75
// ===========================================================================
#define POOL_SIZE 8

typedef struct pnode {
    int           data;
    struct pnode *next;
} pnode_t;

static pnode_t  g_pool[POOL_SIZE];  // 정적 노드 풀 (heap 아님)
static pnode_t *g_free_list = NULL; // 사용 가능 노드들의 연결 리스트

// Q75a. 풀 전체를 free-list 로 초기화.
void pool_init(void) {
    (void)g_pool;  // (구현하면 이 줄은 지우세요)
    // TODO: g_pool[i].next = &g_pool[i+1], 마지막은 NULL, g_free_list = &g_pool[0]
}

// Q75b. free-list 머리에서 한 노드 떼어 반환.  고갈 시 NULL.  O(1).
pnode_t *pool_alloc(void) {
    // TODO
    return NULL;    // placeholder
}

// Q75c. 노드를 free-list 머리에 되돌림.  O(1).
void pool_free(pnode_t *n) {
    (void)n;
    // TODO
}

// Q75d. 풀에서 앞쪽 삽입.  고갈 시 false.
bool pool_push_front(pnode_t **head, int data) {
    (void)head; (void)data;
    // TODO: pool_alloc 실패하면 false
    return false;   // placeholder
}

// Q75e. 풀에서 뒤쪽 삽입.  고갈 시 false.
bool pool_push_back(pnode_t **head, int data) {
    (void)head; (void)data;
    // TODO
    return false;   // placeholder
}

// Q75f. 값 삭제 후 노드를 pool_free 로 반납.
bool pool_delete_value(pnode_t **head, int value) {
    (void)head; (void)value;
    // TODO: free() 아니라 pool_free() 로 반납
    return false;   // placeholder
}

// (유틸) 전량 반납 — pool_free 를 사용하도록 구현하면 됨.
void pool_free_all(pnode_t **head) {
    (void)head;
    // TODO (선택): 리스트 전체를 pool_free 로 반납
}

// 테스트용: 현재 free-list 에 남은 노드 수.
size_t pool_free_count(void) {
    size_t c = 0;
    for (pnode_t *p = g_free_list; p; p = p->next) ++c;
    return c;
}

// ===========================================================================
// 테스트 헬퍼
// ===========================================================================
static bool list_equals(const node_t *h, const int *arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!h || h->data != arr[i]) return false;
        h = h->next;
    }
    return h == NULL;
}
static bool dlist_equals(const dnode_t *h, const int *arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!h || h->data != arr[i]) return false;
        h = h->next;
    }
    return h == NULL;
}
static bool dlist_prev_ok(const dnode_t *h) {
    const dnode_t *prev = NULL;
    while (h) { if (h->prev != prev) return false; prev = h; h = h->next; }
    return true;
}
static bool pool_equals(const pnode_t *h, const int *arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!h || h->data != arr[i]) return false;
        h = h->next;
    }
    return h == NULL;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- (A) Singly --------
    printf("== (A) Singly Linked List (Q66~Q73) ==\n");
    node_t *head = NULL;

    list_push_front(&head, 1);
    list_push_front(&head, 2);
    list_push_front(&head, 3);
    T("push_front 1,2,3 -> [3 2 1]", list_equals(head, (int[]){3,2,1}, 3));
    list_push_front(NULL, 9);
    T("push_front(NULL) 무해",        true);

    list_push_back(&head, 4);
    list_push_back(&head, 5);
    T("push_back 4,5 -> [3 2 1 4 5]", list_equals(head, (int[]){3,2,1,4,5}, 5));
    node_t *empty = NULL;
    list_push_back(&empty, 7);
    T("push_back into empty -> [7]", list_equals(empty, (int[]){7}, 1));
    list_free(&empty);

    T("delete 3 (head) -> true",     list_delete_value(&head, 3));
    T("  list == [2 1 4 5]",         list_equals(head, (int[]){2,1,4,5}, 4));
    T("delete 5 (tail) -> true",     list_delete_value(&head, 5));
    T("  list == [2 1 4]",           list_equals(head, (int[]){2,1,4}, 3));
    T("delete 99 (missing) -> false", !list_delete_value(&head, 99));
    T("  list unchanged [2 1 4]",    list_equals(head, (int[]){2,1,4}, 3));

    list_reverse(&head);
    T("reverse [2 1 4] -> [4 1 2]",  list_equals(head, (int[]){4,1,2}, 3));
    node_t *single = NULL; list_push_front(&single, 42);
    list_reverse(&single);
    T("reverse single -> unchanged", list_equals(single, (int[]){42}, 1));
    list_free(&single);
    node_t *nullh = NULL; list_reverse(&nullh);
    T("reverse empty -> still empty", nullh == NULL);

    node_t *odd = NULL;
    for (int i = 5; i >= 1; --i) list_push_front(&odd, i);
    node_t *m1 = list_find_middle(odd);
    T("middle [1..5] -> 3",          m1 && m1->data == 3);
    node_t *evn = NULL;
    for (int i = 4; i >= 1; --i) list_push_front(&evn, i);
    node_t *m2 = list_find_middle(evn);
    T("middle [1..4] -> 3 (upper)",  m2 && m2->data == 3);
    T("middle empty -> NULL",        list_find_middle(NULL) == NULL);

    T("no cycle on [1..5] -> false", !list_has_cycle(odd));
    T("cycle: NULL -> false",        !list_has_cycle(NULL));
    node_t *tail = odd; while (tail && tail->next) tail = tail->next;
    if (odd && odd->next && odd->next->next && tail) {
        node_t *third = odd->next->next;
        tail->next = third;
        T("cycle present -> true",   list_has_cycle(odd));
        tail->next = NULL;
    } else {
        T("cycle present -> true",   false);  // push_front 미구현 시 FAIL
    }
    node_t *selfc = NULL; list_push_front(&selfc, 1);
    if (selfc) { selfc->next = selfc; }
    T("self-loop single -> true",    list_has_cycle(selfc));
    if (selfc) { selfc->next = NULL; } list_free(&selfc);

    node_t *a = NULL, *b = NULL;
    list_push_back(&a, 1); list_push_back(&a, 3); list_push_back(&a, 5);
    list_push_back(&b, 2); list_push_back(&b, 4); list_push_back(&b, 6);
    node_t *merged = list_merge_sorted(a, b);
    T("merge [1 3 5]+[2 4 6] -> [1..6]", list_equals(merged, (int[]){1,2,3,4,5,6}, 6));
    node_t *only = NULL; list_push_back(&only, 8);
    node_t *m3 = list_merge_sorted(NULL, only);
    T("merge NULL+[8] -> [8]",       list_equals(m3, (int[]){8}, 1));
    list_free(&m3);
    list_free(&merged);

    node_t *nth1 = list_nth_from_end(head, 1);
    T("nth_from_end(1) -> 2 (last)", nth1 && nth1->data == 2);
    node_t *nth3 = list_nth_from_end(head, 3);
    T("nth_from_end(3) -> 4 (first)", nth3 && nth3->data == 4);
    T("nth_from_end(4) -> NULL (too big)", list_nth_from_end(head, 4) == NULL);
    T("nth_from_end(0) -> NULL",     list_nth_from_end(head, 0) == NULL);

    list_free(&head);
    list_free(&odd);
    list_free(&evn);

    // -------- (B) Doubly (Q74) --------
    printf("== (B) Doubly Linked List (Q74) ==\n");
    dnode_t *dh = NULL;
    dlist_push_front(&dh, 1);
    dlist_push_front(&dh, 2);
    dlist_push_front(&dh, 3);
    dlist_push_back(&dh, 4);
    dlist_push_back(&dh, 5);
    T("dlist build -> [3 2 1 4 5]", dlist_equals(dh, (int[]){3,2,1,4,5}, 5));
    T("  prev links consistent",    dlist_prev_ok(dh));

    T("dlist delete 3 (head) -> true", dlist_delete_value(&dh, 3));
    T("  -> [2 1 4 5]",             dlist_equals(dh, (int[]){2,1,4,5}, 4));
    T("  prev links consistent",    dlist_prev_ok(dh));
    T("dlist delete 5 (tail) -> true", dlist_delete_value(&dh, 5));
    T("  -> [2 1 4]",               dlist_equals(dh, (int[]){2,1,4}, 3));
    T("  prev links consistent",    dlist_prev_ok(dh));
    T("dlist delete 99 -> false",   !dlist_delete_value(&dh, 99));

    dlist_reverse(&dh);
    T("dlist reverse -> [4 1 2]",   dlist_equals(dh, (int[]){4,1,2}, 3));
    T("  prev links consistent",    dlist_prev_ok(dh));
    dlist_free(&dh);

    // -------- (C) malloc 없는 노드 풀 (Q75) --------
    printf("== (C) No-malloc static node pool (Q75) ==\n");
    pool_init();
    T("pool_init -> free_count == 8", pool_free_count() == POOL_SIZE);
    pnode_t *ph = NULL;
    pool_push_front(&ph, 1);
    pool_push_front(&ph, 2);
    pool_push_front(&ph, 3);
    pool_push_back(&ph, 4);
    pool_push_back(&ph, 5);
    T("pool build -> [3 2 1 4 5]",  pool_equals(ph, (int[]){3,2,1,4,5}, 5));
    T("  free_count == 3",          pool_free_count() == 3);
    T("pool delete 3 -> true",      pool_delete_value(&ph, 3));
    T("  -> [2 1 4 5]",             pool_equals(ph, (int[]){2,1,4,5}, 4));
    T("  free_count back to 4",     pool_free_count() == 4);

    pnode_t *fill = NULL;
    int ok_count = 0;
    while (pool_push_front(&fill, 0)) ++ok_count;
    T("pool exhaustion: 4 more then fail", ok_count == 4);
    T("  free_count == 0 at exhaustion", pool_free_count() == 0);
    T("push on empty pool -> false", !pool_push_front(&fill, 0));

    pool_free_all(&fill);
    pool_free_all(&ph);
    T("free_all -> free_count == 8 (no leak)", pool_free_count() == POOL_SIZE);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
