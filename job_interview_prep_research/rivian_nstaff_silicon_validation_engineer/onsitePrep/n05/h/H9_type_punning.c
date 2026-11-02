/* H9 · type punning done right: memcpy (or a union in C), shifts for endian. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t float_bits(float f) {        /* the portable way */
    uint32_t u;
    _Static_assert(sizeof u == sizeof f, "32-bit float");
    memcpy(&u, &f, sizeof u);                /* compiles to one fmov at -O2 */
    return u;
}
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

static uint32_t float_bits_union(float f) {  /* OK in C (C11 6.5.2.3 fn 95); NOT in C++ */
    union { float f; uint32_t u; } x = { .f = f };
    return x.u;
}

static uint32_t load32_native(const uint8_t *p) {  /* any alignment, host order */
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}
static uint32_t load_le32(const uint8_t *p) {      /* any alignment, fixed order */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int main(void) {
    assert(float_bits(1.0f) == 0x3F800000u && float_bits(-2.0f) == 0xC0000000u);
    assert(float_bits_union(1.0f) == 0x3F800000u);
    assert(bits_float(0x40490FDBu) > 3.14159f && bits_float(0x40490FDBu) < 3.14160f);

    static const uint8_t buf[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    assert(load_le32(buf) == 0x78563412u && load_be32(buf) == 0x12345678u);
    assert(load_le32(buf + 1) == 0x9A785634u && load_be32(buf + 1) == 0x3456789Au);
    const uint16_t probe = 1;                      /* endianness check via char view */
    int little = *(const uint8_t *)&probe == 1;    /* char-type access is allowed */
    assert(load32_native(buf + 1) == (little ? 0x9A785634u : 0x3456789Au));

    printf("float_bits(1.0f)=0x%08X  le32(buf+1)=0x%08X  be32(buf+1)=0x%08X  host=%s\n",
           (unsigned)float_bits(1.0f), (unsigned)load_le32(buf + 1),
           (unsigned)load_be32(buf + 1), little ? "little" : "big");
    puts("H9 PASS");
    return 0;
}
