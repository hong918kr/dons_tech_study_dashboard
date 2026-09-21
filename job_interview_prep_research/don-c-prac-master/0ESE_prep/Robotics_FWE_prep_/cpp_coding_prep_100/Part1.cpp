/*

#### **Part 1: 기본기 마스터 - 비트 조작 및 메모리 접근 (문제 1-15)**
*이 섹션은 모든 임베디드 엔지니어의 기본 소양을 테스트합니다. 코드는 간결하지만, 정확성과 효율성이 핵심입니다.*

1.  `uint32_t` 타입의 레지스터 값과 비트 번호(0-31)를 받아, 해당 비트를 1로 세팅하는 함수를 구현하세요.
2.  `uint32_t` 타입의 레지스터 값과 비트 번호를 받아, 해당 비트를 0으로 클리어하는 함수를 구현하세요.
3.  `uint32_t` 타입의 레지스터 값과 비트 번호를 받아, 해당 비트를 반전(toggle)시키는 함수를 구현하세요.
4.  `uint32_t` 타입의 레지스터 값과 비트 번호를 받아, 해당 비트가 1인지 0인지 확인하는 함수를 구현하세요.
5.  `uint32_t` 타입의 값에서 1로 세팅된 비트의 개수를 세는 함수를 구현하세요. (Follow-up: 더 효율적인 방법은?)
6.  `uint32_t` 타입의 값에서 비트 순서를 거꾸로 뒤집는 함수를 구현하세요. (예: `1100...` -> `...0011`)
7.  `uint32_t` 타입의 레지스터 값, 시작 비트, 끝 비트를 받아, 해당 범위의 비트들만 추출하는 함수를 구현하세요.
8.  `uint32_t` 타입의 레지스터 값, 시작 비트, 그리고 삽입할 값을 받아, 원래 값의 다른 비트는 건드리지 않고 해당 범위에 새 값을 삽입하는 함수를 구현하세요.
9.  두 `uint32_t` 값의 비트가 몇 개나 다른지 계산하는 함수를 구현하세요 (해밍 거리).
10. 주어진 `uint32_t` 값이 2의 거듭제곱인지 확인하는 가장 효율적인 방법을 구현하세요.
11. `uint16_t` 타입의 값에서 상위 바이트(MSB)와 하위 바이트(LSB)를 바꾸는 함수를 구현하세요 (Endianness swap).
12. 특정 메모리 주소(예: `0x40001000`)에 `uint32_t` 값을 쓰는 함수를 구현하세요. `volatile`의 필요성을 설명하세요.
13. 특정 메모리 주소에서 `uint32_t` 값을 읽어오는 함수를 구현하세요.
14. `uint32_t` 배열의 모든 요소에 대해 5번 비트를 세팅하는 함수를 구현하세요.
15. `uint8_t` 값의 패리티(parity)가 짝수인지 홀수인지 확인하는 함수를 구현하세요.

*/

/*
#### Part 1: 기본기 마스터 - 비트 조작 및 메모리 접근 (문제 1-15)
*이 섹션은 모든 임베디드 엔지니어의 기본 소양을 테스트합니다. C++ 스타일로 구현하세요.*
*/

#include <cstdint>
#include <cstdio>
#include <vector>
#include <iostream>

// 1. 비트 세팅
uint32_t set_bit(uint32_t n, uint8_t pos) {
    return n | (1U << pos);
}

// 2. 비트 클리어
uint32_t clear_bit(uint32_t n, uint8_t pos) {
    return n & ~(1U << pos);
}

// 3. 비트 토글
uint32_t toggle_bit(uint32_t n, uint8_t pos) {
    return n ^ (1U << pos);
}

// 4. 비트 확인
bool check_bit(uint32_t n, uint8_t pos) {
    return (n >> pos) & 1U;
}

// 5. 1로 세팅된 비트 개수 (해밍 가중치)
uint32_t hamming_weight(uint32_t n) {
    uint32_t count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

// 6. 비트 순서 뒤집기
uint32_t reverse_bits(uint32_t n) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res = (res << 1) | (n & 1);
        n >>= 1;
    }
    return res;
}

// 7. 비트 범위 추출
uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    uint32_t mask = ((1U << (end - start + 1)) - 1) << start;
    return (n & mask) >> start;
}

// 8. 비트 범위에 값 삽입
uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value) {
    uint32_t mask = ((1U << (end - start + 1)) - 1) << start;
    n = (n & ~mask) | ((value << start) & mask);
    return n;
}

