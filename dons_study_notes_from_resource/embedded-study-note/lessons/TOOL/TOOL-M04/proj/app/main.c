/* main.c — producer/consumer through the ring buffer, report count + CRC.
 * Usage: app            -> one report line, exit 0
 *        app <anything> -> "usage" on stderr, exit 2 (used by a negative CTest). */
#include <stdio.h>
#include "ringbuf.h"       /* public header: reachable ONLY via target_link_libraries(... ringbuf) */
#include "sensor.h"

#if defined(__has_include)
#if __has_include("build_info.h")   /* generated into the BUILD tree by configure_file (9. P3) */
#include "build_info.h"
#endif
#endif
#ifndef FW_VERSION_STR
#define FW_VERSION_STR "dev"
#endif

#define NSAMP 64u

int main(int argc, char **argv)
{
    rb_t rb;
    uint32_t got = 0, sum = 0;
    uint8_t b;

    (void)argv;
    if (argc > 1) {
        fprintf(stderr, "usage: app (no arguments)\n");
        return 2;
    }
    sensor_init(12345u);
    rb_init(&rb);
    for (uint32_t i = 0; i < NSAMP; i++) {
        if (rb_put(&rb, sensor_read()) != 0)
            return 1;
        if ((i & 3u) == 3u) {               /* consumer drains every 4th sample */
            while (rb_get(&rb, &b) == 0) {
                got++;
                sum += b;
            }
        }
    }
#if RB_HAVE_CRC
    printf("[app %s] samples=%u sum=%u crc=0x%04X\n", FW_VERSION_STR,
           (unsigned)got, (unsigned)sum, (unsigned)rb_running_crc(&rb));
#else
    printf("[app %s] samples=%u sum=%u crc=off\n", FW_VERSION_STR, (unsigned)got, (unsigned)sum);
#endif
    return 0;
}
