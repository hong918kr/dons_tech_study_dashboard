#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// 1. 나눗셈 구현 (몫만 구하기)
int divider(int a, int b) {
    int res = 0;
    while (a >= b) {
        a = a - b;
        res += 1;
    }
    return res;
}

// 2. 나눗셈 최적화 (이진 곱셈/시프트 활용)
int divider_perf_1(int dividend, int divisor) {
    int res = 0;
    while (dividend >= divisor) {
        int temp = divisor;
        int multiple = 1;
        while (temp + temp <= dividend) {
            temp = temp + temp;
            multiple = multiple + multiple;
        }
        dividend = dividend - temp;
        res = res + multiple;
    }
    return res;
}

int divider_perf_2(int dividend, int divisor) {
    int res = 0;
    while (dividend >= divisor) {
        int temp = divisor;
        int mul = 1;
        while ((temp << 1) <= dividend) {
            temp = (temp << 1);
            mul = (mul << 1);
        }
        dividend = dividend - temp;
        res = res + mul;
    }
    return res;
}

// 3. 비트 조작 함수
bool set_bit(uint32_t *variable, uint8_t bit_position) {
    if (bit_position >= 32) return false;
    *variable |= (1UL << bit_position);
    return true;
}
bool clear_bit(uint32_t *variable, uint8_t bit_position) {
    if (bit_position >= 32) return false;
    *variable &= ~(1UL << bit_position);
    return true;
}
bool get_bit(uint32_t variable, uint8_t bit_position) {
    if (bit_position >= 32) return false;
    return (variable >> bit_position) & 1;
}

// 4. 64비트 타이머 안전 읽기 (Race Condition 방지)
// 아래는 실제 하드웨어가 없으므로 시뮬레이션용 코드입니다.
volatile uint32_t TIMER_A = 0x12345678;
volatile uint32_t TIMER_B = 0x9ABCDEF0;
#define TIMER_A_READ_REG TIMER_A
#define TIMER_B_READ_REG TIMER_B

uint64_t read_combined_timer() {
    uint32_t timer_a_value_1;
    uint32_t timer_b_value;
    uint32_t timer_a_value_2;
    timer_a_value_1 = TIMER_A_READ_REG;
    timer_b_value = TIMER_B_READ_REG;
    timer_a_value_2 = TIMER_A_READ_REG;
    if (timer_a_value_1 != timer_a_value_2) {
        timer_b_value = TIMER_B_READ_REG;
    }
    return ((uint64_t)timer_b_value << 32) | timer_a_value_2;
}

// 5. 문자열을 정수로 변환 (atoi)
int my_atoi(const char *str) {
    int res = 0, sign = 1;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

// 6. 비트 리버스
uint32_t reverseBits(uint32_t n) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }
    return res;
}

// 7. 문자열이 Palindrome인지 판별
int isStringPalindrome(const char *str) {
    int l = 0, r = strlen(str) - 1;
    while (l < r) {
        if (str[l] != str[r]) return 0;
        l++; r--;
    }
    return 1;
}

// 8. 정수가 Palindrome인지 판별
int isIntegerPalindrome(int num) {
    if (num < 0) return 0;
    int orig = num, rev = 0;
    while (num) {
        rev = rev * 10 + num % 10;
        num /= 10;
    }
    return orig == rev;
}

// 9. 링 버퍼(Ring Buffer, Circular Queue) 구현
#define QMAX 5
typedef struct {
    uint8_t buf[QMAX];
    uint8_t head, tail, count;
} cb_t;

void cb_init(cb_t* q) {
    q->head = q->tail = q->count = 0;
}
bool cb_push(cb_t* q, uint8_t data) {
    if (q->count == QMAX) return false;
    q->buf[q->tail] = data;
    q->tail = (q->tail + 1) % QMAX;
    q->count++;
    return true;
}
bool cb_pop(cb_t* q, uint8_t* data) {
    if (q->count == 0) return false;
    *data = q->buf[q->head];
    q->head = (q->head + 1) % QMAX;
    q->count--;
    return true;
}

// 10. 단일 연결리스트 (Singly Linked List)
typedef struct Node {
    int data;
    struct Node* next;
} Node;

