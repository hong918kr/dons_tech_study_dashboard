#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// 1. 나눗셈 구현 (몫과 나머지 구하기) - O(quotient)
void divide_with_remainder(int a, int b, int* quotient, int* remainder) {
    
    *quotient = 0;
    while(a>=b)
    {
        a-=b;
        (*quotient)++;
    }
    *remainder = a;
}

// 2. 나눗셈 최적화 (이진 곱셈/시프트 활용, 몫과 나머지 구하기) - O(Log2(quotient))
void divide_perf_with_remainder(int dividend, int divisor, int* quotient, int* remainder) {
    *quotient = 0;
    while (dividend <= divisor)
    {
        int temp = divisor;
        int multiple = 1;
        while ((temp<<1) <= dividend)
        {
            temp <<= 1;
            multiple <<= 1;
        }
        dividend -= temp;
        *quotient += multiple;
    }
    *remainder = dividend;
}

// 3. 비트 조작 함수
bool set_bit(uint32_t *variable, uint8_t bit_position) {
    // TODO: implement
    if (bit_position >= 32) return false;
    volatile uint32_t* v = (volatile uint32_t*) variable;
    *v |= (1U << bit_position);
    return true;
}
bool clear_bit(uint32_t *variable, uint8_t bit_position) {
    // TODO: implement
    if (bit_position >= 32) return false;
    volatile uint32_t* v = (volatile uint32_t*) variable;
    *v &= ~(1U << bit_position);
    return true;
}
bool get_bit(uint32_t variable, uint8_t bit_position) {
    // TODO: implement
    if (bit_position >= 32) return false;
    return (variable & (1U << bit_position)) != 0;
}

// 4. 64비트 타이머 안전 읽기 (Race Condition 방지)

volatile uint32_t TIMER_A = 0x12345678;
volatile uint32_t TIMER_B = 0x9ABCDEF0;

uint32_t read_reg(uint32_t* addr)
{
    volatile uint32_t* reg = (volatile uint32_t*)addr;
    return *reg;
}

void write_reg(uint32_t* addr, uint32_t val)
{
    volatile uint32_t* reg = (volatile uint32_t*)addr;
    *reg = val;
}

uint64_t read_combined_timer() {
    // TODO: implement
    uint32_t timer_a_val_1 = read_reg((uint32_t*)&TIMER_A);
    uint32_t timer_b_val = read_reg((uint32_t*)&TIMER_B);
    uint32_t timer_a_val_2= read_reg((uint32_t*)&TIMER_A);

    if (timer_a_val_1 != timer_a_val_2)
    {
        timer_b_val = read_reg((uint32_t*)&TIMER_B);
        timer_a_val_2 = read_reg((uint32_t*)&TIMER_A);
    }
    
    return ((uint64_t)timer_b_val << 32 | timer_a_val_2);
}

// 5. 문자열을 정수로 변환 (atoi)
int my_atoi(const char *str) {
    // TODO: implement
    int res = 0, sign = 1;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-')
    {
        sign = -1;
        str++;
    } else if (*str == '+')
    {
        str++;
    }
    while (*str >= '0' && *str <= '9')
    {
        res = res * 10 + (*str - '0');
        str++;
    }
    return sign * res;
}

// 6. 비트 리버스
uint32_t reverseBits(uint32_t n) {
    // TODO: implement
    uint32_t res = 0;
    for (size_t i = 0; i < 32; ++i)
    {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }

    return res;    
}

// 7. 문자열이 Palindrome인지 판별
int isStringPalindrome(const char *str) {
    // TODO: implement
    int l = 0;
    int r =0;
    const char* p = str;
    while(*p++) ++r;
    r--;
    p = str;
    while (l < r) 
    {
        if (str[l] != str[r]) return 0;
        l++; r--;
    }
    return 1;
}

// 8. 정수가 Palindrome인지 판별
int isIntegerPalindrome(int num) {
    // TODO: implement
    if (num < 0) return 0;
    int org = num;
    int rev = 0;
    while (num)
    {
        rev = rev * 10 + (num % 10);
        num = num / 10;
    }
    return org == rev;
}

// 9. 링 버퍼(Ring Buffer, Circular Queue) 구현
#define QMAX 5
typedef struct {
    uint8_t buf[QMAX];
    uint8_t head, tail, count;
} cb_t;

void cb_init(cb_t* q) {
    // TODO: implement
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}
bool cb_push(cb_t* q, uint8_t data) {
    // TODO: implement
    if (q->count == QMAX) return false;
    q->buf[q->tail] = data;
    q->tail = (q->tail + 1) % QMAX;
    q->count++;
    return true;
}
bool cb_pop(cb_t* q, uint8_t* data) {
    // TODO: implement
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
    // TODO: implement
    
}
void delete_node(Node** head, int data) {
    // TODO: implement
}
void reverse_list(Node** head) {
    // TODO: implement
}

// 11. 이중 연결리스트 (Doubly Linked List)
typedef struct DNode {
    int data;
    struct DNode* prev;
    struct DNode* next;
} DNode;

void dlist_add_node(DNode** head, int data) {
    // TODO: implement
}
void dlist_delete_node(DNode** head, int data) {
    // TODO: implement
}
void dlist_reverse(DNode** head) {
    // TODO: implement
}

// 12. 함수 포인터 기본 사용
int add(int a, int b) {
    // TODO: implement
    return 0;
}



// ------------------- 테스트 코드 -------------------

#define PASS 1
#define FAIL 0
int test_divide_with_remainder() {
    int result = PASS;
    int q, r;
    divide_with_remainder(10, 3, &q, &r);
    if (q != 3 || r != 1) result = FAIL;
    divide_with_remainder(15, 5, &q, &r);
    if (q != 3 || r != 0) result = FAIL;
    printf("1. divide_with_remainder: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_divide_perf_with_remainder() {
    int result = PASS;
    int q, r;
    divide_perf_with_remainder(10, 3, &q, &r);
    if (q != 3 || r != 1) result = FAIL;
    divide_perf_with_remainder(15, 5, &q, &r);
    if (q != 3 || r != 0) result = FAIL;
    printf("2. divide_perf_with_remainder: %s\n", result ? "PASS" : "FAIL");
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
    total += test_divide_with_remainder();
    total += test_divide_perf_with_remainder();
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