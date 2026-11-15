/* util.c — second translation unit */
#include "util.h"

const char fw_version[] = FW_VERSION;

uint32_t sectors_for(uint32_t bytes)
{
    return SECTORS_FOR(bytes);
}

uint8_t checksum8(const uint8_t *p, uint32_t n)
{
    uint8_t s = 0;
    while (n--)
        s = (uint8_t)(s + *p++);
    return (uint8_t)(0u - s);                /* two's-complement checksum: sum of all + csum == 0 */
}
