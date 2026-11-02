/* I5 - 2D shmoo pass/fail grid -> per-voltage pass window and margin center.
 * Context: DDR read-eye style sweep: rows = voltage (Vref or VDD) steps,
 * cols = delay taps. Build/run: ./run.sh
 */
//@@BEGIN I5
#include <stdint.h>
#include <stddef.h>

typedef struct { int start; int len; } window_t;   /* len 0 -> no pass */

/* Longest run of consecutive pass (nonzero) cells; ties -> first run. */
window_t longest_pass(const uint8_t *row, int n)
{
    window_t best = { -1, 0 };
    int run_start = 0, run_len = 0;
    for (int i = 0; i < n; i++) {
        if (row[i]) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len > best.len) { best.start = run_start; best.len = run_len; }
        } else {
            run_len = 0;
        }
    }
    return best;
}

int window_center(window_t w) { return w.start + (w.len - 1) / 2; }

/* grid[r * ncols + c]. Returns the row with the widest window
 * (ties -> lower row) and its window in *w, or -1 if nothing passes. */
int best_row(const uint8_t *grid, int nrows, int ncols, window_t *w)
{
    int best = -1;
    window_t bw = { -1, 0 };
    for (int r = 0; r < nrows; r++) {
        window_t cur = longest_pass(&grid[(size_t)r * ncols], ncols);
        if (cur.len > bw.len) { bw = cur; best = r; }
    }
    if (w) *w = bw;
    return best;
}
//@@END I5

//@@BEGIN I5_circ
/* Follow-up: circular taps (phase wraps n-1 -> 0). Scan 2n, cap run at n. */
window_t longest_pass_circular(const uint8_t *row, int n)
{
    window_t best = { -1, 0 };
    int run_start = 0, run_len = 0;
    for (int j = 0; j < 2 * n; j++) {
        if (row[j % n]) {
            if (run_len == 0) run_start = j;
            if (run_len < n) run_len++;
            if (run_len > best.len) { best.start = run_start % n; best.len = run_len; }
        } else {
            run_len = 0;
        }
    }
    return best;   /* center = (start + (len-1)/2) % n */
}
//@@END I5_circ

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* test helper: '#' = pass, '.' = fail */
static void parse(uint8_t *dst, const char *s) { for (size_t i = 0; s[i]; i++) dst[i] = (s[i] == '#'); }

int main(void)
{
    uint8_t g[4 * 10];
    parse(&g[0],  "..##......");
    parse(&g[10], ".######.#.");
    parse(&g[20], "..#######.");
    parse(&g[30], "##.#.##.##");
    window_t w;
    assert(best_row(g, 4, 10, &w) == 2);
    assert(w.start == 2 && w.len == 7 && window_center(w) == 5);

    window_t r1 = longest_pass(&g[10], 10);
    assert(r1.start == 1 && r1.len == 6 && window_center(r1) == 3);   /* even len -> lower middle */
    window_t r3 = longest_pass(&g[30], 10);
    assert(r3.start == 0 && r3.len == 2);                             /* tie -> first run */

    /* a single hole splits the window */
    uint8_t h[8]; parse(h, "###.####");
    window_t wh = longest_pass(h, 8);
    assert(wh.start == 4 && wh.len == 4);

    /* run touching the right edge must still be counted */
    uint8_t e[6]; parse(e, "#..###");
    window_t we = longest_pass(e, 6);
    assert(we.start == 3 && we.len == 3);

    /* all pass / all fail / empty */
    uint8_t all[5]; memset(all, 1, 5);
    window_t wa = longest_pass(all, 5);
    assert(wa.start == 0 && wa.len == 5 && window_center(wa) == 2);
    uint8_t none[5] = { 0 };
    assert(longest_pass(none, 5).len == 0);
    assert(best_row(none, 1, 5, &w) == -1 && w.len == 0);
    assert(best_row(g, 0, 10, NULL) == -1);

    /* tie between rows -> lower row index */
    uint8_t t[2 * 4]; parse(&t[0], "##.."); parse(&t[4], "..##");
    assert(best_row(t, 2, 4, &w) == 0 && w.start == 0);

    /* circular */
    uint8_t c[9]; parse(c, "##....###");
    window_t wc = longest_pass_circular(c, 9);
    assert(wc.start == 6 && wc.len == 5 && (wc.start + (wc.len - 1) / 2) % 9 == 8);
    assert(longest_pass(c, 9).len == 3);          /* linear scan misses the wrap */
    window_t wca = longest_pass_circular(all, 5);
    assert(wca.start == 0 && wca.len == 5);
    assert(longest_pass_circular(none, 5).len == 0);

    printf("I5 PASS\n");
    return 0;
}
