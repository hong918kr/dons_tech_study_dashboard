#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
/*
1. 특정 비트 설정(Set) 함수/매크로 구현
   Implement a function/macro to set a specific bit.
   Input: set_bit(0x00, 3)
   Output: 0x8
*/
uint32_t set_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    value |= (1U<<bit);
    return value;
}

/*
2. 특정 비트 클리어(Clear) 함수/매크로 구현
   Implement a function/macro to clear a specific bit.
   Input: clear_bit(0xFF, 4)
   Output: 0xEF
*/
uint32_t clear_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    value &= ~(1U << bit);
    return value;
}

/*
3. 특정 비트 토글(Toggle) 함수/매크로 구현
   Implement a function/macro to toggle a specific bit.
   Input: toggle_bit(0x10, 4)
   Output: 0x0
*/
uint32_t toggle_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    value ^= (1U<<bit);
    return value;
}

/*
4. 특정 비트가 설정되었는지 확인하는 함수/매크로 구현
   Implement a function/macro to check if a specific bit is set.
   Input: is_bit_set(0x10, 4)
   Output: 1
*/
bool is_bit_set(uint32_t value, uint8_t bit) {
    // TODO: implement
    return value & (1U<<bit);
}

/*
5. 상위/하위 니블(nibble) 스왑 구현
   Implement swapping of upper and lower nibbles in a byte.
   Input: swap_nibbles(0xAB)
   Output: 0xBA
*/
uint8_t swap_nibbles(uint8_t value) {
    // TODO: implement
    uint8_t result;
    uint8_t left = (value << 4) ;
    uint8_t right = (value >> 4 ) ;    
    result = left | right;    
    return result;
}

/*
6. 주어진 숫자의 패리티(parity) 계산 (짝수/홀수)
   Calculate the parity of a given number (even/odd).
   Input: parity_even(0x5)
   Output: 0


   설명:
   - 패리티란 1의 개수가 짝수면 1(짝수 패리티), 홀수면 0(홀수 패리티)로 반환하는 것.
   - 아래는 "짝수 패리티"를 반환하는 코드입니다.
*/
bool parity_even(uint32_t value) {
    // TODO: implement
    bool parity = 0;
    while(value)
    {
        parity ^=(value&1);
        value>>=1;
    }
    return !parity; // if # of 1 is even, then 1 / or odd -> 0
}

/*
7. 정수에서 가장 낮은 설정 비트(LSB) 클리어
   Clear the lowest set bit (LSB) of an integer.
   Input: clear_lsb(0x18)
   Output: 0x10

   설명:
   - value & (value - 1)은 가장 오른쪽 1비트만 0으로 만듭니다.
   - 예: 0x18(00011000) & 0x17(00010111) = 0x10(00010000)

자세한 설명:

    value & (value - 1)의 동작 원리를 비트 단위로 자세히 설명드릴게요.

    1. 목적
    이 연산은 정수에서 가장 오른쪽(최하위)의 1비트(LSB, Least Significant Bit)를 0으로 만든다는 뜻입니다.

    2. 예시로 살펴보기
    예를 들어,
    value = 0x18 (16진수) → 2진수로는 0001 1000

    value : 0001 1000
    value - 1 : 0001 0111
    이제 AND 연산(&)을 해보면:

    즉, 가장 오른쪽의 1비트만 0으로 바뀌고, 나머지는 그대로입니다.

    3. 왜 이렇게 되나?
    어떤 수에서 1을 빼면, 가장 오른쪽의 1비트가 0이 되고, 그 오른쪽 비트들은 모두 1로 바뀝니다.
    AND 연산을 하면, 그 자리만 0이 되고 나머지는 그대로 남습니다.
    예시:
    value = 0b1011000
    value - 1 = 0b1010111

    오른쪽에서 첫 번째 1만 0으로 바뀜.

    4. 활용
    비트마스크에서 한 개씩 1을 제거할 때
    1의 개수 세기(하나씩 지우면서 카운트)
    최하위 1비트 위치 찾기 등

    5. 결론
    value & (value - 1)은 가장 오른쪽 1비트만 0으로 만든다
    나머지 비트는 변하지 않는다
    비트 연산에서 매우 자주 쓰이는 패턴!
    추가 참고
    만약 value가 0이면, 결과도 0입니다.
    value가 2의 거듭제곱이면, 결과는 0이 됩니다(예: 0x8 & 0x7 = 0).
    궁금한 점 있으면 추가로 질문 주세요!

*/
uint32_t clear_lsb(uint32_t value) {
    // TODO: implement
    return value & (value - 1);
}

/*
8. 정수에서 가장 오른쪽의 1 찾기
   Find the rightmost set bit of an integer.
   Input: rightmost_set_bit(0x18)
   Output: 0x8

자세한 설명: 
    8. 정수에서 가장 오른쪽의 1 찾기
    즉, rightmost_set_bit(value) 함수의 동작 원리를 자세히 설명드릴게요.

    목적
    주어진 정수에서 가장 오른쪽(최하위)의 1비트만 남기고 나머지는 모두 0으로 만드는 연산입니다.

    대표 구현

    uint32_t rightmost_set_bit(uint32_t value) return value & (~value + 1);

    또는

    (2의 보수에서 -value는 ~value + 1과 같습니다.)

    동작 원리
    예시:
    value = 0x18 (16진수) → 2진수로는 0001 1000

    ~value : 1110 0111
    ~value + 1 : 1110 1000 (2의 보수, 즉 -0x18)
    value & (~value + 1) :

       0001 1000
    &  1110 1000
    -------------
       0000 1000   (0x8)

    즉, 가장 오른쪽의 1비트만 남고 나머지는 0이 됩니다.

    왜 이렇게 되나?
    2의 보수에서 -value는, value의 가장 오른쪽 1비트만 남기고 나머지는 0이 되도록 만듭니다.
    AND 연산을 하면, 그 자리만 1이고 나머지는 0이 됩니다.

    예시 2:
    value = 0b1010100
    -value = 0b010100 (2의 보수)
    value & -value = 0b000100 (가장 오른쪽 1만 남음)

    활용
        최하위 1비트 위치 찾기
        비트마스크에서 특정 비트만 추출
        비트 연산 최적화, 효율적 알고리즘 구현


    결론
        value & (~value + 1) 또는 value & -value는
        가장 오른쪽 1비트만 남기고 나머지는 0으로 만든다
        비트 트릭(bit trick) 중에서 매우 자주 쓰이는 패턴입니다!        

*/

