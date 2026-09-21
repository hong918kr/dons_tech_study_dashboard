#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
11. uint32_t reverseBits(uint32_t n) 구현
    Implement uint32_t reverseBits(uint32_t n).
    Example: reverseBits(0xF0F0F0F0) -> 0x0F0F0F0F
*/
uint32_t reverseBits(uint32_t n) {
    // TODO: implement

    uint32_t res = 0;
    for (size_t i = 0; i < 32; ++i )
    {
        res = res << 1;
        res = res | (n & 1);
        n = n >> 1;
    }
    
    return res;
}

/*
12. 설정된 비트 수 카운트 (해밍 가중치)
    Count the number of set bits (Hamming weight).
    Example: hamming_weight(0xF0F0F0F0) -> 16
*/
uint32_t hamming_weight(uint32_t n) {
    // TODO: implement

    uint32_t count = 0;

    // for (size_t i = 0; i < 32; ++i)
    // {
    //     if ( (n & (1U << i)) ) count++;
    // }

    
        while (n)
        {
            count = count + (n & 1);
            n >>= 1;
        }
    

    return count;
}

/*
13. 주어진 숫자가 2의 거듭제곱인지 확인
    Check if a given number is a power of 2.
    Example: is_power_of_two(16) -> true, is_power_of_two(18) -> false
*/
bool is_power_of_two(uint32_t n) {
    // TODO: implement
    


    
    return false;
}

/*
14. 정수의 홀수 번째와 짝수 번째 비트 스왑
    Swap odd and even bits of an integer.
    Example: swap_odd_even_bits(0xAAAAAAAA) -> 0x55555555
*/
uint32_t swap_odd_even_bits(uint32_t n) {
    // TODO: implement
    return 0;
}

/*
15. 주어진 범위의 비트 추출
    Extract a range of bits from a number.
    // start: 시작 비트 위치, end: 끝 비트 위치 (포함)
    Example: extract_bit_range(0xF0F0, 4, 7) -> 0xF
*/
uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    // TODO: implement
    return 0;
}

/*
16. 주어진 범위의 비트 설정
    Set a range of bits in a number.
    Example: set_bit_range(0x0000, 4, 7, 0xF) -> 0xF0
*/
uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value) {
    // TODO: implement
    return 0;
}

/*
17. 주어진 범위의 비트 반전
    Invert a range of bits in a number.
    Example: invert_bit_range(0xF0F0, 4, 7) -> 0xF0F0 ^ 0xF0 = 0xF000
*/
uint32_t invert_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    // TODO: implement
    return 0;
}

/*
18. 가장 가까운 2의 거듭제곱 수 찾기 (올림/내림)
    Find the next/previous power of 2 for a given number.
    Example: next_power_of_two(17) -> 32, prev_power_of_two(17) -> 16
*/
uint32_t next_power_of_two(uint32_t n) {
    // TODO: implement
    if (n == 0) {} return 0;


}
uint32_t prev_power_of_two(uint32_t n) {
    // TODO: implement
    return 0;
}

/*
19. 두 숫자를 임시 변수 없이 스왑 (XOR 사용)
    Swap two numbers without a temporary variable (using XOR).
    Example: a=5, b=7 -> xor_swap(&a, &b) -> a=7, b=5
*/
void xor_swap(uint32_t *a, uint32_t *b) {
    // TODO: 
    if (a == b) return;
    *a = *a ^ *b;
    *b = *b ^ *a;
    *a = *a^ *b;    
    
}

/*
20. 비트 회전(Rotate) 구현 (왼쪽/오른쪽)
    Implement bit rotation (left/right).
    Example: rotate_left(0x80000001, 1) -> 0x00000003
             rotate_right(0x80000001, 1) -> 0xC0000000
*/
uint32_t rotate_left(uint32_t n, uint8_t k) {
    // TODO: implement
    uint32_t res = 0;
    res = (n << k) | (n >> (32-k));

}
uint32_t rotate_right(uint32_t n, uint8_t k) {
    // TODO: implement
    uint32_t res = 0;
    res = (n >> k) | (n << 32-k);
}

/*
21. 32비트 정수의 엔디안(Endianness) 변환
    Convert endianness of a 32-bit integer.
    Example: swap_endian32(0x12345678) -> 0x78563412
*/
uint32_t swap_endian32(uint32_t n) {
    // TODO: implement

    uint32_t res = ( ((n >> 24) & 0xFF)     |
                     ((n >> 8)  & 0xFF00)   |
                     ((n << 8)  & 0xFF0000) |
                     ((n << 24) & 0xFF000000) 
                    );
    return res;
}

/*
22. 비트 필드(bit-field)를 비트 연산으로 시뮬레이션
    Simulate bit-fields using bitwise operations.
    // 예시: 특정 필드의 값 읽기/쓰기 함수
    Example: get_field(0xF0F0, 4, 7) -> 0xF
             set_field(0x0000, 4, 7, 0xF) -> 0xF0
*/
uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width) {
    // TODO: implement
    uint32_t mask = ((1U << width) - 1) << pos;
    return (reg & mask) >> pos;

}
uint32_t set_field(uint32_t reg, uint8_t pos, uint8_t width, uint32_t value) {
    // TODO: implement
    uint32_t mask = ( (1U << width) - 1) << pos;
    reg = (reg &~mask) | ((value << pos) & mask);
    // 기존비트는 0으로 지우고, value를 해당 위치로 이동시켜 OR연산 으로 합침
}

