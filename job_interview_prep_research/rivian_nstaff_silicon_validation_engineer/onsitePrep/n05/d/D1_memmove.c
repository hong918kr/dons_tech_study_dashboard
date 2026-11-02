#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
/* Overlap-safe copy. Direction: dst below src -> copy front to back,
 * otherwise back to front. Word path only when both pointers are 4-aligned. */
void *my_memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {                 /* forward */
        if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
            for (; n >= 4; n -= 4, d += 4, s += 4)
                *(uint32_t *)d = *(const uint32_t *)s;
        }
        while (n--)
            *d++ = *s++;
    } else {                                           /* backward */
        d += n;
        s += n;
        if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
            for (; n >= 4; n -= 4) {
                d -= 4;
                s -= 4;
                *(uint32_t *)d = *(const uint32_t *)s;
            }
        }
        while (n--)
            *--d = *--s;
    }
    return dst;
}
/* ---- end model answer ---- */

#define WORDS 16
#define BYTES (WORDS * 4)

static void fill(uint8_t *p)
{
    for (int i = 0; i < BYTES; i++)
        p[i] = (uint8_t)(i * 7 + 1);
}

int main(void)
{
    /* storage declared as uint32_t so word access through uint32_t* is legal */
    uint32_t a_store[WORDS], b_store[WORDS];
    uint8_t *a = (uint8_t *)a_store, *b = (uint8_t *)b_store;

    /* example from the page */
    char m[] = "123456789";
    my_memmove(m + 2, m, 5);
    assert(strcmp(m, "121234589") == 0);
    char m2[] = "123456789";
    my_memmove(m2, m2 + 2, 5);
    assert(strcmp(m2, "345676789") == 0);

    /* exhaustive vs libc memmove: every (dst off, src off, len) in 64 bytes */
    for (size_t doff = 0; doff < 20; doff++)
        for (size_t soff = 0; soff < 20; soff++)
            for (size_t n = 0; n + (doff > soff ? doff : soff) <= BYTES; n++) {
                fill(a);
                fill(b);
                void *r = my_memmove(a + doff, a + soff, n);
                memmove(b + doff, b + soff, n);
                assert(r == a + doff);
                assert(memcmp(a, b, BYTES) == 0);
            }

    /* non-overlapping, n = 0, d == s */
    fill(a);
    fill(b);
    my_memmove(a, a, BYTES);
    my_memmove(a + 3, a + 1, 0);
    assert(memcmp(a, b, BYTES) == 0);

    puts("D1 PASS");
    return 0;
}
