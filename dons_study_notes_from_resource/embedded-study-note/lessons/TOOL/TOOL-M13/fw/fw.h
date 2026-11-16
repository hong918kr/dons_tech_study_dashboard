/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 데모 펌웨어 공용 헤더. 대상: Cortex-M4 (QEMU mps2-an386 메모리 맵).
 * 로그 출력 방식은 컴파일 플래그 하나로 고른다:
 *   (기본)       log_printf = fmt.c 의 작은 포매터 → uart_putc  (libc 없이 -nostdlib 로 링크 가능)
 *   -DUSE_PRINTF log_printf = newlib printf → _write() → uart_write  (VM 의 newlib / newlib-nano 단계)
 */
#ifndef FW_H
#define FW_H
#include <stddef.h>
#include <stdint.h>

#define FW_VERSION "1.3.0"

/* ---- uart.c (libdrv.a) ---- */
void uart_init(void);
void uart_putc(char c);
void uart_write(const char *s, size_t n);
size_t uart_read_line(char *buf, size_t n); /* 안 씀 (dead) */

/* ---- crc.c ---- */
uint32_t crc32(const void *data, size_t n);
uint16_t crc16_ccitt(const void *data, size_t n);   /* 안 씀 (dead) */
uint32_t crc32_bitwise(const void *data, size_t n); /* 안 씀 (dead) */

/* ---- sensor.c (libdrv.a) ---- */
void sensor_push(int16_t raw);
int32_t sensor_avg(void);
int16_t sensor_min(void);
int16_t sensor_max(void);
int sensor_selftest(void);           /* 안 씀 (dead) */
void sensor_calibrate(int32_t gain); /* 안 씀 (dead) */

/* ---- 로그 ---- */
#ifdef USE_PRINTF
#include <stdio.h>
#define log_printf printf
#define log_flush() fflush(stdout) /* newlib stdout 은 FILE 버퍼를 거친다: 종료 전에 비운다 (방어용) */
#else
#define log_flush() ((void)0)
int log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_hexdump(const void *p, size_t n); /* 안 씀 (dead) */
#endif

/* ---- startup.c ---- */
extern volatile uint32_t g_ticks;
void systick_start(uint32_t reload);
__attribute__((noreturn)) void board_exit(int code);

#endif