// 9. 해밍 거리 (두 값의 비트가 몇 개 다른지)
uint32_t hamming_distance(uint32_t a, uint32_t b) {
    return hamming_weight(a ^ b);
}

// 10. 2의 거듭제곱 판별
bool is_power_of_two(uint32_t n) {
    return n && ((n & (n - 1)) == 0);
}

// 11. 16비트 상위/하위 바이트 스왑 (엔디안 변환)
uint16_t swap_bytes16(uint16_t n) {
    return (n >> 8) | (n << 8);
}

// 12. 특정 메모리 주소에 값 쓰기 (volatile 필요성)
// 실제 임베디드 환경에서만 동작, 여기선 시뮬레이션 불가
void write_mem32(uint32_t addr, uint32_t value) {
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(addr);
    *p = value;
    // volatile: 최적화 방지, 실제 하드웨어 레지스터 접근 시 필수
}

// 13. 특정 메모리 주소에서 값 읽기
uint32_t read_mem32(uint32_t addr) {
    volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(addr);
    return *p;
}

// 14. 배열의 모든 요소에 5번 비트 세팅
void set_bit5_all(std::vector<uint32_t>& arr) {
    for (auto& v : arr) {
        v |= (1U << 5);
    }
}

// 15. 8비트 값의 패리티(짝수/홀수) 확인
bool parity_even(uint8_t n) {
    bool parity = 0;
    while (n) {
        parity ^= (n & 1);
        n >>= 1;
    }
    return !parity; // true: even, false: odd
}

// --- Test code ---
void test_result(const char* label, uint32_t result, uint32_t expected) {
    printf("%-40s = 0x%X", label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected 0x%X)\n", expected);
    }
}
void test_result_bool(const char* label, bool result, bool expected) {
    printf("%-40s = %d", label, result);
    if (result == expected) {
        printf(" [PASS]\n");
    } else {
        printf(" [FAIL] (expected %d)\n", expected);
    }
}

int main() {
    // 1. set_bit
    test_result("1. set_bit(0x00, 3)", set_bit(0x00, 3), 0x08);

    // 2. clear_bit
    test_result("2. clear_bit(0xFF, 3)", clear_bit(0xFF, 3), 0xF7);

    // 3. toggle_bit
    test_result("3. toggle_bit(0x08, 3)", toggle_bit(0x08, 3), 0x00);

    // 4. check_bit
    test_result_bool("4. check_bit(0x08, 3)", check_bit(0x08, 3), true);
    test_result_bool("4. check_bit(0x08, 2)", check_bit(0x08, 2), false);

    // 5. hamming_weight
    test_result("5. hamming_weight(0xF0F0F0F0)", hamming_weight(0xF0F0F0F0), 16);

    // 6. reverse_bits
    test_result("6. reverse_bits(0xF0F0F0F0)", reverse_bits(0xF0F0F0F0), 0x0F0F0F0F);

    // 7. extract_bit_range
    test_result("7. extract_bit_range(0xF0F0,4,7)", extract_bit_range(0xF0F0, 4, 7), 0xF);

    // 8. set_bit_range
    test_result("8. set_bit_range(0x0000,4,7,0xF)", set_bit_range(0x0000, 4, 7, 0xF), 0xF0);

    // 9. hamming_distance
    test_result("9. hamming_distance(0b1011,0b1110)", hamming_distance(0b1011, 0b1110), 3);

    // 10. is_power_of_two
    test_result_bool("10. is_power_of_two(16)", is_power_of_two(16), true);
    test_result_bool("10. is_power_of_two(18)", is_power_of_two(18), false);

    // 11. swap_bytes16
    test_result("11. swap_bytes16(0x1234)", swap_bytes16(0x1234), 0x3412);

    // 12/13. write_mem32/read_mem32 (실제 하드웨어 환경에서만 테스트)
    // uint32_t addr = 0x20000000;
    // write_mem32(addr, 0x12345678);
    // test_result("13. read_mem32(addr)", read_mem32(addr), 0x12345678);

    // 14. set_bit5_all
    std::vector<uint32_t> arr = {0x00, 0x10, 0x20};
    set_bit5_all(arr);
    printf("14. set_bit5_all: arr = {0x%X, 0x%X, 0x%X} [PASS]\n", arr[0], arr[1], arr[2]);

    // 15. parity_even
    test_result_bool("15. parity_even(0b1011)", parity_even(0b1011), false);
    test_result_bool("15. parity_even(0b1010)", parity_even(0b1010), true);

    return 0;
}