/*
8. 정수에서 가장 오른쪽의 1 찾기
   Find the rightmost set bit of an integer.
   Input: rightmost_set_bit(0x18)
   Output: 0x8
*/
uint32_t rightmost_set_bit(uint32_t value) {
    // TODO: implement
    /*
    모범답안: 
    return value & (~value + 1); // 더 빠름
    */

    return value & (~value + 1);

}

/*
9. 두 정수의 부호가 다른지 비트 연산으로 확인
   Check if two integers have opposite signs using bitwise ops.
   Input: is_opposite_sign(-5, 7)
   Output: 1

   설명:
   - 두 정수의 부호가 다르면 XOR 연산 결과의 최상위 비트(부호 비트)가 1이 됩니다.
   - (a ^ b) < 0 또는 (a ^ b) & 0x80000000로 판별할 수 있습니다.
*/
bool is_opposite_sign(int32_t a, int32_t b) {
    // TODO: implement
    return (a ^ b) < 0; // or return ((a^b) & 0x80000000);
}

/*
10. 정수의 절대값 계산 (분기문 없이)
    Compute the absolute value of an integer without branching.
    Input: abs_no_branch(-123)
    Output: 123


    설명:
    - 부호 비트만큼 오른쪽 시프트하면 음수면 -1, 양수면 0이 됩니다.
    - (value ^ mask) - mask로 분기 없이 절대값을 구할 수 있습니다.
    - mask = value >> 31 (32비트 int 기준)



int32_t 하고 uint32_t 는 사실상 비트로보면 같은거아닌가?
    Yes,
    int32_t와 uint32_t는 메모리에서 모두 32비트(4바이트)로 저장되며,
    비트 패턴 자체는 동일합니다.

    차이점은 해석(interpretation) 방식에 있습니다.
    int32_t는 부호 있는 정수(signed, 2의 보수)로 해석
    uint32_t는 부호 없는 정수(unsigned)로 해석
    예시:

    0xFFFFFFFF
    int32_t로 해석하면 -1
    uint32_t로 해석하면 4294967295
    **비트 연산(AND, OR, XOR, SHIFT 등)**에서는 두 타입 모두 동일하게 동작합니다.
    다만, 산술 연산(+, -, /, >> 등)이나 비교 연산에서 해석이 달라집니다.

    정리:

    비트 패턴은 동일
    해석(부호 유무)에 따라 값이 달라짐
    비트 연산에는 타입 구분 없이 사용 가능 (단, 산술/논리 연산 결과 해석에 주의)


*/
int32_t abs_no_branch(int32_t value) {
    // TODO: implement

    int32_t mask = value >> 31;
    return (value ^ mask) - mask;

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
    test_result_bool("6. parity_even(0x5)", parity_even(0x5), 1); // expected 1
    test_result("7. clear_lsb(0x18)", clear_lsb(0x18), 0x10);
    test_result("8. rightmost_set_bit(0x18)", rightmost_set_bit(0x18), 0x8);
    test_result_bool("9. is_opposite_sign(-5, 7)", is_opposite_sign(-5, 7), 1);
    test_result_int("10. abs_no_branch(-123)", abs_no_branch(-123), 123);

    return 0;
}
/*
------------------ Expected Result ------------------

set_bit(0x00, 3) = 0x8
clear_bit(0xFF, 4) = 0xEF
toggle_bit(0x10, 4) = 0x0
is_bit_set(0x10, 4) = 1
swap_nibbles(0xAB) = 0xBA
parity_even(0x5) = 0
clear_lsb(0x18) = 0x10
rightmost_set_bit(0x18) = 0x8
is_opposite_sign(-5, 7) = 1
abs_no_branch(-123) = 123

-----------------------------------------------------
*/


/*
이 코드는 C 언어의 printf 스타일 포맷 문자열입니다. 각 부분을 설명하면 다음과 같습니다:

"%-*s = 0x%X"
%-*s : 문자열(s)을 출력하는데,
*는 출력 폭(width)을 함수 인자로 받아서 동적으로 지정합니다.
-는 왼쪽 정렬을 의미합니다.
예: printf("%-10s", "abc");는 "abc       "처럼 왼쪽 정렬로 10칸을 채웁니다.
= : 등호 기호를 그대로 출력합니다.
0x%X : 16진수(대문자)로 정수를 출력합니다.
예: printf("0x%X", 255);는 0xFF를 출력합니다.
예시:

정리:
이 포맷 문자열은

지정한 폭만큼 왼쪽 정렬로 문자열을 출력하고
등호(=)를 출력한 뒤
정수를 16진수(대문자)로 출력합니다.
*/