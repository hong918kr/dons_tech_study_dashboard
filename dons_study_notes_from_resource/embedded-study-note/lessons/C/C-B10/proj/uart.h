/* uart.h — 공개 인터페이스: 선언(declaration)만 둔다. 정의는 uart.c 한 곳. */
#ifndef UART_H
#define UART_H
#include <stdint.h>

extern uint32_t uart_tx_bytes;      /* extern 변수 선언: 메모리를 만들지 않는다 */
extern int      uart_log_level;

unsigned uart_init(uint32_t baud);  /* 몇 번째 호출인지 돌려준다 (init-once 데모) */
void     uart_send(const char *s);
uint32_t uart_baud(void);

#endif
