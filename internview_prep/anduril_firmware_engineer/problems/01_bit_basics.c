/* =============================================================================
 * 01_bit_basics — 비트 조작 기초 (Bit Manipulation Basics)   [PRACTICE STUB]
 * -----------------------------------------------------------------------------
 * MCU 레지스터 조작(register manipulation)의 기본 어휘. 드론 펌웨어에서 GPIO,
 * 타이머, ADC/DAC, 통신 페리페럴(SPI/I2C/UART) 레지스터의 특정 비트를 켜고/끄고/
 * 읽는 일은 매 프레임 수백 번 일어난다. 여기 10개는 그 "손가락 근육".
 *
 * 사용법: 각 함수의 // TODO 를 채우고 재빌드하면 FAIL 이 PASS 로 바뀐다.
 * 빌드:   cc -std=c11 -Wall -Wextra problems/01_bit_basics.c -o /tmp/andb_bit_basics
 *
 * 참고: 스텁은 지금도 컴파일/실행되며 (placeholder 반환) 전부 FAIL 로 나온다.
 * =========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* -----------------------------------------------------------------------------
 * 1. set_bit — 특정 비트 설정(Set)
 *    Set bit `bit` of `value` to 1.
 *    Ex: set_bit(0x00, 3) -> 0x8      set_bit(0x00, 31) -> 0x80000000
 *    힌트: bit >= 32 일 때 (1u << bit) 는 UB — 방어 코드를 넣어라.
 * -------------------------------------------------------------------------- */
uint32_t set_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    (void)bit;
    return value;
}

/* -----------------------------------------------------------------------------
 * 2. clear_bit — 특정 비트 클리어(Clear)
 *    Clear bit `bit` of `value` to 0.
 *    Ex: clear_bit(0xFF, 4) -> 0xEF
 * -------------------------------------------------------------------------- */
uint32_t clear_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    (void)bit;
    return value;
}

/* -----------------------------------------------------------------------------
 * 3. toggle_bit — 특정 비트 토글(Toggle)
 *    Flip bit `bit` of `value`.
 *    Ex: toggle_bit(0x10, 4) -> 0x0       toggle_bit(0x00, 4) -> 0x10
 * -------------------------------------------------------------------------- */
uint32_t toggle_bit(uint32_t value, uint8_t bit) {
    // TODO: implement
    (void)bit;
    return value;
}

/* -----------------------------------------------------------------------------
 * 4. is_bit_set — 특정 비트가 1인지 확인(Test)
 *    Return true if bit `bit` of `value` is set.
 *    Ex: is_bit_set(0x10, 4) -> true      is_bit_set(0x10, 3) -> false
 * -------------------------------------------------------------------------- */
bool is_bit_set(uint32_t value, uint8_t bit) {
    // TODO: implement
    (void)value; (void)bit;
    return false;
}

/* -----------------------------------------------------------------------------
 * 5. swap_nibbles — 상위/하위 니블(nibble) 스왑
 *    Swap the upper and lower 4-bit nibbles of a byte.
 *    Ex: swap_nibbles(0xAB) -> 0xBA       swap_nibbles(0x0F) -> 0xF0
 * -------------------------------------------------------------------------- */
uint8_t swap_nibbles(uint8_t value) {
    // TODO: implement
    (void)value;
    return 0;
}

/* -----------------------------------------------------------------------------
 * 6. parity_even — 짝수 패리티 확인
 *    Return true if the number of set bits is EVEN (0 개도 짝수 -> true).
 *    Ex: parity_even(0x5) -> true (1 이 2개)   parity_even(0x7) -> false (3개)
 * -------------------------------------------------------------------------- */
bool parity_even(uint32_t value) {
    // TODO: implement
    (void)value;
    return false;
}

/* -----------------------------------------------------------------------------
 * 7. clear_lsb — 가장 낮은 설정 비트(LSB) 하나 클리어
 *    Clear the lowest set bit.
 *    Ex: clear_lsb(0x18) -> 0x10          clear_lsb(0x1) -> 0x0
 * -------------------------------------------------------------------------- */
uint32_t clear_lsb(uint32_t value) {
    // TODO: implement
    return value;
}

/* -----------------------------------------------------------------------------
 * 8. rightmost_set_bit — 가장 오른쪽 1비트만 남기기 (isolate lowest set bit)
 *    Ex: rightmost_set_bit(0x18) -> 0x8   rightmost_set_bit(0x80000000)->0x80000000
 * -------------------------------------------------------------------------- */
uint32_t rightmost_set_bit(uint32_t value) {
    // TODO: implement
    (void)value;
    return 0;
}

/* -----------------------------------------------------------------------------
 * 9. is_opposite_sign — 두 정수의 부호가 다른지 (분기 없이)
 *    Ex: is_opposite_sign(-5, 7) -> true   is_opposite_sign(0, -1) -> true
 * -------------------------------------------------------------------------- */
bool is_opposite_sign(int32_t a, int32_t b) {
    // TODO: implement
    (void)a; (void)b;
    return false;
}

/* -----------------------------------------------------------------------------
 * 10. abs_no_branch — 분기 없는 절대값
 *     Ex: abs_no_branch(-123) -> 123       abs_no_branch(123) -> 123
 *     주의: INT32_MIN 은 표현 불가 (오버플로).
 * -------------------------------------------------------------------------- */
int32_t abs_no_branch(int32_t value) {
    // TODO: implement
    (void)value;
    return 0;
}

/* ========================= Test harness ================================== */
#define LABEL_WIDTH 34
static int g_total = 0, g_pass = 0;