/*
23. 이진수를 그레이 코드로 변환
    Convert a binary number to Gray code.
    Example: binary_to_gray(0b1011) -> 0b1110
*/
uint32_t binary_to_gray(uint32_t n) {
    // TODO: implement
    return 0;
}

/*
24. 그레이 코드를 이진수로 변환
    Convert Gray code to a binary number.
    Example: gray_to_binary(0b1110) -> 0b1011
*/
uint32_t gray_to_binary(uint32_t n) {
    // TODO: implement
    return 0;
}

/*
25. 곱셈과 나눗셈을 시프트 연산으로 구현
    Implement multiplication/division by powers of 2 using shifts.
    Example: mul_pow2(3, 2) -> 12, div_pow2(12, 2) -> 3
*/
uint32_t mul_pow2(uint32_t n, uint8_t k) {
    // TODO: implement
    return n << k;
}
uint32_t div_pow2(uint32_t n, uint8_t k) {
    // TODO: implement
    return n >> k;
}

// --- Test code ---
#define LABEL_WIDTH 38

void test_result(const char* label, uint32_t result, uint32_t expected) {
    printf("%-*s = 0x%X", LABEL_WIDTH, label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected 0x%X)\n", expected);
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
    test_result("11. reverseBits(0xF0F0F0F0)", reverseBits(0xF0F0F0F0), 0x0F0F0F0F);
    test_result("12. hamming_weight(0xF0F0F0F0)", hamming_weight(0xF0F0F0F0), 16);
    test_result_bool("13. is_power_of_two(16)", is_power_of_two(16), 1);
    test_result_bool("13. is_power_of_two(18)", is_power_of_two(18), 0);
    test_result("14. swap_odd_even_bits(0xAAAAAAAA)", swap_odd_even_bits(0xAAAAAAAA), 0x55555555);
    test_result("15. extract_bit_range(0xF0F0,4,7)", extract_bit_range(0xF0F0, 4, 7), 0xF);
    test_result("16. set_bit_range(0x0000,4,7,0xF)", set_bit_range(0x0000, 4, 7, 0xF), 0xF0);
    test_result("17. invert_bit_range(0xF0F0,4,7)", invert_bit_range(0xF0F0, 4, 7), 0xF000);
    test_result("18. next_power_of_two(17)", next_power_of_two(17), 32);
    test_result("18. prev_power_of_two(17)", prev_power_of_two(17), 16);

    uint32_t a = 5, b = 7;
    xor_swap(&a, &b);
    printf("%-*s = a=%u, b=%u", LABEL_WIDTH, "19. xor_swap(&a=5, &b=7)", a, b);
    if (a == 7 && b == 5) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected a=7, b=5)\n");
    }

    test_result("20. rotate_left(0x80000001,1)", rotate_left(0x80000001, 1), 0x00000003);
    test_result("20. rotate_right(0x80000001,1)", rotate_right(0x80000001, 1), 0xC0000000);
    test_result("21. swap_endian32(0x12345678)", swap_endian32(0x12345678), 0x78563412);
    test_result("22. get_field(0xF0F0,4,4)", get_field(0xF0F0, 4, 4), 0xF);
    test_result("22. set_field(0x0000,4,4,0xF)", set_field(0x0000, 4, 4, 0xF), 0xF0);
    test_result("23. binary_to_gray(0b1011)", binary_to_gray(0b1011), 0b1110);
    test_result("24. gray_to_binary(0b1110)", gray_to_binary(0b1110), 0b1011);
    test_result("25. mul_pow2(3,2)", mul_pow2(3, 2), 12);
    test_result("25. div_pow2(12,2)", div_pow2(12, 2), 3);

    return 0;
}


/*
------------------ Example Input/Output ------------------

reverseBits(0xF0F0F0F0) = 0x0F0F0F0F
hamming_weight(0xF0F0F0F0) = 16
is_power_of_two(16) = 1
is_power_of_two(18) = 0
swap_odd_even_bits(0xAAAAAAAA) = 0x55555555
extract_bit_range(0xF0F0, 4, 7) = 0xF
set_bit_range(0x0000, 4, 7, 0xF) = 0xF0
invert_bit_range(0xF0F0, 4, 7) = 0xF000
next_power_of_two(17) = 32
prev_power_of_two(17) = 16
xor_swap: a=7, b=5
rotate_left(0x80000001, 1) = 0x00000003
rotate_right(0x80000001, 1) = 0xC0000000
swap_endian32(0x12345678) = 0x78563412
get_field(0xF0F0, 4, 4) = 0xF
set_field(0x0000, 4, 4, 0xF) = 0xF0
binary_to_gray(0b1011) = 0b1110
gray_to_binary(0b1110) = 0b1011
mul_pow2(3, 2) = 12
div_pow2(12, 2) = 3*/