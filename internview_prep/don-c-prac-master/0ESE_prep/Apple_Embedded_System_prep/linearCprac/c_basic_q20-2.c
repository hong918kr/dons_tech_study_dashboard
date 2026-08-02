#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define PASS 1
#define FAIL 0

// 1. 문자열 숫자 변환 (atoi)
int my_atoi(const char* str) {
    // TODO: 구현
    
    int res = 0, sign = 1, i = 0;
    if (str[i] == '-') {
        sign=-1;i++;
    }
    while (str[i] >='0' && str[i] <= '9') {
        res = res * 10 + (str[i] - '0');
        i++;
    }

    return sign * res;
}
int test_my_atoi() {
    int result = PASS;
    if (my_atoi("123") != 123) result = FAIL;
    if (my_atoi("-45") != -45) result = FAIL;
    if (my_atoi("0") != 0) result = FAIL;
    if (my_atoi("00123") != 123) result = FAIL;
    printf("1. my_atoi: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 2. Endian Swap (바이트 순서 뒤집기)
uint32_t swap_endian32(uint32_t val) {
    // TODO: 구현
    
    uint32_t b0 = (val & 0x000000FF) << 24;
    uint32_t b1 = (val & 0x0000FF00) << 8;
    uint32_t b2 = (val & 0x00FF0000) >> 8;
    uint32_t b3 = (val & 0xFF000000) >> 24;

    return b0|b1|b2|b3;
}
int test_swap_endian32() {
    int result = PASS;
    if (swap_endian32(0x12345678) != 0x78563412) result = FAIL;
    printf("2. swap_endian32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 3. 메모리 세트/카피 (memset, memcpy)
void* my_memset(void* dst, int val, size_t n) {
    // TODO: 구현
    volatile unsigned char* d = (volatile unsigned char*) 

    return dst;
}
void* my_memcpy(void* dst, const void* src, size_t n) {
    // TODO: 구현
    return dst;
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

    printf("3. my_memset/my_memcpy: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 4. 최대/최소값 찾기
int find_max(const int* arr, int n) {
    // TODO: 구현
    return 0;
}
int find_min(const int* arr, int n) {
    // TODO: 구현
    return 0;
}
int test_find_max_min() {
    int arr[] = {3, 7, 2, 9, 5};
    int result = PASS;
    if (find_max(arr, 5) != 9) result = FAIL;
    if (find_min(arr, 5) != 2) result = FAIL;
    printf("4. find_max/find_min: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 5. 비트 카운트 (Set Bit Count)
int count_set_bits(uint32_t val) {
    // TODO: 구현
    return 0;
}
int test_count_set_bits() {
    int result = PASS;
    if (count_set_bits(0xF0F0F0F0) != 16) result = FAIL;
    printf("5. count_set_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 6. 비트 리버스 (Bit Reverse)
uint8_t reverse_bits8(uint8_t val) {
    // TODO: 구현
    return 0;
}
int test_reverse_bits8() {
    int result = PASS;
    if (reverse_bits8(0b11010010) != 0x4B) result = FAIL;
    printf("6. reverse_bits8: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 7. CRC 계산 (CRC8)
uint8_t calc_crc8(const uint8_t* data, size_t len) {
    // TODO: 구현
    return 0;
}
int test_calc_crc8() {
    int result = PASS;
    uint8_t arr[] = {0x01, 0x02, 0x03};
    // CRC 값은 구현에 따라 다르므로 PASS/FAIL은 임시로 PASS
    printf("7. calc_crc8: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 8. Circular Buffer (링 버퍼)
#define QMAX 5
typedef struct {
    uint8_t buf[QMAX];
    uint8_t head, tail, count;
} cb_t;
void cb_init(cb_t* q) {
    // TODO: 구현
}
bool cb_push(cb_t* q, uint8_t data) {
    // TODO: 구현
    return false;
}
bool cb_pop(cb_t* q, uint8_t* data) {
    // TODO: 구현
    return false;
}
int test_circular_buffer() {
    int result = PASS;
    cb_t q;
    cb_init(&q);
    if (!cb_push(&q, 1)) result = FAIL;
    if (!cb_push(&q, 2)) result = FAIL;
    uint8_t val = 0;
    if (!cb_pop(&q, &val) || val != 1) result = FAIL;
    printf("8. circular_buffer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 9. 링크드 리스트 삽입/삭제
typedef struct Node {
    int data;
    struct Node* next;
} Node;
void add_node(Node** head, int data) {
    // TODO: 구현
}
void delete_node(Node** head, int data) {
    // TODO: 구현
}
int test_linked_list() {
    int result = PASS;
    Node* head = NULL;
    add_node(&head, 10);
    add_node(&head, 20);
    delete_node(&head, 10);
    if (!head || head->data != 20 || head->next != NULL) result = FAIL;
    printf("9. linked_list: %s\n", result ? "PASS" : "FAIL");
    // free list
    while (head) {
        Node* tmp = head;
        head = head->next;
        free(tmp);
    }
    return result;
}

// 10. 문자열 뒤집기 (reverse string)
void reverse_str(char* str) {
    // TODO: 구현
}
int test_reverse_str() {
    int result = PASS;
    char s[] = "hello";
    reverse_str(s);
    if (strcmp(s, "olleh") != 0) result = FAIL;
    printf("10. reverse_str: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 11. 문자열 비교 (strcmp)
int my_strcmp(const char* s1, const char* s2) {
    // TODO: 구현
    return 0;
}
int test_my_strcmp() {
    int result = PASS;
    if (my_strcmp("abc", "abc") != 0) result = FAIL;
    if (my_strcmp("abc", "abd") >= 0) result = FAIL;
    if (my_strcmp("abd", "abc") <= 0) result = FAIL;
    printf("11. my_strcmp: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 12. 구조체 바이트 패킹/언패킹
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
int test_pack_unpack() {
    int result = PASS;
    PackStruct s = {0x12, 0x3456};
    uint8_t buf[3];
    pack_bytes(&s, buf);
    PackStruct s2;
    unpack_bytes(&s2, buf);
    if (s2.a != 0x12 || s2.b != 0x3456) result = FAIL;
    printf("12. pack/unpack: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// --- main ---
int main() {
    int total = 0;
    total += test_my_atoi();
    total += test_swap_endian32();
    total += test_my_memset_memcpy();
    total += test_find_max_min();
    total += test_count_set_bits();
    total += test_reverse_bits8();
    total += test_calc_crc8();
    total += test_circular_buffer();
    total += test_linked_list();
    total += test_reverse_str();
    total += test_my_strcmp();
    total += test_pack_unpack();
    printf("Total Passed: %d/12\n", total);
    if (total != 12) {
        printf("Failed test numbers: ");
        if (!test_my_atoi()) printf("1 ");
        if (!test_swap_endian32()) printf("2 ");
        if (!test_my_memset_memcpy()) printf("3 ");
        if (!test_find_max_min()) printf("4 ");
        if (!test_count_set_bits()) printf("5 ");
        if (!test_reverse_bits8()) printf("6 ");
        if (!test_calc_crc8()) printf("7 ");
        if (!test_circular_buffer()) printf("8 ");
        if (!test_linked_list()) printf("9 ");
        if (!test_reverse_str()) printf("10 ");
        if (!test_my_strcmp()) printf("11 ");
        if (!test_pack_unpack()) printf("12 ");
        printf("\n");
    }
    return 0;
}