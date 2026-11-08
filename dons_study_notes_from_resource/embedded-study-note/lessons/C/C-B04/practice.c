/* C-B04 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define GENMASK(h, l)         ((0xFFFFFFFFu << (l)) & (0xFFFFFFFFu >> (31u - (h))))
#define FIELD_LSB(mask)       ((uint32_t)(mask) & (~(uint32_t)(mask) + 1u))
#define FIELD_GET(mask, reg)  (((uint32_t)(reg) & (uint32_t)(mask)) / FIELD_LSB(mask))
#define FIELD_PREP(mask, val) (((uint32_t)(val) * FIELD_LSB(mask)) & (uint32_t)(mask))
#define FIELD_REPLACE(mask, reg, val) (((uint32_t)(reg) & ~(uint32_t)(mask)) | FIELD_PREP(mask, val))
#define FIELD_MAX(mask)       ((uint32_t)(mask) / FIELD_LSB(mask))

/* 연습 1: 내장 함수 없이 "상수식" 으로 shift 구하기 — LSB-1 의 1 개수 = LSB 아래 0 개수 */
#define POP_1(x)        ((x) - (((x) >> 1) & 0x55555555u))
#define POP_2(x)        (((x) & 0x33333333u) + (((x) >> 2) & 0x33333333u))
#define POP_3(x)        (((((x) + ((x) >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24)
#define POPCOUNT32(x)   POP_3(POP_2(POP_1((uint32_t)(x))))
#define FIELD_SHIFT(mask) POPCOUNT32(FIELD_LSB(mask) - 1u)
_Static_assert(FIELD_SHIFT(0x00FF0000u) == 16, "FIELD_SHIFT is an integer constant expression");

/* 연습 2: 부호 있는 필드 (폭 1..31). 예: 12비트 2의 보수 온도값이 bits 27:16 에 있음 */
static int32_t field_get_signed(uint32_t reg, uint32_t mask) {
    uint32_t u    = FIELD_GET(mask, reg);
    uint32_t sign = (FIELD_MAX(mask) >> 1) + 1u;          /* 필드의 최상위 비트 값 */
    return (int32_t)(u ^ sign) - (int32_t)sign;           /* 둘 다 < 2^31 → 변환·뺄셈 모두 안전 */
}

/* 연습 3: 64비트 레지스터 (예: AArch64 MPIDR_EL1 의 Aff0/1/2/3) */
#define GENMASK_ULL(h, l)       ((~0ull << (l)) & (~0ull >> (63u - (h))))
#define FIELD_LSB64(mask)       ((uint64_t)(mask) & (~(uint64_t)(mask) + 1u))
#define FIELD_GET64(mask, reg)  (((uint64_t)(reg) & (uint64_t)(mask)) / FIELD_LSB64(mask))
#define FIELD_PREP64(mask, val) (((uint64_t)(val) * FIELD_LSB64(mask)) & (uint64_t)(mask))
#define MPIDR_AFF0_Msk GENMASK_ULL(7, 0)
#define MPIDR_AFF1_Msk GENMASK_ULL(15, 8)
#define MPIDR_AFF3_Msk GENMASK_ULL(39, 32)

/* 연습 4: 쓰기 기록이 남는 가짜 레지스터 — 두 필드를 따로 RMW 하면 중간 상태가 하드웨어에 보인다 */
#define CTRL_MODE_Msk  GENMASK(3, 1)
#define CTRL_PRESC_Msk GENMASK(7, 4)
static uint32_t hw_reg, wlog[8];
static unsigned wcount;
static uint32_t reg_read(void)        { return hw_reg; }
static void     reg_write(uint32_t v) { hw_reg = v; if (wcount < 8) wlog[wcount] = v; wcount++; }
static void cfg_two_writes(uint32_t mode, uint32_t presc) {
    reg_write(FIELD_REPLACE(CTRL_MODE_Msk, reg_read(), mode));     /* 이 순간: 새 MODE + 옛 PRESC */
    reg_write(FIELD_REPLACE(CTRL_PRESC_Msk, reg_read(), presc));
}
static void cfg_one_write(uint32_t mode, uint32_t presc) {
    uint32_t v = reg_read() & ~(CTRL_MODE_Msk | CTRL_PRESC_Msk);
    reg_write(v | FIELD_PREP(CTRL_MODE_Msk, mode) | FIELD_PREP(CTRL_PRESC_Msk, presc));
}

