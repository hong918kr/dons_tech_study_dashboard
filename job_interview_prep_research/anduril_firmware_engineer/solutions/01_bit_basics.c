/* =============================================================================
 * 01_bit_basics — 비트 조작 기초 (Bit Manipulation Basics)   [SOLUTION]
 * -----------------------------------------------------------------------------
 * MCU 레지스터 조작(register manipulation)의 기본 어휘. 드론 펌웨어에서 GPIO,
 * 타이머, ADC/DAC, 통신 페리페럴(SPI/I2C/UART) 레지스터의 특정 비트를 켜고/끄고/
 * 읽는 일은 매 프레임 수백 번 일어난다. 여기 10개는 그 "손가락 근육".
 *
 * 빌드: cc -std=c11 -Wall -Wextra solutions/01_bit_basics.c -o /tmp/andb_bit_basics
 * =========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* -----------------------------------------------------------------------------
 * 1. set_bit — 특정 비트 설정(Set)
 *    Set bit `bit` of `value` to 1.
 *    Ex: set_bit(0x00, 3) -> 0x8      set_bit(0x00, 31) -> 0x80000000
 * -------------------------------------------------------------------------- */
uint32_t set_bit(uint32_t value, uint8_t bit) {
    /* shift 폭이 타입 폭(32) 이상이면 UB. MCU 코드에선 방어적으로 no-op 처리. */
    if (bit >= 32u) return value;
    return value | (1u << bit);            /* OR 로 해당 비트만 1 */
}

/* -----------------------------------------------------------------------------
 * 2. clear_bit — 특정 비트 클리어(Clear)
 *    Clear bit `bit` of `value` to 0.
 *    Ex: clear_bit(0xFF, 4) -> 0xEF
 * -------------------------------------------------------------------------- */
uint32_t clear_bit(uint32_t value, uint8_t bit) {
    if (bit >= 32u) return value;
    return value & ~(1u << bit);           /* 반전 마스크로 AND -> 그 비트만 0 */
}

/* -----------------------------------------------------------------------------
 * 3. toggle_bit — 특정 비트 토글(Toggle)
 *    Flip bit `bit` of `value`.
 *    Ex: toggle_bit(0x10, 4) -> 0x0       toggle_bit(0x00, 4) -> 0x10
 * -------------------------------------------------------------------------- */
uint32_t toggle_bit(uint32_t value, uint8_t bit) {
    if (bit >= 32u) return value;
    return value ^ (1u << bit);            /* XOR 로 상태 반전 */
}

/* -----------------------------------------------------------------------------
 * 4. is_bit_set — 특정 비트가 1인지 확인(Test)
 *    Return true if bit `bit` of `value` is set.
 *    Ex: is_bit_set(0x10, 4) -> true      is_bit_set(0x10, 3) -> false
 * -------------------------------------------------------------------------- */
bool is_bit_set(uint32_t value, uint8_t bit) {
    if (bit >= 32u) return false;
    return (value & (1u << bit)) != 0u;    /* !=0 으로 명시적 bool 변환 */
}

/* -----------------------------------------------------------------------------
 * 5. swap_nibbles — 상위/하위 니블(nibble) 스왑
 *    Swap the upper and lower 4-bit nibbles of a byte.
 *    Ex: swap_nibbles(0xAB) -> 0xBA       swap_nibbles(0x0F) -> 0xF0
 * -------------------------------------------------------------------------- */
uint8_t swap_nibbles(uint8_t value) {
    /* (uint8_t) 캐스트로 int 승격 후 상위 비트 잘림을 명시. */
    return (uint8_t)((value << 4) | (value >> 4));
}

/* -----------------------------------------------------------------------------
 * 6. parity_even — 짝수 패리티 확인
 *    Return true if the number of set bits is EVEN (0 개도 짝수 -> true).
 *    Ex: parity_even(0x5) -> true (1 이 2개)   parity_even(0x7) -> false (3개)
 * -------------------------------------------------------------------------- */
bool parity_even(uint32_t value) {
    /* XOR-folding: 절반씩 접어 O(log n) 에 1의 개수 홀짝을 최하위 비트에 모은다. */
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1u) == 0u;             /* 최하위 비트=1 이면 1의 개수 홀수 */
}