void add_node(Node** head, int data) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->data = data;
    new_node->next = NULL;
    if (!*head) *head = new_node;
    else {
        Node* cur = *head;
        while (cur->next) cur = cur->next;
        cur->next = new_node;
    }
}
void delete_node(Node** head, int data) {
    Node* cur = *head, *prev = NULL;
    while (cur) {
        if (cur->data == data) {
            if (prev)
            {
                prev->next = cur->next;
            }
            else
            {
                *head = cur->next;
            } 
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}
void reverse_list(Node** head) {
    Node* prev = NULL, *cur = *head, *next = NULL;
    while (cur) {
        next = cur->next;
        cur->next = prev;
        prev = cur;
        cur = next;
    }
    *head = prev;
}

// 11. 이중 연결리스트 (Doubly Linked List)
typedef struct DNode {
    int data;
    struct DNode* prev;
    struct DNode* next;
} DNode;

void dlist_add_node(DNode** head, int data) {
    DNode* new_node = (DNode*)malloc(sizeof(DNode));
    new_node->data = data;
    new_node->prev = NULL;
    new_node->next = NULL;
    if (!*head) *head = new_node;
    else {
        DNode* cur = *head;
        while (cur->next) cur = cur->next;
        cur->next = new_node;
        new_node->prev = cur;
    }
}
void dlist_delete_node(DNode** head, int data) {
    DNode* cur = *head;
    while (cur) {
        if (cur->data == data) {
            if (cur->prev) cur->prev->next = cur->next;
            else *head = cur->next;
            if (cur->next) cur->next->prev = cur->prev;
            free(cur);
            return;
        }
        cur = cur->next;
    }
}
void dlist_reverse(DNode** head) {
    DNode* cur = *head;
    DNode* prev = NULL;
    while (cur) {
        DNode* next = cur->next;
        cur->next = prev;
        cur->prev = next;
        prev = cur;
        cur = next;
    }
    *head = prev;
}

// 12. 함수 포인터 기본 사용
int add(int a, int b) {
    return a + b;
}

// ------------------- 테스트 코드 -------------------

#define PASS 1
#define FAIL 0

int test_divider() {
    int result = PASS;
    if (divider(10, 3) != 3) result = FAIL;
    if (divider(15, 5) != 3) result = FAIL;
    printf("1. divider: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_divider_perf() {
    int result = PASS;
    if (divider_perf_1(10, 3) != 3) result = FAIL;
    if (divider_perf_2(15, 5) != 3) result = FAIL;
    printf("2. divider_perf: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_bit_ops() {
    int result = PASS;
    uint32_t v = 0;
    set_bit(&v, 3);
    if (!(v & (1 << 3))) result = FAIL;
    clear_bit(&v, 3);
    if (v & (1 << 3)) result = FAIL;
    if (!set_bit(&v, 31)) result = FAIL;
    if (!get_bit(v, 31)) result = FAIL;
    printf("3. bit_ops: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_read_combined_timer() {
    int result = PASS;
    uint64_t t = read_combined_timer();
    if ((uint32_t)t != TIMER_A) result = FAIL;
    if ((uint32_t)(t >> 32) != TIMER_B) result = FAIL;
    printf("4. read_combined_timer: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_my_atoi() {
    int result = PASS;
    if (my_atoi("123") != 123) result = FAIL;
    if (my_atoi("-45") != -45) result = FAIL;
    if (my_atoi("0") != 0) result = FAIL;
    if (my_atoi("00123") != 123) result = FAIL;
    printf("5. my_atoi: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_reverseBits() {
    int result = PASS;
    if (reverseBits(0x80000000) != 0x1) result = FAIL;
    if (reverseBits(0xF0F0F0F0) != 0x0F0F0F0F) result = FAIL;
    printf("6. reverseBits: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_isStringPalindrome() {
    int result = PASS;
    if (!isStringPalindrome("abba")) result = FAIL;
    if (isStringPalindrome("abc")) result = FAIL;
    printf("7. isStringPalindrome: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_isIntegerPalindrome() {
    int result = PASS;
    if (!isIntegerPalindrome(121)) result = FAIL;
    if (isIntegerPalindrome(123)) result = FAIL;
    printf("8. isIntegerPalindrome: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_ring_buffer() {
    int result = PASS;
    cb_t q;
    cb_init(&q);
    if (!cb_push(&q, 1)) result = FAIL;
    if (!cb_push(&q, 2)) result = FAIL;
    uint8_t val = 0;
    if (!cb_pop(&q, &val) || val != 1) result = FAIL;
    printf("9. ring_buffer: %s\n", result ? "PASS" : "FAIL");
    return result;
}
int test_singly_linked_list() {
    int result = PASS;
    Node* head = NULL;
    add_node(&head, 10);
    add_node(&head, 20);
    add_node(&head, 30);
    delete_node(&head, 20);
    reverse_list(&head);
    if (!head || head->data != 30 || !head->next || head->next->data != 10 || head->next->next != NULL) result = FAIL;
    printf("10. singly_linked_list: %s\n", result ? "PASS" : "FAIL");
    // free list
    while (head) {
        Node* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}
int test_doubly_linked_list() {
    int result = PASS;
    DNode* head = NULL;
    dlist_add_node(&head, 1);
    dlist_add_node(&head, 2);
    dlist_add_node(&head, 3);
    dlist_delete_node(&head, 2);
    dlist_reverse(&head);
    if (!head || head->data != 1 || !head->next || head->next->data != 3 || head->next->next != NULL) result = FAIL;
    printf("11. doubly_linked_list: %s\n", result ? "PASS" : "FAIL");
    // free list
    while (head) {
        DNode* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}
int test_func_ptr() {
    int result = PASS;
    int (*fptr)(int, int) = add;
    if (fptr(2, 3) != 5) result = FAIL;
    printf("12. function pointer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int main() {
    int total = 0;
    total += test_divider();
    total += test_divider_perf();
    total += test_bit_ops();
    total += test_read_combined_timer();
    total += test_my_atoi();
    total += test_reverseBits();
    total += test_isStringPalindrome();
    total += test_isIntegerPalindrome();
    total += test_ring_buffer();
    total += test_singly_linked_list();
    total += test_doubly_linked_list();
    total += test_func_ptr();
    printf("Total Passed: %d/12\n", total);
    return 0;
}