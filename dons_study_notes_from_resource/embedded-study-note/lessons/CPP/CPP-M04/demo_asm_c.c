/* CPP-M04 demo: 손으로 쓴 C — demo_asm_pin.cpp 와 비교 대상 (asm_compare.sh) */
typedef unsigned int u32;

#define GPIOA_BASE 0x40020000u
#define GPIOA_IDR  (*(volatile u32 *)(GPIOA_BASE + 0x10u))
#define GPIOA_ODR  (*(volatile u32 *)(GPIOA_BASE + 0x14u))
#define GPIOA_BSRR (*(volatile u32 *)(GPIOA_BASE + 0x18u))
#define PIN5 (1u << 5)
#define PIN6 (1u << 6)

void c_set(void)         { GPIOA_BSRR = PIN5; }
void c_clear(void)       { GPIOA_BSRR = PIN5 << 16; }
void c_clear15(void)     { GPIOA_BSRR = (1u << 15) << 16; }
void c_write(_Bool v)    { GPIOA_BSRR = v ? PIN5 : (PIN5 << 16); }
_Bool c_read(void)       { return (GPIOA_IDR & PIN5) != 0; }
void c_toggle(void)      { GPIOA_ODR ^= PIN5; }
void c_toggle_bsrr(void) { u32 o = GPIOA_ODR; GPIOA_BSRR = ((o & PIN5) << 16) | (~o & PIN5); }
void c_pulse(void)       { GPIOA_BSRR = PIN5; GPIOA_BSRR = PIN5 << 16; }
void c_two_pins(void)    { GPIOA_BSRR = PIN5; GPIOA_BSRR = PIN6; }

/* PA0,PA3,PA7,PA12 네 핀에 니블을 흩뿌려 한 번에 쓴다 */
#define NIB_MASK ((1u << 0) | (1u << 3) | (1u << 7) | (1u << 12))
void c_bus_const(void) { u32 on = (1u << 3) | (1u << 12); GPIOA_BSRR = on | ((NIB_MASK & ~on) << 16); }
void c_bus_var(u32 v) {
    u32 on = ((v >> 0) & 1u) << 0 | ((v >> 1) & 1u) << 3 | ((v >> 2) & 1u) << 7 | ((v >> 3) & 1u) << 12;
    GPIOA_BSRR = on | ((NIB_MASK & ~on) << 16);
}

/* 비교용: 핀을 런타임 인자/테이블로 받는 Arduino digitalWrite 스타일 */
typedef struct { volatile u32 MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR; } Gpio;
typedef struct { Gpio *port; u32 mask; } PinCfg;
extern const PinCfg g_pins[];

void rt_set(Gpio *p, u32 n)     { p->BSRR = 1u << n; }
void tbl_set(unsigned idx)      { g_pins[idx].port->BSRR = g_pins[idx].mask; }

/* 라이브러리 함수로 부를 때 호출 측에 남는 코드 (인라인 안 되는 경우) */
extern void gpio_set_ext(Gpio *p, u32 n);
void call_rt_set(void)          { gpio_set_ext((Gpio *)GPIOA_BASE, 5u); }
