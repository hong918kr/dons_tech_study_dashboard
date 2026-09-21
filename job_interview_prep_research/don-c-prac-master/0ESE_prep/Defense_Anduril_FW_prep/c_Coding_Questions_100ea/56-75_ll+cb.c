#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define CB_SIZE 8

/*
56. uint8_t를 저장하는 원형 버퍼(Circular Buffer) 구현
*/
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t head;
    size_t tail;
    size_t count;
} circular_buffer_t; // 56

/*
57. 원형 버퍼 초기화 함수 구현
*/
void cb_init(circular_buffer_t *cb, uint8_t *buffer, size_t size) { // 57
    cb->buffer = buffer;
    cb->size = size;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

/*
58. 데이터 추가(push/enqueue) 함수 구현
*/
bool cb_push(circular_buffer_t *cb, uint8_t data) { // 58
    if (cb->count == cb->size) return false; // full
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->size;
    cb->count++;
    return true;
}

/*
59. 데이터 추출(pop/dequeue) 함수 구현
*/
bool cb_pop(circular_buffer_t *cb, uint8_t *data) { // 59
    if (cb->count == 0) return false; // empty
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->size;
    cb->count--;
    return true;
}

/*
60. 버퍼가 비어있는지 확인하는 함수 구현
*/
bool cb_is_empty(const circular_buffer_t *cb) { // 60
    return cb->count == 0;
}

/*
61. 버퍼가 가득 찼는지 확인하는 함수 구현
*/
bool cb_is_full(const circular_buffer_t *cb) { // 61
    return cb->count == cb->size;
}

/*
62. 카운터 변수를 사용하여 full/empty 상태 관리 구현
*/
// 62
/*
카운터 변수를 사용하면 head, tail 포인터만으로는 구분이 어려운 full/empty 상태를 명확하게 관리할 수 있음.
데이터 추가/삭제 시 카운터를 증가/감소시켜 상태를 추적함.
*/

/*
63. 원형 버퍼의 현재 데이터 개수 반환 함수 구현
*/
size_t cb_size(const circular_buffer_t *cb) { // 63
    return cb->count;
}

/*
64. 원형 버퍼를 비우는(flush) 함수 구현
*/
void cb_flush(circular_buffer_t *cb) { // 64
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

/*
65. 단일 생산자-단일 소비자 환경에서의 스레드 안전성 논의
*/
// 65
/*
단일 생산자-단일 소비자 환경에서는 head/tail 포인터를 각각 한 쪽에서만 접근하므로 락 없이도 스레드 안전성을 보장할 수 있음.
단, 변수의 원자적 접근이 보장되어야 하며, 멀티 프로듀서/멀티 컨슈머 환경에서는 추가적인 동기화가 필요함.
*/

/*
66. 단일 연결 리스트(Singly Linked List) 노드 구조체 정의
*/
typedef struct node {
    int data;
    struct node *next;
} node_t; // 66

/*
67. 연결 리스트의 맨 앞에 노드 추가
*/
void list_push_front(node_t **head, int data) { // 67
    node_t *new_node = (node_t*)malloc(sizeof(node_t));
    new_node->data = data;
    new_node->next = *head;
    *head = new_node;
}

/*
68. 연결 리스트의 맨 뒤에 노드 추가
*/
void list_push_back(node_t **head, int data) { // 68
    node_t *new_node = (node_t*)malloc(sizeof(node_t));
    new_node->data = data;
    new_node->next = NULL;
    if (*head == NULL) {
        *head = new_node;
        return;
    }
    node_t *cur = *head;
    while (cur->next) cur = cur->next;
    cur->next = new_node;
}

/*
69. 특정 값을 가진 노드 삭제
*/
void list_delete_value(node_t **head, int value) { // 69
    node_t **cur = head;
    while (*cur) {
        if ((*cur)->data == value) {
            node_t *tmp = *cur;
            *cur = (*cur)->next;
            free(tmp);
            return;
        }
        cur = &((*cur)->next);
    }
}

/*
70. 연결 리스트 뒤집기 (in-place)
*/
void list_reverse(node_t **head) { // 70
    node_t *prev = NULL, *cur = *head, *next = NULL;
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    *head = prev;
}

/*
71. 연결 리스트의 중간 노드 찾기
*/
node_t* list_find_middle(node_t *head) { // 71
    node_t *slow = head, *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

/*
72. 연결 리스트에 순환(cycle)이 있는지 탐지
*/
bool list_has_cycle(node_t *head) { // 72
    node_t *slow = head, *fast = head;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return true;
    }
    return false;
}

/*
73. 두 개의 정렬된 연결 리스트 병합
*/
node_t* list_merge_sorted(node_t *l1, node_t *l2) { // 73
    node_t dummy = {0, NULL};
    node_t *tail = &dummy;
    while (l1 && l2) {
        if (l1->data < l2->data) {
            tail->next = l1;
            l1 = l1->next;
        } else {
            tail->next = l2;
            l2 = l2->next;
        }
        tail = tail->next;
    }
    tail->next = l1 ? l1 : l2;
    return dummy.next;
}

/*
74. 연결 리스트의 끝에서 N번째 노드 찾기
*/
node_t* list_nth_from_end(node_t *head, size_t n) { // 74
    node_t *fast = head, *slow = head;
    for (size_t i = 0; i < n; ++i) {
        if (!fast) return NULL;
        fast = fast->next;
    }
    while (fast) {
        fast = fast->next;
        slow = slow->next;
    }
    return slow;
}

/*
75. 임베디드 시스템에서 malloc 없는 연결 리스트 구현
*/
// 75
/*
정적 배열을 노드 풀로 사용하고, 사용하지 않는 노드를 free list로 관리하여 동적 할당 없이 연결 리스트를 구현할 수 있음.
*/

// ------------------- Test Code -------------------
void print_cb(const circular_buffer_t *cb) {
    printf("CB: ");
    size_t idx = cb->tail;
    for (size_t i = 0; i < cb->count; ++i) {
        printf("%u ", cb->buffer[idx]);
        idx = (idx + 1) % cb->size;
    }
    printf("\n");
}

void print_list(node_t *head) {
    printf("List: ");
    while (head) {
        printf("%d ", head->data);
        head = head->next;
    }
    printf("\n");
}

int main(void) {
    // Circular Buffer Test
    uint8_t buffer[CB_SIZE];
    circular_buffer_t cb;
    cb_init(&cb, buffer, CB_SIZE);

    printf("Push 1~8 to circular buffer:\n");
    for (uint8_t i = 1; i <= 8; ++i) {
        if (cb_push(&cb, i))
            printf("Pushed %u\n", i);
        else
            printf("Buffer full at %u\n", i);
    }
    print_cb(&cb);

    printf("Pop 3 elements:\n");
    for (int i = 0; i < 3; ++i) {
        uint8_t val;
        if (cb_pop(&cb, &val))
            printf("Popped %u\n", val);
        else
            printf("Buffer empty\n");
    }
    print_cb(&cb);

    printf("Flush buffer\n");
    cb_flush(&cb);
    print_cb(&cb);

    // Linked List Test
    node_t *head = NULL;
    printf("\nPush front 3, 2, 1:\n");
    list_push_front(&head, 1);
    list_push_front(&head, 2);
    list_push_front(&head, 3);
    print_list(head);

    printf("Push back 4, 5:\n");
    list_push_back(&head, 4);
    list_push_back(&head, 5);
    print_list(head);

    printf("Delete value 3:\n");
    list_delete_value(&head, 3);
    print_list(head);

    printf("Reverse list:\n");
    list_reverse(&head);
    print_list(head);

    printf("Middle node: ");
    node_t *mid = list_find_middle(head);
    if (mid) printf("%d\n", mid->data);

    printf("N-th from end (n=2): ");
    node_t *nth = list_nth_from_end(head, 2);
    if (nth) printf("%d\n", nth->data);

    // Merge sorted lists
    node_t *l1 = NULL, *l2 = NULL;
    list_push_back(&l1, 1);
    list_push_back(&l1, 3);
    list_push_back(&l1, 5);
    list_push_back(&l2, 2);
    list_push_back(&l2, 4);
    printf("\nMerge sorted lists:\n");
    node_t *merged = list_merge_sorted(l1, l2);
    print_list(merged);

    // Cycle detection
    printf("Cycle detection: ");
    printf("%s\n", list_has_cycle(merged) ? "Cycle" : "No Cycle");

    // Free all nodes
    while (merged) {
        node_t *tmp = merged;
        merged = merged->next;
        free(tmp);
    }
    while (head) {
        node_t *tmp = head;
        head = head->next;
        free(tmp);
    }

    return 0;
}