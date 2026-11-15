/* ARM-B02 — SPOILER: the hidden C sources of shared_mystery.S (1–6) and practice.S (7–10).
 * Don't open before you've tried to re-implement them from the asm.
 * shared_mystery.S / practice.S = the -O2 output of THIS file, mechanically cleaned:
 *     clang -S -O2 -fno-vectorize -fno-slp-vectorize -fno-asynchronous-unwind-tables demo_mystery_src.c
 * check_mystery_sync.sh proves the instruction streams are identical.
 */
#include <stddef.h>
#include <stdint.h>

/* ---- shared_mystery.S ---- */
uint32_t mystery1(int32_t a, int32_t b) {          /* |a - b| as unsigned, no UB */
    return a > b ? (uint32_t)a - (uint32_t)b : (uint32_t)b - (uint32_t)a;
}
long mystery2(long x, long lo, long hi) {           /* clamp, lo wins if lo > hi */
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
int mystery3(uint32_t x) {                          /* popcount of even bit positions */
    int n = 0;
    for (; x; x >>= 2) n += x & 1;
    return n;
}
size_t mystery4(const char *s) {                    /* token length: stop at <= ' ' (signed char!) */
    const char *p = s;
    while (*p > ' ') p++;
    return (size_t)(p - s);
}
struct sensor_tab { uint32_t n; uint16_t id[16]; int16_t temp[16]; };
int32_t mystery5(const struct sensor_tab *t, uint16_t id) {  /* SoA lookup, temp * 2.5 */
    for (uint32_t i = 0; i < t->n; i++)
        if (t->id[i] == id) return t->temp[i] * 5 / 2;
    return INT32_MIN;
}
uint32_t mystery6(uint32_t head, uint32_t tail) {    /* ring buffer (24 slots) fill count */
    return (head + 24 - tail) % 24;
}

/* ---- practice.S ---- */
uint32_t mystery7(uint32_t x) {                      /* decimal digit sum */
    uint32_t s = 0;
    while (x) { s += x % 10; x /= 10; }
    return s;
}
int32_t mystery8(int op, int32_t x) {                /* 10-case switch -> jump table */
    switch (op) {
    case 0: return x + 7;
    case 1: return x * 3;
    case 2: return x >> 2;
    case 3: return -x;
    case 4: return x ^ 0x55;
    case 5: return x / 8;
    case 6: return x & 0xF0;
    case 7: return x - 100;
    case 8: return x << 4;
    case 9: return ~x;
    default: return 0;
    }
}
uint32_t mystery9(uint32_t reg, uint32_t v) {        /* register field patch [10:4] */
    return (reg & ~0x7F0u) | ((v & 0x7Fu) << 4);
}
long mystery10(const long *a, size_t n, long (*f)(long)) {  /* sum of f(a[i]) */
    long s = 0;
    for (size_t i = 0; i < n; i++) s += f(a[i]);
    return s;
}
