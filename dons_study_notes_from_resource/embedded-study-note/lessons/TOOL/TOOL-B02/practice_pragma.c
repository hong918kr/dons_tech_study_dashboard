/* practice_pragma.c - TOOL-B02 practice: silence ONE warning for ONE legacy
 * function, without touching the build flags and without leaking the
 * suppression into the rest of the file.
 *   cc ... <practice.flags> -Werror -c practice_pragma.c            -> clean
 *   cc ... <practice.flags> -DSCOPE_PROBE -c practice_pragma.c      -> the
 *       probe line after the legacy block must STILL get -Wsign-conversion */
#include <stddef.h>
#include <stdint.h>

uint16_t legacy_crc16(const uint8_t *p, int n);
uint32_t ring_index(int head, int size);

/* Legacy code copied from an old product: proven in the field, written for a
 * compiler that never warned.  Policy: do not rewrite it in this release. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
uint16_t legacy_crc16(const uint8_t *p, int n)
{
    uint16_t crc = 0xFFFF;
    size_t len = n;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}
#pragma GCC diagnostic pop

uint32_t ring_index(int head, int size)
{
#ifdef SCOPE_PROBE
    return head % size;                                 // PROBE
#else
    return (uint32_t)(head % size);
#endif
}
