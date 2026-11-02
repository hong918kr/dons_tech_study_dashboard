#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- model answer ---- */
static void reverse(uint32_t *a, size_t lo, size_t hi)   /* [lo, hi) */
{
    while (hi - lo > 1) {                 /* hi > lo + 1, no size_t underflow */
        uint32_t t = a[lo];
        a[lo++] = a[--hi];
        a[hi] = t;
    }
}

/* Rotate left by k in place: [1 2 3 4 5], k=2 -> [3 4 5 1 2]. O(n) time, O(1) space. */
void rotate_left(uint32_t *a, size_t n, size_t k)
{
    if (n < 2)
        return;
    k %= n;
    if (k == 0)
        return;
    reverse(a, 0, k);
    reverse(a, k, n);
    reverse(a, 0, n);
}

/* Remove consecutive duplicates (sorted input -> unique). Returns new length. */
size_t dedup_sorted(uint32_t *a, size_t n)
{
    if (n == 0)
        return 0;
    size_t w = 1;                         /* a[0..w) is the kept prefix */
    for (size_t r = 1; r < n; r++)
        if (a[r] != a[w - 1])
            a[w++] = a[r];
    return w;
}
/* ---- end model answer ---- */

static bool eq(const uint32_t *a, const uint32_t *b, size_t n)
{
    return memcmp(a, b, n * sizeof *a) == 0;
}

int main(void)
{
    uint32_t a[] = {1, 2, 3, 4, 5};
    rotate_left(a, 5, 2);
    assert(eq(a, (uint32_t[]){3, 4, 5, 1, 2}, 5));

    uint32_t b[] = {1, 2, 3, 4, 5};
    rotate_left(b, 5, 7);                 /* k > n -> k % n = 2 */
    assert(eq(b, (uint32_t[]){3, 4, 5, 1, 2}, 5));

    uint32_t c[] = {1, 2, 3, 4, 5};
    rotate_left(c, 5, 5);                 /* k == n -> unchanged */
    assert(eq(c, (uint32_t[]){1, 2, 3, 4, 5}, 5));
    rotate_left(c, 5, 0);
    assert(eq(c, (uint32_t[]){1, 2, 3, 4, 5}, 5));
    rotate_left(c, 5, 4);                 /* == rotate right by 1 */
    assert(eq(c, (uint32_t[]){5, 1, 2, 3, 4}, 5));

    uint32_t one[] = {9};
    rotate_left(one, 1, 3);
    assert(one[0] == 9);
    rotate_left(NULL, 0, 3);              /* n == 0 is a no-op */

    /* exhaustive vs naive for n <= 9 */
    for (size_t n = 1; n <= 9; n++)
        for (size_t k = 0; k <= 2 * n; k++) {
            uint32_t x[9], y[9];
            for (size_t i = 0; i < n; i++)
                x[i] = (uint32_t)i;
            for (size_t i = 0; i < n; i++)
                y[i] = (uint32_t)((i + k) % n);
            rotate_left(x, n, k);
            assert(eq(x, y, n));
        }

    uint32_t d[] = {1, 1, 2, 3, 3, 3, 7};
    size_t m = dedup_sorted(d, 7);
    assert(m == 4 && eq(d, (uint32_t[]){1, 2, 3, 7}, 4));

    uint32_t e[] = {5, 5, 5};
    assert(dedup_sorted(e, 3) == 1 && e[0] == 5);
    uint32_t f[] = {1, 2, 3};
    assert(dedup_sorted(f, 3) == 3 && eq(f, (uint32_t[]){1, 2, 3}, 3));
    assert(dedup_sorted(NULL, 0) == 0);
    assert(dedup_sorted(f, 1) == 1);
    /* idempotent */
    assert(dedup_sorted(d, m) == m);

    puts("D5 PASS");
    return 0;
}
