// ============================================================================
// 02_bit_advanced.c  —  REFERENCE SOLUTION
// 비트 조작 심화 (Bit Manipulation Advanced) / Q11-25
//
// 빌드: cc -std=c11 -Wall -Wextra 02_bit_advanced.c -o /tmp/andb_bit_advanced
// 실행: /tmp/andb_bit_advanced   (모든 테스트가 [PASS] 여야 함)
//
// Anduril FW 인터뷰 맥락: 레지스터 필드 조작, 프로토콜 바이트 순서(endianness),
// 엔코더용 gray code, popcount 기반 상태 카운팅 등 드론/무기체계 펌웨어의
// 저수준 비트 연산 전반을 다룬다.
// ============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// ----------------------------------------------------------------------------
// 11. reverseBits — 32비트 비트 순서 뒤집기 (MSB<->LSB)
//     Reverse the bit order of a 32-bit word.
//     예: reverseBits(0xF0F0F0F0) -> 0x0F0F0F0F, reverseBits(1) -> 0x80000000
// ----------------------------------------------------------------------------
uint32_t reverseBits(uint32_t n) {
    // SWAR(병렬 비트 교환): 1-2-4-8-16 단위로 그룹을 맞바꾼다. 반복문 없이 O(1).
    n = ((n >> 1)  & 0x55555555u) | ((n & 0x55555555u) << 1);
    n = ((n >> 2)  & 0x33333333u) | ((n & 0x33333333u) << 2);
    n = ((n >> 4)  & 0x0F0F0F0Fu) | ((n & 0x0F0F0F0Fu) << 4);
    n = ((n >> 8)  & 0x00FF00FFu) | ((n & 0x00FF00FFu) << 8);
    n = (n >> 16) | (n << 16);
    return n;
}

// ----------------------------------------------------------------------------
// 12. hamming_weight — set bit 개수 (popcount)
//     Count the number of set bits (Hamming weight).
//     예: hamming_weight(0xF0F0F0F0) -> 16, hamming_weight(0) -> 0
// ----------------------------------------------------------------------------
uint32_t hamming_weight(uint32_t n) {
    // Kernighan: n &= n-1 은 최하위 1비트를 하나 지운다 → 1의 개수만큼만 반복.
    uint32_t count = 0;
    while (n) {
        n &= (n - 1);
        count++;
    }
    return count;
}

// ----------------------------------------------------------------------------
// 13. is_power_of_two — 2의 거듭제곱 판정
//     예: is_power_of_two(16) -> true, is_power_of_two(18)/is_power_of_two(0) -> false
// ----------------------------------------------------------------------------
bool is_power_of_two(uint32_t n) {
    // 2^k 는 비트가 정확히 1개. n & (n-1) 로 최하위 1비트를 지우면 0이 되어야 함.
    // n != 0 조건으로 0을 걸러낸다.
    return n && !(n & (n - 1));
}

// ----------------------------------------------------------------------------
// 14. swap_odd_even_bits — 홀수/짝수 위치 비트 스왑
//     예: swap_odd_even_bits(0xAAAAAAAA) -> 0x55555555
// ----------------------------------------------------------------------------
uint32_t swap_odd_even_bits(uint32_t n) {
    // 짝수 비트(0xAAAA...는 홀수 위치 1)를 오른쪽으로, 홀수 위치를 왼쪽으로.
    return ((n & 0xAAAAAAAAu) >> 1) | ((n & 0x55555555u) << 1);
}

// ----------------------------------------------------------------------------
// 15. extract_bit_range — [start, end] 비트 추출 (end 포함)
//     예: extract_bit_range(0xF0F0, 4, 7) -> 0xF
// ----------------------------------------------------------------------------
uint32_t extract_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    uint8_t width = (uint8_t)(end - start + 1);
    // width==32 이면 (1u<<32) 는 UB → 32비트 전체 마스크를 분기로 처리.
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : (((1u << width) - 1u) << start);
    return (n & mask) >> start;
}

// ----------------------------------------------------------------------------
// 16. set_bit_range — [start, end] 비트에 value 기록 (나머지는 보존)
//     예: set_bit_range(0x0000, 4, 7, 0xF) -> 0xF0
// ----------------------------------------------------------------------------
uint32_t set_bit_range(uint32_t n, uint8_t start, uint8_t end, uint32_t value) {
    uint8_t width = (uint8_t)(end - start + 1);
    uint32_t field = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    uint32_t mask  = field << start;
    // 기존 비트 clear 후, value 를 자리로 이동시켜 OR (value 초과 비트는 mask로 잘림).
    return (n & ~mask) | ((value << start) & mask);
}

// ----------------------------------------------------------------------------
// 17. invert_bit_range — [start, end] 비트만 토글
//     예: invert_bit_range(0xF0F0, 4, 7) -> 0xF000
// ----------------------------------------------------------------------------
uint32_t invert_bit_range(uint32_t n, uint8_t start, uint8_t end) {
    uint8_t width = (uint8_t)(end - start + 1);
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : (((1u << width) - 1u) << start);
    return n ^ mask;   // XOR 로 해당 범위만 반전
}

