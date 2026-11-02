/* C3 - register block as a struct overlay with reserved padding + offset static_asserts. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ==== ANSWER BEGIN ==== */
/* UART block map: 0x000 CTRL, 0x004 STATUS(RO), 0x008 BAUD_DIV, 0x00C reserved,
 *                 0x010 DATA, 0x014..0x0FC reserved, 0x100 ID(RO). Total 0x104. */
typedef struct {
    volatile uint32_t       CTRL;        /* 0x000 */
    volatile const uint32_t STATUS;      /* 0x004 RO: const catches stray writes */
    volatile uint32_t       BAUD_DIV;    /* 0x008 */
    uint32_t                RSVD0[1];    /* 0x00C */
    volatile uint32_t       DATA;        /* 0x010 */
    uint32_t                RSVD1[59];   /* 0x014..0x0FC: (0x100 - 0x014) / 4 = 59 */
    volatile const uint32_t ID;          /* 0x100 RO */
} uart_regs_t;

_Static_assert(offsetof(uart_regs_t, STATUS)   == 0x004, "STATUS offset");
_Static_assert(offsetof(uart_regs_t, BAUD_DIV) == 0x008, "BAUD_DIV offset");
_Static_assert(offsetof(uart_regs_t, DATA)     == 0x010, "DATA offset");
_Static_assert(offsetof(uart_regs_t, ID)       == 0x100, "ID offset");
_Static_assert(sizeof(uart_regs_t)             == 0x104, "block size");

#define UART_CTRL_EN    (1u << 0)
#define UART_CTRL_TXEN  (1u << 1)
#define UART_CTRL_RXEN  (1u << 2)
/* On target: #define UART0 ((uart_regs_t *)0x40002000u) */

/* Disable, program divisor, then enable: never change the divisor while running. */
void uart_setup(uart_regs_t *u, uint16_t div)
{
    u->CTRL     = 0;
    u->BAUD_DIV = div;
    u->CTRL     = UART_CTRL_EN | UART_CTRL_TXEN | UART_CTRL_RXEN;
}
/* ==== ANSWER END ==== */

int main(void)
{
    /* fake register window: plain aligned memory standing in for MMIO */
    static uint32_t fake[0x104 / 4];
    memset(fake, 0xEE, sizeof fake);
    fake[0x100 / 4] = 0x55A70102u;               /* "ID" preset by HW model */
    fake[0x004 / 4] = 0x00000001u;               /* STATUS preset */

    uart_regs_t *u = (uart_regs_t *)(void *)fake;
    uart_setup(u, 26);                            /* e.g. 48 MHz / (16*115200) ~ 26 */

    assert(fake[0x000 / 4] == 0x7u);              /* CTRL = EN|TXEN|RXEN */
    assert(fake[0x008 / 4] == 26u);               /* BAUD_DIV at 0x008 */
    assert(fake[0x00C / 4] == 0xEEEEEEEEu);       /* reserved untouched */
    assert(fake[0x010 / 4] == 0xEEEEEEEEu);       /* DATA untouched */
    assert(u->ID == 0x55A70102u && u->STATUS == 1u);

    /* byte distance check (what the static_asserts guarantee at compile time) */
    assert((uintptr_t)&u->DATA - (uintptr_t)u == 0x10u);
    assert((uintptr_t)&u->ID   - (uintptr_t)u == 0x100u);

    /* write through DATA lands at 0x010 */
    u->DATA = 'A';
    assert(fake[0x010 / 4] == 'A');

    printf("C3 PASS\n");
    return 0;
}
