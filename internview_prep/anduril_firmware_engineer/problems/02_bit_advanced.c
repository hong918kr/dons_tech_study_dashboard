// ============================================================================
// 02_bit_advanced.c  —  PRACTICE STUB (드릴용)
// 비트 조작 심화 (Bit Manipulation Advanced) / Q11-25
//
// 각 함수의 // TODO 를 채우고 재빌드 → [FAIL] 을 [PASS] 로 바꾸는 훈련.
// 빌드: cc -std=c11 -Wall -Wextra 02_bit_advanced.c -o /tmp/andb_bit_advanced
// 실행: /tmp/andb_bit_advanced
//
// Anduril FW 인터뷰 맥락: 레지스터 필드 조작, 프로토콜 바이트 순서(endianness),
// 엔코더용 gray code, popcount 등 드론 펌웨어의 저수준 비트 연산 전반.
// 핵심 함정: 시프트량 >= 폭(32) 은 UB → k>=32, width==32 반드시 가드.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// ----------------------------------------------------------------------------
// 11. reverseBits — 32비트 비트 순서 뒤집기 (MSB<->LSB)
//     예: reverseBits(0xF0F0F0F0) -> 0x0F0F0F0F, reverseBits(1) -> 0x80000000
// ----------------------------------------------------------------------------
uint32_t reverseBits(uint32_t n) {
    // TODO: implement (반복문 또는 SWAR 병렬 교환)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 12. hamming_weight — set bit 개수 (popcount)
//     예: hamming_weight(0xF0F0F0F0) -> 16, hamming_weight(0) -> 0
// ----------------------------------------------------------------------------
uint32_t hamming_weight(uint32_t n) {
    // TODO: implement (Kernighan: n &= n-1)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 13. is_power_of_two — 2의 거듭제곱 판정 (0 은 false)
//     예: is_power_of_two(16) -> true, is_power_of_two(18)/is_power_of_two(0) -> false
// ----------------------------------------------------------------------------
bool is_power_of_two(uint32_t n) {
    // TODO: implement (힌트: n && !(n & (n-1)))
    (void)n;
    return false;
}

// ----------------------------------------------------------------------------
// 14. swap_odd_even_bits — 홀수/짝수 위치 비트 스왑
//     예: swap_odd_even_bits(0xAAAAAAAA) -> 0x55555555
// ----------------------------------------------------------------------------
uint32_t swap_odd_even_bits(uint32_t n) {
    // TODO: implement (마스크 0xAAAAAAAA / 0x55555555)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 15. extract_bit_range — [start, end] 비트 추출 (end 포함)
//     예: extract_bit_range(0xF0F0, 4, 7) -> 0xF
// ----------------------------------------------------------------------------
uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    // TODO: implement (width==32 UB 가드 주의)
    (void)n; (void)start; (void)end;
    return 0;
}

// ----------------------------------------------------------------------------
// 16. set_bit_range — [start, end] 비트에 value 기록 (나머지는 보존)
//     예: set_bit_range(0x0000, 4, 7, 0xF) -> 0xF0
// ----------------------------------------------------------------------------
uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value) {
    // TODO: implement (clear 후 OR)
    (void)n; (void)start; (void)end; (void)value;
    return 0;
}

// ----------------------------------------------------------------------------
// 17. invert_bit_range — [start, end] 비트만 토글
//     예: invert_bit_range(0xF0F0, 4, 7) -> 0xF000
// ----------------------------------------------------------------------------
uint32_t invert_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    // TODO: implement (mask 만들어 XOR)
    (void)n; (void)start; (void)end;
    return 0;
}