// ----------------------------------------------------------------------------
// 18. next/prev_power_of_two — 가장 가까운 2의 거듭제곱 (올림/내림)
//     예: next_power_of_two(17) -> 32, prev_power_of_two(17) -> 16
//     경계: next(0)->1, next(1)->1, prev(0)->0, prev(1)->1
// ----------------------------------------------------------------------------
uint32_t next_power_of_two(uint32_t n) {
    // n 이하의 모든 하위 비트를 1로 채운 뒤 +1 → 바로 위(또는 자신)의 2의 거듭제곱.
    // 주의: n > 2^31 이면 결과가 32비트를 넘어 0 으로 오버플로(호출측 책임).
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
    // 최상위 set bit만 남긴다. 하위비트를 다 채운 뒤 (n - n>>1).
    // 곱셈 루프(p<<=1) 방식은 n>=2^31 에서 오버플로/무한루프 → 비트 채움이 안전.
    if (n == 0) return 0;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n - (n >> 1);
}

// ----------------------------------------------------------------------------
// 19. xor_swap — 임시변수 없이 스왑 (XOR trick)
//     예: a=5,b=7 -> xor_swap(&a,&b) -> a=7,b=5
//     함정: 같은 주소(a==b)면 0이 되어버림 → 반드시 가드.
// ----------------------------------------------------------------------------
void xor_swap(uint32_t *a, uint32_t *b) {
    if (a == NULL || b == NULL || a == b) return;
    *a ^= *b;
    *b ^= *a;   // = (a^b)^b = a
    *a ^= *b;   // = (a^b)^a = b
}

// ----------------------------------------------------------------------------
// 20. rotate_left / rotate_right — 32비트 비트 회전
//     예: rotate_left(0x80000001,1) -> 0x00000003
//         rotate_right(0x80000001,1) -> 0xC0000000
//     함정: k>=32 또는 k==0 에서 (n >> (32-k)) 는 shift>=width → UB. 반드시 가드.
// ----------------------------------------------------------------------------
uint32_t rotate_left(uint32_t n, uint8_t k) {
    k &= 31u;                 // 회전량은 mod 32
    if (k == 0) return n;     // 32-0=32 시프트(UB) 회피
    return (n << k) | (n >> (32 - k));
}
uint32_t rotate_right(uint32_t n, uint8_t k) {
    k &= 31u;
    if (k == 0) return n;
    return (n >> k) | (n << (32 - k));
}

// ----------------------------------------------------------------------------
// 21. swap_endian32 — 바이트 순서 반전 (프로토콜 network<->host)
//     예: swap_endian32(0x12345678) -> 0x78563412
// ----------------------------------------------------------------------------
uint32_t swap_endian32(uint32_t n) {
    return ((n >> 24) & 0x000000FFu) |
           ((n >> 8)  & 0x0000FF00u) |
           ((n << 8)  & 0x00FF0000u) |
           ((n << 24) & 0xFF000000u);
}

// ----------------------------------------------------------------------------
// 22. get_field / set_field — 레지스터 비트필드 read/write
//     예: get_field(0xF0F0,4,4) -> 0xF, set_field(0x0000,4,4,0xF) -> 0xF0
// ----------------------------------------------------------------------------
uint32_t get_field(uint32_t reg, uint8_t pos, uint8_t width) {
    uint32_t fmask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (reg >> pos) & fmask;
}
uint32_t set_field(uint32_t reg, uint8_t pos, uint8_t width, uint32_t value) {
    uint32_t fmask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    uint32_t mask  = fmask << pos;
    return (reg & ~mask) | ((value << pos) & mask);
}

// ----------------------------------------------------------------------------
// 23. binary_to_gray — 이진수 -> 그레이 코드
//     예: binary_to_gray(0b1011) -> 0b1110
// ----------------------------------------------------------------------------
uint32_t binary_to_gray(uint32_t n) {
    return n ^ (n >> 1);
}

// ----------------------------------------------------------------------------
// 24. gray_to_binary — 그레이 코드 -> 이진수
//     예: gray_to_binary(0b1110) -> 0b1011
// ----------------------------------------------------------------------------
uint32_t gray_to_binary(uint32_t n) {
    uint32_t res = n;
    while (n >>= 1) res ^= n;   // 상위비트부터 XOR 누적
    return res;
}

// ----------------------------------------------------------------------------
// 25. mul_pow2 / div_pow2 — 2의 거듭제곱 곱셈/나눗셈을 시프트로
//     예: mul_pow2(3,2) -> 12, div_pow2(12,2) -> 3
//     주의: 부호없는 값에만 안전. 음수(산술 시프트)나 오버플로는 별개 이슈.
// ----------------------------------------------------------------------------
uint32_t mul_pow2(uint32_t n, uint8_t k) {
    return (k >= 32) ? 0u : (n << k);   // k>=32 가드
}
uint32_t div_pow2(uint32_t n, uint8_t k) {
    return (k >= 32) ? 0u : (n >> k);
}

// ============================================================================
// --- Test harness ---
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
