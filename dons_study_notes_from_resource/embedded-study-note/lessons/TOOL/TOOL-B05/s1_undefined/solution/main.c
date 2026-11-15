/* main.c — 센서 노드 부팅 시퀀스 (TOOL-B05 s1: undefined reference 4종) */
#include <stdio.h>
#include "sensor.h"
#include "uart.h"
#include "crc.h"
#include "adc.h"

/* adc_read_raw 선언은 adc.h 한 곳에만 둔다 */

int main(void)
{
    static const uint8_t msg[] = "123456789";
    printf("selftest=%d\n", sensor_selftest());
    printf("crc8=0x%02X\n", crc8(msg, sizeof msg - 1));
    printf("adc_raw=%d adc_mv=%d\n", adc_read_raw(), adc_read_mv());
    uart_puts("UART OK\n");
    return 0;
}
