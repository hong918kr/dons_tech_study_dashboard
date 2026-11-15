/* demo_noise.c - TOOL-B02: legal code that -Wconversion / -Wsign-conversion
 * still flags (the "noise" you pay for the two real bugs it catches).
 * Host:   cc -std=c11 -O2 -Wconversion -c demo_noise.c
 * Target: cc -std=c11 -O2 -Wconversion --target=thumbv7em-none-eabi \
 *            -mcpu=cortex-m4 -ffreestanding -c demo_noise.c              */
#include <stddef.h>
#include <stdint.h>

uint8_t  n1_add(uint8_t a, uint8_t b);
uint16_t n2_setbit(uint16_t reg, unsigned bit);
uint8_t  n3_shift(uint8_t x);
int      n4_len(const char *s, size_t n);
float    n5_scale(float v);
uint32_t n6_index(int i);
long     n7_ticks(uint32_t t);

uint8_t n1_add(uint8_t a, uint8_t b)
{
    return a + b;                       /* N1: uint8+uint8 is int (C-B02)    */
}

uint16_t n2_setbit(uint16_t reg, unsigned bit)
{
    reg |= 1u << bit;                   /* N2: compound assign from unsigned */
    return reg;
}

uint8_t n3_shift(uint8_t x)
{
    x <<= 1;                            /* N3: shift in int, store to uint8  */
    return x;
}

int n4_len(const char *s, size_t n)
{
    (void)s;
    return n;                           /* N4: size_t -> int                 */
}

float n5_scale(float v)
{
    return v * 0.5;                     /* N5: double literal                */
}

uint32_t n6_index(int i)
{
    return i;                           /* N6: int -> uint32_t               */
}

long n7_ticks(uint32_t t)
{
    return t;                           /* N7: uint32_t -> long              */
}
