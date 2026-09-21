#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// 1. 원형 버퍼 구현 (정수형, 고정 크기)
typedef struct {
    int* buf;
    int head;
    int tail;
    int size;
    int capacity;
} cb_t;

void cb_init(cb_t* cb, int* buffer, int capacity);
bool cb_push(cb_t* cb, int val);
bool cb_pop(cb_t* cb, int* val);
bool cb_empty(cb_t* cb);
bool cb_full(cb_t* cb);

// 2. 문자열이 회문인지 확인하는 함수
bool is_palindrome(const char* s);

// =================== 테스트 코드 ===================
int main() {
    int pass_cnt = 0;

    // 1. 원형 버퍼 테스트
    int buf[3];
    cb_t cb;
    cb_init(&cb, buf, 3);

    if (cb_empty(&cb)) { printf("TC1 PASS\n"); pass_cnt++; } else { printf("TC1 FAIL\n"); }
    if (cb_push(&cb, 1) && cb_push(&cb, 2) && cb_push(&cb, 3) && !cb_push(&cb, 4)) { printf("TC2 PASS\n"); pass_cnt++; } else { printf("TC2 FAIL\n"); }
    int v;
    if (cb_pop(&cb, &v) && v == 1) { printf("TC3 PASS\n"); pass_cnt++; } else { printf("TC3 FAIL\n"); }
    if (cb_push(&cb, 4) && cb_full(&cb)) { printf("TC4 PASS\n"); pass_cnt++; } else { printf("TC4 FAIL\n"); }
    cb_pop(&cb, &v); cb_pop(&cb, &v); cb_pop(&cb, &v);
    if (cb_empty(&cb)) { printf("TC5 PASS\n"); pass_cnt++; } else { printf("TC5 FAIL\n"); }

    // 2. 회문 테스트
    if (is_palindrome("abba")) { printf("TC6 PASS\n"); pass_cnt++; } else { printf("TC6 FAIL\n"); }
    if (!is_palindrome("abc")) { printf("TC7 PASS\n"); pass_cnt++; } else { printf("TC7 FAIL\n"); }
    if (is_palindrome("a")) { printf("TC8 PASS\n"); pass_cnt++; } else { printf("TC8 FAIL\n"); }
    if (is_palindrome("")) { printf("TC9 PASS\n"); pass_cnt++; } else { printf("TC9 FAIL\n"); }
    if (is_palindrome("racecar")) { printf("TC10 PASS\n"); pass_cnt++; } else { printf("TC10 FAIL\n"); }

    printf("Total Passed: %d/10\n", pass_cnt);
    return 0;
}

// 함수 시그니처만, 구현은 직접 작성하세요.
void cb_init(cb_t* cb, int* buffer, int capacity) {
     /* TODO */
    cb->buf = buffer;
    cb->capacity = capacity; cb->size=0;cb->head=0;cb->tail=0;
}
bool cb_push(cb_t* cb, int val) {
     /* TODO */ 
    if (cb->size == cb->capacity) return false;
    cb->buf[cb->tail] = val;
    cb->tail = (cb->tail + 1) % cb->capacity;
    cb->size++;
    return true;
}
bool cb_pop(cb_t* cb, int* val) {
     /* TODO */ 
    if (cb->size == 0) return false;
    
    *val = cb->buf[cb->head];
    cb->head = (cb->head + 1) % cb->capacity;
    cb->size--;

    return true; 
}
bool cb_empty(cb_t* cb) {
     /* TODO */ 
    return (cb->size == 0);
}
bool cb_full(cb_t* cb) {
     /* TODO */ 
     
    return (cb->size == cb->capacity); 
}

bool is_palindrome(const char* s) {
     /* TODO */ 
    
    int left = 0;
    int right = 0;
    const char* c = s;
    while(*c) {
        c++;
        right++;
    }
    c--;
    right--;
    while (left < right)
    {
        if (s[left] != s[right]) return false;
        left++;
        right--;
    }
    return true; 
}