/* test_crc.c — black-box test of the CRC feature (built only when FEATURE_CRC=ON). */
#include <stdio.h>
#include <string.h>
#include "ringbuf.h"

static int pass, total;
#define CHECK(e) do { total++; if (e) { pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

int main(void)
{
    const uint8_t s[] = "123456789";
    rb_t rb;
    uint8_t b;

    CHECK(rb_crc16(s, 9) == 0x29B1u);             /* CRC-16/CCITT-FALSE check value */
    CHECK(rb_struct_size() == sizeof(rb_t));      /* consumer sees the same layout as the lib */
    rb_init(&rb);
    for (size_t i = 0; i < 9; i++) {              /* running CRC survives get() */
        (void)rb_put(&rb, s[i]);
        (void)rb_get(&rb, &b);
    }
    CHECK(rb_running_crc(&rb) == 0x29B1u);
    printf("test_crc: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
