#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/*
56. uint8_t를 저장하는 원형 버퍼(Circular Buffer) 구현
    Implement a circular buffer for uint8_t.
*/
typedef struct {
    // TODO: implement
} circular_buffer_t; // 56

/*
57. 원형 버퍼 초기화 함수 구현
    Implement a function to initialize the circular buffer.
*/
void cb_init(circular_buffer_t *cb, uint8_t *buffer, size_t size) { // 57
    // TODO: implement
}

/*
58. 데이터 추가(push/enqueue) 함수 구현
    Implement a function to push/enqueue data into the circular buffer.
*/
bool cb_push(circular_buffer_t *cb, uint8_t data) { // 58
    // TODO: implement
}

/*
59. 데이터 추출(pop/dequeue) 함수 구현
    Implement a function to pop/dequeue data from the circular buffer.
*/
bool cb_pop(circular_buffer_t *cb, uint8_t *data) { // 59
    // TODO: implement
}

/*
60. 버퍼가 비어있는지 확인하는 함수 구현
    Implement a function to check if the buffer is empty.
*/
bool cb_is_empty(const circular_buffer_t *cb) { // 60
    // TODO: implement
}

/*
61. 버퍼가 가득 찼는지 확인하는 함수 구현
    Implement a function to check if the buffer is full.
*/
bool cb_is_full(const circular_buffer_t *cb) { // 61
    // TODO: implement
}

/*
62. 카운터 변수를 사용하여 full/empty 상태 관리 구현
    Manage full/empty state using a counter variable.
*/
// 62
/*
카운터 변수를 사용하면 head, tail 포인터만으로는 구분이 어려운 full/empty 상태를 명확하게 관리할 수 있음.
데이터 추가/삭제 시 카운터를 증가/감소시켜 상태를 추적함.
*/

/*
63. 원형 버퍼의 현재 데이터 개수 반환 함수 구현
    Implement a function to return the current number of elements in the buffer.
*/
size_t cb_size(const circular_buffer_t *cb) { // 63
    // TODO: implement
}

/*
64. 원형 버퍼를 비우는(flush) 함수 구현
    Implement a function to flush the circular buffer.
*/
void cb_flush(circular_buffer_t *cb) { // 64
    // TODO: implement
}

/*
65. 단일 생산자-단일 소비자 환경에서의 스레드 안전성 논의
    Discuss thread safety in single producer-single consumer environments.
*/
// 65
/*
단일 생산자-단일 소비자 환경에서는 head/tail 포인터를 각각 한 쪽에서만 접근하므로 락 없이도 스레드 안전성을 보장할 수 있음.
단, 변수의 원자적 접근이 보장되어야 하며, 멀티 프로듀서/멀티 컨슈머 환경에서는 추가적인 동기화가 필요함.
*/

/*
66. 단일 연결 리스트(Singly Linked List) 노드 구조체 정의
    Define a node struct for a singly linked list.
*/
typedef struct node {
    int data;
    struct node *next;
} node_t; // 66

/*
67. 연결 리스트의 맨 앞에 노드 추가
    Add a node at the beginning of the linked list.
*/
void list_push_front(node_t **head, int data) { // 67
    // TODO: implement
}

/*
68. 연결 리스트의 맨 뒤에 노드 추가
    Add a node at the end of the linked list.
*/
void list_push_back(node_t **head, int data) { // 68
    // TODO: implement
}

/*
69. 특정 값을 가진 노드 삭제
    Delete a node with a specific value from the linked list.
*/
void list_delete_value(node_t **head, int value) { // 69
    // TODO: implement
}

/*
70. 연결 리스트 뒤집기 (in-place)
    Reverse the linked list in-place.
*/
void list_reverse(node_t **head) { // 70
    // TODO: implement
}

/*
71. 연결 리스트의 중간 노드 찾기
    Find the middle node of the linked list.
*/
node_t* list_find_middle(node_t *head) { // 71
    // TODO: implement
}

/*
72. 연결 리스트에 순환(cycle)이 있는지 탐지
    Detect if the linked list has a cycle.
*/
bool list_has_cycle(node_t *head) { // 72
    // TODO: implement
}

/*
73. 두 개의 정렬된 연결 리스트 병합
    Merge two sorted linked lists.
*/
node_t* list_merge_sorted(node_t *l1, node_t *l2) { // 73
    // TODO: implement
}

/*
74. 연결 리스트의 끝에서 N번째 노드 찾기
    Find the N-th node from the end of the linked list.
*/
node_t* list_nth_from_end(node_t *head, size_t n) { // 74
    // TODO: implement
}

/*
75. 임베디드 시스템에서 malloc 없는 연결 리스트 구현
    Implement a linked list without malloc for embedded systems.
*/
// 75
/*
정적 배열을 노드 풀로 사용하고, 사용하지 않는 노드를 free list로 관리하여 동적 할당 없이 연결 리스트를 구현할 수 있음.
*/