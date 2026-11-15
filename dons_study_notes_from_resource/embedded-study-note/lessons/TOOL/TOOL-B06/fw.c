/* ENV: cross — demo_size.sh 가 thumbv7em(Mac clang) / arm-none-eabi-gcc(VM) 로 빌드한다. 호스트에서 실행 금지. */
/* TOOL-B06 size 실험용 "펌웨어 같은" 파일. 손잡이는 -D 플래그 하나씩.
 *   표(table) 저장 방식 : -DTBL_UNINIT | -DTBL_ZERO | -DTBL_INIT | -DTBL_CONST   (정확히 하나)
 *   문자열 출력         : -DWITH_STRINGS   (UART 레지스터에 직접 쓰는 uart_puts)
 *   포인터 표 함정      : -DSTR_PTRS_NONCONST (const char *msgs[] → 포인터 배열이 .data 로)
 *   printf 사용         : -DWITH_PRINTF    (uart_puts 대신 newlib printf)
 * main 은 절대 반환하지 않는다 (펌웨어 슈퍼루프 흉내). 호스트에서 실행하지 말 것 — UART 주소에 쓴다.
 */
#include <stdint.h>
#include <stddef.h>
#ifdef WITH_PRINTF
#include <stdio.h>
#endif

#define UART_DR (*(volatile uint32_t *)0x40011004u)   /* STM32F4 USART1->DR 흉내 */

/* ---- 512 바이트 표: 저장 방식 4가지 ---- */
#if defined(TBL_UNINIT)
uint16_t table[256];                     /* 초기값 없음 (-fno-common 이면 .bss)    */
#elif defined(TBL_ZERO)
uint16_t table[256] = {0};               /* 0 으로 초기화 → 여전히 .bss            */
#elif defined(TBL_INIT)
uint16_t table[256] = {1, 2, 3};         /* 0 아닌 초기값 하나라도 → .data 전체   */
#elif defined(TBL_CONST)
const uint16_t table[256] = {1, 2, 3};   /* const → .rodata (Flash 에만)           */
#else
#error "pick one of -DTBL_UNINIT / -DTBL_ZERO / -DTBL_INIT / -DTBL_CONST"
#endif

static uint8_t rx_ring[64];              /* 작은 수신 링 버퍼: 항상 .bss            */
uint32_t boot_count = 7;                 /* 0 아닌 초기값 → 항상 .data (4 B)        */

#ifdef WITH_STRINGS
#ifdef STR_PTRS_NONCONST
const char *msgs[] = {                   /* 포인터 자체는 const 아님 → 포인터 배열이 .data */
#else
const char *const msgs[] = {             /* 포인터도 const → 포인터 배열도 .rodata */
#endif
    "boot ok\n", "sensor timeout\n", "crc mismatch\n",
};
#ifndef WITH_PRINTF
static void uart_puts(const char *s) {
    while (*s) UART_DR = (uint8_t)*s++;
}
#endif
#endif

int main(void) {
    uint32_t acc = 0;
    for (unsigned i = 0; i < 256; i++) acc += table[i] * (i + 1u);
#if !defined(TBL_CONST)
    table[acc & 0xFFu] ^= 1u;            /* 쓰기가 있으니 RAM 에 있어야 한다 */
#endif
    rx_ring[acc & 63u] = (uint8_t)acc;
    boot_count++;
#ifdef WITH_STRINGS
    unsigned which = (unsigned)(acc % 3u);
#ifdef WITH_PRINTF
    printf("%s acc=%lu boot=%lu\n", msgs[which], (unsigned long)acc, (unsigned long)boot_count);
#else
    uart_puts(msgs[which]);
#endif
#endif
    UART_DR = acc + rx_ring[5];
    for (;;) {
    }
}
