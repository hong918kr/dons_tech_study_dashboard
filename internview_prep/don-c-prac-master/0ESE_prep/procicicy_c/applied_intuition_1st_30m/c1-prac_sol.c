/*
Applied Intuition 코딩 챌린지 대비 50제

카테고리 1: 비트 연산 (Bit Manipulation)
1. reg 변수의 pos 위치 비트를 1로 설정하는 SET_BIT 매크로를 작성하세요.
   Follow-up: CLEAR_BIT, TOGGLE_BIT, CHECK_BIT 매크로도 작성하세요.
2. 32비트 부호 없는 정수(uint32_t)에서 1로 설정된 비트의 개수를 세는 함수를 작성하세요 (해밍 가중치).
   Follow-up: 더 효율적인 계산 방법은? (Brian Kernighan 알고리즘)
3. 주어진 정수가 2의 거듭제곱인지 확인하는 함수 bool is_power_of_two(int n)를 작성하세요.
   Follow-up: 비트 연산만으로 구현 (n && !(n & (n - 1)))
4. 32비트 정수의 엔디언(Endianness)을 변환하는 함수 uint32_t swap_endian(uint32_t val)를 작성하세요.
   Follow-up: 시스템 엔디언 확인 코드 작성
5. 부호 없는 8비트 정수(uint8_t)의 비트 순서를 뒤집는 함수 uint8_t reverse_bits(uint8_t num)를 작성하세요.
   Follow-up: 32비트 정수로 확장
6. 두 정수를 임시 변수 없이 XOR로 교환(swap)하는 코드를 작성하세요.
   Follow-up: 이 방법의 장단점은?
7. 정수의 특정 비트가 1인지 0인지 확인하는 함수를 작성하세요.
   Follow-up: 특정 범위의 비트 추출 함수 작성
8. 정수의 홀수 번째 비트들만 1로 설정하는 함수를 작성하세요.
   Follow-up: 짝수 번째 비트들을 모두 0으로 만드는 함수
9. 두 정수 a와 b를 더할 때, 덧셈 연산자(+) 없이 비트 연산만으로 구현하세요.
   Follow-up: 이 방법의 성능상 이점/한계는?
10. 주어진 숫자의 패리티(parity)가 짝수인지 홀수인지 확인하는 함수를 작성하세요.
    Follow-up: 큰 데이터 스트림의 패리티 효율적 계산법
11. (1 << n)으로 n번째 비트 마스크 만드는 코드
    Follow-up: n번째 비트만 0이고 나머지는 1인 마스크 (~(1 << n))
12. x & (-x) 비트 트릭 설명 및 예시
    Follow-up: LSB 분리, 활용 예시
13. 레지스터의 특정 필드(3-5번 비트)를 주어진 값으로 업데이트하는 함수
    Follow-up: 원자적(atomic) 수행 이유
14. uint32_t 변수에서 상위 16비트와 하위 16비트 교환 함수
    Follow-up: 데이터 압축/암호화에서의 활용
15. 숫자를 2로 나누거나 곱하는 연산을 시프트 연산자로 구현
    Follow-up: 음수의 오른쪽 시프트 동작(산술/논리)


*/

/*
카테고리 1: 비트 연산 (Bit Manipulation)
각 문제는 함수 시그니처, 설명, 예제 변수, 테스트 코드(PASS/FAIL), 그리고 follow-up 설명을 포함합니다.
*/

#include <stdio.h>
#include <stdint.h>

// 1. SET_BIT, CLEAR_BIT, TOGGLE_BIT, CHECK_BIT 매크로
// 설명: reg 변수의 pos 위치 비트를 1로 설정/해제/반전/확인하는 매크로입니다.
// Follow-up: CLEAR_BIT는 해당 비트를 0으로, TOGGLE_BIT는 반전, CHECK_BIT는 해당 비트가 1인지 확인합니다.
#define SET_BIT(reg, pos)    ((reg) |= (1U << (pos)))
#define CLEAR_BIT(reg, pos)  ((reg) &= ~(1U << (pos)))
#define TOGGLE_BIT(reg, pos) ((reg) ^= (1U << (pos)))
#define CHECK_BIT(reg, pos)  (((reg) >> (pos)) & 1U)
// 예시: reg=0x00, SET_BIT(reg, 3) → reg=0x08

