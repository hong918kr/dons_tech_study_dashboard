/* hal.h — tiny "HAL": UART, sensor, log. */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>

void     uart_init(uint32_t baud);
void     uart_write(const char *s);
uint32_t uart_bytes_sent(void);

void     sensor_init(uint32_t seed);
int16_t  sensor_read(void);

void     log_msg(const char *tag, const char *msg);

#endif
