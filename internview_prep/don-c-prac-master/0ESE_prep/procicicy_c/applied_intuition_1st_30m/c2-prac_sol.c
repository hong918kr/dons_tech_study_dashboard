/*

카테고리 2: 자료구조 (Data Structures)
16. uint8_t 데이터를 저장하는 원형 버퍼(Circular Buffer) 구현 (init, push, pop, is_empty, is_full)
    Follow-up: head==tail일 때 가득참/비어있음 구분법
17. (16번 연계) ISR과 메인 루프 간 데이터 전송 시 스레드 안전성 보장법
    Follow-up: volatile 변수 사용 이유
18. 고정 크기 메모리 풀에서 노드 할당하는 단일 연결 리스트 구현 (malloc 금지)
    Follow-up: 실시간 시스템에서 malloc을 피하는 이유
19. 배열로 스택(LIFO) 구현 (push, pop, peek)
    Follow-up: 오버플로우/언더플로우 감지법
20. 배열로 큐(FIFO) 구현 (enqueue, dequeue)
    Follow-up: 원형 버퍼와의 차이, 적합 상황
21. 키-값 쌍 저장 간단 해시 테이블 구현 (충돌은 연결 리스트)
    Follow-up: 임베디드에서 해시 테이블의 메모리/성능 고려
22. 이진 탐색 트리(BST)에 정수 삽입 함수
    Follow-up: 펌웨어에서 BST가 자주 쓰이지 않는 이유

*/

/*
카테고리 2: 자료구조 (Data Structures)
각 문제는 함수 시그니처, 설명, 예제 변수, follow-up 답변, 테스트 코드(PASS/FAIL)를 포함합니다.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 16. 원형 버퍼(Circular Buffer) 구현
// 설명: uint8_t 데이터를 저장하는 원형 버퍼를 구현하세요. (init, push, pop, is_empty, is_full)
// Follow-up: head==tail일 때 가득참/비어있음 구분법은 count 변수를 추가하거나, 한 칸 비워두는 방식이 있습니다.
#define CB_SIZE 4
typedef struct {
    uint8_t buf[CB_SIZE];
    int head, tail, count;
} CircularBuffer;

void cb_init(CircularBuffer* cb) {
    cb->head = cb->tail = cb->count = 0;
}
int cb_is_empty(CircularBuffer* cb) {
    return cb->count == 0;
}
int cb_is_full(CircularBuffer* cb) {
    return cb->count == CB_SIZE;
}
int cb_push(CircularBuffer* cb, uint8_t val) {
    if (cb_is_full(cb)) return 0;
    cb->buf[cb->head] = val;
    cb->head = (cb->head + 1) % CB_SIZE;
    cb->count++;
    return 1;
}
int cb_pop(CircularBuffer* cb, uint8_t* val) {
    if (cb_is_empty(cb)) return 0;
    *val = cb->buf[cb->tail];
    cb->tail = (cb->tail + 1) % CB_SIZE;
    cb->count--;
    return 1;
}
// Follow-up 답변: head==tail일 때 count로 구분하거나, 한 칸 비워두면 (head+1)%size==tail이면 full로 간주할 수 있습니다.

// 17. ISR과 메인 루프 간 데이터 전송 시 스레드 안전성 보장법
// 설명: ISR과 메인 루프가 동시에 원형 버퍼를 접근할 때 스레드 안전성을 보장하려면 어떻게 해야 하나요?
// Follow-up: volatile 변수 사용 이유는 컴파일러 최적화로 인한 값 캐싱을 방지하기 위함입니다.
volatile uint8_t isr_flag = 0; // ISR에서 변경, 메인 루프에서 확인
// 답변: 임계구역 보호(예: 인터럽트 마스킹), volatile로 변수 선언 필요

// 18. 고정 크기 메모리 풀에서 노드 할당하는 단일 연결 리스트 구현 (malloc 금지)
// 설명: malloc/free 없이 정적 배열을 이용해 단일 연결 리스트를 구현하세요.
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

/*
Struct 문법 보충설명:
결론
    Node: 자기 자신을 가리키는 포인터(struct Node* next)가 필요하므로 태그와 typedef 둘 다 사용
    PoolList: 자기 자신을 가리킬 필요가 없으므로 태그 없이 typedef만 사용
즉,
    자기 참조가 필요한 구조체는 태그와 typedef를 모두 쓰고,
    필요 없는 구조체는 typedef만 써도 됩니다!

typedef struct {
    int val;
    struct Node* next;
} Node; -> 그니까 이렇게 쓰면 틀린 문법인거다.


*/


