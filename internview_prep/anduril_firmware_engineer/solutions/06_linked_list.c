// 06_linked_list.c  —  REFERENCE SOLUTION
// 연결 리스트 (Linked List, malloc 없는 노드 풀 포함)  —  Q66~Q75
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 06_linked_list.c -o /tmp/andb_linked_list
// 임베디드 관점 핵심:
//   - 이중 포인터(node **) 삭제 관용구 -> head 특수 케이스 없이 깔끔.
//   - slow/fast 포인터 -> 중앙 찾기, Floyd 사이클 탐지 (O(1) 공간).
//   - two-pointer 간격 -> 끝에서 N번째 (한 번 순회).
//   - Q75: 정적 노드 풀 + free-list -> 힙 없이 결정론적 할당 (베어메탈/RTOS 필수).
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
// ---------------------------------------------------------------------------
// Q66. 노드 정의 + 앞쪽 삽입(push_front).  head 는 node**(이중 포인터)로 받아
//      빈 리스트/head 갱신을 호출자에게 자연스럽게 반영한다.  O(1).
// ===========================================================================
typedef struct node {
    int          data;
    struct node *next;
} node_t;

void list_push_front(node_t **head, int data) {
    if (!head) return;
    node_t *n = (node_t *)malloc(sizeof *n);
    if (!n) return;                 // 실제 펌웨어에선 실패를 반드시 처리
    n->data = data;
    n->next = *head;                // 새 노드가 기존 head 를 가리키고
    *head   = n;                    // head 를 새 노드로 교체
}

// Q67. 뒤쪽 삽입(push_back).  꼬리까지 순회 후 연결.  O(n).
//      이중 포인터로 "다음에 채울 링크 슬롯"을 추적하면 빈 리스트도 특수 케이스 없음.
void list_push_back(node_t **head, int data) {
    if (!head) return;
    node_t *n = (node_t *)malloc(sizeof *n);
    if (!n) return;
    n->data = data;
    n->next = NULL;
    node_t **pp = head;             // 채울 슬롯의 주소
    while (*pp) pp = &(*pp)->next;  // 꼬리의 next(=NULL) 슬롯까지 이동
    *pp = n;
}

// Q68. 특정 값 삭제(첫 번째 일치).  이중 포인터 관용구의 정석:
//      *pp 가 "현재 노드로 들어오는 링크"라서 head 삭제도 중간 삭제와 동일 코드.
//      반환: 삭제했으면 true.  O(n), O(1) 공간.
bool list_delete_value(node_t **head, int value) {
    if (!head) return false;
    node_t **pp = head;
    while (*pp) {
        if ((*pp)->data == value) {
            node_t *dead = *pp;     // 끊어낼 노드
            *pp = dead->next;       // 들어오는 링크를 건너뛰게 다시 연결
            free(dead);
            return true;
        }
        pp = &(*pp)->next;          // 다음 링크 슬롯으로
    }
    return false;
}

// Q69. 제자리 뒤집기(in-place reverse).  prev/cur/next 3-포인터.  O(n), O(1).
void list_reverse(node_t **head) {
    if (!head) return;
    node_t *prev = NULL, *cur = *head;
    while (cur) {
        node_t *nxt = cur->next;    // 끊기 전에 다음 저장
        cur->next = prev;           // 링크 반전
        prev = cur;                 // 한 칸 전진
        cur  = nxt;
    }
    *head = prev;                   // 옛 꼬리가 새 head
}

// Q70. 중앙 노드 찾기(slow/fast).  fast 가 2칸씩 -> fast 가 끝나면 slow 는 중앙.
//      길이 짝수면 뒤쪽 중앙(upper middle)을 반환한다.  단 한 번 순회, O(1) 공간.
//      예: [1 2 3 4 5]->3,  [1 2 3 4]->3.  빈 리스트-> NULL.
node_t *list_find_middle(node_t *head) {
    node_t *slow = head, *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

// Q71. 사이클 탐지(Floyd tortoise & hare).  만나면 사이클.  O(n), O(1) 공간.
//      노드 주소 저장 없이 판별 -> 임베디드에서 메모리 안 쓰는 정석.
bool list_has_cycle(node_t *head) {
    node_t *slow = head, *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return true;  // 순환이면 반드시 재회
    }
    return false;                        // fast 가 NULL 도달 -> 무한이 아님
}

