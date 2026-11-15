/* bugs12.c - TOOL-B02: a small UART/sensor module with 12 planted bugs.
 * Each bug is tagged // BUGnn on the line a compiler reports.  Two bugs carry
 * a second tag because clang and GCC point at different lines (BUG01, BUG09).
 * Do NOT fix them: choose warning flags (in <variant>.flags) that flag all 12. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { ST_IDLE, ST_HEADER, ST_PAYLOAD, ST_CRC, ST_ERROR } rx_state_t;

int adc_average(const uint16_t *s, int n);
unsigned count_over(const int16_t *v, int n, unsigned limit);
size_t frame_payload_len(const uint8_t *buf, size_t len);
void report_uptime(uint64_t bytes_total);
uint8_t pwm_duty(int32_t percent_x10);
uint32_t temp_offset(int16_t raw);
int crc_ok(const uint8_t *p, size_t n);
__attribute__((warn_unused_result)) int flash_erase(uint32_t sector);
void storage_reset(void);
int uart_baud_div(int baud);
bool link_check(int retries);
size_t table_bytes(const uint32_t table[16]);
const char *state_name(rx_state_t st);

int adc_average(const uint16_t *s, int n)
{
    int sum;
    if (n > 0) {                                     // BUG01
        sum = 0;
        for (int i = 0; i < n; i++)
            sum += s[i];
    }
    return sum / (n > 0 ? n : 1);                    // BUG01 (gcc reports here)
}

unsigned count_over(const int16_t *v, int n, unsigned limit)
{
    unsigned c = 0;
    for (int i = 0; i < n; i++)
        if (v[i] > limit)                            // BUG02
            c++;
    return c;
}

size_t frame_payload_len(const uint8_t *buf, size_t len)
{
    size_t payload = 0;
    if (len >= 2 && buf[0] == 0x7E) {
        size_t len = buf[1];                         // BUG03
        payload = len;
    }
    return payload;
}

void report_uptime(uint64_t bytes_total)
{
    printf("bytes=%u\n", bytes_total);               // BUG04
}

uint8_t pwm_duty(int32_t percent_x10)
{
    uint8_t duty = percent_x10 * 255 / 1000;         // BUG05
    return duty;
}

uint32_t temp_offset(int16_t raw)
{
    int32_t off = raw - 400;
    return off;                                      // BUG06
}

int crc_ok(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++)
        c ^= p[i];
    if (c == 0)
        return 1;
}                                                    // BUG07

int flash_erase(uint32_t sector)
{
    return sector < 64 ? 0 : -1;
}

void storage_reset(void)
{
    flash_erase(0);                                  // BUG08
}

int uart_baud_div(int baud)
{
    int div = 0;
    switch (baud) {
    case 9600:
        div = 1666;                                  // BUG09 (gcc reports here)
    case 115200:                                     // BUG09
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
    if (retries = 0)                                 // BUG10
        return false;
    return true;
}

size_t table_bytes(const uint32_t table[16])
{
    return sizeof(table);                            // BUG11
}

const char *state_name(rx_state_t st)
{
    switch (st) {                                    // BUG12
    case ST_IDLE:    return "idle";
    case ST_HEADER:  return "header";
    case ST_PAYLOAD: return "payload";
    case ST_ERROR:   return "error";
    default:         return "?";
    }
}
