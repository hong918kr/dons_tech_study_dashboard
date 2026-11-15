/* good.c - TOOL-B02: the same UART/sensor module written correctly.
 * Must build with the project warning set + -Werror and zero warnings,
 * and its self-test must print "good.c: 14/14 self-checks OK". */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "vendor_hal.h"

#if defined(__GNUC__)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH ((void)0)
#endif

typedef enum { ST_IDLE, ST_HEADER, ST_PAYLOAD, ST_CRC, ST_ERROR } rx_state_t;

int adc_average(const uint16_t *s, int n);
unsigned count_over(const int16_t *v, int n, int limit);
size_t frame_payload_len(const uint8_t *buf, size_t len);
void report_bytes(uint64_t bytes_total);
uint8_t pwm_duty(int32_t percent_x10);
int32_t temp_offset(int16_t raw);
bool crc_ok(const uint8_t *p, size_t n);
__attribute__((warn_unused_result)) int flash_erase(uint32_t sector);
int storage_reset(void);
int uart_baud_div(int baud);
bool link_check(int retries);
size_t table_bytes(const uint32_t table[16]);
const char *state_name(rx_state_t st);

int adc_average(const uint16_t *s, int n)
{
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += s[i];
    return n > 0 ? sum / n : 0;
}

unsigned count_over(const int16_t *v, int n, int limit)
{
    unsigned c = 0;
    for (int i = 0; i < n; i++)
        if (v[i] > limit)
            c++;
    return c;
}

size_t frame_payload_len(const uint8_t *buf, size_t len)
{
    size_t payload = 0;
    if (len >= 2 && buf[0] == 0x7E)
        payload = buf[1];
    return payload;
}

void report_bytes(uint64_t bytes_total)
{
    printf("bytes=%llu\n", (unsigned long long)bytes_total);
}

uint8_t pwm_duty(int32_t percent_x10)
{
    if (percent_x10 < 0)
        percent_x10 = 0;
    if (percent_x10 > 1000)
        percent_x10 = 1000;
    return (uint8_t)(percent_x10 * 255 / 1000);
}

int32_t temp_offset(int16_t raw)
{
    return (int32_t)raw - 400;
}

bool crc_ok(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++)
        c ^= p[i];
    return c == 0;
}

int flash_erase(uint32_t sector)
{
    return sector < 64 ? 0 : -1;
}

int storage_reset(void)
{
    return flash_erase(0);
}

int uart_baud_div(int baud)
{
    int div;
    int shift = 0;
    switch (baud) {
    case 19200:
        shift = 1;                      /* deliberate fall-through, annotated */
        FALLTHROUGH;
    case 9600:
        div = 1666 >> shift;
        break;
    case 115200:
        div = 139;
        break;
    default:
        div = -1;
        break;
    }
    return div;
}

bool link_check(int retries)
{
    if (retries == 0)
        return false;
    return true;
}

size_t table_bytes(const uint32_t table[16])
{
    (void)table;
    return 16 * sizeof(uint32_t);
}

const char *state_name(rx_state_t st)
{
    switch (st) {
    case ST_IDLE:    return "idle";
    case ST_HEADER:  return "header";
    case ST_PAYLOAD: return "payload";
    case ST_CRC:     return "crc";
    case ST_ERROR:   return "error";
    }
    return "?";
}

static int ok_count, total_count;
static void self_check(bool cond, const char *what)
{
    total_count++;
    if (cond)
        ok_count++;
    else
        printf("good.c: self-check failed: %s\n", what);
}

int main(void)
{
    const uint16_t adc[4] = {100, 200, 300, 400};
    const int16_t temps[4] = {-5, 10, 300, -400};
    const uint8_t frame[4] = {0x7E, 0x03, 0xAA, 0x55};
    const uint8_t zero_xor[3] = {0x12, 0x34, 0x26};
    const uint32_t table[16] = {0};

    self_check(adc_average(adc, 4) == 250, "adc_average");
    self_check(adc_average(adc, 0) == 0, "adc_average n=0");
    self_check(count_over(temps, 4, 0) == 2, "count_over");
    self_check(frame_payload_len(frame, 4) == 3, "frame_payload_len");
    self_check(pwm_duty(500) == 127 && pwm_duty(2000) == 255, "pwm_duty");
    self_check(temp_offset(100) == -300, "temp_offset");
    self_check(crc_ok(zero_xor, 3), "crc_ok");
    self_check(storage_reset() == 0, "storage_reset");
    self_check(uart_baud_div(19200) == 833 && uart_baud_div(9600) == 1666, "uart_baud_div fallthrough");
    self_check(uart_baud_div(115200) == 139, "uart_baud_div");
    self_check(!link_check(0) && link_check(3), "link_check");
    self_check(table_bytes(table) == 64, "table_bytes");
    self_check(state_name(ST_CRC)[0] == 'c', "state_name");
    self_check(hal_field_set(0xFFFFFFFFu, 4, 4, 0x5) == 0xFFFFFF5Fu
               && hal_prio_encode(3) == 0x30, "vendor_hal");
    report_bytes(UINT64_C(5000000000));
    printf("good.c: %d/%d self-checks OK\n", ok_count, total_count);
    return ok_count == total_count ? 0 : 1;
}
