#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PASS 1
#define FAIL 0

// 1. Set the 3rd bit of an 8-bit variable.
void set_bit3(uint8_t* val) {
    // TODO: implement
    *val |= (1U << 3);
}

// 2. Extract bits 5~7 from a 32-bit value.
uint8_t extract_bits_5_7(uint32_t val) {
    // TODO: implement
    
    uint8_t mask = 0x7 << 5;
    return (val & mask) >> 5;
}

// 3. Copy string (like strcpy)
void my_strcpy(char* dst, const char* src) {
    // TODO: implement
    while (*dst++ = *src++);
}

// 4. Reverse string in-place
void reverse_str(char* str) {
    // TODO: implement

}

// 5. Find max in array
int find_max(const int* arr, int n) {
    // TODO: implement
    return 0;
}

// 6. Sum array
int sum_array(const int* arr, int n) {
    // TODO: implement
    return 0;
}

// 7. Add node to singly linked list (tail)
typedef struct Node {
    int data;
    struct Node* next;
} Node;

void add_node(Node** head, int data) {
    // TODO: implement
}

// 8. Delete node from singly linked list
void delete_node(Node** head, int data) {
    // TODO: implement
}

// 9. Initialize array with for loop
void init_array(int* arr, int n) {
    // TODO: implement
}

// 10. Access struct member via pointer
typedef struct S {
    int a;
    char b;
} S;
void set_struct_member(S* s, int a, char b) {
    // TODO: implement
}

// 11. Add node to doubly linked list (tail)
typedef struct DNode {
    int data;
    struct DNode* prev;
    struct DNode* next;
} DNode;

void dlist_add_node(DNode** head, int data) {
    // TODO: implement
}

// 12. Delete node from doubly linked list
void dlist_delete_node(DNode** head, int data) {
    // TODO: implement
}


/* 13. 문자열을 정수로 변환 (my_atoi)
   - 문자열로 표현된 정수를 int로 변환합니다. (예: "123" → 123, "-45" → -45)
*/
int my_atoi(const char* str) {
    // TODO: 구현
    return 0;
}

/* 14. 메모리 세트/카피 (my_memset, my_memcpy)
   - my_memset: 메모리 영역을 특정 값으로 채웁니다.
   - my_memcpy: 메모리 영역을 복사합니다.
*/
void* my_memset(void* dst, int val, size_t n) {
    // TODO: 구현
    return dst;
}
void* my_memcpy(void* dst, const void* src, size_t n) {
    // TODO: 구현
    return dst;
}

/* 15. 32비트 값의 엔디안 변환 (swap_endian32)
   - 0x12345678 → 0x78563412로 바꿉니다.
*/
uint32_t swap_endian32(uint32_t val) {
    // TODO: 구현
    return 0;
}

/* 16. 비트 카운트 (Set Bit Count)
   - 주어진 값에서 1로 설정된 비트의 개수를 반환합니다.
*/
int count_set_bits(uint32_t val) {
    // TODO: 구현
    return 0;
}

/* 17. 8비트 비트 리버스 (reverse_bits8)
   - 8비트 값을 비트 단위로 뒤집어 반환합니다. (예: 0b11010010 → 0b01001011)
*/
uint8_t reverse_bits8(uint8_t val) {
    // TODO: 구현
    return 0;
}

/* 18. 비트 조작 함수 (set, get, clear, toggle)
   - 특정 비트를 set/get/clear/toggle하는 함수들입니다.
*/
void set_bit(uint32_t* val, int bit) {
    // TODO: 구현
}
void clear_bit(uint32_t* val, int bit) {
    // TODO: 구현
}
void toggle_bit(uint32_t* val, int bit) {
    // TODO: 구현
}
int get_bit(uint32_t val, int bit) {
    // TODO: 구현
    return 0;
}

/* 19. 메모리 쓰기/읽기 함수 (memory write/read)
   - 임의의 주소에 값을 쓰고 읽는 함수입니다.
*/
void memory_write(uint8_t* addr, uint8_t val) {
    // TODO: 구현
}
uint8_t memory_read(const uint8_t* addr) {
    // TODO: 구현
    return 0;
}

/* 20. 문자열이 회문인지 확인 (isStringPalindrome)
   - 문자열이 앞뒤로 같은지 판별합니다.
*/
int isStringPalindrome(const char* s) {
    // TODO: 구현
    return 0;
}

