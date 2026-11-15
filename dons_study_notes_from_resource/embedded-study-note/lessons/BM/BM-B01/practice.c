/* BM-B01 추가 연습 — 한 번에 검증
 *
 * 연습 1  핀 번호를 인자로 받는 gpio_pin_mode(): 핀별 매크로 대신 MODER0 마스크를 (2·pin) 만큼 이동
 * 연습 2  gpio_pin_af(): AFR[2] 배열 — 핀 0–7 은 AFR[0](AFRL), 8–15 는 AFR[1](AFRH)
 * 연습 3  레지스터 되읽기: BRR 과 CR1.OVER8 에서 실제 baud 계산 (FLD2VAL)
 * 연습 4  옛 StdPeriph 헤더의 BSRRL/BSRRH (uint16_t 두 개) ↔ 지금의 32비트 BSRR
 * 연습 5  _Static_assert 의 런타임 판: 잘못된 RESERVED 패딩을 표 비교로 찾아내고, 쓰기가 어디로 가는지 계산
 *
 * 빌드 (레슨 폴더에서): cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define UNIT_TEST 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device_excerpt.h"

#define VAL2FLD(field, value)   (((uint32_t)(value) << field ## _Pos) & field ## _Msk)
#define FLD2VAL(field, value)   (((uint32_t)(value) & field ## _Msk) >> field ## _Pos)
#define MODIFY_REG(REG, CLEARMASK, SETMASK) WRITE_REG((REG), (READ_REG(REG) & ~(CLEARMASK)) | (SETMASK))

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x1234567u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- 연습 1: 핀 번호 인자 ---------- */
static int gpio_pin_mode(GPIO_TypeDef *g, unsigned pin, uint32_t mode) {
    if (pin > 15u || mode > 3u) return -1;
    MODIFY_REG(g->MODER, GPIO_MODER_MODER0_Msk << (pin * 2u),
               VAL2FLD(GPIO_MODER_MODER0, mode) << (pin * 2u));
    return 0;
}

/* ---------- 연습 2: AFR[2] ---------- */
static int gpio_pin_af(GPIO_TypeDef *g, unsigned pin, uint32_t af) {
    if (pin > 15u || af > 15u) return -1;
    unsigned idx = pin >> 3u, sh = (pin & 7u) * 4u;
    MODIFY_REG(g->AFR[idx], GPIO_AFRL_AFSEL0_Msk << sh, VAL2FLD(GPIO_AFRL_AFSEL0, af) << sh);
    return 0;
}

/* ---------- 연습 3: BRR → 실제 baud ---------- */
static uint32_t usart_actual_baud(uint32_t pclk_hz, const USART_TypeDef *u) {
    uint32_t mant = FLD2VAL(USART_BRR_DIV_Mantissa, u->BRR);
    uint32_t frac = FLD2VAL(USART_BRR_DIV_Fraction, u->BRR);
    bool over8 = FLD2VAL(USART_CR1_OVER8, u->CR1) != 0u;
    /* OVER8=0: baud = f/(16·(M + F/16)) = f/(16M + F)   OVER8=1: f/(8·(M + F/8)) = f/(8M + F), F 3비트 */
    uint32_t div = over8 ? (mant * 8u + (frac & 7u)) : (mant * 16u + frac);
    if (div == 0u) return 0u;
    return (uint32_t)(((uint64_t)pclk_hz + div / 2u) / div);
}

/* ---------- 연습 4: 옛 헤더 (STM32F4 StdPeriph 시절 모양 — 확인 권장) ---------- */
typedef struct {
    __IO uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR;
    __IO uint16_t BSRRL;    /* 0x18: set 절반 */
    __IO uint16_t BSRRH;    /* 0x1A: reset 절반 */
    __IO uint32_t LCKR;
    __IO uint32_t AFR[2];
} GPIO_TypeDef_SPL;
_Static_assert(offsetof(GPIO_TypeDef_SPL, BSRRL) == 0x18, "BSRRL");
_Static_assert(offsetof(GPIO_TypeDef_SPL, BSRRH) == 0x1A, "BSRRH");
_Static_assert(offsetof(GPIO_TypeDef_SPL, LCKR) == 0x1C, "LCKR");
_Static_assert(sizeof(GPIO_TypeDef_SPL) == sizeof(GPIO_TypeDef), "same footprint");

static void spl_pin_set(GPIO_TypeDef_SPL *g, unsigned pin)   { g->BSRRL = (uint16_t)(1u << pin); }
static void spl_pin_reset(GPIO_TypeDef_SPL *g, unsigned pin) { g->BSRRH = (uint16_t)(1u << pin); }

/* ---------- 연습 5: 패딩 버그를 런타임 표로 ---------- */
typedef struct {                        /* RESERVED1 을 [2] 대신 1 워드로 쓴 "틀린 헤더" */
    __IO uint32_t CR, PLLCFGR, CFGR, CIR, AHB1RSTR, AHB2RSTR, AHB3RSTR;
    uint32_t      RESERVED0;
    __IO uint32_t APB1RSTR, APB2RSTR;
    uint32_t      RESERVED1;            /* 버그 */
    __IO uint32_t AHB1ENR, AHB2ENR, AHB3ENR;
    uint32_t      RESERVED2;
    __IO uint32_t APB1ENR, APB2ENR;
} RCC_Bad;

typedef struct { const char *name; size_t rm_off; size_t actual; } layout_row_t;
#define ROW(T, f, off) { #f, (off), offsetof(T, f) }

static int first_layout_mismatch(const layout_row_t *rows, int n) {
    for (int i = 0; i < n; i++)
        if (rows[i].rm_off != rows[i].actual) return i;
    return -1;
}

