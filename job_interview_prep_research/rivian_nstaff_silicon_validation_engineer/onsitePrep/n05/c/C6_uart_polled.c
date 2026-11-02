/* C6 - polled UART: putc (wait TX FIFO not full), non-blocking getc, print_hex32 without printf. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only) ----
 * 0x00 DATA : write = push TX FIFO; read = pop RX FIFO (READ SIDE EFFECT)
 * 0x04 STAT : bit0 TXFULL, bit1 RXEMPTY
 * TX FIFO depth 4; the "wire" drains one TX byte every 3 STAT reads into m_wire[]. */
#define UART_DATA 0x00u
#define UART_STAT 0x04u
#define ST_TXFULL  (1u << 0)
#define ST_RXEMPTY (1u << 1)
#define TXDEPTH 4u
static uint8_t  m_txq[TXDEPTH]; static unsigned m_txn;
static char     m_wire[128];    static unsigned m_wn;
static uint8_t  m_rx[16];       static unsigned m_rxh, m_rxn;
static unsigned m_stat_reads, m_overflow;
uint32_t reg_read(uint32_t off)
{
    if (off == UART_STAT) {
        if (++m_stat_reads % 3 == 0 && m_txn) {          /* shift one byte out */
            m_wire[m_wn++] = (char)m_txq[0];
            memmove(m_txq, m_txq + 1, --m_txn);
        }
        return (m_txn == TXDEPTH ? ST_TXFULL : 0) | (m_rxh == m_rxn ? ST_RXEMPTY : 0);
    }
    return (m_rxh < m_rxn) ? m_rx[m_rxh++] : 0;        /* pop */
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off != UART_DATA) return;
    if (m_txn == TXDEPTH) { m_overflow++; return; }     /* byte lost */
    m_txq[m_txn++] = (uint8_t)val;
}
static void flush_wire(void) { while (m_txn) (void)reg_read(UART_STAT); m_wire[m_wn] = 0; }
static void model_reset(void) { m_txn = m_wn = m_rxh = m_rxn = m_stat_reads = m_overflow = 0; }

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');                        /* terminals want CRLF */
    while (reg_read(UART_STAT) & ST_TXFULL)
        ;                                       /* bring-up: block; see follow-up for timeout */
    reg_write(UART_DATA, (uint8_t)c);
}

/* Non-blocking: -1 if nothing. int, not char: 0xFF is a valid byte. */
int uart_getc_nb(void)
{
    if (reg_read(UART_STAT) & ST_RXEMPTY)
        return -1;                              /* never read DATA when empty: read pops */
    return (int)(reg_read(UART_DATA) & 0xFFu);
}

void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

/* "0x" + exactly 8 upper-case hex digits, MSB nibble first. */
void uart_print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    for (int sh = 28; sh >= 0; sh -= 4)
        uart_putc(hex[(v >> sh) & 0xFu]);
}
/* ==== ANSWER END ==== */

int main(void)
{
    model_reset();
    uart_print_hex32(0xDEADBEEFu); flush_wire();
    assert(strcmp(m_wire, "0xDEADBEEF") == 0 && m_overflow == 0);   /* 10 bytes > depth 4 */

    model_reset();
    uart_print_hex32(0); uart_putc(' '); uart_print_hex32(0x0000000Au); flush_wire();
    assert(strcmp(m_wire, "0x00000000 0x0000000A") == 0);           /* leading zeros kept */

    model_reset();
    uart_puts("ok\n"); flush_wire();
    assert(strcmp(m_wire, "ok\r\n") == 0);

    model_reset();
    uart_print_hex32(0x12345678u); uart_print_hex32(UINT32_MAX); flush_wire();
    assert(strcmp(m_wire, "0x123456780xFFFFFFFF") == 0 && m_overflow == 0);

    /* getc: empty -> -1 without touching DATA; 0xFF is data, not EOF */
    model_reset();
    assert(uart_getc_nb() == -1 && m_rxh == 0);
    m_rx[0] = 'A'; m_rx[1] = 0xFF; m_rx[2] = 0x00; m_rxn = 3;
    assert(uart_getc_nb() == 'A');
    assert(uart_getc_nb() == 0xFF);
    assert(uart_getc_nb() == 0x00);
    assert(uart_getc_nb() == -1);

    printf("C6 PASS\n");
    return 0;
}