// 2. 32비트 정수의 1 비트 개수 세기 (해밍 가중치)
// 설명: 32비트 부호 없는 정수에서 1로 설정된 비트의 개수를 셉니다.
// Follow-up: Brian Kernighan 알고리즘을 사용하면 더 효율적으로 1의 개수를 셀 수 있습니다.
int count_set_bits(uint32_t n) {
    int count = 0;
    while (n) {
        count += n & 1U;
        n >>= 1;
    }
    return count;
}
// Brian Kernighan 알고리즘 (더 빠름):
int count_set_bits_fast(uint32_t n) {
    int count = 0;
    while (n) {
        n &= (n - 1);
        count++;
    }
    return count;
}
// 예시: 0xF0F0F0F0 → 16

// 3. 2의 거듭제곱 판별
// 설명: 주어진 정수가 2의 거듭제곱인지 확인합니다.
// Follow-up: n && !(n & (n - 1)) 비트 연산만으로 구현합니다.
int is_power_of_two(uint32_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}
// 예시: 16 → 1, 18 → 0

// 4. 32비트 엔디언 변환
// 설명: 32비트 정수의 엔디언(Endianness)을 변환합니다.
// Follow-up: 시스템 엔디언을 확인하려면 union이나 포인터 캐스팅을 사용할 수 있습니다.
uint32_t swap_endian(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}
// 예시: 0x12345678 → 0x78563412

// 5. 8비트 비트 순서 뒤집기
// 설명: 8비트 정수의 비트 순서를 뒤집습니다.
// Follow-up: 32비트 정수로 확장할 때 동일한 원리를 적용하면 됩니다.
uint8_t reverse_bits(uint8_t num) {
    uint8_t res = 0;
    for (int i = 0; i < 8; ++i) {
        res <<= 1;
        res |= (num & 1);
        num >>= 1;
    }
    return res;
}
// 예시: 0b11010000 → 0b00001011

// 6. XOR로 swap
// 설명: 임시 변수 없이 XOR 연산만으로 두 정수의 값을 교환합니다.
// Follow-up: 같은 주소를 넘기면 0이 되므로, a==b일 때는 swap하지 않아야 합니다.
void xor_swap(int *a, int *b) {
    if (a == b) return;
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}
// 예시: a=5, b=7 → a=7, b=5

// 7. 특정 비트 확인 함수
// 설명: 정수 n의 pos번째 비트가 1인지 0인지 확인합니다.
// Follow-up: 특정 범위의 비트 추출은 (n >> start) & ((1U << width) - 1)로 구현할 수 있습니다.
int is_bit_set(uint32_t n, int pos) {
    return (n >> pos) & 1U;
}
// 예시: n=0x10, pos=4 → 1

// 8. 홀수 번째 비트만 1로 설정
// 설명: 정수의 홀수(1,3,5...)번째 비트만 1로 만듭니다.
// Follow-up: 짝수 번째 비트만 0으로 만드는 함수는 n & 0xAAAAAAAA로 구현할 수 있습니다.
uint32_t set_odd_bits(uint32_t n) {
    uint32_t mask = 0xAAAAAAAA; // 1010...
    return n | mask;
}
// 예시: n=0x0 → 0xAAAAAAAA

// 9. 덧셈 없이 두 정수 더하기
// 설명: + 연산자 없이 비트 연산만으로 두 정수를 더합니다.
// Follow-up: 이 방법은 반복문이 많아 느릴 수 있습니다.
int add_no_plus(int a, int b) {
    while (b) {
        int carry = a & b;
        a = a ^ b;
        b = carry << 1;
    }
    return a;
}
// 예시: 13, 29 → 42

