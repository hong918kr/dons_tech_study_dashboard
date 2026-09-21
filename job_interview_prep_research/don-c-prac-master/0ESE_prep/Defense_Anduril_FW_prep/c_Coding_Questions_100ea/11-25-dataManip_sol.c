#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/*
11. uint32_t reverseBits(uint32_t n) 구현
    Implement uint32_t reverseBits(uint32_t n).
    Example: reverseBits(0xF0F0F0F0) -> 0x0F0F0F0F
*/

/*
1. 반복문으로 한 비트씩 뒤집기
uint32_t reverseBits_loop(uint32_t n) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }
    return res;
}

2. 마스크와 시프트를 이용한 비트 그룹 교환 (SWAR 방식)
uint32_t reverseBits_swar(uint32_t n) {
    n = ((n >> 1) & 0x55555555) | ((n & 0x55555555) << 1);
    n = ((n >> 2) & 0x33333333) | ((n & 0x33333333) << 2);
    n = ((n >> 4) & 0x0F0F0F0F) | ((n & 0x0F0F0F0F) << 4);
    n = ((n >> 8) & 0x00FF00FF) | ((n & 0x00FF00FF) << 8);
    n = (n >> 16) | (n << 16);
    return n;
}
    이 방식은 병렬 비트 교환으로 매우 빠릅니다. → 임베디드에서는 이 방식이 가장 많이 쓰입니다!

3. 바이트 단위로 뒤집기 (lookup table 활용, 8비트씩)
// 8비트 비트반전 테이블
static const uint8_t reverse8[256] = {
    0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
    0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
    0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
    0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
    0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
    0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
    0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
    0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
    0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
    0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
    0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
    0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
    0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
    0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
    0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
    0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF
};

uint32_t reverseBits_table(uint32_t n) {
    return (reverse8[n & 0xFF] << 24) |
           (reverse8[(n >> 8) & 0xFF] << 16) |
           (reverse8[(n >> 16) & 0xFF] << 8) |
           (reverse8[(n >> 24) & 0xFF]);
}

정리:

반복문, SWAR, 테이블, 내장함수 등 다양한 방식이 있으며,
SWAR/테이블 방식이 속도 면에서 가장 효율적입니다.
반복문은 코드가 가장 단순합니다.

lookup table 방식(예: 8비트 비트반전 테이블 256바이트)은 속도는 빠르지만,
메모리(ROM/FLASH/RAM) 사용량이 증가합니다.

장단점 비교
1. Lookup Table 방식
장점:
매우 빠름 (O(1)), 반복문/비트연산 없이 바로 변환
단점:
256바이트(8비트 테이블) ~ 1024바이트(16비트 테이블) 등 코드/데이터 메모리 사용 증가
임베디드(특히 작은 MCU)에서는 코드/데이터 공간이 부족할 수 있음


2. 반복문/비트연산 방식
장점:
메모리 사용 거의 없음 (코드만 필요)
코드가 간단하고 이식성 높음
단점:
속도가 lookup table보다 느릴 수 있음 (특히 반복문 사용 시)



3. SWAR(병렬 비트 교환) 방식
장점:
메모리 사용 없이 빠름 (몇 번의 비트 연산만으로 처리)
단점:
코드가 약간 복잡할 수 있음
*/

uint32_t reverseBits(uint32_t n) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }
    return res;
}

/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*
12. 설정된 비트 수 카운트 (해밍 가중치)
    Count the number of set bits (Hamming weight).
    Example: hamming_weight(0xF0F0F0F0) -> 16
*/

