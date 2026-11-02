/* march_cminus_whiteboard.c - BV09: the interview-sized March C- (function body <= 40 lines).
 *
 * The function is what Don writes on the whiteboard; main() is only a smoke test (clean memory
 * must pass, memory ends all-zero, n = 0 / n = 1 edges, unaligned length). Fault detection of the
 * same element sequence is proven in memtest_matrix.c (fault injection).
 *
 * Build/run (macOS):
 *   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
 *      -fsanitize=address,undefined march_cminus_whiteboard.c -o /tmp/march_wb && /tmp/march_wb
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- whiteboard part starts ---------------- */
typedef struct { size_t idx; uint32_t exp, act; int elem; } MemFail;

/* March C-: {any(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); any(r0)}  = 10N
 * returns 0 = pass, -1 = fail (*f = first failure). m must be uncached / device memory. */
int march_cminus(volatile uint32_t *m, size_t n, MemFail *f)
{
    static const struct { int up, rd, wr; uint32_t rv, wv; } el[6] = {
        { 1, 0, 1, 0,           0           },   /* M0 any(w0)     */
        { 1, 1, 1, 0,           0xFFFFFFFFu },   /* M1 up(r0,w1)   */
        { 1, 1, 1, 0xFFFFFFFFu, 0           },   /* M2 up(r1,w0)   */
        { 0, 1, 1, 0,           0xFFFFFFFFu },   /* M3 down(r0,w1) */
        { 0, 1, 1, 0xFFFFFFFFu, 0           },   /* M4 down(r1,w0) */
        { 1, 1, 0, 0,           0           },   /* M5 any(r0)     */
    };
    for (int e = 0; e < 6; e++) {
        for (size_t i = 0; i < n; i++) {
            size_t a = el[e].up ? i : n - 1 - i;     /* no unsigned a >= 0 bug */
            if (el[e].rd) {
                uint32_t v = m[a];
                if (v != el[e].rv) {
                    f->idx = a; f->exp = el[e].rv; f->act = v; f->elem = e;
                    return -1;                       /* stop at FIRST failure */
                }
            }
            if (el[e].wr)
                m[a] = el[e].wv;
        }
    }
    return 0;
}
/* ---------------- whiteboard part ends ---------------- */

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static uint32_t buf[1000];
    MemFail f = { 0, 0, 0, -1 };
    for (size_t i = 0; i < 1000; i++) buf[i] = 0xA5A5A5A5u;     /* power-up garbage */

    CHECK(march_cminus(buf, 1000, &f) == 0);
    int allzero = 1;
    for (size_t i = 0; i < 1000; i++) allzero &= (buf[i] == 0);
    CHECK(allzero);                                             /* M4 leaves background 0 */
    CHECK(march_cminus(buf, 0, &f) == 0);                       /* n = 0: no access at all */
    CHECK(march_cminus(buf + 3, 1, &f) == 0);                   /* n = 1, odd base */
    CHECK(march_cminus(buf + 1, 997, &f) == 0);                 /* unaligned length */
    CHECK(f.elem == -1);                                        /* f untouched on pass */

    printf("march_cminus whiteboard smoke test: %s\n", fails ? "FAIL" : "ALL PASS");
    return fails != 0;
}