/* -----------------------------------------------------------------------------
 * 7. clear_lsb — 가장 낮은 설정 비트(LSB) 하나 클리어
 *    Clear the lowest set bit.  value & (value-1)
 *    Ex: clear_lsb(0x18) -> 0x10          clear_lsb(0x1) -> 0x0
 * -------------------------------------------------------------------------- */
uint32_t clear_lsb(uint32_t value) {
    /* value-1 은 최하위 1을 0으로, 그 아래를 1로 바꿔 AND 시 그 자리만 지운다.
       value==0 이면 0u-1=0xFFFFFFFF, 0 & 0xFFFFFFFF = 0 으로 안전. */
    return value & (value - 1u);
}

/* -----------------------------------------------------------------------------
 * 8. rightmost_set_bit — 가장 오른쪽 1비트만 남기기 (isolate lowest set bit)
 *    Ex: rightmost_set_bit(0x18) -> 0x8   rightmost_set_bit(0x80000000)->0x80000000
 * -------------------------------------------------------------------------- */
uint32_t rightmost_set_bit(uint32_t value) {
    /* -value == ~value+1 (2의 보수). unsigned 부정은 well-defined (wrap). */
    return value & (uint32_t)(0u - value);
}

/* -----------------------------------------------------------------------------
 * 9. is_opposite_sign — 두 정수의 부호가 다른지 (분기 없이)
 *    Ex: is_opposite_sign(-5, 7) -> true   is_opposite_sign(0, -1) -> true
 * -------------------------------------------------------------------------- */
bool is_opposite_sign(int32_t a, int32_t b) {
    /* 부호가 다르면 XOR 의 부호비트(MSB)=1 -> 음수. 0 은 양수 취급(부호비트 0). */
    return (a ^ b) < 0;
}

/* -----------------------------------------------------------------------------
 * 10. abs_no_branch — 분기 없는 절대값
 *     Ex: abs_no_branch(-123) -> 123       abs_no_branch(123) -> 123
 *     주의: INT32_MIN 은 표현 불가 (오버플로) — 결과는 그대로 INT32_MIN.
 * -------------------------------------------------------------------------- */
int32_t abs_no_branch(int32_t value) {
    uint32_t uv   = (uint32_t)value;
    uint32_t mask = 0u - (uv >> 31);       /* 음수면 0xFFFFFFFF, 양수면 0 (부호 UB 회피) */
    /* 전 과정을 unsigned 로 계산해 signed overflow UB 회피, 마지막에 캐스트. */
    return (int32_t)((uv ^ mask) - mask);
}

/* =============================================================================
 * [추가] 11~17 — 범위 마스크 (Bit-range masks)   ★ 인터뷰 빈출
 * -----------------------------------------------------------------------------
 * "lo 번째 비트부터 hi 번째 비트까지 세우고/끄고/뒤집고/읽고/쓰기".
 * 레지스터의 한 필드(예: bit[7:4] = 클럭 분주비)를 다루는 실제 드라이버 코드 그 자체다.
 * 규칙: 비트 번호는 0부터, 범위는 [lo, hi] 양 끝 포함(inclusive), lo <= hi <= 31.
 *       범위가 잘못되면(lo > hi 또는 hi > 31) value 를 그대로 반환(마스크는 0).
 * 번호 11~17 은 이 세트 안에서만 쓰는 번호다 (02_bit_advanced 의 Q11~ 과 별개).
 * =========================================================================== */

/* -----------------------------------------------------------------------------
 * 11. range_mask — [lo, hi] 비트만 1인 마스크 만들기
 *     Build a mask with bits lo..hi (inclusive) set.
 *     Ex: range_mask(4, 7) -> 0xF0     range_mask(0, 31) -> 0xFFFFFFFF
 *         range_mask(5, 3) -> 0x0 (잘못된 범위)
 * -------------------------------------------------------------------------- */
uint32_t range_mask(uint8_t lo, uint8_t hi) {
    if (hi > 31u || lo > hi) return 0u;
    /* 함정: width==32 일 때 (1u << 32) - 1 은 UB.
       해법 A: width 로 분기.   해법 B(아래): 양쪽에서 잘라내기 — 시프트 폭이 항상 0..31.
         ~0u >> (31 - hi)  : hi 위쪽 비트를 0 으로
         ~0u << lo         : lo 아래쪽 비트를 0 으로
       두 개를 AND 하면 [lo, hi] 만 남는다. */
    return (~0u >> (31u - hi)) & (~0u << lo);
}

