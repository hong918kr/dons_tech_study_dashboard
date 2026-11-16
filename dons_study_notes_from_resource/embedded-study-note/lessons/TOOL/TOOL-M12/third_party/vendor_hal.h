/* third_party/vendor_hal.h - stand-in for a vendor HAL / CMSIS-style header.
 * We do not own this file and must not edit it (the next vendor drop would
 * overwrite our changes).  It works, but it is written in the classic vendor
 * style: unparenthesized macro arguments and reserved "__HAL_" names. */
#ifndef VENDOR_HAL_H
#define VENDOR_HAL_H
#include <stdint.h>

#define HAL_UART_SR_RXNE_POS 5u
#define HAL_BIT(n)           1u << n
#define __HAL_UART_FLAG(sr, pos) ((sr) & (1u << pos))

static inline uint32_t __HAL_UART_BRR(uint32_t pclk, uint32_t baud)
{
    return (pclk + baud / 2u) / baud;
}

#endif
