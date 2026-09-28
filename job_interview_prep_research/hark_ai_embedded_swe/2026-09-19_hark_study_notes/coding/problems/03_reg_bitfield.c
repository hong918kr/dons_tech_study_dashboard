/* 03_reg_bitfield.c — 레지스터 비트필드 매크로와 read-modify-write
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 03_reg_bitfield.c -o run_03
 * 또는: make prob N=03 && make run N=03
 *
 * 요구사항: 문제 파일(03_reg_bitfield.md)을 읽고, 아래 // TODO 섹션들을 채워라.
 *
 * 개요:
 * - 비트필드 매크로: BIT, GENMASK, FIELD_MASK, FIELD_SHIFT, FIELD_PREP, FIELD_GET
 * - 레지스터 접근: read, write, read-modify-write, set/clear bits
 * - 필드 단위 get/set
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 1. 비트필드 헬퍼 매크로 — TODO: 구현하세요                             */
/* ------------------------------------------------------------------ */

/* BIT(n): bit n을 세운 값 (1 << n) */
#define BIT(n)
    // TODO: ((uint32_t)1u << (n))

/* GENMASK(hi, lo): bit hi ~ lo가 1인 마스크.
 * 예: GENMASK(3, 1) = 0b1110 = 0x0E
 */
#define GENMASK(hi, lo)
    // TODO: ((0xFFFFFFFFu >> (31u - (hi))) & (0xFFFFFFFFu << (lo)))

/* FIELD_MASK(width, shift): shift 위치에서 width비트 마스크.
 * 예: FIELD_MASK(3, 1) = GENMASK(3, 1) = 0x0E
 */
#define FIELD_MASK(width, shift)
    // TODO: ((0xFFFFFFFFu >> (32u - (width))) << (shift))

/* FIELD_SHIFT(mask): mask의 최하위 1비트 위치 (몇 칸 시프트했는지).
 * 예: FIELD_SHIFT(0x0E) = __builtin_ctz(0x0E) = 1
 */
#define FIELD_SHIFT(mask)
    // TODO: ((uint32_t)__builtin_ctz(mask))

/* FIELD_PREP(mask, val): 값을 필드 위치로 올린다.
 * 예: FIELD_PREP(0x0E, 5) = (5 << 1) & 0x0E = 0x0A
 */
#define FIELD_PREP(mask, val)
    // TODO: (((uint32_t)(val) << FIELD_SHIFT(mask)) & (mask))

/* FIELD_GET(mask, reg): 레지스터에서 필드를 추출한다.
 * 예: FIELD_GET(0x0E, 0x0A) = (0x0A & 0x0E) >> 1 = 0x05
 */
#define FIELD_GET(mask, reg)
    // TODO: (((uint32_t)(reg) & (mask)) >> FIELD_SHIFT(mask))

/* ------------------------------------------------------------------ */
/* 2. 레지스터 접근 함수 — TODO: 구현하세요                               */
/* ------------------------------------------------------------------ */

static inline uint32_t reg_read(const volatile uint32_t *reg)
{
    // TODO: return *reg; (volatile로 매번 실제 버스 read)
}

static inline void reg_write(volatile uint32_t *reg, uint32_t val)
{
    // TODO: *reg = val; (1회 write)
}

/* read-modify-write: mask 영역만 val로 교체, 나머지 비트 보존 */
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    // TODO: 구현하세요.
    // 1. r = *reg (read)
    // 2. r = (r & ~mask) | (val & mask)  (mask 영역 교체)
    // 3. *reg = r (write)
}

static inline void reg_set_bits(volatile uint32_t *reg, uint32_t mask)
{
    // TODO: reg_update를 사용해서 mask 비트를 모두 1로 만든다.
    // reg_update(reg, mask, mask);
}

static inline void reg_clear_bits(volatile uint32_t *reg, uint32_t mask)
{
    // TODO: reg_update를 사용해서 mask 비트를 모두 0으로 만든다.
    // reg_update(reg, mask, 0u);
}

/* 필드 단위 read */
static inline uint32_t reg_get_field(const volatile uint32_t *reg, uint32_t mask)
{
    // TODO: FIELD_GET를 사용해서 필드를 추출한다.
    // return FIELD_GET(mask, *reg);
}

/* 필드 단위 write (다른 비트 보존) */
static inline void reg_set_field(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    // TODO: FIELD_PREP를 사용해서 값을 필드 형태로 준비하고,
    //       reg_update로 쓴다.
    // reg_update(reg, mask, FIELD_PREP(mask, val));
}

/* ------------------------------------------------------------------ */
/* 3. 테스트 모의 레지스터                                              */
/* ------------------------------------------------------------------ */

#define OK(msg) printf("  ok  %s\n", (msg))

static void test_bit_macro(void)
{
    assert(BIT(0) == 0x00000001u);
    assert(BIT(1) == 0x00000002u);
    assert(BIT(3) == 0x00000008u);
    assert(BIT(31) == 0x80000000u);
    OK("BIT(n) 매크로");
}

