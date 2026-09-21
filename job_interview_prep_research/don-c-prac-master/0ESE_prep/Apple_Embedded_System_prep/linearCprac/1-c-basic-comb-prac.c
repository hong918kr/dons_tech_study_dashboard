#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// ===================== 샘플 문제 =====================

// 샘플 문제 1
// 32비트 레지스터 값에서 특정 비트 범위(start_bit부터 end_bit까지)를 새로운 값으로 설정
// 예: set_bit_range(0xAABBCCDD, 8, 15, 0xEF) → 0xAABBEFDD
uint32_t set_bit_range(uint32_t reg_val, uint8_t start_bit, uint8_t end_bit, uint32_t new_val) {
    // TODO: 구현

    uint32_t mask = ( (1U << (end_bit - start_bit + 1)) - 1 ) << start_bit;
    reg_val = reg_val & ~(mask);
    reg_val = reg_val | ((new_val << start_bit) & mask);
    return reg_val;
}
int test_set_bit_range() {
    int result = 1;
    if (set_bit_range(0xAABBCCDD, 8, 15, 0xEF) != 0xAABBEFDD) result = 0;
    printf("샘플1 set_bit_range: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 샘플 문제 2
// 정수 n이 2의 거듭제곱인지 확인 (비트 연산만 사용)
bool is_power_of_two(int n) {
    // TODO: 구현
    

    return false;
}
int test_is_power_of_two() {
    int result = 1;
    if (!is_power_of_two(8)) result = 0;
    if (is_power_of_two(0)) result = 0;
    if (is_power_of_two(7)) result = 0;
    printf("샘플2 is_power_of_two: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 샘플 문제 3
// 두 32비트 정수의 최소 비트 플립(bit flip) 횟수 계산
int min_bit_flips(uint32_t start, uint32_t goal) {
    // TODO: 구현
    return 0;
}
int test_min_bit_flips() {
    int result = 1;
    if (min_bit_flips(0b1010, 0b0110) != 1) result = 0;
    if (min_bit_flips(0xFFFFFFFF, 0x0) != 32) result = 0;
    printf("샘플3 min_bit_flips: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// ===================== 추가 문제 10선 =====================

// 1. 1의 개수 세기 (Brian Kernighan's 알고리즘)
int count_set_bits(unsigned int n) {
    // TODO: 구현
    return 0;
}
int test_count_set_bits() {
    int result = 1;
    if (count_set_bits(0xF0F0F0F0) != 16) result = 0;
    if (count_set_bits(0xFFFFFFFF) != 32) result = 0;
    printf("추가1 count_set_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 2. 임시 변수 없이 두 숫자 교환하기 (XOR)
void swap_numbers(int *a, int *b) {
    // TODO: 구현
}
int test_swap_numbers() {
    int result = 1;
    int x = 5, y = 10;
    swap_numbers(&x, &y);
    if (x != 10 || y != 5) result = 0;
    printf("추가2 swap_numbers: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 3. 두 정수의 부호 판별하기 (비트 연산만)
bool have_opposite_signs(int x, int y) {
    // TODO: 구현
    return false;
}
int test_have_opposite_signs() {
    int result = 1;
    if (!have_opposite_signs(-1, 2)) result = 0;
    if (have_opposite_signs(3, 4)) result = 0;
    printf("추가3 have_opposite_signs: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 4. 가장 오른쪽의 1비트 끄기
int turn_off_rightmost_set_bit(int n) {
    // TODO: 구현
    return 0;
}
int test_turn_off_rightmost_set_bit() {
    int result = 1;
    if (turn_off_rightmost_set_bit(0b10110) != 0b10100) result = 0;
    printf("추가4 turn_off_rightmost_set_bit: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 5. 유일한 1비트의 위치 찾기 (2의 거듭제곱만 입력)
int find_position_of_set_bit(unsigned int n) {
    // TODO: 구현
    return 0;
}
int test_find_position_of_set_bit() {
    int result = 1;
    if (find_position_of_set_bit(0x10) != 4) result = 0;
    if (find_position_of_set_bit(0x80000000) != 31) result = 0;
    printf("추가5 find_position_of_set_bit: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 6. 특정 위치(k번째) 비트 확인하기
bool is_kth_bit_set(unsigned int n, int k) {
    // TODO: 구현
    return false;
}
int test_is_kth_bit_set() {
    int result = 1;
    if (!is_kth_bit_set(0b1000, 3)) result = 0;
    if (is_kth_bit_set(0b1000, 2)) result = 0;
    printf("추가6 is_kth_bit_set: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 7. 비트 회전시키기 (왼쪽)
uint32_t rotate_left(uint32_t n, unsigned int d) {
    // TODO: 구현
    return 0;
}
int test_rotate_left() {
    int result = 1;
    if (rotate_left(0x80000000, 1) != 0x1) result = 0;
    if (rotate_left(0x1, 4) != 0x10) result = 0;
    printf("추가7 rotate_left: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 8. 홀수 번 나타나는 숫자 찾기 (XOR)
int find_odd_occurrence(const int* arr, int size) {
    // TODO: 구현
    return 0;
}
int test_find_odd_occurrence() {
    int arr[] = {1, 2, 3, 2, 3, 1, 3};
    int result = 1;
    if (find_odd_occurrence(arr, 7) != 3) result = 0;
    printf("추가8 find_odd_occurrence: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 9. 숫자의 특정 비트들 교환하기
unsigned int swap_bits(unsigned int n, unsigned int p1, unsigned int p2) {
    // TODO: 구현
    return 0;
}
int test_swap_bits() {
    int result = 1;
    if (swap_bits(0b1011, 0, 1) != 0b1010) result = 0;
    printf("추가9 swap_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// 10. 희소 수(Sparse Number) 판별하기
bool is_sparse(int n) {
    // TODO: 구현
    return false;
}
int test_is_sparse() {
    int result = 1;
    if (!is_sparse(5)) result = 0;    // 0101
    if (is_sparse(6)) result = 0;     // 0110
    printf("추가10 is_sparse: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// ===================== main 함수 =====================
int main() {
    int total = 0;
    total += test_set_bit_range();
    total += test_is_power_of_two();
    total += test_min_bit_flips();
    total += test_count_set_bits();
    total += test_swap_numbers();
    total += test_have_opposite_signs();
    total += test_turn_off_rightmost_set_bit();
    total += test_find_position_of_set_bit();
    total += test_is_kth_bit_set();
    total += test_rotate_left();
    total += test_find_odd_occurrence();
    total += test_swap_bits();
    total += test_is_sparse();
    printf("Total Passed: %d/13\n", total);
    return 0;
}