/*
자세한 설명: 
hamming_weight 함수는 정수에서 1로 설정된 비트(= set bit)의 개수를 세는 함수입니다.
이 값은 "해밍 가중치(Hamming weight)" 또는 "popcount"라고도 부릅니다.

1. 동작 원리
    uint32_t hamming_weight(uint32_t n) {
        uint32_t count = 0;
        while (n) {
            count += n & 1; // n의 최하위 비트가 1이면 count 증가
            n >>= 1;        // n을 오른쪽으로 1비트 이동
        }
        return count;
    }

    n & 1 : n의 최하위 비트가 1인지 확인
    count += n & 1 : 1이면 count 증가
    n >>= 1 : n을 오른쪽으로 한 비트 이동(다음 비트 검사)
    n이 0이 될 때까지 반복


2. 예시
    예시 1
        입력: hamming_weight(0xF0F0F0F0)

        0xF0F0F0F0 = 1111 0000 1111 0000 1111 0000 1111 0000 (2진수)
        1의 개수: 16개
        결과: 16


    예시 2
        입력: hamming_weight(0x7)

        0x7 = 0000 0111
        1의 개수: 3개
        결과: 3


3. 활용 예시
    비트마스크에서 활성화된 비트 개수 세기
    집합(set)에서 원소 개수 세기
    에러 검출/수정 코드(해밍 코드 등)
    최적화된 알고리즘(예: 비트 연산 기반 DP)

4. 참고: 더 빠른 방법
    위 코드는 모든 비트를 순회(최대 32번)합니다.
    더 빠른 방법:

    while (n) {
        n &= (n - 1); // 최하위 1비트 제거
        count++;
    }

    이 방법은 1의 개수만큼만 반복합니다.

5. 결론
    **hamming_weight(n)**은 n의 이진수 표현에서 1의 개수를 반환합니다.
    비트 연산 문제, 집합 문제, 최적화 문제 등에서 매우 자주 쓰입니다!


*/
uint32_t hamming_weight(uint32_t n) {
    uint32_t count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}


/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*
13. 주어진 숫자가 2의 거듭제곱인지 확인
    Check if a given number is a power of 2.
    Example: is_power_of_two(16) -> true, is_power_of_two(18) -> false
*/
bool is_power_of_two(uint32_t n) {
    return n && !(n & (n - 1));
}

/*
14. 정수의 홀수 번째와 짝수 번째 비트 스왑
    Swap odd and even bits of an integer.
    Example: swap_odd_even_bits(0xAAAAAAAA) -> 0x55555555
*/
uint32_t swap_odd_even_bits(uint32_t n) {
    return ((n & 0xAAAAAAAA) >> 1) | ((n & 0x55555555) << 1);
}

/*
15. 주어진 범위의 비트 추출
    Extract a range of bits from a number.
    // start: 시작 비트 위치, end: 끝 비트 위치 (포함)
    Example: extract_bit_range(0xF0F0, 4, 7) -> 0xF
*/
uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    uint32_t mask = ((1U << (end - start + 1)) - 1) << start;
    return (n & mask) >> start;
}

/*
16. 주어진 범위의 비트 설정
    Set a range of bits in a number.
    Example: set_bit_range(0x0000, 4, 7, 0xF) -> 0xF0
*/
uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value) {
    uint32_t mask = ((1U << (end - start + 1)) - 1) << start;
    n = (n & ~mask) | ((value << start) & mask);
    return n;
}

/*
17. 주어진 범위의 비트 반전
    Invert a range of bits in a number.
    Example: invert_bit_range(0xF0F0, 4, 7) -> 0xF0F0 ^ 0xF0 = 0xF000
*/
uint32_t invert_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    uint32_t mask = ((1U << (end - start + 1)) - 1) << start;
    return n ^ mask;
}



