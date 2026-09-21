#include <iostream>
#include <cassert>
#include <cstdint>
using namespace std;

// 비트 연산 관련 함수 선언
uint32_t set_bit(uint32_t val, int pos);         // pos번째 비트 1로 설정
uint32_t clear_bit(uint32_t val, int pos);       // pos번째 비트 0으로 설정
bool get_bit(uint32_t val, int pos);             // pos번째 비트 값 반환
uint32_t toggle_bit(uint32_t val, int pos);      // pos번째 비트 토글
int count_set_bits(uint32_t val);                // 1로 설정된 비트 개수
uint32_t swap_endian32(uint32_t val);            // 32비트 엔디안 변환
uint32_t reverse_bits32(uint32_t val);           // 32비트 비트 리버스

// TODO: 각 함수 구현

// 테스트 코드
int main() {
    // set_bit, clear_bit, get_bit, toggle_bit
    uint32_t v = 0b1010; // 10
    assert(set_bit(v, 1) == 0b1010);      // 이미 1
    assert(set_bit(v, 2) == 0b1110);      // 14
    assert(clear_bit(v, 3) == 0b0010);    // 2
    assert(get_bit(v, 3) == 1);
    assert(get_bit(v, 0) == 0);
    assert(toggle_bit(v, 1) == 0b1000);   // 8
    assert(toggle_bit(v, 0) == 0b1011);   // 11

    // count_set_bits
    assert(count_set_bits(0b10101010) == 4);
    assert(count_set_bits(0xFFFFFFFF) == 32);

    // swap_endian32
    assert(swap_endian32(0x12345678) == 0x78563412);

    // reverse_bits32
    assert(reverse_bits32(0b00000000000000000000000000000001) == 0x80000000);
    assert(reverse_bits32(0xF0F0F0F0) == 0x0F0F0F0F);

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- 비트 연산(set, clear, get, toggle), 비트 카운트, 엔디안 변환, 비트 리버스 함수를 직접 구현하라.
- main에서 위 함수들을 테스트하는 코드를 작성하라.
*/