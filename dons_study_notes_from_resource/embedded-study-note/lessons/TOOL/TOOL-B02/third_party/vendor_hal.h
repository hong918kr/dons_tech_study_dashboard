/* third_party/vendor_hal.h - stand-in for a vendor HAL / CMSIS-style header.
 * We do not own this file and must not edit it.  Its inline helpers are
 * correct for the values the vendor passes, but they are written without
 * casts, so -Wconversion / -Wsign-conversion flag them. */
#ifndef VENDOR_HAL_H
#define VENDOR_HAL_H
#include <stdint.h>

static inline uint32_t hal_field_set(uint32_t reg, int shift, int width, int val)
{
    uint32_t mask = ((1u << width) - 1u) << shift;
    return (reg & ~mask) | ((val << shift) & mask);
}

static inline uint8_t hal_prio_encode(int prio)
{
    return prio << 4;
}
#endif
