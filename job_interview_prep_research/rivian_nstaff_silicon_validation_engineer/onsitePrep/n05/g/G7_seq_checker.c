/* G7 - streaming 16-bit frame-counter checker: gap / duplicate / wrap / reorder / reset */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>

#define MAX_GAP     1024u   /* forward jump > this = counter reset/jump, not "lost frames" */
#define REORDER_WIN 8u      /* backward step <= this = late (out-of-order) frame */

typedef struct {
    bool     have_prev;
    uint16_t prev;          /* baseline = highest in-order counter so far */
    uint32_t frames, lost, dup, ooo, resync;
} seq_t;

void seq_init(seq_t *s) { *s = (seq_t){0}; }

void seq_feed(seq_t *s, uint16_t cnt)
{
    s->frames++;
    if (!s->have_prev) { s->have_prev = true; s->prev = cnt; return; }

    uint16_t fwd  = (uint16_t)(cnt - s->prev);    /* wrap-safe: 65535 -> 0 gives 1 */
    uint16_t back = (uint16_t)(s->prev - cnt);

    if (fwd == 0) {
        s->dup++;                                 /* same frame twice */
    } else if (fwd <= MAX_GAP) {
        s->lost += fwd - 1u;                      /* 0 when fwd == 1 */
        s->prev = cnt;
    } else if (back <= REORDER_WIN) {
        s->ooo++;                                 /* late frame: keep baseline */
    } else {
        s->resync++;                              /* sensor reset / big jump: re-baseline */
        s->prev = cnt;
    }
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>

static void feed(seq_t *s, const uint16_t *c, int n) { for (int i = 0; i < n; i++) seq_feed(s, c[i]); }

int main(void)
{
    seq_t s;

    /* clean stream */
    const uint16_t a[] = {100, 101, 102, 103};
    seq_init(&s); feed(&s, a, 4);
    assert(s.frames == 4 && s.lost == 0 && s.dup == 0 && s.ooo == 0 && s.resync == 0);

    /* wrap 65534 65535 0 1 is clean; gap 1 -> 4 loses 2 */
    const uint16_t b[] = {65534, 65535, 0, 1, 4};
    seq_init(&s); feed(&s, b, 5);
    assert(s.lost == 2 && s.dup == 0 && s.resync == 0);

    /* gap across wrap: 65533 -> 2 loses 65534 65535 0 1 = 4 */
    const uint16_t c[] = {65533, 2};
    seq_init(&s); feed(&s, c, 2);
    assert(s.lost == 4);

    /* duplicate and out-of-order */
    const uint16_t d[] = {10, 11, 11, 13, 12, 14};
    seq_init(&s); feed(&s, d, 6);
    assert(s.dup == 1 && s.ooo == 1 && s.lost == 1 && s.prev == 14);
    /* note: 12 arrived late after being counted lost -> lost is an upper bound */

    /* sensor reset: 5000 -> 0 is a resync, not 60535 lost frames */
    const uint16_t e[] = {4999, 5000, 0, 1, 2};
    seq_init(&s); feed(&s, e, 5);
    assert(s.resync == 1 && s.lost == 0 && s.ooo == 0 && s.prev == 2);

    /* boundaries: fwd == MAX_GAP counts as loss, MAX_GAP+1 is resync; back == WIN is ooo, WIN+1 resync */
    seq_init(&s); seq_feed(&s, 0); seq_feed(&s, (uint16_t)MAX_GAP);
    assert(s.lost == MAX_GAP - 1 && s.resync == 0);
    seq_init(&s); seq_feed(&s, 0); seq_feed(&s, (uint16_t)(MAX_GAP + 1));
    assert(s.lost == 0 && s.resync == 1);
    seq_init(&s); seq_feed(&s, 3); seq_feed(&s, (uint16_t)(3 - REORDER_WIN));   /* 65531 */
    assert(s.ooo == 1 && s.prev == 3);
    seq_init(&s); seq_feed(&s, 3); seq_feed(&s, (uint16_t)(3 - REORDER_WIN - 1));
    assert(s.resync == 1 && s.ooo == 0);

    /* empty and single frame */
    seq_init(&s);
    assert(s.frames == 0);
    seq_feed(&s, 7);
    assert(s.frames == 1 && s.lost == 0 && s.prev == 7);

    printf("G7 PASS\n");
    return 0;
}
