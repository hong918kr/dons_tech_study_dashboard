/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 main.c — 부팅 로그 7줄을 UART 로 찍고 세미호스팅으로 종료한다.
 * 이 출력(golden_uart.txt)이 "다이어트 후에도 이미지가 똑같이 동작하는가" 의 기준이다.
 */
#include "fw.h"

uint32_t g_boot_count = 7; /* .data: 0 아닌 초기값 → startup 의 .data 복사가 맞아야 7 이 찍힌다 */

static int16_t fake_adc(unsigned i) { /* 결정적인 가짜 센서 값 (1000 ± 25) */
    return (int16_t)(975 + (int)((i * 37u) % 51u));
}

int main(void) {
    uart_init();
    log_printf("TOOL-M13 fw %s boot=%u\n", FW_VERSION, (unsigned)g_boot_count);
    log_printf("crc32(\"123456789\")=%08lx\n", (unsigned long)crc32("123456789", 9));

    for (unsigned i = 0; i < 40; i++) sensor_push(fake_adc(i));
    log_printf("adc avg=%ld min=%d max=%d\n", (long)sensor_avg(), sensor_min(), sensor_max());

    int deci = (int)sensor_avg() - 977; /* 섭씨 0.1도 단위라고 치자 */
#ifdef LOG_FLOAT
    log_printf("temp=%.1fC\n", deci / 10.0); /* newlib-nano 는 -u _printf_float 없으면 %f 를 못 찍는다 */
#else
    log_printf("temp=%d.%dC\n", deci / 10, deci % 10);
#endif
    log_printf("hex=%x pad=[%5d] [%-4s] %c%%\n", 0xBEEFu, -42, "ok", 'Z');

    systick_start(1000u);
    for (uint32_t n = 0; g_ticks < 3u && n < 20000000u; n++) {
    }
    log_printf("systick=%s\n", g_ticks >= 3u ? "ok" : "DEAD");
    g_boot_count++;
    log_printf("done boot=%u\n", (unsigned)g_boot_count);
    log_flush();
    return 0;
}
