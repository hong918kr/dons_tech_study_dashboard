/* test_dsp.c — host unit test for filter.c + crc.c (no HAL needed). */
#include <stdio.h>
#include <string.h>
#include "dsp.h"

static int pass, total;
#define CHECK(e) do { total++; if (e) { pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

int main(void)
{
    const uint8_t s[] = "123456789";
    mavg_t m;

    CHECK(crc16_ccitt(s, 9) == 0x29B1u);          /* CRC-16/CCITT-FALSE check value */
    mavg_init(&m);
    CHECK(mavg_push(&m, 8) == 8);
    int32_t y = 0;
    for (int i = 0; i < 100; i++)
        y = mavg_push(&m, 40);
    CHECK(y == 40);
    printf("test_dsp: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