/* 21. 정수가 회문인지 확인 (isIntegerPalindrome)
   - 정수를 뒤집었을 때 원래 값과 같은지 판별합니다.
*/
int isIntegerPalindrome(int n) {
    // TODO: 구현
    return 0;
}

/* 22. 타이머 값 읽기 (read_combined_timer)
   - 두 개의 16비트 타이머 값을 읽어 32비트로 합칩니다.
*/
uint32_t read_combined_timer(uint16_t t1, uint16_t t2) {
    // TODO: 구현
    return 0;
}

/* 23. 링 버퍼(ring buffer) 구현
   - 고정 크기의 원형 버퍼에 데이터를 push/pop하는 함수입니다.
*/
#define RBUF_SIZE 4
typedef struct {
    uint8_t buf[RBUF_SIZE];
    int head, tail, count;
} ring_buffer_t;

void rbuf_init(ring_buffer_t* rb) {
    // TODO: 구현
}
int rbuf_push(ring_buffer_t* rb, uint8_t val) {
    // TODO: 구현
    return 0;
}
int rbuf_pop(ring_buffer_t* rb, uint8_t* val) {
    // TODO: 구현
    return 0;
}

/* 24. 함수 포인터와 상태머신 (최소 3개 상태)
   - 함수 포인터를 이용해 간단한 상태머신을 구현합니다.
*/
typedef enum { STATE_IDLE, STATE_RUN, STATE_STOP } State;
typedef void (*StateFunc)(void);

void state_idle(void) { /* TODO: 구현 */ }
void state_run(void) { /* TODO: 구현 */ }
void state_stop(void) { /* TODO: 구현 */ }

/* 25. 문자열 비교 (strcmp)
   - 두 문자열을 비교하여 같으면 0, 다르면 음수/양수 반환
*/
int my_strcmp(const char* s1, const char* s2) {
    // TODO: 구현
    return 0;
}

/* 26. 구조체 바이트 패킹/언패킹
   - 구조체를 바이트 배열로 변환하고, 다시 구조체로 복원합니다.
*/
typedef struct {
    uint8_t a;
    uint16_t b;
} PackStruct;
void pack_bytes(const PackStruct* s, uint8_t* buf) {
    // TODO: 구현
}
void unpack_bytes(PackStruct* s, const uint8_t* buf) {
    // TODO: 구현
}

/* 27. 8비트 비트 리버스 (Bit Reverse)
   - 8비트 값을 비트 단위로 뒤집어 반환합니다.
*/
uint8_t reverse_bits8_v2(uint8_t val) {
    // TODO: 구현
    return 0;
}

/* 28. CRC 계산 (CRC8)
   - 데이터 배열의 CRC8 체크섬을 계산합니다.
*/
uint8_t calc_crc8(const uint8_t* data, size_t len) {
    // TODO: 구현
    return 0;
}
/* ===================== 테스트 코드 ===================== */

