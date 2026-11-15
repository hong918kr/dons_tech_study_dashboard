/* RTOS-M08 — fw_graph.c 의 타입과 함수 선언 (freestanding). demo_paint_measure.c 가 같이 쓴다. */
#ifndef FW_GRAPH_H
#define FW_GRAPH_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { int16_t ax, ay, az, gx, gy, gz, temp; } imu_raw_t;
typedef struct { float roll, pitch, yaw_rate; float hist[8]; uint32_t n; } att_t;
typedef struct { float kp, ki, kd, integ, prev, lo, hi; } pid_ctl_t;
typedef struct { att_t att; pid_ctl_t pid[3]; uint32_t cycle; } ctl_state_t;

extern volatile uint8_t  g_spi_fifo[32];
extern volatile uint32_t g_uart_dr;
extern volatile uint32_t g_pwm_ccr[3];
extern volatile uint16_t g_adc_dma[16];
extern uint8_t  g_uart_rx_ring[64];
extern uint32_t g_uart_rx_head, g_uart_rx_tail, g_uart_tx_count, g_tick_count, g_adc_avg;

void     spi_burst_read(uint8_t reg, uint8_t *dst, size_t n);
int      imu_read(imu_raw_t *out);
void     filter_update(att_t *s, const imu_raw_t *r);
float    pid_step(pid_ctl_t *p, float sp, float meas);
size_t   fmt_u32(char *dst, uint32_t v, unsigned base);
size_t   log_vformat(char *dst, size_t cap, const char *fmt, va_list ap);
void     uart_write(const char *s, size_t n);
void     log_printf(const char *fmt, ...);
void     control_step(ctl_state_t *st);
uint16_t crc16_ccitt(const uint8_t *p, size_t n);
size_t   cobs_encode(const uint8_t *in, size_t n, uint8_t *out);
void     comms_send_telemetry(const ctl_state_t *st, uint8_t seq);
int      rx_parse_once(void);
void     isr_uart_rx(void);
uint32_t adc_average(const volatile uint16_t *buf, size_t n);
void     isr_adc_dma(void);
void     isr_tick(void);
size_t   fmt_u32_rec(char *dst, uint32_t v);
void     ctrl_task_once(ctl_state_t *st);
void     comms_task_once(const ctl_state_t *st);
#endif