static void test_genmask_macro(void)
{
    assert(GENMASK(0, 0) == 0x00000001u);
    assert(GENMASK(3, 1) == 0x0000000Eu);
    assert(GENMASK(7, 0) == 0x000000FFu);
    assert(GENMASK(31, 0) == 0xFFFFFFFFu);
    OK("GENMASK(hi, lo) 매크로");
}

static void test_field_mask(void)
{
    assert(FIELD_MASK(1, 0) == 0x00000001u);
    assert(FIELD_MASK(3, 1) == 0x0000000Eu);
    assert(FIELD_MASK(8, 0) == 0x000000FFu);
    assert(FIELD_MASK(4, 16) == 0x000F0000u);
    OK("FIELD_MASK(width, shift) 매크로");
}

static void test_field_shift(void)
{
    assert(FIELD_SHIFT(0x00000001u) == 0);
    assert(FIELD_SHIFT(0x00000002u) == 1);
    assert(FIELD_SHIFT(0x0000000Eu) == 1);
    assert(FIELD_SHIFT(0x000F0000u) == 16);
    OK("FIELD_SHIFT(mask) 매크로");
}

static void test_field_prep(void)
{
    /* mask=0x0E (bit 3..1), value=5 */
    assert(FIELD_PREP(0x0Eu, 5) == 0x0Au);
    assert(FIELD_PREP(0x0Eu, 7) == 0x0Eu);  /* overflow 잘려서 0x07 -> 0x0E */

    /* mask=0x000000FF (byte), value=0x42 */
    assert(FIELD_PREP(0xFFu, 0x42) == 0x42);

    /* mask=0xF0000000 (top nibble), value=0x5 */
    assert(FIELD_PREP(0xF0000000u, 5) == 0x50000000u);
    OK("FIELD_PREP(mask, val) 매크로");
}

static void test_field_get(void)
{
    assert(FIELD_GET(0x0Eu, 0x0A) == 5);
    assert(FIELD_GET(0xFFu, 0x42) == 0x42);
    assert(FIELD_GET(0xF0000000u, 0x50000000u) == 5);
    OK("FIELD_GET(mask, reg) 매크로");
}

static void test_reg_read_write(void)
{
    volatile uint32_t r = 0;
    reg_write(&r, 0x12345678u);
    assert(reg_read(&r) == 0x12345678u);
    OK("reg_read/write");
}

static void test_reg_update(void)
{
    volatile uint32_t r = 0xFFFFFFFFu;
    reg_update(&r, 0x0Eu, 0x0Au);  /* bit 3..1을 5(0b101)로 */
    assert(r == 0xFFFFFFF5u);  /* bit 3..1이 0b101, 나머지 1 */
    OK("reg_update (read-modify-write)");
}

static void test_reg_set_clear_bits(void)
{
    volatile uint32_t r = 0x00000000u;
    reg_set_bits(&r, 0x0Eu);
    assert(r == 0x0Eu);

    reg_clear_bits(&r, 0x06u);
    assert(r == 0x08u);  /* bit 3만 남음 */
    OK("reg_set_bits / reg_clear_bits");
}

static void test_reg_get_set_field(void)
{
    volatile uint32_t r = 0xFFFFFFFFu;

    /* 필드 mask=0x0E (bit 3..1)에서 값 3 설정 */
    reg_set_field(&r, 0x0Eu, 3);
    assert(reg_get_field(&r, 0x0Eu) == 3);
    assert((r & 0x0Eu) == 0x06u);  /* 3 << 1 = 0x06 */

    /* 다른 비트는 보존 확인 */
    assert((r & 0xFFFFFFF1u) == 0xFFFFFFF1u);
    OK("reg_get_field / reg_set_field");
}

static void test_multi_field_register(void)
{
    volatile uint32_t r = 0x00000000u;

    uint32_t field_a = 0x0Fu;      /* bit 3..0 */
    uint32_t field_b = 0xF0u;      /* bit 7..4 */

    reg_set_field(&r, field_a, 0xA);
    reg_set_field(&r, field_b, 0x5);

    assert(reg_get_field(&r, field_a) == 0xA);
    assert(reg_get_field(&r, field_b) == 0x5);
    assert(r == 0x5Au);  /* 0x50 | 0x0A */

    OK("다중 필드 레지스터 (필드 간 독립적 조작)");
}

int main(void)
{
    printf("=== 03 레지스터 비트필드 ===\n");
    test_bit_macro();
    test_genmask_macro();
    test_field_mask();
    test_field_shift();
    test_field_prep();
    test_field_get();
    test_reg_read_write();
    test_reg_update();
    test_reg_set_clear_bits();
    test_reg_get_set_field();
    test_multi_field_register();
    printf("ALL TESTS PASSED\n");
    return 0;
}