/* -----------------------------------------------------------------------------
 * 12. set_bits_range — [lo, hi] 비트 전부 1로
 *     Ex: set_bits_range(0x00, 4, 7) -> 0xF0     set_bits_range(0x0F, 2, 5) -> 0x3F
 * -------------------------------------------------------------------------- */
uint32_t set_bits_range(uint32_t value, uint8_t lo, uint8_t hi) {
    return value | range_mask(lo, hi);     /* 잘못된 범위면 mask=0 -> value 그대로 */
}

/* -----------------------------------------------------------------------------
 * 13. clear_bits_range — [lo, hi] 비트 전부 0으로
 *     Ex: clear_bits_range(0xFFFFFFFF, 8, 15) -> 0xFFFF00FF
 * -------------------------------------------------------------------------- */
uint32_t clear_bits_range(uint32_t value, uint8_t lo, uint8_t hi) {
    return value & ~range_mask(lo, hi);    /* 잘못된 범위면 ~0 과 AND -> value 그대로 */
}

/* -----------------------------------------------------------------------------
 * 14. toggle_bits_range — [lo, hi] 비트 전부 반전
 *     Ex: toggle_bits_range(0xF0, 0, 7) -> 0x0F     toggle_bits_range(0xAAAA, 4, 11) -> 0xA55A
 * -------------------------------------------------------------------------- */
uint32_t toggle_bits_range(uint32_t value, uint8_t lo, uint8_t hi) {
    return value ^ range_mask(lo, hi);
}

/* -----------------------------------------------------------------------------
 * 15. get_bits_range — [lo, hi] 필드 값을 읽어 0번 비트부터 정렬해 반환
 *     Read the field bits lo..hi, right-aligned.
 *     Ex: get_bits_range(0xABCD, 4, 11) -> 0xBC     get_bits_range(0xDEADBEEF, 16, 23) -> 0xAD
 * -------------------------------------------------------------------------- */
uint32_t get_bits_range(uint32_t value, uint8_t lo, uint8_t hi) {
    if (hi > 31u || lo > hi) return 0u;
    return (value & range_mask(lo, hi)) >> lo;   /* 마스크로 잘라낸 뒤 아래로 내린다 */
}

/* -----------------------------------------------------------------------------
 * 16. set_field — [lo, hi] 필드에 새 값 쓰기 (read-modify-write)
 *     Write `field` into bits lo..hi, leaving every other bit untouched.
 *     field 가 필드 폭보다 크면 넘치는 비트는 잘라낸다.
 *     Ex: set_field(0xFFFF, 4, 7, 0x0) -> 0xFF0F
 *         set_field(0x1234, 8, 11, 0x1F) -> 0x1F34   (0x1F -> 4비트 0xF 로 잘림)
 * -------------------------------------------------------------------------- */
uint32_t set_field(uint32_t value, uint8_t lo, uint8_t hi, uint32_t field) {
    uint32_t mask = range_mask(lo, hi);
    if (mask == 0u) return value;
    /* ① 필드 자리를 0으로 비우고  ② 새 값을 lo 만큼 올린 뒤 마스크로 잘라 OR.
       (field << lo) & mask 순서가 중요: 넘치는 상위 비트가 이웃 필드를 오염시키지 않는다. */
    return (value & ~mask) | ((field << lo) & mask);
}

/* -----------------------------------------------------------------------------
 * 17. set_bits_n — pos 번째부터 count 개 비트를 1로 (위치+개수 표기)
 *     Set `count` bits starting at bit `pos`.  (K&R getbits 스타일 표기)
 *     Ex: set_bits_n(0x0, 4, 4) -> 0xF0     set_bits_n(0x0, 0, 32) -> 0xFFFFFFFF
 *         set_bits_n(0x1, 30, 4) -> 0x1 (31번을 넘어감 -> 잘못된 범위)
 * -------------------------------------------------------------------------- */