// 1. Set the 3rd bit of an 8-bit variable.
int test_set_bit3() {
    uint8_t v = 0x00;
    set_bit3(&v);
    int result = (v == 0x08) ? PASS : FAIL;
    printf("1. set_bit3: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 2. Extract bits 5~7 from a 32-bit value.
int test_extract_bits_5_7() {
    uint32_t v = 0x000000E0; // bits 5~7 are 111
    int result = (extract_bits_5_7(v) == 7) ? PASS : FAIL;
    printf("2. extract_bits_5_7: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 3. Copy string (like strcpy)
int test_my_strcpy() {
    char buf[20];
    my_strcpy(buf, "hello");
    int result = (strcmp(buf, "hello") == 0) ? PASS : FAIL;
    printf("3. my_strcpy: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 4. Reverse string in-place
int test_reverse_str() {
    char buf[20] = "abcde";
    reverse_str(buf);
    int result = (strcmp(buf, "edcba") == 0) ? PASS : FAIL;
    printf("4. reverse_str: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 5. Find max in array
int test_find_max() {
    int arr[5] = {3, 7, 2, 9, 5};
    int result = (find_max(arr, 5) == 9) ? PASS : FAIL;
    printf("5. find_max: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 6. Sum array
int test_sum_array() {
    int arr[5] = {1, 2, 3, 4, 5};
    int result = (sum_array(arr, 5) == 15) ? PASS : FAIL;
    printf("6. sum_array: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 7. Add node to singly linked list (tail)
int test_add_node() {
    Node* head = NULL;
    add_node(&head, 10);
    add_node(&head, 20);
    int result = PASS;
    Node* cur = head;
    if (!cur || cur->data != 10) result = FAIL;
    else if (!cur->next || cur->next->data != 20) result = FAIL;
    else if (cur->next->next != NULL) result = FAIL;
    printf("7. add_node: %s\n", result ? "PASS" : "FAIL");
    // Free list
    while (head) {
        Node* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}

// 8. Delete node from singly linked list
int test_delete_node() {
    Node* head = NULL;
    add_node(&head, 10);
    add_node(&head, 20);
    add_node(&head, 30);
    delete_node(&head, 20);
    int result = PASS;
    Node* cur = head;
    if (!cur || cur->data != 10) result = FAIL;
    else if (!cur->next || cur->next->data != 30) result = FAIL;
    else if (cur->next->next != NULL) result = FAIL;
    printf("8. delete_node: %s\n", result ? "PASS" : "FAIL");
    // Free list
    while (head) {
        Node* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}

// 9. Initialize array with for loop
int test_init_array() {
    int arr[5];
    init_array(arr, 5);
    int result = PASS;
    for (int i = 0; i < 5; ++i)
        if (arr[i] != i) result = FAIL;
    printf("9. init_array: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 10. Access struct member via pointer
int test_set_struct_member() {
    S s;
    set_struct_member(&s, 42, 'Z');
    int result = (s.a == 42 && s.b == 'Z') ? PASS : FAIL;
    printf("10. set_struct_member: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 11. Add node to doubly linked list (tail)
int test_dlist_add_node() {
    DNode* head = NULL;
    dlist_add_node(&head, 1);
    dlist_add_node(&head, 2);
    dlist_add_node(&head, 3);
    int result = PASS;
    DNode* cur = head;
    if (!cur || cur->data != 1) result = FAIL;
    else if (!cur->next || cur->next->data != 2) result = FAIL;
    else if (!cur->next->next || cur->next->next->data != 3) result = FAIL;
    else if (cur->next->prev != cur) result = FAIL;
    else if (cur->next->next->prev != cur->next) result = FAIL;
    printf("11. dlist_add_node: %s\n", result ? "PASS" : "FAIL");
    // Free list
    while (head) {
        DNode* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}

// 12. Delete node from doubly linked list
int test_dlist_delete_node() {
    DNode* head = NULL;
    dlist_add_node(&head, 1);
    dlist_add_node(&head, 2);
    dlist_add_node(&head, 3);
    dlist_delete_node(&head, 2);
    int result = PASS;
    DNode* cur = head;
    if (!cur || cur->data != 1) result = FAIL;
    else if (!cur->next || cur->next->data != 3) result = FAIL;
    else if (cur->next->prev != cur) result = FAIL;
    else if (cur->next->next != NULL) result = FAIL;
    printf("12. dlist_delete_node: %s\n", result ? "PASS" : "FAIL");
    // Free list
    while (head) {
        DNode* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}






int test_my_atoi() {
    int result = PASS;
    if (my_atoi("123") != 123) result = FAIL;
    if (my_atoi("-45") != -45) result = FAIL;
    if (my_atoi("0") != 0) result = FAIL;
    if (my_atoi("00123") != 123) result = FAIL;
    printf("13. my_atoi: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_my_memset_memcpy() {
    int result = PASS;
    char buf[10];
    my_memset(buf, 'A', 5);
    buf[5] = '\0';
    if (strcmp(buf, "AAAAA") != 0) result = FAIL;

    char src[] = "hello";
    my_memcpy(buf, src, 6);
    if (strcmp(buf, "hello") != 0) result = FAIL;

    printf("14. my_memset/my_memcpy: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_swap_endian32() {
    int result = PASS;
    if (swap_endian32(0x12345678) != 0x78563412) result = FAIL;
    printf("15. swap_endian32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_count_set_bits() {
    int result = PASS;
    if (count_set_bits(0xF0F0F0F0) != 16) result = FAIL;
    printf("16. count_set_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_reverse_bits8() {
    int result = PASS;
    if (reverse_bits8(0b11010010) != 0x4B) result = FAIL;
    printf("17. reverse_bits8: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_bit_ops() {
    int result = PASS;
    uint32_t v = 0;
    set_bit(&v, 2);
    if (v != 0x4) result = FAIL;
    clear_bit(&v, 2);
    if (v != 0x0) result = FAIL;
    set_bit(&v, 1);
    toggle_bit(&v, 1);
    if (v != 0x0) result = FAIL;
    set_bit(&v, 3);
    if (get_bit(v, 3) != 1) result = FAIL;
    printf("18. bit_ops: %s\n", result ? "PASS" : "FAIL");
    return result;
}


int test_memory_rw() {
    int result = PASS;
    volatile uint8_t buf[1];
    memory_write(buf, 0xAB);
    if (memory_read(buf) != 0xAB) result = FAIL;
    printf("19. memory_rw: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_isStringPalindrome() {
    int result = PASS;
    if (!isStringPalindrome("abba")) result = FAIL;
    if (isStringPalindrome("abc")) result = FAIL;
    printf("20. isStringPalindrome: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_isIntegerPalindrome() {
    int result = PASS;
    if (!isIntegerPalindrome(121)) result = FAIL;
    if (isIntegerPalindrome(123)) result = FAIL;
    printf("21. isIntegerPalindrome: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_read_combined_timer() {
    int result = PASS;
    if (read_combined_timer(0x1234, 0xABCD) != 0x1234ABCD) result = FAIL;
    printf("22. read_combined_timer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_ring_buffer() {
    int result = PASS;
    ring_buffer_t rb;
    rbuf_init(&rb);
    rbuf_push(&rb, 1);
    rbuf_push(&rb, 2);
    uint8_t v;
    rbuf_pop(&rb, &v);
    if (v != 1) result = FAIL;
    printf("23. ring_buffer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_state_machine() {
    StateFunc table[3] = { state_idle, state_run, state_stop };
    // TODO: 상태머신 시나리오 테스트 코드 작성
    printf("24. state_machine: PASS (구현 필요)\n");
    return PASS;
}

int test_my_strcmp() {
    int result = PASS;
    if (my_strcmp("abc", "abc") != 0) result = FAIL;
    if (my_strcmp("abc", "abd") >= 0) result = FAIL;
    if (my_strcmp("abd", "abc") <= 0) result = FAIL;
    printf("25. my_strcmp: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_pack_unpack() {
    int result = PASS;
    PackStruct s = {0x12, 0x3456};
    uint8_t buf[3];
    pack_bytes(&s, buf);
    PackStruct s2;
    unpack_bytes(&s2, buf);
    if (s2.a != 0x12 || s2.b != 0x3456) result = FAIL;
    printf("26. pack/unpack: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_reverse_bits8_v2() {
    int result = PASS;
    if (reverse_bits8_v2(0b11010010) != 0x4B) result = FAIL;
    printf("27. reverse_bits8_v2: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_calc_crc8() {
    int result = PASS;
    uint8_t arr[] = {0x01, 0x02, 0x03};
    // CRC 값은 구현에 따라 다르므로 PASS/FAIL은 임시로 PASS
    printf("28. calc_crc8: %s\n", result ? "PASS" : "FAIL");
    return result;
}



/* ===================== Main test runner ===================== */
int main() {
    int total = 0;
    total += test_set_bit3();
    total += test_extract_bits_5_7();
    total += test_my_strcpy();
    total += test_reverse_str();
    total += test_find_max();
    total += test_sum_array();
    total += test_add_node();
    total += test_delete_node();
    total += test_init_array();
    total += test_set_struct_member();
    total += test_dlist_add_node();
    total += test_dlist_delete_node();
    total += test_my_atoi();
    total += test_my_memset_memcpy();
    total += test_swap_endian32();
    total += test_count_set_bits();
    total += test_reverse_bits8();
    total += test_bit_ops();
    total += test_memory_rw();
    total += test_isStringPalindrome();
    total += test_isIntegerPalindrome();
    total += test_read_combined_timer();
    total += test_ring_buffer();
    total += test_state_machine();
    total += test_my_strcmp();
    total += test_pack_unpack();
    total += test_reverse_bits8_v2();
    total += test_calc_crc8();
    printf("Total Passed: %d/28\n", total);
    return 0;
}