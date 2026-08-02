#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
1. 특정 비트 설정(Set) 함수/매크로 구현
   Input: set_bit(0x00, 3)
   Output: 0x8
*/
uint32_t set_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
}

/*
2. 특정 비트 클리어(Clear) 함수/매크로 구현
   Input: clear_bit(0xFF, 4)
   Output: 0xEF
*/
uint32_t clear_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
}

/*
3. 특정 비트 토글(Toggle) 함수/매크로 구현
   Input: toggle_bit(0x10, 4)
   Output: 0x0
*/
uint32_t toggle_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
}

/*
4. 특정 비트가 설정되었는지 확인하는 함수/매크로 구현
   Input: is_bit_set(0x10, 4)
   Output: 1
*/
bool is_bit_set(uint32_t value, uint8_t bit) {
    // TODO: implement
}

/*
5. 상위/하위 니블(nibble) 스왑 구현
   Input: swap_nibbles(0xAB)
   Output: 0xBA
*/
uint8_t swap_nibbles(uint8_t value) {
    // TODO: implement
}

/*
6. 주어진 숫자의 패리티(parity) 계산 (짝수/홀수)
   Input: parity_even(0x5)
   Output: 1
*/
bool parity_even(uint32_t value) {
    // TODO: implement
}

/*
7. 정수에서 가장 낮은 설정 비트(LSB) 클리어
   Input: clear_lsb(0x18)
   Output: 0x10
*/
uint32_t clear_lsb(uint32_t value) {
    // TODO: implement
}

/*
8. 정수에서 가장 오른쪽의 1 찾기
   Input: rightmost_set_bit(0x18)
   Output: 0x8
*/
uint32_t rightmost_set_bit(uint32_t value) {
    // TODO: implement
}

/*
9. 두 정수의 부호가 다른지 비트 연산으로 확인
   Input: is_opposite_sign(-5, 7)
   Output: 1
*/
bool is_opposite_sign(int32_t a, int32_t b) {
    // TODO: implement
}

/*
10. 정수의 절대값 계산 (분기문 없이)
    Input: abs_no_branch(-123)
    Output: 123
*/
int32_t abs_no_branch(int32_t value) {
    // TODO: implement
}

// --- Test code ---
#define LABEL_WIDTH 28

void test_result(const char* label, uint32_t result, uint32_t expected) {
    printf("%-*s = 0x%X", LABEL_WIDTH, label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected 0x%X)\n", expected);
    }
}
void test_result_int(const char* label, int32_t result, int32_t expected) {
    printf("%-*s = %d", LABEL_WIDTH, label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected %d)\n", expected);
    }
}
void test_result_bool(const char* label, bool result, bool expected) {
    printf("%-*s = %d", LABEL_WIDTH, label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected %d)\n", expected);
    }
}

int main(void) {
    test_result("1. set_bit(0x00, 3)", set_bit(0x00, 3), 0x8);
    test_result("2. clear_bit(0xFF, 4)", clear_bit(0xFF, 4), 0xEF);
    test_result("3. toggle_bit(0x10, 4)", toggle_bit(0x10, 4), 0x0);
    test_result_bool("4. is_bit_set(0x10, 4)", is_bit_set(0x10, 4), 1);
    test_result("5. swap_nibbles(0xAB)", swap_nibbles(0xAB), 0xBA);    
    test_result_bool("6. parity_even(0x5)", parity_even(0x5), 1);
    test_result("7. clear_lsb(0x18)", clear_lsb(0x18), 0x10);
    test_result("8. rightmost_set_bit(0x18)", rightmost_set_bit(0x18), 0x8);
    test_result_bool("9. is_opposite_sign(-5, 7)", is_opposite_sign(-5, 7), 1);
    test_result_int("10. abs_no_branch(-123)", abs_no_branch(-123), 123);

    return 0;
}