uint32_t set_bits_n(uint32_t value, uint8_t pos, uint8_t count) {
    if (count == 0u || pos > 31u || count > 32u - pos) return value;
    /* 위치+개수 -> [lo, hi] 로 바꾸면 11번 문제로 환원된다: hi = pos + count - 1 */
    return value | range_mask(pos, (uint8_t)(pos + count - 1u));
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


    /* ---------------- [추가] 11~17 범위 마스크 ---------------- */
    /* 11. range_mask */
    test_hex("11. range_mask(0, 3)",        range_mask(0, 3),          0xF);
    test_hex("11. range_mask(4, 7)",        range_mask(4, 7),          0xF0);
    test_hex("11. range_mask(0, 31)",       range_mask(0, 31),         0xFFFFFFFFu); /* width 32: UB 함정 */
    test_hex("11. range_mask(31, 31)",      range_mask(31, 31),        0x80000000u);
    test_hex("11. range_mask(5, 3)",        range_mask(5, 3),          0x0);         /* lo > hi */

    /* 12. set_bits_range */
    test_hex("12. set_bits_range(0x0,4,7)", set_bits_range(0x0, 4, 7), 0xF0);
    test_hex("12. set_bits_range(0xF,2,5)", set_bits_range(0xF, 2, 5), 0x3F);
    test_hex("12. set_bits_range(0x0,0,31)",set_bits_range(0x0, 0, 31),0xFFFFFFFFu);
    test_hex("12. set_bits_range(0x1,8,40)",set_bits_range(0x1, 8, 40),0x1);         /* hi > 31 */

    /* 13. clear_bits_range */
    test_hex("13. clear_bits_range(~0,8,15)",   clear_bits_range(0xFFFFFFFFu, 8, 15), 0xFFFF00FFu);
    test_hex("13. clear_bits_range(0xFF,0,3)",  clear_bits_range(0xFF, 0, 3),         0xF0);
    test_hex("13. clear_bits_range(~0,0,31)",   clear_bits_range(0xFFFFFFFFu, 0, 31), 0x0);
    test_hex("13. clear_bits_range(0xAB,5,3)",  clear_bits_range(0xAB, 5, 3),         0xAB);

    /* 14. toggle_bits_range */
    test_hex("14. toggle_bits_range(0xF0,0,7)",   toggle_bits_range(0xF0, 0, 7),     0x0F);
    test_hex("14. toggle_bits_range(0xAAAA,4,11)",toggle_bits_range(0xAAAA, 4, 11),  0xA55A);
    test_hex("14. toggle_bits_range(0x0,28,31)",  toggle_bits_range(0x0, 28, 31),    0xF0000000u);
    test_hex("14. toggle_bits_range(0x5,9,2)",    toggle_bits_range(0x5, 9, 2),      0x5);

    /* 15. get_bits_range */
    test_hex("15. get_bits_range(0xABCD,4,11)",   get_bits_range(0xABCD, 4, 11),        0xBC);
    test_hex("15. get_bits_range(0xDEADBEEF,16,23)", get_bits_range(0xDEADBEEFu, 16, 23), 0xAD);
    test_hex("15. get_bits_range(0x80000000,31,31)", get_bits_range(0x80000000u, 31, 31), 0x1);
    test_hex("15. get_bits_range(0xDEADBEEF,0,31)",  get_bits_range(0xDEADBEEFu, 0, 31),  0xDEADBEEFu);

    /* 16. set_field */
    test_hex("16. set_field(0x0,4,7,0xA)",        set_field(0x0, 4, 7, 0xA),         0xA0);
    test_hex("16. set_field(0xFFFF,4,7,0x0)",     set_field(0xFFFF, 4, 7, 0x0),      0xFF0F);
    test_hex("16. set_field(0x1234,8,11,0x1F)",   set_field(0x1234, 8, 11, 0x1F),    0x1F34);  /* 넘침 잘라냄 */
    test_hex("16. set_field(~0,0,31,0x12345678)", set_field(0xFFFFFFFFu, 0, 31, 0x12345678u), 0x12345678u);

    /* 17. set_bits_n */
    test_hex("17. set_bits_n(0x0,4,4)",    set_bits_n(0x0, 4, 4),     0xF0);
    test_hex("17. set_bits_n(0x0,0,32)",   set_bits_n(0x0, 0, 32),    0xFFFFFFFFu);
    test_hex("17. set_bits_n(0x1,28,4)",   set_bits_n(0x1, 28, 4),    0xF0000001u);
    test_hex("17. set_bits_n(0x1,30,4)",   set_bits_n(0x1, 30, 4),    0x1);           /* 31 초과 */
    test_hex("17. set_bits_n(0x7,3,0)",    set_bits_n(0x7, 3, 0),     0x7);           /* count 0 */

    printf("\n==> %d/%d PASS\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