/*
18. 가장 가까운 2의 거듭제곱 수 찾기 (올림/내림)
    Find the next/previous power of 2 for a given number.
    Example: next_power_of_two(17) -> 32, prev_power_of_two(17) -> 16
*/
/*

조금더 자세한 설명

18. 가장 가까운 2의 거듭제곱 수 찾기 (올림/내림)

    1) next_power_of_two (올림)
        설명:
            주어진 n보다 크거나 같은 가장 작은 2의 거듭제곱을 찾습니다.
        원리:
            비트를 오른쪽으로 계속 OR 연산하여, n보다 큰 모든 비트를 1로 만듭니다.
            마지막에 1을 더하면 바로 위의 2의 거듭제곱이 됩니다.
        예시:
            n = 17 (0b10001)
        특징:
            n이 이미 2의 거듭제곱이면 그대로 반환
            n이 0이면 1 반환

    2) prev_power_of_two (내림)
        설명:
            n보다 작거나 같은 가장 큰 2의 거듭제곱을 찾습니다.
        원리:
            1부터 시작해서 n을 넘지 않을 때까지 2배씩 곱합니다.
        예시:
            n = 17
            p = 1 → 2 → 4 → 8 → 16 → 32(넘음) → 16 반환

    3) 활용 예시

        메모리 할당(버퍼 크기), 비트마스크, 트리 구조 등에서 자주 사용
        예: 17바이트를 저장할 때 32바이트 버퍼 할당



네, next_power_of_two(올림) 알고리즘이 직관적으로 이해가 어려울 수 있습니다.
아래에 비주얼 예시와 비트 연산의 원리를 단계별로 설명드릴게요.

next_power_of_two 함수의 원리
    목적
        n보다 크거나 같은 가장 작은 2의 거듭제곱을 구한다.
        예: n=17 → 32, n=32 → 32, n=33 → 64
    핵심 아이디어
        n보다 큰 모든 비트를 1로 만든다.
        1을 더하면 바로 위의 2의 거듭제곱이 된다.

단계별 예시 (n = 17)
    1. n = 17 (2진수: 0001 0001)
        n = 17; // 0001 0001

    2. n-- (n = 16, 0001 0000)
        이미 2의 거듭제곱이면 그대로 반환하기 위해 n--을 먼저 해줌

    3. 비트 OR 연산으로 모든 하위 비트를 1로 만든다

        n |= n >> 1;
        0001 0000 | 0000 1000 = 0001 1000

        n |= n >> 2;
        0001 1000 | 0000 0110 = 0001 1110

        n |= n >> 4;
        0001 1110 | 0000 0001 = 0001 1111

        n |= n >> 8, n |= n >> 16; (이 예시에서는 변화 없음)

        최종적으로 n = 0001 1111 (31)

    4. n + 1
        0001 1111 + 1 = 0010 0000 (32)


왜 이렇게 동작할까?
    비트 OR와 시프트를 반복하면, n보다 작은 모든 비트가 1로 채워집니다.
    예를 들어, 17보다 큰 2의 거듭제곱은 32(0010 0000)입니다.
    17보다 작은 모든 수(16, 8, 4, 2, 1)의 비트가 1로 채워진 후, 1을 더하면 32가 됩니다.

        n = 17 (0001 0001)
        n-- = 16 (0001 0000)
        n |= n >> 1  -> 0001 1000
        n |= n >> 2  -> 0001 1110
        n |= n >> 4  -> 0001 1111
        n + 1        -> 0010 0000 (32)

    요약
        n보다 큰 모든 비트를 1로 만든 뒤 1을 더하면 바로 위의 2의 거듭제곱이 된다.
        이 방식은 반복문 없이 빠르게 계산할 수 있어 효율적입니다.
    참고:
        n이 이미 2의 거듭제곱이면 그대로 반환됩니다.
        n이 0이면 1을 반환합니다.

    즉, 이 알고리즘은 "n보다 크거나 같은 2의 거듭제곱"을 비트 연산만으로 빠르게 구하는 방법입니다!
*/
uint32_t next_power_of_two(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}
uint32_t prev_power_of_two(uint32_t n) {
    if (n == 0) return 0;
    uint32_t p = 1;
    while (p <= n) p <<= 1;
    return p >> 1;
}

/*
19. 두 숫자를 임시 변수 없이 스왑 (XOR 사용)
    Swap two numbers without a temporary variable (using XOR).
    Example: a=5, b=7 -> xor_swap(&a, &b) -> a=7, b=5
*/

/*
조금더 자세한설명

19. 두 숫자를 임시 변수 없이 XOR로 스왑
    설명
        임시 변수 없이 두 변수의 값을 교환하는 고전적인 비트 연산 트릭입니다.
    원리:
        XOR의 성질을 이용합니다.
            a = a ^ b
            b = b ^ a (이 시점에서 b = (a^b)^b = a)
            a = a ^ b (이 시점에서 a = (a^b)^a = b)
    예시
        a = 5 (0b0101), b = 7 (0b0111)

            a = a ^ b; // a = 0b0101 ^ 0b0111 = 0b0010 (2)
            b = b ^ a; // b = 0b0111 ^ 0b0010 = 0b0101 (5)
            a = a ^ b; // a = 0b0010 ^ 0b0101 = 0b0111 (7)

        결과: a=7, b=5 (서로 값이 바뀜)
    주의사항
        a와 b가 같은 주소(동일 변수)일 때는 사용하면 안 됩니다. (0이 됨)
        가독성이나 최적화 측면에서 최근에는 임시 변수 사용이 더 권장되기도 합니다.
    활용
        임베디드, 인터뷰, 저수준 알고리즘 등에서 자주 등장하는 트릭입니다.
*/
void xor_swap(uint32_t *a, uint32_t *b) {
    if (a == b) return;
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}