static void test_hex(const char* label, uint32_t result, uint32_t expected) {
    g_total++;
    printf("%-*s = 0x%X", LABEL_WIDTH, label, result);
    if (result == expected) { g_pass++; printf("  [PASS]\n"); }
    else                    { printf("  [FAIL] (expected 0x%X)\n", expected); }
}
static void test_int(const char* label, int32_t result, int32_t expected) {
    g_total++;
    printf("%-*s = %d", LABEL_WIDTH, label, result);
    if (result == expected) { g_pass++; printf("  [PASS]\n"); }
    else                    { printf("  [FAIL] (expected %d)\n", expected); }
}
static void test_bool(const char* label, bool result, bool expected) {
    g_total++;
    printf("%-*s = %d", LABEL_WIDTH, label, result);
    if (result == expected) { g_pass++; printf("  [PASS]\n"); }
    else                    { printf("  [FAIL] (expected %d)\n", expected); }
}

int main(void) {
    /* 1. set_bit */
    test_hex("1. set_bit(0x00, 3)",         set_bit(0x00, 3),          0x8);
    test_hex("1. set_bit(0xFF, 0)",         set_bit(0xFF, 0),          0xFF);        /* idempotent */
    test_hex("1. set_bit(0x00, 31)",        set_bit(0x00, 31),         0x80000000u); /* MSB */
    test_hex("1. set_bit(0x00, 32)",        set_bit(0x00, 32),         0x0);         /* guard: no-op */

    /* 2. clear_bit */
    test_hex("2. clear_bit(0xFF, 4)",       clear_bit(0xFF, 4),        0xEF);
    test_hex("2. clear_bit(0x00, 5)",       clear_bit(0x00, 5),        0x0);         /* already clear */
    test_hex("2. clear_bit(0x80000000,31)", clear_bit(0x80000000u, 31),0x0);
    test_hex("2. clear_bit(0xFF, 32)",      clear_bit(0xFF, 32),       0xFF);        /* guard */

    /* 3. toggle_bit */
    test_hex("3. toggle_bit(0x10, 4)",      toggle_bit(0x10, 4),       0x0);
    test_hex("3. toggle_bit(0x00, 4)",      toggle_bit(0x00, 4),       0x10);
    test_hex("3. toggle_bit(0x80000000,31)",toggle_bit(0x80000000u,31),0x0);
    test_hex("3. toggle_bit(0xFF, 32)",     toggle_bit(0xFF, 32),      0xFF);        /* guard */

    /* 4. is_bit_set */
    test_bool("4. is_bit_set(0x10, 4)",     is_bit_set(0x10, 4),       true);
    test_bool("4. is_bit_set(0x10, 3)",     is_bit_set(0x10, 3),       false);
    test_bool("4. is_bit_set(0x80000000,31)",is_bit_set(0x80000000u,31),true);
    test_bool("4. is_bit_set(0xFF, 32)",    is_bit_set(0xFF, 32),      false);       /* guard */

    /* 5. swap_nibbles */
    test_hex("5. swap_nibbles(0xAB)",       swap_nibbles(0xAB),        0xBA);
    test_hex("5. swap_nibbles(0x0F)",       swap_nibbles(0x0F),        0xF0);
    test_hex("5. swap_nibbles(0x00)",       swap_nibbles(0x00),        0x00);
    test_hex("5. swap_nibbles(0xFF)",       swap_nibbles(0xFF),        0xFF);

    /* 6. parity_even  (짝수 개의 1 -> true) */
    test_bool("6. parity_even(0x5)",        parity_even(0x5),          true);        /* 2 ones */
    test_bool("6. parity_even(0x7)",        parity_even(0x7),          false);       /* 3 ones */
    test_bool("6. parity_even(0x0)",        parity_even(0x0),          true);        /* 0 ones */
    test_bool("6. parity_even(0xFFFFFFFF)", parity_even(0xFFFFFFFFu),  true);        /* 32 ones */

    /* 7. clear_lsb */
    test_hex("7. clear_lsb(0x18)",          clear_lsb(0x18),           0x10);
    test_hex("7. clear_lsb(0x1)",           clear_lsb(0x1),            0x0);
    test_hex("7. clear_lsb(0x0)",           clear_lsb(0x0),            0x0);
    test_hex("7. clear_lsb(0xFFFFFFFF)",    clear_lsb(0xFFFFFFFFu),    0xFFFFFFFEu);

    /* 8. rightmost_set_bit */
    test_hex("8. rightmost_set_bit(0x18)",  rightmost_set_bit(0x18),   0x8);
    test_hex("8. rightmost_set_bit(0x1)",   rightmost_set_bit(0x1),    0x1);
    test_hex("8. rightmost_set_bit(0x0)",   rightmost_set_bit(0x0),    0x0);
    test_hex("8. rightmost_set_bit(0x80000000)", rightmost_set_bit(0x80000000u), 0x80000000u);

    /* 9. is_opposite_sign */
    test_bool("9. is_opposite_sign(-5, 7)", is_opposite_sign(-5, 7),   true);
    test_bool("9. is_opposite_sign(5, 7)",  is_opposite_sign(5, 7),    false);
    test_bool("9. is_opposite_sign(-5,-7)", is_opposite_sign(-5, -7),  false);
    test_bool("9. is_opposite_sign(0, -1)", is_opposite_sign(0, -1),   true);

    /* 10. abs_no_branch */
    test_int("10. abs_no_branch(-123)",     abs_no_branch(-123),       123);
    test_int("10. abs_no_branch(123)",      abs_no_branch(123),        123);
    test_int("10. abs_no_branch(0)",        abs_no_branch(0),          0);
    test_int("10. abs_no_branch(INT32_MAX)",abs_no_branch(2147483647), 2147483647);

    printf("\n==> %d/%d PASS\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