// ----------------------------------------------------------------------------
// 18. next/prev_power_of_two — 가장 가까운 2의 거듭제곱 (올림/내림)
//     예: next_power_of_two(17) -> 32, prev_power_of_two(17) -> 16
//     경계: next(0)->1, next(1)->1, prev(0)->0, prev(1)->1
// ----------------------------------------------------------------------------
uint32_t next_power_of_two(uint32_t n) {
    // TODO: implement (n-- 후 n |= n>>1..>>16, +1)
    (void)n;
    return 0;
}
uint32_t prev_power_of_two(uint32_t n) {
    // TODO: implement (최상위 set bit만 남기기)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 19. xor_swap — 임시변수 없이 스왑 (XOR trick)
//     예: a=5,b=7 -> xor_swap(&a,&b) -> a=7,b=5
//     함정: 같은 주소(a==b)면 0이 됨 → 반드시 가드
// ----------------------------------------------------------------------------
void xor_swap(uint32_t *a, uint32_t *b) {
    // TODO: implement (NULL / a==b 가드 후 3회 XOR)
    (void)a; (void)b;
}

// ----------------------------------------------------------------------------
// 20. rotate_left / rotate_right — 32비트 비트 회전
//     예: rotate_left(0x80000001,1) -> 0x00000003
//         rotate_right(0x80000001,1) -> 0xC0000000
//     함정: k>=32 또는 k==0 에서 (n >> (32-k)) 는 UB → 반드시 가드
// ----------------------------------------------------------------------------
uint32_t rotate_left(uint32_t n, uint8_t k) {
    // TODO: implement (k &= 31; k==0 가드)
    (void)n; (void)k;
    return 0;
}
uint32_t rotate_right(uint32_t n, uint8_t k) {
    // TODO: implement
    (void)n; (void)k;
    return 0;
}

// ----------------------------------------------------------------------------
// 21. swap_endian32 — 바이트 순서 반전 (network<->host)
//     예: swap_endian32(0x12345678) -> 0x78563412
// ----------------------------------------------------------------------------
uint32_t swap_endian32(uint32_t n) {
    // TODO: implement (4바이트 위치 교환)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 22. get_field / set_field — 레지스터 비트필드 read/write
//     예: get_field(0xF0F0,4,4) -> 0xF, set_field(0x0000,4,4,0xF) -> 0xF0
// ----------------------------------------------------------------------------
uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width) {
    // TODO: implement ((reg >> pos) & fieldmask)
    (void)reg; (void)pos; (void)width;
    return 0;
}
uint32_t set_field(uint32_t reg, uint8_t pos, uint8_t width, uint32_t value) {
    // TODO: implement (clear 후 value 삽입)
    (void)reg; (void)pos; (void)width; (void)value;
    return 0;
}

// ----------------------------------------------------------------------------
// 23. binary_to_gray — 이진수 -> 그레이 코드
//     예: binary_to_gray(0b1011) -> 0b1110
// ----------------------------------------------------------------------------
uint32_t binary_to_gray(uint32_t n) {
    // TODO: implement (n ^ (n>>1))
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 24. gray_to_binary — 그레이 코드 -> 이진수
//     예: gray_to_binary(0b1110) -> 0b1011
// ----------------------------------------------------------------------------
uint32_t gray_to_binary(uint32_t n) {
    // TODO: implement (상위비트부터 XOR 누적)
    (void)n;
    return 0;
}

// ----------------------------------------------------------------------------
// 25. mul_pow2 / div_pow2 — 2의 거듭제곱 곱셈/나눗셈을 시프트로
//     예: mul_pow2(3,2) -> 12, div_pow2(12,2) -> 3
// ----------------------------------------------------------------------------
uint32_t mul_pow2(uint32_t n, uint8_t k) {
    // TODO: implement (k>=32 가드)
    (void)n; (void)k;
    return 0;
}
uint32_t div_pow2(uint32_t n, uint8_t k) {
    // TODO: implement
    (void)n; (void)k;
    return 0;
}

// ============================================================================
// --- Test harness (수정하지 말 것) ---
// ============================================================================
#define LABEL_WIDTH 42
static int g_pass = 0, g_fail = 0;

static void test_result(const char* label, uint32_t result, uint32_t expected) {
    printf("%-*s = 0x%08X", LABEL_WIDTH, label, result);
    if (result == expected) { printf(" [PASS]\n"); g_pass++; }
    else { printf(" [FAIL] (expected 0x%08X)\n", expected); g_fail++; }
}
static void test_result_bool(const char* label, bool result, bool expected) {
    printf("%-*s = %d", LABEL_WIDTH, label, result);
    if (result == expected) { printf(" [PASS]\n"); g_pass++; }
    else { printf(" [FAIL] (expected %d)\n", expected); g_fail++; }
}

int main(void) {
    // 11. reverseBits
    test_result("11. reverseBits(0xF0F0F0F0)", reverseBits(0xF0F0F0F0), 0x0F0F0F0F);
    test_result("11. reverseBits(1)", reverseBits(1), 0x80000000);
    test_result("11. reverseBits(0)", reverseBits(0), 0);
    test_result("11. reverseBits(0xFFFFFFFF)", reverseBits(0xFFFFFFFF), 0xFFFFFFFF);

    // 12. hamming_weight
    test_result("12. hamming_weight(0xF0F0F0F0)", hamming_weight(0xF0F0F0F0), 16);
    test_result("12. hamming_weight(0)", hamming_weight(0), 0);
    test_result("12. hamming_weight(0xFFFFFFFF)", hamming_weight(0xFFFFFFFF), 32);

    // 13. is_power_of_two
    test_result_bool("13. is_power_of_two(16)", is_power_of_two(16), 1);
    test_result_bool("13. is_power_of_two(18)", is_power_of_two(18), 0);
    test_result_bool("13. is_power_of_two(0)", is_power_of_two(0), 0);
    test_result_bool("13. is_power_of_two(1)", is_power_of_two(1), 1);
    test_result_bool("13. is_power_of_two(0x80000000)", is_power_of_two(0x80000000), 1);

    // 14. swap_odd_even_bits
    test_result("14. swap_odd_even_bits(0xAAAAAAAA)", swap_odd_even_bits(0xAAAAAAAA), 0x55555555);
    test_result("14. swap_odd_even_bits(0x55555555)", swap_odd_even_bits(0x55555555), 0xAAAAAAAA);

    // 15. extract_bit_range
    test_result("15. extract_bit_range(0xF0F0,4,7)", extract_bit_range(0xF0F0, 4, 7), 0xF);
    test_result("15. extract_bit_range(0x12345678,0,31)", extract_bit_range(0x12345678, 0, 31), 0x12345678);
    test_result("15. extract_bit_range(0xDEADBEEF,8,15)", extract_bit_range(0xDEADBEEF, 8, 15), 0xBE);

    // 16. set_bit_range
    test_result("16. set_bit_range(0x0000,4,7,0xF)", set_bit_range(0x0000, 4, 7, 0xF), 0xF0);
    test_result("16. set_bit_range(0xFFFF,4,7,0x0)", set_bit_range(0xFFFF, 4, 7, 0x0), 0xFF0F);
    test_result("16. set_bit_range(0x00,0,31,0xDEADBEEF)", set_bit_range(0x00, 0, 31, 0xDEADBEEF), 0xDEADBEEF);

    // 17. invert_bit_range
    test_result("17. invert_bit_range(0xF0F0,4,7)", invert_bit_range(0xF0F0, 4, 7), 0xF000);
    test_result("17. invert_bit_range(0x0,0,31)", invert_bit_range(0x0, 0, 31), 0xFFFFFFFF);

    // 18. next/prev power of two
    test_result("18. next_power_of_two(17)", next_power_of_two(17), 32);
    test_result("18. next_power_of_two(0)", next_power_of_two(0), 1);
    test_result("18. next_power_of_two(1)", next_power_of_two(1), 1);
    test_result("18. next_power_of_two(16)", next_power_of_two(16), 16);
    test_result("18. prev_power_of_two(17)", prev_power_of_two(17), 16);
    test_result("18. prev_power_of_two(1)", prev_power_of_two(1), 1);
    test_result("18. prev_power_of_two(0)", prev_power_of_two(0), 0);
    test_result("18. prev_power_of_two(0xFFFFFFFF)", prev_power_of_two(0xFFFFFFFF), 0x80000000);

    // 19. xor_swap
    uint32_t a = 5, b = 7;
    xor_swap(&a, &b);
    printf("%-*s = a=%u, b=%u", LABEL_WIDTH, "19. xor_swap(&a=5, &b=7)", a, b);
    if (a == 7 && b == 5) { printf(" [PASS]\n"); g_pass++; }
    else { printf(" [FAIL] (expected a=7, b=5)\n"); g_fail++; }
    uint32_t same = 42;
    xor_swap(&same, &same);   // 같은 주소 가드 확인
    printf("%-*s = %u", LABEL_WIDTH, "19. xor_swap(&x,&x) guard", same);
    if (same == 42) { printf(" [PASS]\n"); g_pass++; }
    else { printf(" [FAIL] (expected 42)\n"); g_fail++; }

    // 20. rotate
    test_result("20. rotate_left(0x80000001,1)", rotate_left(0x80000001, 1), 0x00000003);
    test_result("20. rotate_right(0x80000001,1)", rotate_right(0x80000001, 1), 0xC0000000);
    test_result("20. rotate_left(0x12345678,0)", rotate_left(0x12345678, 0), 0x12345678);
    test_result("20. rotate_left(0x12345678,32)", rotate_left(0x12345678, 32), 0x12345678);
    test_result("20. rotate_right(0x12345678,4)", rotate_right(0x12345678, 4), 0x81234567);

    // 21. swap_endian32
    test_result("21. swap_endian32(0x12345678)", swap_endian32(0x12345678), 0x78563412);
    test_result("21. swap_endian32(0x000000FF)", swap_endian32(0x000000FF), 0xFF000000);

    // 22. bit-field
    test_result("22. get_field(0xF0F0,4,4)", get_field(0xF0F0, 4, 4), 0xF);
    test_result("22. set_field(0x0000,4,4,0xF)", set_field(0x0000, 4, 4, 0xF), 0xF0);
    test_result("22. set_field(0xFF00,0,8,0x5A)", set_field(0xFF00, 0, 8, 0x5A), 0xFF5A);
    test_result("22. get_field(0xDEADBEEF,16,16)", get_field(0xDEADBEEF, 16, 16), 0xDEAD);

    // 23/24. gray code
    test_result("23. binary_to_gray(0b1011)", binary_to_gray(0x0B), 0x0E);
    test_result("24. gray_to_binary(0b1110)", gray_to_binary(0x0E), 0x0B);
    test_result("23/24. roundtrip(0xABCD)", gray_to_binary(binary_to_gray(0xABCD)), 0xABCD);

    // 25. shift mul/div
    test_result("25. mul_pow2(3,2)", mul_pow2(3, 2), 12);
    test_result("25. div_pow2(12,2)", div_pow2(12, 2), 3);
    test_result("25. mul_pow2(1,31)", mul_pow2(1, 31), 0x80000000);

    printf("\n==== %d PASS, %d FAIL ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