/*
----------------------------------------------------------
비트 회전에 대한 자세한 설명


1. 비트 시프트(shift)와의 차이
        비트 시프트(shift): 밀려난 비트는 버려지고, 빈자리는 0으로 채워집니다.
        비트 회전(rotate): 밀려난 비트가 반대쪽 끝으로 돌아옵니다.
        예시 (8비트 기준):

        연산	        입력	    결과	        설명
        Shift left	1011 0110	0110 1100	맨 왼쪽 1은 버려짐, 오른쪽 0 추가
        Rotate left	1011 0110	0110 1101	맨 왼쪽 1이 오른쪽 끝으로


2. 비트 회전 종류
        왼쪽 회전 (rotate left, ROL)
        n비트를 왼쪽으로 밀고, 밀려난 비트는 오른쪽 끝으로 이동

        오른쪽 회전 (rotate right, ROR)
        n비트를 오른쪽으로 밀고, 밀려난 비트는 왼쪽 끝으로 이동

3. C에서의 구현 예시 (32비트)


        // 32비트 왼쪽 회전
        uint32_t rotate_left(uint32_t n, uint8_t k) {
            return (n << k) | (n >> (32 - k));
        }

        // 32비트 오른쪽 회전
        uint32_t rotate_right(uint32_t n, uint8_t k) {
            return (n >> k) | (n << (32 - k));
        }

4. 예시
        입력: n = 0x80000001 (1000...0001, 32비트)

        왼쪽 1비트 회전:
            rotate_left(0x80000001, 1)
            결과: 0x00000003 (000...0011)
        오른쪽 1비트 회전:
            rotate_right(0x80000001, 1)
            결과: 0xC0000000 (1100...0000)

5. 활용 예시
        암호 알고리즘(블록 암호, 해시 함수 등)에서 자주 사용
        임베디드 시스템, CRC, 체크섬 등에서 효율적인 비트 조작 필요할 때

요약:
        비트 회전은 비트들을 한쪽으로 밀면서, 밀려난 비트를 반대쪽 끝으로 다시 넣는 연산입니다.
        시프트와 달리 정보가 손실되지 않으며, 다양한 저수준 알고리즘에서 매우 유용하게 사용됩니다.

-----------------------------
*/

/*
20. 비트 회전(Rotate) 구현 (왼쪽/오른쪽)
    Implement bit rotation (left/right).
    Example: rotate_left(0x80000001, 1) -> 0x00000003
             rotate_right(0x80000001, 1) -> 0xC0000000
*/
uint32_t rotate_left(uint32_t n, uint8_t k) {
    return (n << k) | (n >> (32 - k));
}
uint32_t rotate_right(uint32_t n, uint8_t k) {
    return (n >> k) | (n << (32 - k));
}

/*
21. 32비트 정수의 엔디안(Endianness) 변환
    Convert endianness of a 32-bit integer.
    Example: swap_endian32(0x12345678) -> 0x78563412
*/
uint32_t swap_endian32(uint32_t n) {
    return ((n >> 24) & 0xFF) | ((n >> 8) & 0xFF00) |
           ((n << 8) & 0xFF0000) | ((n << 24) & 0xFF000000);
}

/*
22. 비트 필드(bit-field)를 비트 연산으로 시뮬레이션

1. 목적
    비트 필드란, 하나의 정수(예: 32비트)에서 여러 개의 값을 비트 단위로 나누어 저장하는 방식입니다.
    하드웨어 레지스터, 통신 프로토콜, 임베디드 시스템 등에서 자주 사용합니다.
    C의 struct bit-field도 있지만, 이식성과 명확성을 위해 직접 비트 연산으로 구현하는 경우가 많습니다.


    Simulate bit-fields using bitwise operations.
    // 예시: 특정 필드의 값 읽기/쓰기 함수
    Example: get_field(0xF0F0, 4, 7) -> 0xF
             set_field(0x0000, 4, 7, 0xF) -> 0xF0
*/
uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width) {
    uint32_t mask = ((1U << width) - 1) << pos;
    return (reg & mask) >> pos;
}
uint32_t set_field(uint32_t reg, uint8_t pos, uint8_t width, uint32_t value) {
    uint32_t mask = ((1U << width) - 1) << pos;
    reg = (reg & ~mask) | ((value << pos) & mask);
    return reg;
}