// Q72. 정렬된 두 리스트 병합.  스택 더미(dummy) 노드로 head 특수 케이스 제거.
//      노드를 재사용(새 할당 없음) -> 입력 리스트는 소비된다.  O(n+m).
node_t *list_merge_sorted(node_t *a, node_t *b) {
    node_t dummy;                   // 스택에 둠 -> malloc 불필요
    dummy.next = NULL;
    node_t *tail = &dummy;
    while (a && b) {
        if (a->data <= b->data) { tail->next = a; a = a->next; } // <= 로 안정성
        else                    { tail->next = b; b = b->next; }
        tail = tail->next;
    }
    tail->next = a ? a : b;         // 남은 꼬리 붙이기
    return dummy.next;
}

// Q73. 끝에서 N번째 노드(n=1 이 마지막).  two-pointer 간격 유지, 한 번 순회.
//      n 이 길이보다 크면 NULL.  O(n), O(1) 공간.
node_t *list_nth_from_end(node_t *head, size_t n) {
    if (n == 0) return NULL;        // 0-번째는 정의 안 함
    node_t *lead = head;
    for (size_t i = 0; i < n; ++i) {
        if (!lead) return NULL;     // 리스트가 n 보다 짧음
        lead = lead->next;
    }
    node_t *trail = head;
    while (lead) { lead = lead->next; trail = trail->next; } // 간격 유지 이동
    return trail;
}

void list_free(node_t **head) {
    if (!head) return;
    node_t *cur = *head;
    while (cur) { node_t *nx = cur->next; free(cur); cur = nx; }
    *head = NULL;
}

// ===========================================================================
// (B) Doubly Linked List  —  Q74
// ---------------------------------------------------------------------------
// prev/next 양방향.  삭제 시 인접 노드의 prev/next 를 함께 갱신해야 한다.
// 양방향 순회가 필요한 곳(예: 스크롤 로그, LRU 캐시)에서 사용.
// ===========================================================================
typedef struct dnode {
    int           data;
    struct dnode *prev;
    struct dnode *next;
} dnode_t;

void dlist_push_front(dnode_t **head, int data) {
    if (!head) return;
    dnode_t *n = (dnode_t *)malloc(sizeof *n);
    if (!n) return;
    n->data = data;
    n->prev = NULL;
    n->next = *head;
    if (*head) (*head)->prev = n;   // 기존 head 의 prev 를 새 노드로
    *head = n;
}

void dlist_push_back(dnode_t **head, int data) {
    if (!head) return;
    dnode_t *n = (dnode_t *)malloc(sizeof *n);
    if (!n) return;
    n->data = data;
    n->next = NULL;
    if (!*head) { n->prev = NULL; *head = n; return; }
    dnode_t *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = n;
    n->prev   = cur;                // 양방향 연결
}

// 첫 번째 일치 노드 삭제.  prev/next 양쪽 재연결이 핵심.
bool dlist_delete_value(dnode_t **head, int value) {
    if (!head) return false;
    dnode_t *cur = *head;
    while (cur && cur->data != value) cur = cur->next;
    if (!cur) return false;
    if (cur->prev) cur->prev->next = cur->next;
    else           *head = cur->next;           // head 삭제
    if (cur->next) cur->next->prev = cur->prev;  // 꼬리 삭제면 생략됨
    free(cur);
    return true;
}

// 제자리 뒤집기.  각 노드의 prev/next 를 맞바꾸고, 마지막에 head 를 옛 꼬리로.
void dlist_reverse(dnode_t **head) {
    if (!head) return;
    dnode_t *cur = *head, *newhead = *head;
    while (cur) {
        dnode_t *nxt = cur->next;
        cur->next = cur->prev;      // prev <-> next swap
        cur->prev = nxt;
        newhead = cur;              // 마지막으로 처리한 노드가 새 head
        cur = nxt;
    }
    *head = newhead;
}

void dlist_free(dnode_t **head) {
    if (!head) return;
    dnode_t *cur = *head;
    while (cur) { dnode_t *nx = cur->next; free(cur); cur = nx; }
    *head = NULL;
}

// ===========================================================================
// (C) malloc 없는 연결 리스트: 정적 노드 풀 + free-list  —  Q75
// ---------------------------------------------------------------------------
// 힙이 없거나(베어메탈) 결정론적 지연이 필요한(RTOS/ISR) 환경의 정석.
//   - 컴파일 타임에 노드 배열을 확보(정적 저장).
//   - 사용 안 하는 노드는 free-list 로 엮어 O(1) alloc/free.
//   - 풀 고갈 시 NULL/false 로 우아하게 실패 (heap fragmentation 없음).
// ===========================================================================
#define POOL_SIZE 8

typedef struct pnode {
    int           data;
    struct pnode *next;
} pnode_t;

static pnode_t  g_pool[POOL_SIZE];  // 정적 노드 풀 (heap 아님)
static pnode_t *g_free_list = NULL; // 사용 가능 노드들의 단일 연결 리스트

