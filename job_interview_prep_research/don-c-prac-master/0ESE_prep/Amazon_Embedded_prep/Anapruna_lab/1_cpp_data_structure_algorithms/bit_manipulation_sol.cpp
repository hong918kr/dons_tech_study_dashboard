#include <iostream>
#include <cassert>
#include <cstdint>
using namespace std;

// 비트 연산 관련 함수 구현
uint32_t set_bit(uint32_t val, int pos) {
    return val | (1U << pos);
}

uint32_t clear_bit(uint32_t val, int pos) {
    return val & ~(1U << pos);
}

bool get_bit(uint32_t val, int pos) {
    return (val >> pos) & 1U;
}

uint32_t toggle_bit(uint32_t val, int pos) {
    return val ^ (1U << pos);
}

int count_set_bits(uint32_t val) {
    int cnt = 0;
    while (val) {
        cnt += val & 1U;
        val >>= 1;
    }
    return cnt;
}

uint32_t swap_endian32(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
}

uint32_t reverse_bits32(uint32_t val) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res <<= 1;
        res |= (val & 1U);
        val >>= 1;
    }
    return res;
}

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