void pool_list_init(PoolList* pl) {
    pl->head = NULL;
    pl->free_list = &pl->pool[0];
    for (int i = 0; i < POOL_SIZE-1; ++i)
        pl->pool[i].next = &pl->pool[i+1];
    pl->pool[POOL_SIZE-1].next = NULL;
}
Node* pool_alloc(PoolList* pl) {
    if (!pl->free_list) return NULL;
    Node* n = pl->free_list;
    pl->free_list = n->next;
    n->next = NULL;
    return n;
}
void pool_free(PoolList* pl, Node* n) {
    n->next = pl->free_list;
    pl->free_list = n;
}
int pool_list_insert(PoolList* pl, int val) {
    Node* n = pool_alloc(pl);
    if (!n) return 0;
    n->val = val;
    n->next = pl->head;
    pl->head = n;
    return 1;
}
int pool_list_remove(PoolList* pl, int val) {
    Node** cur = &pl->head;
    while (*cur) {
        if ((*cur)->val == val) {
            Node* tmp = *cur;
            *cur = (*cur)->next;
            pool_free(pl, tmp);
            return 1;
        }
        cur = &(*cur)->next;
    }
    return 0;
}
// Follow-up 답변: 실시간 시스템에서 malloc/free는 예측 불가한 지연과 단편화 문제로 피해야 합니다.

// 19. 배열로 스택(LIFO) 구현
// 설명: 배열로 스택을 구현하세요. (push, pop, peek)
// Follow-up: 오버플로우/언더플로우는 top 인덱스로 감지할 수 있습니다.
#define STACK_SIZE 4
typedef struct {
    int arr[STACK_SIZE];
    int top;
} Stack;
void stack_init(Stack* s) { s->top = 0; }
int stack_push(Stack* s, int val) {
    if (s->top == STACK_SIZE) return 0;
    s->arr[s->top++] = val;
    return 1;
}
int stack_pop(Stack* s, int* val) {
    if (s->top == 0) return 0;
    *val = s->arr[--s->top];
    return 1;
}
int stack_peek(Stack* s, int* val) {
    if (s->top == 0) return 0;
    *val = s->arr[s->top-1];
    return 1;
}
// Follow-up 답변: top==0이면 언더플로우, top==STACK_SIZE면 오버플로우

// 20. 배열로 큐(FIFO) 구현
// 설명: 배열로 큐를 구현하세요. (enqueue, dequeue)
// Follow-up: 원형 버퍼는 head/tail로 구현, 일반 배열 큐는 front/rear 인덱스만 증가
#define QUEUE_SIZE 4
typedef struct {
    int arr[QUEUE_SIZE];
    int front, rear, count;
} Queue;
void queue_init(Queue* q) { q->front = q->rear = q->count = 0; }
int queue_enqueue(Queue* q, int val) {
    if (q->count == QUEUE_SIZE) return 0;
    q->arr[q->rear] = val;
    q->rear = (q->rear + 1) % QUEUE_SIZE;
    q->count++;
    return 1;
}
int queue_dequeue(Queue* q, int* val) {
    if (q->count == 0) return 0;
    *val = q->arr[q->front];
    q->front = (q->front + 1) % QUEUE_SIZE;
    q->count--;
    return 1;
}
// Follow-up 답변: 원형 버퍼는 공간 활용이 좋고, 일반 배열 큐는 front/rear가 끝까지 가면 shift 필요

// ------------------- 테스트 코드 -------------------
#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 16. 원형 버퍼 테스트
    CircularBuffer cb; cb_init(&cb);
    test_result("16. cb_is_empty (init)", cb_is_empty(&cb));
    cb_push(&cb, 1); cb_push(&cb, 2); cb_push(&cb, 3); cb_push(&cb, 4);
    test_result("16. cb_is_full", cb_is_full(&cb));
    uint8_t v;
    cb_pop(&cb, &v); test_result("16. cb_pop==1", v == 1);
    cb_pop(&cb, &v); test_result("16. cb_pop==2", v == 2);

    // 18. 메모리 풀 리스트 테스트
    PoolList pl; pool_list_init(&pl);
    test_result("18. pool_list_insert 1", pool_list_insert(&pl, 10));
    test_result("18. pool_list_insert 2", pool_list_insert(&pl, 20));
    test_result("18. pool_list_insert 3", pool_list_insert(&pl, 30));
    test_result("18. pool_list_insert 4", pool_list_insert(&pl, 40));
    test_result("18. pool_list_insert full", !pool_list_insert(&pl, 50));
    test_result("18. pool_list_remove 20", pool_list_remove(&pl, 20));
    test_result("18. pool_list_insert after free", pool_list_insert(&pl, 50));

    // 19. 스택 테스트
    Stack s; stack_init(&s);
    test_result("19. stack_push 1", stack_push(&s, 1));
    test_result("19. stack_push 2", stack_push(&s, 2));
    int sval;
    stack_pop(&s, &sval); test_result("19. stack_pop==2", sval == 2);
    stack_peek(&s, &sval); test_result("19. stack_peek==1", sval == 1);

    // 20. 큐 테스트
    Queue q; queue_init(&q);
    test_result("20. queue_enqueue 1", queue_enqueue(&q, 1));
    test_result("20. queue_enqueue 2", queue_enqueue(&q, 2));
    int qval;
    queue_dequeue(&q, &qval); test_result("20. queue_dequeue==1", qval == 1);
    queue_dequeue(&q, &qval); test_result("20. queue_dequeue==2", qval == 2);

    return 0;
}