/*
그레이 코드에 대한 자세한 설명

1. 그레이 코드란?
    그레이 코드는 이진수의 일종이지만, 
    연속된 두 값이 항상 1비트만 다르게 표현되는 특별한 이진수 체계입니다.
    예를 들어, 일반 이진수에서 3(011)에서 4(100)로 넘어갈 때 2비트가 바뀌지만, 
    그레이 코드에서는 항상 1비트만 바뀝니다.

2. 왜 그레이 코드를 쓰나요?
    하드웨어 신호 처리나 기계식 엔코더 등에서
    여러 비트가 동시에 바뀌면 오류가 발생할 수 있습니다.
    그레이 코드는 한 번에 한 비트만 바뀌므로, 
    신호 전이 오류를 최소화할 수 있습니다.

3. 3비트 예시
    10진수	이진수	그레이 코드
    0	    000	    000
    1	    001	    001
    2	    010	    011
    3	    011	    010
    4	    100	    110
    5	    101	    111
    6	    110	    101
    7	    111	    100

    위 표에서 그레이 코드 열을 보면,
     위아래로 한 칸씩만 이동할 때마다
      오직 1비트만 바뀌는 것을 알 수 있습니다.

4. 변환 공식
    이진수 → 그레이 코드:
    gray = n ^ (n >> 1)
    그레이 코드 → 이진수:
    반복적으로 오른쪽 시프트하며 XOR 누적

uint32_t gray_to_binary(uint32_t n) {
    uint32_t res = n;
    while (n >>= 1) res ^= n;
    return res;
}


5. 예시 변환
    이진수 11 (0b1011) → 그레이 코드

    n = 0b1011
    n >> 1 = 0b0101
    n ^ (n >> 1) = 0b1011 ^ 0b0101 = 0b1110 (14)
    그레이 코드 0b1110 → 이진수

    res = 0b1110
    n >>= 1 → 0b0111, res ^= 0b0111 → 0b1001
    n >>= 1 → 0b0011, res ^= 0b0011 → 0b1010
    n >>= 1 → 0b0001, res ^= 0b0001 → 0b1011
    n >>= 1 → 0b0000, 종료
    결과: 0b1011 (11)


6. 그림으로 이해하기
    

    이진수:   000 → 001 → 010 → 011 → 100 → 101 → 110 → 111
    그레이:   000 → 001 → 011 → 010 → 110 → 111 → 101 → 100

    각 단계에서 오직 1비트만 바뀜.

7. 요약
    그레이 코드는 연속된 값이 1비트만 다르게 표현되는 이진수 체계입니다.
    하드웨어, 신호처리, 엔코더 등에서 오류를 줄이기 위해 사용합니다.
    변환 공식은 간단하며, C 코드로도 쉽게 구현할 수 있습니다.


*/


/*
23. 이진수를 그레이 코드로 변환
    Convert a binary number to Gray code.
    Example: binary_to_gray(0b1011) -> 0b1110
*/
uint32_t binary_to_gray(uint32_t n) {
    return n ^ (n >> 1);
}

/*
24. 그레이 코드를 이진수로 변환
    Convert Gray code to a binary number.
    Example: gray_to_binary(0b1110) -> 0b1011
*/
uint32_t gray_to_binary(uint32_t n) {
    uint32_t res = n;
    while (n >>= 1) res ^= n;
    return res;
}

/*
25. 곱셈과 나눗셈을 시프트 연산으로 구현
    Implement multiplication/division by powers of 2 using shifts.
    Example: mul_pow2(3, 2) -> 12, div_pow2(12, 2) -> 3
*/
uint32_t mul_pow2(uint32_t n, uint8_t k) {
    return n << k;
}
uint32_t div_pow2(uint32_t n, uint8_t k) {
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
div_pow2(12, 2) = 3

----------------------------------------------------------
*/