// 풀 전체를 free-list 로 초기화.  전원/리셋 시 한 번 호출.
void pool_init(void) {
    for (int i = 0; i < POOL_SIZE - 1; ++i)
        g_pool[i].next = &g_pool[i + 1];
    g_pool[POOL_SIZE - 1].next = NULL;
    g_free_list = &g_pool[0];
}

// free-list 머리에서 한 노드 떼어 반환.  고갈 시 NULL.  O(1).
pnode_t *pool_alloc(void) {
    if (!g_free_list) return NULL;
    pnode_t *n = g_free_list;
    g_free_list = g_free_list->next;
    n->next = NULL;
    return n;
}

// 노드를 free-list 머리에 되돌림.  O(1).
void pool_free(pnode_t *n) {
    if (!n) return;
    n->next = g_free_list;
    g_free_list = n;
}

bool pool_push_front(pnode_t **head, int data) {
    if (!head) return false;
    pnode_t *n = pool_alloc();
    if (!n) return false;           // 풀 고갈 -> 우아하게 실패
    n->data = data;
    n->next = *head;
    *head = n;
    return true;
}

bool pool_push_back(pnode_t **head, int data) {
    if (!head) return false;
    pnode_t *n = pool_alloc();
    if (!n) return false;
    n->data = data;
    n->next = NULL;
    pnode_t **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = n;
    return true;
}

// 이중 포인터 삭제: 끊어낸 노드는 반드시 pool_free 로 풀에 반납.
bool pool_delete_value(pnode_t **head, int value) {
    if (!head) return false;
    pnode_t **pp = head;
    while (*pp) {
        if ((*pp)->data == value) {
            pnode_t *dead = *pp;
            *pp = dead->next;
            pool_free(dead);        // free() 대신 풀 반납
            return true;
        }
        pp = &(*pp)->next;
    }
    return false;
}

void pool_free_all(pnode_t **head) {
    if (!head) return;
    while (*head) {
        pnode_t *dead = *head;
        *head = dead->next;
        pool_free(dead);
    }
}

// 테스트용: 현재 free-list 에 남은 노드 수 (풀 고갈/누수 검증).
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
    return h == NULL;   // 길이도 정확히 일치해야 함
}

static bool dlist_equals(const dnode_t *h, const int *arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!h || h->data != arr[i]) return false;
        h = h->next;
    }
    return h == NULL;
}