/* 연습 5: 런타임 인덱스 필드 배열 (STM32 GPIOx_MODER: 핀당 2비트) — CMSIS _Pos/_Msk 스타일 */
#define GPIO_MODER_MODE0_Pos 0u
#define GPIO_MODER_MODE0_Msk (0x3u << GPIO_MODER_MODE0_Pos)
static uint32_t moder_set_pos(uint32_t moder, unsigned pin, uint32_t mode) {
    unsigned pos = pin * 2u;                                      /* shift 를 직접 안다 → 그냥 시프트 */
    return (moder & ~(GPIO_MODER_MODE0_Msk << pos)) | ((mode & 0x3u) << pos);
}
static uint32_t moder_set_mask(uint32_t moder, unsigned pin, uint32_t mode) {
    return FIELD_REPLACE(GPIO_MODER_MODE0_Msk << (pin * 2u), moder, mode);   /* 런타임 마스크 */
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    bool ok1 = true;
    for (unsigned b = 0; b < 32; b++) {
        uint32_t m = GENMASK(31, b);
        if (FIELD_SHIFT(m) != (uint32_t)__builtin_ctz(m)) ok1 = false;
    }
    CHECK(ok1);
    CHECK(FIELD_SHIFT(GENMASK(3, 1)) == 1u);

    /* 연습 2 */
    const uint32_t TEMP_Msk = GENMASK(27, 16);
    CHECK(field_get_signed(FIELD_PREP(TEMP_Msk, 0x7FFu), TEMP_Msk) == 2047);
    CHECK(field_get_signed(FIELD_PREP(TEMP_Msk, 0x800u), TEMP_Msk) == -2048);
    CHECK(field_get_signed(FIELD_PREP(TEMP_Msk, 0xFFFu) | 0xF000FFFFu, TEMP_Msk) == -1);
    CHECK(field_get_signed(0xFFFFFFFFu & ~TEMP_Msk, TEMP_Msk) == 0);

    /* 연습 3 */
    const uint64_t mpidr = 0x000000AB80CCDDEEull;
    CHECK(FIELD_GET64(MPIDR_AFF0_Msk, mpidr) == 0xEEu);
    CHECK(FIELD_GET64(MPIDR_AFF1_Msk, mpidr) == 0xDDu);
    CHECK(FIELD_GET64(MPIDR_AFF3_Msk, mpidr) == 0xABu);
    CHECK(GENMASK_ULL(63, 0) == ~0ull && GENMASK_ULL(63, 60) == 0xF000000000000000ull);
    CHECK(FIELD_PREP64(MPIDR_AFF3_Msk, 0x1FFu) == 0x000000FF00000000ull);

    /* 연습 4 */
    hw_reg = FIELD_PREP(CTRL_MODE_Msk, 1u) | FIELD_PREP(CTRL_PRESC_Msk, 0u) | 1u;  /* EN=1 동작 중 */
    wcount = 0;
    cfg_two_writes(2u, 9u);
    CHECK(wcount == 2 && FIELD_GET(CTRL_MODE_Msk, wlog[0]) == 2u && FIELD_GET(CTRL_PRESC_Msk, wlog[0]) == 0u);
    uint32_t after_two = hw_reg;
    hw_reg = FIELD_PREP(CTRL_MODE_Msk, 1u) | 1u;
    wcount = 0;
    cfg_one_write(2u, 9u);
    CHECK(wcount == 1 && hw_reg == after_two);                    /* 같은 최종값, 쓰기 1번, 중간 상태 없음 */

    /* 연습 5 */
    bool ok5 = true;
    for (unsigned pin = 0; pin < 16; pin++)
        for (uint32_t mode = 0; mode < 4; mode++) {
            uint32_t a = moder_set_pos(0xA5A5A5A5u, pin, mode), b = moder_set_mask(0xA5A5A5A5u, pin, mode);
            if (a != b || ((a ^ 0xA5A5A5A5u) & ~(0x3u << (pin * 2u))) != 0u) ok5 = false;
        }
    CHECK(ok5);
    CHECK(moder_set_pos(0xFFFFFFFFu, 5, 1u) == 0xFFFFF7FFu);      /* C-B01 gpio_set_mode 와 같은 값 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
