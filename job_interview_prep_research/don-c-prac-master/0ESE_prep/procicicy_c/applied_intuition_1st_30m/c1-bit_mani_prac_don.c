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

#include <stdio.h>
#include <stdint.h>

// 1. SET_BIT, CLEAR_BIT, TOGGLE_BIT, CHECK_BIT 매크로
#define SET_BIT(reg, pos)       ( (reg) |= (1U << (pos)) )
#define CLEAR_BIT(reg, pos)     ( (reg) &~ (1U << (pos)) )
#define TOGGLE_BIT(reg, pos)    ( (reg) ^= (1U << (pos)) )
#define CHECK_BIT(reg, pos)     ( (reg) & (1U <<  (pos)) )

// 2. 32비트 정수의 1 비트 개수 세기 (해밍 가중치)
/*

*/
int count_set_bits(uint32_t n) {
    int count = 0;
    while (n)
    {
        count = count + (n & 1U);
        n = n >> 1;
    }
    return count;
}
// Brian Kernighan 알고리즘 (더 빠름):
int count_set_bits_fast(uint32_t n)
{
    int count = 0;
    while (n)
    {
        n = n & (n - 1);
        count++;
    }
    return count;
}
/*
2번 부연설명,

예시: n = 0b10101000 (10진수 168, 1이 3개)

1. 첫 번째 반복
    n = 0b10101000
    n - 1 = 0b10100111
    n & (n - 1) = 0b10100000 (맨 오른쪽 1비트가 0으로 바뀜)
    count = 1

2. 두 번째 반복
    n = 0b10100000
    n - 1 = 0b10011111
    n & (n - 1) = 0b10000000 (맨 오른쪽 1비트가 0으로 바뀜)
    count = 2

3. 세 번째 반복
    n = 0b10000000
    n - 1 = 0b01111111
    n & (n - 1) = 0b00000000 (마지막 1비트가 0으로 바뀜)
    count = 3

4. 종료
    n = 0, 반복 끝
*/



// 3. 2의 거듭제곱 판별
int is_power_of_two(uint32_t n)
{    
    return n != 0 && (n & (n - 1)) == 0;
}

// 4. 32비트 엔디언 변환
/*
엔디언(Endian)은 여러 바이트로 이루어진 데이터(예: 32비트 정수)를 메모리에 저장할 때 바이트의 순서를 의미합니다.

1. 리틀 엔디언(Little Endian)
    가장 작은 바이트(LSB)가 가장 낮은 주소에 저장
    예) 0x12345678을 메모리에 저장하면:

    주소:   +0   +1   +2   +3
    값:   0x78 0x56 0x34 0x12

2. 빅 엔디언(Big Endian)
    가장 큰 바이트(MSB)가 가장 낮은 주소에 저장
    예) 0x12345678을 메모리에 저장하면:

    주소:   +0   +1   +2   +3
    값:   0x12 0x34 0x56 0x78

swap_endian 함수란?
    리틀 엔디언 ↔ 빅 엔디언 변환을 의미합니다.

즉, 0x12345678을 0x78563412로 바꿔주는 함수입니다.

*/

uint32_t swap_endian(uint32_t val)
{
    uint32_t b0 = (val & 0x000000FF) << 24;
    uint32_t b1 = (val & 0x0000FF00) << 8;
    uint32_t b2 = (val & 0x00FF0000) >> 8;
    uint32_t b3 = (val & 0xFF000000) >> 24;
    
    return b0 | b1 | b2 | b3;
}

// 5. 8비트 비트 순서 뒤집기
uint8_t reverse_bits(uint8_t num)
{
    uint8_t res = 0;
    for (int i = 0; i < 8; ++i)
    {
        res <<= 1;
        res |= (num & 1);
        num >>= 1;
    }
    return res;
}

// 6. XOR로 swap
void xor_swap(int *a, int *b) {
    if (a == b) return;
    *a ^= *b;
    *b ^= *a;
    *a ^= *b; 
    // if a and b are same address, don't use this func to swap since it becomes 0
}

