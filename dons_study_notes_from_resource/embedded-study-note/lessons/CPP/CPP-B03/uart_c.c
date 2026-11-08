/* CPP-B03 asm 비교용 — C enum + #define 버전 (cc -std=c11 -O2 -S) */
#include <stdbool.h>
#include <stdint.h>

#define CR_MODE_Pos 1u
#define CR_MODE_Msk (0x3u << CR_MODE_Pos)
#define CR_PAR_Pos  4u
#define CR_PAR_Msk  (0x3u << CR_PAR_Pos)
#define CR_STOP_Pos 12u
#define CR_STOP_Msk (0x3u << CR_STOP_Pos)

enum uart_mode   { UART_MODE_ASYNC, UART_MODE_SYNC, UART_MODE_IRDA, UART_MODE_SMARTCARD };
enum uart_parity { UART_PARITY_NONE = 0, UART_PARITY_EVEN = 2, UART_PARITY_ODD = 3 };
enum uart_stop   { UART_STOP_1, UART_STOP_0_5, UART_STOP_2, UART_STOP_1_5 };
#define UART_IRQ_RXNE   (1u << 0)   /* 1u<<31 은 C(C23 이전) enum 상수로 못 쓴다 → #define */
#define UART_IRQ_WAKEUP (1u << 31)

uint32_t encode_cr(enum uart_mode m, enum uart_parity p, enum uart_stop s) {
    return (((uint32_t)m << CR_MODE_Pos) & CR_MODE_Msk) | (((uint32_t)p << CR_PAR_Pos) & CR_PAR_Msk)
         | (((uint32_t)s << CR_STOP_Pos) & CR_STOP_Msk);
}
void uart_configure(volatile uint32_t *cr, enum uart_mode m, enum uart_parity p, enum uart_stop s) {
    uint32_t keep = *cr & ~(CR_MODE_Msk | CR_PAR_Msk | CR_STOP_Msk);
    *cr = keep | encode_cr(m, p, s);
}
void uart_disable_irq(volatile uint32_t *ier, uint32_t irq) { *ier = *ier & ~irq; }
bool parity_is_valid(enum uart_parity p) {
    switch (p) { case UART_PARITY_NONE: case UART_PARITY_EVEN: case UART_PARITY_ODD: return true; }
    return false;
}
