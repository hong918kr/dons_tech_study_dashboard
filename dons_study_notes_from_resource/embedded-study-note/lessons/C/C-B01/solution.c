/* C-B01 비트 연산 기초: Set / Clear / Toggle / Test
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: 단일 비트 연산 ---------------- */
static inline uint32_t bit_set(uint32_t reg, unsigned n)    { return reg | (1u << n); }
static inline uint32_t bit_clear(uint32_t reg, unsigned n)  { return reg & ~(1u << n); }
static inline uint32_t bit_toggle(uint32_t reg, unsigned n) { return reg ^ (1u << n); }
static inline bool     bit_test(uint32_t reg, unsigned n)   { return (reg >> n) & 1u; }

/* ---------------- Part B: 다중 비트 필드 ---------------- */
/* width 비트짜리 마스크 (width = 1..32). width == 32 일 때 1u << 32 는 UB! */
static inline uint32_t field_mask(unsigned width) {
    return (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
}
static inline uint32_t field_get(uint32_t reg, unsigned shift, unsigned width) {
    return (reg >> shift) & field_mask(width);
}
static inline uint32_t field_set(uint32_t reg, unsigned shift, unsigned width, uint32_t val) {
    uint32_t m = field_mask(width) << shift;
    return (reg & ~m) | ((val << shift) & m);
}

/* ---------------- Part C: 가짜 GPIO 주변장치 ---------------- */
typedef struct {
    volatile uint32_t MODER; /* 핀당 2비트: 00=input 01=output 10=AF 11=analog */
    volatile uint32_t ODR;   /* output data register (읽기/쓰기)            */
    volatile uint32_t BSRR;  /* [15:0]=set, [31:16]=reset. 쓰기 전용          */
} GPIO_TypeDef;

static GPIO_TypeDef fake_gpio;               /* 실제 칩이라면 (GPIO_TypeDef *)0x48000000 */
#define GPIOA (&fake_gpio)

/* 하드웨어가 BSRR 쓰기를 ODR에 반영하는 동작을 흉내낸다 (테스트용) */
static void sim_apply_bsrr(GPIO_TypeDef *g) {
    uint32_t b = g->BSRR;
    g->ODR = (g->ODR & ~(b >> 16)) | (b & 0xFFFFu);
    g->BSRR = 0;
}

enum { MODE_INPUT = 0, MODE_OUTPUT = 1, MODE_AF = 2, MODE_ANALOG = 3 };

static void gpio_set_mode(GPIO_TypeDef *g, unsigned pin, uint32_t mode) {
    g->MODER = field_set(g->MODER, pin * 2u, 2u, mode);
}
static void gpio_write(GPIO_TypeDef *g, unsigned pin, bool level) {
    g->BSRR = level ? (1u << pin) : (1u << (pin + 16u));   /* RMW 없음 → ISR와 경쟁 없음 */
    sim_apply_bsrr(g);
}
static void gpio_toggle(GPIO_TypeDef *g, unsigned pin) {
    gpio_write(g, pin, !bit_test(g->ODR, pin));
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A */
    CHECK(bit_set(0x00u, 3) == 0x08u);
    CHECK(bit_set(0x00u, 31) == 0x80000000u);
    CHECK(bit_clear(0xFFu, 0) == 0xFEu);
    CHECK(bit_clear(0xFFFFFFFFu, 31) == 0x7FFFFFFFu);
    CHECK(bit_toggle(0x0Fu, 4) == 0x1Fu);
    CHECK(bit_toggle(0x1Fu, 4) == 0x0Fu);
    CHECK(bit_test(0x80000000u, 31) == true);
    CHECK(bit_test(0x80000000u, 30) == false);

    /* Part B */
    CHECK(field_get(0xABCD1234u, 8, 8) == 0x12u);
    CHECK(field_get(0xABCD1234u, 28, 4) == 0xAu);
    CHECK(field_get(0xABCD1234u, 0, 32) == 0xABCD1234u);
    CHECK(field_set(0xFFFFFFFFu, 4, 4, 0x0u) == 0xFFFFFF0Fu);
    CHECK(field_set(0x00000000u, 4, 4, 0xAu) == 0x000000A0u);
    CHECK(field_set(0x00000000u, 4, 4, 0x1Fu) == 0x000000F0u);   /* 넘치는 값은 잘려야 함 */
    CHECK(field_set(0x12345678u, 0, 32, 0xCAFEBABEu) == 0xCAFEBABEu);

    /* Part C */
    GPIOA->MODER = 0xFFFFFFFFu;
    gpio_set_mode(GPIOA, 5, MODE_OUTPUT);
    CHECK(GPIOA->MODER == 0xFFFFF7FFu);
    gpio_set_mode(GPIOA, 0, MODE_INPUT);
    CHECK(GPIOA->MODER == 0xFFFFF7FCu);
    GPIOA->ODR = 0;
    gpio_write(GPIOA, 5, true);
    CHECK(GPIOA->ODR == (1u << 5));
    gpio_write(GPIOA, 7, true);
    gpio_write(GPIOA, 5, false);
    CHECK(GPIOA->ODR == (1u << 7));
    gpio_toggle(GPIOA, 7);
    gpio_toggle(GPIOA, 2);
    CHECK(GPIOA->ODR == (1u << 2));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