// 7. 특정 비트 확인 함수
int is_bit_set(uint32_t n, int pos)
{
    return (n >> pos) & 1U;
}
/*
1U에서 U는 unsigned(부호 없는) 정수임을 명시하는 접미사입니다.

이유
    1은 기본적으로 int(signed) 타입입니다.
    비트 연산에서 1U << n처럼 사용하면,
    왼쪽 피연산자가 unsigned이므로 결과도 unsigned가 되어
    예상치 못한 부호 확장, 오버플로우, 경고 등을 방지할 수 있습니다.

예시

    uint32_t mask = 1U << 31; // OK, 0x80000000 (unsigned)
    uint32_t mask2 = 1 << 31; // int에서 31비트 시프트, 구현에 따라 음수로 해석될 수 있음

결론
    비트 연산, 마스킹, 시프트에서 안전하게 unsigned 연산을 하려면 U를 붙이는 것이 좋습니다.
    특히 32비트 이상 시프트, 마스킹에서 버그 예방에 도움이 됩니다.
*/


// 8. 홀수 번째 비트만 1로 설정
uint32_t set_odd_bits(uint32_t n)
{
    uint32_t mask = 0xAAAAAAAA;
    return n | mask;
}
/*
결론
    n | mask : n의 홀수 비트는 무조건 1, 짝수 비트는 n에 따라
    n & mask : n의 홀수 비트만 남기고 나머지는 0
    mask : 홀수 비트만 1, 나머지는 0

문제 의도가 "홀수 비트만 1로 만드는 것"이면 return 0xAAAAAAAA;

    "n의 홀수 비트만 남기는 것"이면 return n & 0xAAAAAAAA;
    "n의 홀수 비트만 1로 설정"이면 return n | 0xAAAAAAAA; (현재 코드)
*/


// 9. 덧셈 없이 두 정수 더하기
int add_no_plus(int a, int b)
{
    while (b)
    {
        int carry = a & b;
        a = a ^ b;
        b = carry << 1;
    }
    return a;
}
/*
코드 설명: 

9번 문제는 덧셈(+) 연산자 없이 두 정수를 더하는 함수를 구현하는 것입니다.

원리
    이 방법은 **비트 연산(XOR, AND, 시프트)**만을 사용해서 덧셈을 흉내냅니다.

1. XOR 연산 (a ^ b)
    각 자리의 **합(올림수 없는 자리수 덧셈)**을 구합니다.
    예: 1 + 1 = 0 (올림 발생), 1 + 0 = 1, 0 + 1 = 1, 0 + 0 = 0

2. AND 연산 후 시프트 ((a & b) << 1)
    **올림수(carry)**를 구합니다.
    두 비트가 모두 1인 자리에서만 올림이 발생하므로, AND 연산 후 한 칸 왼쪽으로 이동시킵니다.

3. 반복
    올림수가 0이 될 때까지 위 과정을 반복합니다.



예시
1. 
    a = 5 (0b0101), b = 3 (0b0011)
    sum = a ^ b = 0b0110 (6)
    carry = (a & b) << 1 = 0b0010 (2)

2. 
    a = 6, b = 2
    sum = 0b0110 ^ 0b0010 = 0b0100 (4)
    carry = (0b0110 & 0b0010) << 1 = 0b0010 << 1 = 0b0100 (4)
3. 
    a = 4, b = 4
    sum = 0b0100 ^ 0b0100 = 0b0000 (0)
    carry = (0b0100 & 0b0100) << 1 = 0b0100 << 1 = 0b1000 (8)

4.     
    a = 0, b = 8
    sum = 0b0000 ^ 0b1000 = 0b1000 (8)
    carry = (0b0000 & 0b1000) << 1 = 0

5. 
    a = 8, b = 0 → 종료, 결과는 8


*/


// 10. 패리티(홀짝) 확인
int parity_even(uint32_t n) {}

// 11. n번째 비트 마스크, n번째만 0
uint32_t bit_mask(int n) {}
uint32_t bit_mask_inv(int n) {}

// 12. x & -x (LSB 분리)
uint32_t lsb_only(uint32_t x) {}

// 13. 특정 필드 업데이트
uint32_t update_field(uint32_t reg, uint32_t value) {}

// 14. 상위/하위 16비트 교환
uint32_t swap_16(uint32_t x) {}

// 15. 시프트 곱셈/나눗셈
int mul2(int x) {}
int div2(int x) {}


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