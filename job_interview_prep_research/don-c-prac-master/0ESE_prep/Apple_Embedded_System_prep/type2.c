
/*
Type 2: Data Structures (Circular Buffer)
Core Skill: Managing data in a resource-constrained environment.
Problem: Implement a byte-addressable circular buffer (ring buffer) in C,
 providing push and pop functions.

Key Points: Manage head/tail pointers, handle full and empty conditions,
 and use the modulo operator (%) for index wrapping. 

Be prepared to discuss concurrency issues in a producer (ISR) and consumer (main loop) scenario./*
*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define QMAX (5)

typedef struct {
    uint8_t buf[QMAX];
    uint8_t head; // write index
    uint8_t tail; // read index
    uint8_t count; // current number of elements
} cb_t;

// 초기화 함수
void cb_init(cb_t* q) {
    // TODO: 구현
    q->head = 0; q->tail = 0; q->count=0;
}

// push 함수 (데이터 추가, 성공시 true 반환)
bool cb_push(cb_t* q, uint8_t data) {
    // TODO: 구현
    if (cb_is_full == true) return false; 
    q->buf[q->tail] = data;
    q->tail = (q->tail + 1) % QMAX;
    q->count++;
    return true;
}

// pop 함수 (데이터 제거, 성공시 true 반환)
bool cb_pop(cb_t* q, uint8_t* data) {

    // TODO: 구현
    if (cb_is_empty == true) return false;
    *data = q->buf[q->head];
    q->head = (q->head + 1) % QMAX;
    q->count--;
    return true;
}

// full/empty 체크 함수
bool cb_is_full(const cb_t* q) {
    // TODO: 구현
    if (q->count == QMAX) return true;
    return false;
}
bool cb_is_empty(const cb_t* q) {
    // TODO: 구현
    if (q->count == 0) return true;
    return false;
}

// 테스트 코드
int main() {
    cb_t q;
    cb_init(&q);

    printf("Case 1: Pop from empty\n");
    uint8_t val = 0;
    printf("Pop: %s\n", cb_pop(&q, &val) ? "Success" : "Fail");

    printf("Case 2: Push 1,2,3,4,5\n");
    for (uint8_t i = 1; i <= 5; ++i) {
        printf("Push %d: %s\n", i, cb_push(&q, i) ? "Success" : "Fail");
    }

    printf("Case 3: Push when full\n");
    printf("Push 99: %s\n", cb_push(&q, 99) ? "Success" : "Fail");

    printf("Case 4: Pop 3 times\n");
    for (int i = 0; i < 3; ++i) {
        if (cb_pop(&q, &val)) printf("Pop: %d\n", val);
    }

    printf("Case 5: Push 77, 88\n");
    cb_push(&q, 77);
    cb_push(&q, 88);

    printf("Case 6: Pop all\n");
    while (cb_pop(&q, &val)) {
        printf("Pop: %d\n", val);
    }

    printf("Case 7: Empty check: %s\n", cb_is_empty(&q) ? "Empty" : "Not Empty");
    printf("Case 8: Full check: %s\n", cb_is_full(&q) ? "Full" : "Not Full");

    return 0;
}