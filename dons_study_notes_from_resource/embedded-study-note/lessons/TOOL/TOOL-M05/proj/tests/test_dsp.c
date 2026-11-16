/* test_dsp.c — HOST-only unit tests for lib/ (uses <stdio.h>: never built for the MCU).
 * Usage: test_dsp <crc|mavg|wrap>  — one CTest test per case. */
#include <stdio.h>
#include <string.h>
#include "dsp.h"

static int pass, total;
#define CHECK(e) do { total++; if (e) { pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    int all = strcmp(which, "all") == 0;
    mavg_t m;

    if (all || strcmp(which, "crc") == 0) {
        const uint8_t s[] = "123456789";
        CHECK(crc16_ccitt(s, 9) == 0x29B1u);      /* CRC-16/CCITT-FALSE check value */
        CHECK(crc16_ccitt(s, 0) == 0xFFFFu);      /* empty input = init value */
    }
    if (all || strcmp(which, "mavg") == 0) {
        mavg_init(&m);
        CHECK(mavg_push(&m, 8) == 8);             /* first sample: average of 1 */
        CHECK(mavg_push(&m, 4) == 6);             /* (8+4)/2 */
    }
    if (all || strcmp(which, "wrap") == 0) {
        int32_t y = 0;
        mavg_init(&m);
        for (int i = 0; i < 100; i++)
            y = mavg_push(&m, 40);
        CHECK(y == 40);                           /* ring index wrapped many times */
        CHECK(m.n == FILTER_TAPS && m.idx == 100 % FILTER_TAPS);
    }
    printf("test_dsp %s: %d/%d passed\n", which, pass, total);
    return (total > 0 && pass == total) ? 0 : 1;
}
