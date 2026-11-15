/* TOOL-B12 practice — answers to "9. 추가 연습", all UB-free.
 * check.sh practice builds this with a STRICTER set than -fsanitize=undefined:
 *   -fsanitize=undefined,unsigned-integer-overflow,implicit-conversion
 *   -fno-sanitize-recover=all
 * (the last two catch *defined* but suspicious wrap / truncation), then again in
 * trap mode, then as a Cortex-M4 object. Intentional wrap is annotated. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#ifndef WRAP_OK   /* demo: -DWRAP_OK= removes the annotation */
#define WRAP_OK __attribute__((no_sanitize("unsigned-integer-overflow")))
#endif

/* P1. free-running 32-bit tick counter: elapsed time and "a is after b" */
WRAP_OK static uint32_t ticks_elapsed(uint32_t now, uint32_t then)
{
    return now - then;                            /* modulo 2^32 on purpose */
}

WRAP_OK static bool tick_after(uint32_t a, uint32_t b)
{
    /* a after b  <=>  0 < (a - b) mod 2^32 < 2^31  (pure unsigned, portable) */
    return (uint32_t)(a - b) - 1u < 0x7FFFFFFFu;
}

/* P2. Q15 multiply with saturation: -1.0 * -1.0 must give +0.99997 (0x7FFF) */
static int16_t q15_mul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * b;                   /* |p| <= 2^30: fits int32 */
    p /= 32768;                                   /* trunc toward 0, no >> of negatives */
    if (p > INT16_MAX) return INT16_MAX;
    return (int16_t)p;                            /* explicit, range already checked */
}

/* P3. bounds check that cannot overflow: is [off, off+len) inside [0, cap)? */
static bool span_ok(size_t cap, size_t off, size_t len)
{
    return off <= cap && len <= cap - off;        /* never computes off + len */
}

/* P4. unaligned-safe wire access (any address, explicit byte order) */
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] << 8 | p[1]);
}

/* P5. FNV-1a hash: multiply wraps by design -> annotate, don't disable globally */
WRAP_OK static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    /* P1 */
    CHECK(ticks_elapsed(100u, 40u) == 60u);
    CHECK(ticks_elapsed(5u, 0xFFFFFFF0u) == 21u);          /* across the wrap */
    CHECK(tick_after(5u, 0xFFFFFFF0u));
    CHECK(!tick_after(0xFFFFFFF0u, 5u));
    CHECK(!tick_after(7u, 7u));
    /* P2 */
    CHECK(q15_mul(16384, 16384) == 8192);                  /* 0.5 * 0.5 = 0.25 */
    CHECK(q15_mul(INT16_MIN, INT16_MIN) == INT16_MAX);     /* -1 * -1 saturates */
    CHECK(q15_mul(INT16_MIN, 16384) == -16384);            /* -1 * 0.5 */
    CHECK(q15_mul(-3, 16384) == -1);                       /* trunc toward 0 */
    /* P3 */
    CHECK(span_ok(64, 0, 64));
    CHECK(!span_ok(64, 60, 8));
    CHECK(!span_ok(64, 8, SIZE_MAX));                      /* off + len would wrap */
    CHECK(!span_ok(64, 65, 0));
    /* P4: odd offsets on purpose */
    _Alignas(4) uint8_t frame[12] = { 0 };
    put_le32(frame + 1, 0xDEADBEEFu);
    CHECK(frame[1] == 0xEF && frame[4] == 0xDE);
    CHECK(get_be16(frame + 3) == 0xADDEu);
    /* P5 */
    const uint8_t abc[3] = { 'a', 'b', 'c' };
    CHECK(fnv1a(abc, 0) == 2166136261u);
    CHECK(fnv1a(abc, 1) == 0xE40C292Cu);                   /* FNV-1a("a") */
    CHECK(fnv1a(abc, 3) == 0x1A47E90Bu);                   /* FNV-1a("abc") */

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
