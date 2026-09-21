/*

카테고리 2: 자료구조 (Data Structures)

16. uint8_t 데이터를 저장하는 원형 버퍼(Circular Buffer) 구현 (init, push, pop, is_empty, is_full)
    - 설명: 고정 크기 배열을 사용하여 FIFO 방식의 원형 버퍼를 구현합니다.
    - Follow-up: head==tail일 때 가득참/비어있음 구분법(count 변수 사용 또는 한 칸 비워두기)

17. (16번 연계) ISR과 메인 루프 간 데이터 전송 시 스레드 안전성 보장법
    - 설명: ISR과 메인 루프가 동시에 버퍼를 접근할 때 데이터 무결성을 보장하는 방법을 고민하세요.
    - Follow-up: volatile 변수 사용 이유(컴파일러 최적화 방지)

18. 고정 크기 메모리 풀에서 노드 할당하는 단일 연결 리스트 구현 (malloc 금지)
    - 설명: 정적 배열을 이용해 동적 할당 없이 단일 연결 리스트를 구현합니다.
    - Follow-up: 실시간 시스템에서 malloc/free를 피해야 하는 이유(예측 불가한 지연, 단편화)

19. 배열로 스택(LIFO) 구현 (push, pop, peek)
    - 설명: 배열을 이용해 LIFO 구조의 스택을 구현합니다.
    - Follow-up: 오버플로우/언더플로우 감지(top 인덱스 활용)

20. 배열로 큐(FIFO) 구현 (enqueue, dequeue)
    - 설명: 배열을 이용해 FIFO 구조의 큐를 구현합니다.
    - Follow-up: 원형 버퍼와 일반 배열 큐의 차이, 각각의 적합 상황

21. 키-값 쌍 저장 간단 해시 테이블 구현 (충돌은 연결 리스트)
    - 설명: 해시 충돌 시 연결 리스트로 처리하는 간단 해시 테이블을 구현합니다.
    - Follow-up: 임베디드에서 해시 테이블의 메모리/성능 고려사항

22. 이진 탐색 트리(BST)에 정수 삽입 함수
    - 설명: 이진 탐색 트리에 정수를 삽입하는 함수를 구현합니다.
    - Follow-up: 펌웨어에서 BST가 자주 쓰이지 않는 이유(메모리, 실시간성 등)

*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 16. 원형 버퍼(Circular Buffer) 구조체 및 함수 원형
#define CB_SIZE 4
typedef struct {
    uint8_t buf[CB_SIZE];
    int head, tail, count;
} CB_t;

void cb_init(CB_t* cb)
{
    
}
int cb_is_empty(CB_t* cb)
{
    
}



// 17. ISR과 메인 루프 간 데이터 전송 시 volatile 변수 예시
volatile uint8_t isr_flag = 0;

// 18. 고정 크기 메모리 풀 단일 연결 리스트 구조체 및 함수 원형
#define POOL_SIZE 4
typedef struct Node {
    int val;
    struct Node* next;
} Node;

typedef struct {
    Node pool[POOL_SIZE];
    Node* free_list;
    Node* head;
} PoolList;




void pool_list_init(PoolList* pl) {}
Node* pool_alloc(PoolList* pl) {}
void pool_free(PoolList* pl, Node* n) {}
int pool_list_insert(PoolList* pl, int val) {}
int pool_list_remove(PoolList* pl, int val) {}


// 19. 배열로 스택(LIFO) 구조체 및 함수 원형
#define STACK_SIZE 4
typedef struct {
    int arr[STACK_SIZE];
    int top;
} Stack;
void stack_init(Stack* s) {}
int stack_push(Stack* s, int val) {}
int stack_pop(Stack* s, int* val) {}
int stack_peek(Stack* s, int* val) {}

// 20. 배열로 큐(FIFO) 구조체 및 함수 원형
#define QUEUE_SIZE 4
typedef struct {
    int arr[QUEUE_SIZE];
    int front, rear, count;
} Queue;
void queue_init(Queue* q) {}
int queue_enqueue(Queue* q, int val) {}
int queue_dequeue(Queue* q, int* val) {}

#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    CircularBuffer cb; cb_init(&cb);
    test_result("16. cb_is_empty (init)", cb_is_empty(&cb));
    cb_push(&cb, 1); cb_push(&cb, 2); cb_push(&cb, 3); cb_push(&cb, 4);
    test_result("16. cb_is_full", cb_is_full(&cb));
    uint8_t v;
    cb_pop(&cb, &v); test_result("16. cb_pop==1", v == 1);
    cb_pop(&cb, &v); test_result("16. cb_pop==2", v == 2);

    PoolList pl; pool_list_init(&pl);
    test_result("18. pool_list_insert 1", pool_list_insert(&pl, 10));
    test_result("18. pool_list_insert 2", pool_list_insert(&pl, 20));
    test_result("18. pool_list_insert 3", pool_list_insert(&pl, 30));
    test_result("18. pool_list_insert 4", pool_list_insert(&pl, 40));
    test_result("18. pool_list_insert full", !pool_list_insert(&pl, 50));
    test_result("18. pool_list_remove 20", pool_list_remove(&pl, 20));
    test_result("18. pool_list_insert after free", pool_list_insert(&pl, 50));

    Stack s; stack_init(&s);
    test_result("19. stack_push 1", stack_push(&s, 1));
    test_result("19. stack_push 2", stack_push(&s, 2));
    int sval;
    stack_pop(&s, &sval); test_result("19. stack_pop==2", sval == 2);
    stack_peek(&s, &sval); test_result("19. stack_peek==1", sval == 1);

    Queue q; queue_init(&q);
    test_result("20. queue_enqueue 1", queue_enqueue(&q, 1));
    test_result("20. queue_enqueue 2", queue_enqueue(&q, 2));
    int qval;
    queue_dequeue(&q, &qval); test_result("20. queue_dequeue==1", qval == 1);
    queue_dequeue(&q, &qval); test_result("20. queue_dequeue==2", qval == 2);

    return 0;
}