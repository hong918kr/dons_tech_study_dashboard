/* C-A11 demo — 스니펫 2·4 의 "부호 있는 shift" 를 UBSan 으로 본다.
 *
 *   cc -std=c11 -g -O1 -fsanitize=undefined -fno-omit-frame-pointer demo_shift.c -o demo_shift && ./demo_shift
 *   cc -std=c11 -O2 demo_shift.c -o demo_shift_plain && ./demo_shift_plain      # 같은 입력, 검사 없이
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#define PIN(n)  (1 << (n))                       /* 스니펫 4 원본 */

static uint32_t ts_char(const char *rx) {        /* 스니펫 2 원본: char 바이트 */
    return (uint32_t)(rx[4] | (rx[5] << 8) | (rx[6] << 16) | (rx[7] << 24));
}
static uint32_t ts_u8_int(const uint8_t *u) {    /* uint8_t 로 바꿨지만 캐스트 없이 shift */
    return (uint32_t)(u[4] | (u[5] << 8) | (u[6] << 16) | (u[7] << 24));
}
static uint32_t ts_ok(const uint8_t *u) {        /* 고친 판 */
    return (uint32_t)u[4] | ((uint32_t)u[5] << 8) | ((uint32_t)u[6] << 16) | ((uint32_t)u[7] << 24);
}

int main(void) {
    printf("char is %s (CHAR_MIN=%d)\n", CHAR_MIN < 0 ? "signed" : "unsigned", CHAR_MIN);
    const uint8_t f[8] = { 0xA5, 0x01, 0x00, 0x10, 0x78, 0x90, 0x34, 0x82 };   /* ts = 0x82349078 */
    volatile int pin = 15;                           /* 상수 접기를 막아 런타임 검사가 보이게 */
    printf("ts_char   = 0x%08X\n", (unsigned)ts_char((const char *)f));
    printf("ts_u8_int = 0x%08X\n", (unsigned)ts_u8_int(f));
    printf("ts_ok     = 0x%08X\n", (unsigned)ts_ok(f));
    printf("BSRR reset word for pin 15 = 0x%08X\n", (unsigned)(PIN(pin) << 16));
    return 0;
}