// 10. 패리티(홀짝) 확인
// 설명: 주어진 숫자의 1 비트 개수가 짝수면 1, 홀수면 0을 반환합니다.
// Follow-up: 큰 데이터 스트림의 패리티는 XOR 누적으로 빠르게 계산할 수 있습니다.
int parity_even(uint32_t n) {
    int p = 0;
    while (n) {
        p ^= (n & 1);
        n >>= 1;
    }
    return p == 0; // 1: even, 0: odd
}
// 예시: 0b1010 → 1, 0b1011 → 0

// ------------------- 테스트 코드 -------------------
#define TEST_LABEL_WIDTH 45
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", TEST_LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 1. 비트 매크로 테스트
    uint32_t reg = 0x00;
    SET_BIT(reg, 3);
    test_result("1. SET_BIT", reg == 0x08);
    CLEAR_BIT(reg, 3);
    test_result("1. CLEAR_BIT", reg == 0x00);
    reg = 0x01;
    TOGGLE_BIT(reg, 0);
    test_result("1. TOGGLE_BIT", reg == 0x00);
    reg = 0x10;
    test_result("1. CHECK_BIT", CHECK_BIT(reg, 4) == 1);

    // 2. 1 비트 개수 세기
    test_result("2. count_set_bits", count_set_bits(0xF0F0F0F0) == 16);
    test_result("2. count_set_bits_fast", count_set_bits_fast(0xF0F0F0F0) == 16);

    // 3. 2의 거듭제곱 판별
    test_result("3. is_power_of_two(16)", is_power_of_two(16) == 1);
    test_result("3. is_power_of_two(18)", is_power_of_two(18) == 0);

    // 4. 엔디언 변환
    test_result("4. swap_endian", swap_endian(0x12345678) == 0x78563412);

    // 5. 비트 순서 뒤집기
    test_result("5. reverse_bits", reverse_bits(0b11010000) == 0b00001011);

    // 6. XOR swap
    int a = 5, b = 7;
    xor_swap(&a, &b);
    test_result("6. xor_swap", a == 7 && b == 5);

    // 7. 특정 비트 확인
    test_result("7. is_bit_set", is_bit_set(0x10, 4) == 1);

    // 8. 홀수 비트만 1로 설정
    test_result("8. set_odd_bits", set_odd_bits(0x0) == 0xAAAAAAAA);

    // 9. 덧셈 없이 더하기
    test_result("9. add_no_plus", add_no_plus(13, 29) == 42);

    // 10. 패리티(짝수)
    test_result("10. parity_even", parity_even(0b1010) == 1);
    test_result("10. parity_even(odd)", parity_even(0b1011) == 0);

    // 7. follow-up: 특정 범위의 비트 추출
    uint32_t n = 0b11011100;
    int start = 2, width = 3;
    int extracted = (n >> start) & ((1U << width) - 1); // 0b111 = 7
    test_result("7. follow-up: bit range extract", extracted == 7);

    // 8. follow-up: 짝수 비트만 0으로
    test_result("8. follow-up: even bits zero", (0xFFFFFFFF & 0xAAAAAAAA) == 0xAAAAAAAA);

    // 11. n번째 비트 마스크, n번째만 0
    int nbit = 5;
    uint32_t mask = (1U << nbit);
    uint32_t inv_mask = ~(1U << nbit);
    test_result("11. bit mask", mask == 0x20);
    test_result("11. inv bit mask", inv_mask == 0xFFFFFFDF);

    // 12. x & (-x) LSB 분리
    uint32_t x = 0b1011000;
    test_result("12. x & -x (LSB)", (x & -x) == 0b1000);

    // 14. 상위/하위 16비트 교환
    uint32_t y = 0x12345678;
    uint32_t swapped = ((y >> 16) & 0xFFFF) | ((y & 0xFFFF) << 16);
    test_result("14. swap upper/lower 16bit", swapped == 0x56781234);

    // 15. 시프트로 곱셈/나눗셈
    int val = 8;
    test_result("15. shift left (x2)", (val << 1) == 16);
    test_result("15. shift right (/2)", (val >> 1) == 4);

    return 0;
}