int main(void) {
    static GPIO_TypeDef g;

    /* 연습 1: 16 핀 × 4 모드 × 무작위 선채움 30 개 — 대상 2비트만 바뀐다 */
    {
        int bad = 0;
        for (unsigned pin = 0; pin < 16u; pin++)
            for (uint32_t mode = 0; mode < 4u; mode++)
                for (int k = 0; k < 30; k++) {
                    uint32_t pre = xs32(), fld = 3u << (pin * 2u);
                    g.MODER = pre;
                    if (gpio_pin_mode(&g, pin, mode) != 0) bad++;
                    if (g.MODER != ((pre & ~fld) | (mode << (pin * 2u)))) bad++;
                }
        CHECK(bad == 0);
        g.MODER = 0xA8000000u;
        CHECK(gpio_pin_mode(&g, 16u, 1u) == -1 && gpio_pin_mode(&g, 5u, 4u) == -1 && g.MODER == 0xA8000000u);
        CHECK(gpio_pin_mode(&g, 15u, 1u) == 0 && g.MODER == 0x68000000u);   /* 핀 15 = 비트 31:30 */
    }
    /* 연습 2: AFRL/AFRH 경계 */
    {
        g.AFR[0] = 0; g.AFR[1] = 0;
        CHECK(gpio_pin_af(&g, 7u, 7u) == 0 && g.AFR[0] == 0x70000000u && g.AFR[1] == 0u);
        CHECK(gpio_pin_af(&g, 8u, 5u) == 0 && g.AFR[1] == 0x00000005u && g.AFR[0] == 0x70000000u);
        CHECK(gpio_pin_af(&g, 15u, 0xFu) == 0 && g.AFR[1] == 0xF0000005u);
        CHECK(gpio_pin_af(&g, 16u, 1u) == -1 && gpio_pin_af(&g, 0u, 16u) == -1);
        int bad = 0;
        for (unsigned pin = 0; pin < 16u; pin++)
            for (uint32_t af = 0; af < 16u; af++) {
                uint32_t a0 = xs32(), a1 = xs32();
                g.AFR[0] = a0; g.AFR[1] = a1;
                gpio_pin_af(&g, pin, af);
                uint32_t sh = (pin & 7u) * 4u, m = 0xFu << sh;
                uint32_t e0 = pin < 8u ? ((a0 & ~m) | (af << sh)) : a0;
                uint32_t e1 = pin < 8u ? a1 : ((a1 & ~m) | (af << sh));
                if (g.AFR[0] != e0 || g.AFR[1] != e1) bad++;
            }
        CHECK(bad == 0);
    }
    /* 연습 3: 되읽기 */
    {
        static USART_TypeDef u;
        u.BRR = 0x16Du; u.CR1 = 0x200Cu;
        CHECK(usart_actual_baud(42000000u, &u) == 115068u);        /* 42e6 / 365 */
        u.BRR = 0x2D5u; u.CR1 = 0xA00Cu;                            /* OVER8=1: M=45, F=5 → 8·45+5 = 365 */
        CHECK(usart_actual_baud(42000000u, &u) == 115068u);
        u.BRR = 0x08Bu; u.CR1 = 0x200Cu;
        CHECK(usart_actual_baud(16000000u, &u) == 115108u);        /* 16e6 / 139 = 115107.9 */
        u.BRR = 0u;
        CHECK(usart_actual_baud(16000000u, &u) == 0u);              /* 0 으로 나누지 않는다 */
    }
    /* 연습 4: BSRRL/BSRRH — 리틀 엔디언에서 32비트 BSRR 와 같은 바이트 */
    {
        static GPIO_TypeDef_SPL s;
        uint32_t word;
        spl_pin_reset(&s, 5u);
        memcpy(&word, (const unsigned char *)&s + 0x18, sizeof word);
        CHECK(word == 0x00200000u);                                 /* = GPIO_BSRR_BR5 */
        s.BSRRH = 0;
        spl_pin_set(&s, 5u);
        memcpy(&word, (const unsigned char *)&s + 0x18, sizeof word);
        CHECK(word == GPIO_BSRR_BS5_Msk);
    }
    /* 연습 5: 런타임 레이아웃 비교 */
    {
        const layout_row_t good[] = {
            ROW(RCC_TypeDef, AHB1RSTR, 0x10), ROW(RCC_TypeDef, APB1RSTR, 0x20), ROW(RCC_TypeDef, APB2RSTR, 0x24),
            ROW(RCC_TypeDef, AHB1ENR, 0x30), ROW(RCC_TypeDef, APB1ENR, 0x40), ROW(RCC_TypeDef, APB2ENR, 0x44),
        };
        const layout_row_t bad[] = {
            ROW(RCC_Bad, AHB1RSTR, 0x10), ROW(RCC_Bad, APB1RSTR, 0x20), ROW(RCC_Bad, APB2RSTR, 0x24),
            ROW(RCC_Bad, AHB1ENR, 0x30), ROW(RCC_Bad, APB1ENR, 0x40), ROW(RCC_Bad, APB2ENR, 0x44),
        };
        CHECK(first_layout_mismatch(good, 6) == -1);
        int i = first_layout_mismatch(bad, 6);
        CHECK(i == 3 && strcmp(bad[i].name, "AHB1ENR") == 0 && bad[i].actual == 0x2Cu);
        /* 틀린 헤더로 GPIOA 클럭을 켜면 쓰기는 RM 상 예약 영역으로 간다 */
        CHECK(RCC_BASE + offsetof(RCC_Bad, AHB1ENR) == 0x4002382Cu);
        CHECK(RCC_BASE + offsetof(RCC_Bad, APB1ENR) == 0x4002383Cu);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