// 이중 리스트의 prev 링크 무결성 검증: 앞으로 훑으며 prev 가 직전 노드인지 확인.
static bool dlist_prev_ok(const dnode_t *h) {
    const dnode_t *prev = NULL;
    while (h) {
        if (h->prev != prev) return false;
        prev = h;
        h = h->next;
    }
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

    // Q66 push_front
    list_push_front(&head, 1);
    list_push_front(&head, 2);
    list_push_front(&head, 3);      // [3 2 1]
    T("push_front 1,2,3 -> [3 2 1]", list_equals(head, (int[]){3,2,1}, 3));
    list_push_front(NULL, 9);       // NULL 안전
    T("push_front(NULL) 무해",        true);

    // Q67 push_back
    list_push_back(&head, 4);
    list_push_back(&head, 5);       // [3 2 1 4 5]
    T("push_back 4,5 -> [3 2 1 4 5]", list_equals(head, (int[]){3,2,1,4,5}, 5));
    node_t *empty = NULL;
    list_push_back(&empty, 7);      // 빈 리스트에 push_back
    T("push_back into empty -> [7]", list_equals(empty, (int[]){7}, 1));
    list_free(&empty);

    // Q68 delete_value
    T("delete 3 (head) -> true",     list_delete_value(&head, 3));
    T("  list == [2 1 4 5]",         list_equals(head, (int[]){2,1,4,5}, 4));
    T("delete 5 (tail) -> true",     list_delete_value(&head, 5));
    T("  list == [2 1 4]",           list_equals(head, (int[]){2,1,4}, 3));
    T("delete 99 (missing) -> false", !list_delete_value(&head, 99));
    T("  list unchanged [2 1 4]",    list_equals(head, (int[]){2,1,4}, 3));

    // Q69 reverse
    list_reverse(&head);            // [4 1 2]
    T("reverse [2 1 4] -> [4 1 2]",  list_equals(head, (int[]){4,1,2}, 3));
    node_t *single = NULL; list_push_front(&single, 42);
    list_reverse(&single);
    T("reverse single -> unchanged", list_equals(single, (int[]){42}, 1));
    list_free(&single);
    node_t *nullh = NULL; list_reverse(&nullh);
    T("reverse empty -> still empty", nullh == NULL);

    // Q70 find_middle
    node_t *odd = NULL;   // [1 2 3 4 5]
    for (int i = 5; i >= 1; --i) list_push_front(&odd, i);
    node_t *m1 = list_find_middle(odd);
    T("middle [1..5] -> 3",          m1 && m1->data == 3);
    node_t *evn = NULL;   // [1 2 3 4]
    for (int i = 4; i >= 1; --i) list_push_front(&evn, i);
    node_t *m2 = list_find_middle(evn);
    T("middle [1..4] -> 3 (upper)",  m2 && m2->data == 3);
    T("middle empty -> NULL",        list_find_middle(NULL) == NULL);

    // Q71 cycle detect
    T("no cycle on [1..5] -> false", !list_has_cycle(odd));
    T("cycle: NULL -> false",        !list_has_cycle(NULL));
    // 인위적 사이클: 꼬리 next 를 3번째 노드로
    node_t *tail = odd; while (tail->next) tail = tail->next;
    node_t *third = odd->next->next;      // 값 3 노드
    tail->next = third;                   // 사이클 형성
    T("cycle present -> true",       list_has_cycle(odd));
    tail->next = NULL;                    // 원상복구 후 안전 해제
    // 단일 노드 자기 사이클
    node_t *selfc = NULL; list_push_front(&selfc, 1); selfc->next = selfc;
    T("self-loop single -> true",    list_has_cycle(selfc));
    selfc->next = NULL; list_free(&selfc);

    // Q72 merge_sorted
    node_t *a = NULL, *b = NULL;
    list_push_back(&a, 1); list_push_back(&a, 3); list_push_back(&a, 5);
    list_push_back(&b, 2); list_push_back(&b, 4); list_push_back(&b, 6);
    node_t *merged = list_merge_sorted(a, b);   // [1 2 3 4 5 6]
    T("merge [1 3 5]+[2 4 6] -> [1..6]", list_equals(merged, (int[]){1,2,3,4,5,6}, 6));
    node_t *only = NULL; list_push_back(&only, 8);
    node_t *m3 = list_merge_sorted(NULL, only);  // 한쪽 빈 경우
    T("merge NULL+[8] -> [8]",       list_equals(m3, (int[]){8}, 1));
    list_free(&m3);
    list_free(&merged);

    // Q73 nth_from_end  (head 는 [4 1 2])
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
    dlist_push_front(&dh, 3);       // [3 2 1]
    dlist_push_back(&dh, 4);
    dlist_push_back(&dh, 5);        // [3 2 1 4 5]
    T("dlist build -> [3 2 1 4 5]", dlist_equals(dh, (int[]){3,2,1,4,5}, 5));
    T("  prev links consistent",    dlist_prev_ok(dh));

    T("dlist delete 3 (head) -> true", dlist_delete_value(&dh, 3));
    T("  -> [2 1 4 5]",             dlist_equals(dh, (int[]){2,1,4,5}, 4));
    T("  prev links consistent",    dlist_prev_ok(dh));
    T("dlist delete 5 (tail) -> true", dlist_delete_value(&dh, 5));
    T("  -> [2 1 4]",               dlist_equals(dh, (int[]){2,1,4}, 3));
    T("  prev links consistent",    dlist_prev_ok(dh));
    T("dlist delete 99 -> false",   !dlist_delete_value(&dh, 99));

    dlist_reverse(&dh);             // [4 1 2]
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
    pool_push_front(&ph, 3);        // [3 2 1]
    pool_push_back(&ph, 4);
    pool_push_back(&ph, 5);         // [3 2 1 4 5]
    T("pool build -> [3 2 1 4 5]",  pool_equals(ph, (int[]){3,2,1,4,5}, 5));
    T("  free_count == 3",          pool_free_count() == 3);
    T("pool delete 3 -> true",      pool_delete_value(&ph, 3));
    T("  -> [2 1 4 5]",             pool_equals(ph, (int[]){2,1,4,5}, 4));
    T("  free_count back to 4",     pool_free_count() == 4);

    // 풀 고갈 테스트: 이미 4개 사용 중 -> 남은 4개까지만 성공
    pnode_t *fill = NULL;
    int ok_count = 0;
    while (pool_push_front(&fill, 0)) ++ok_count;
    T("pool exhaustion: 4 more then fail", ok_count == 4);
    T("  free_count == 0 at exhaustion", pool_free_count() == 0);
    T("push on empty pool -> false", !pool_push_front(&fill, 0));

    // 전량 반납 -> 다시 8개 가용 (누수 없음)
    pool_free_all(&fill);
    pool_free_all(&ph);
    T("free_all -> free_count == 8 (no leak)", pool_free_count() == POOL_SIZE);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
