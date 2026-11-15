/* demo_gcc_only.c - TOOL-B02: bugs that only GCC-specific warnings flag.
 * VM: gcc -std=c11 -O2 -Wall -Wextra -Wlogical-op -Wduplicated-cond \
 *         -Wduplicated-branches -Wjump-misses-init -c demo_gcc_only.c        */
#include <stdbool.h>
#include <stdint.h>

#define UART_SR_RXNE 0x20u
#define UART_SR_ORE  0x08u

bool g1_status(uint32_t sr);
int  g2_mode(int code);
int  g3_scale(int raw, bool fast);
int  g4_parse(const uint8_t *p, int n);

bool g1_status(uint32_t sr)
{
    return (sr & UART_SR_RXNE) && (sr & UART_SR_RXNE);      /* G1: meant ORE    */
}

int g2_mode(int code)
{
    if (code == 1)
        return 10;
    else if (code == 2)
        return 20;
    else if (code == 1)                                     /* G2: meant 3      */
        return 30;
    return 0;
}

int g3_scale(int raw, bool fast)
{
    int v;
    if (fast)
        v = raw * 4;
    else
        v = raw * 4;                                        /* G3: meant * 2    */
    return v;
}

int g4_parse(const uint8_t *p, int n)
{
    if (n < 1)
        goto fail;
    int hdr = p[0];                                         /* G4 */
    return hdr;
fail:
    return -1;
}
