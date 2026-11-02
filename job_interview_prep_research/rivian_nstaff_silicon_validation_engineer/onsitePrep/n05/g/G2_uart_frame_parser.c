/* G2 - UART frame parser: SOF 0xAA | LEN | payload[LEN] | CHK, byte-at-a-time FSM + resync */
/* --- model answer --- */
#include <stdint.h>
#include <stdbool.h>

#define SOF      0xAAu
#define MAX_LEN  32u
/* CHK = (LEN + payload[0] + ... + payload[LEN-1]) mod 256 */

typedef enum { ST_IDLE, ST_LEN, ST_PAYLOAD, ST_CHK } pstate_t;
typedef enum { PR_NONE, PR_FRAME, PR_ERR_LEN, PR_ERR_CHK } presult_t;

typedef struct {
    pstate_t st;
    uint8_t  len, idx, sum;
    uint8_t  buf[MAX_LEN];
    uint32_t frames, errors;    /* stats for the validation log */
} parser_t;

void parser_init(parser_t *p) { p->st = ST_IDLE; p->frames = p->errors = 0; }

/* Feed one received byte. PR_FRAME => p->buf[0..p->len-1] holds a valid payload. */
presult_t parser_feed(parser_t *p, uint8_t b)
{
    switch (p->st) {
    case ST_IDLE:
        if (b == SOF) p->st = ST_LEN;          /* anything else: noise, drop it */
        return PR_NONE;
    case ST_LEN:
        if (b > MAX_LEN) {
            p->errors++;
            p->st = (b == SOF) ? ST_LEN : ST_IDLE;   /* "AA AA 03 ..": 2nd AA is the real SOF */
            return PR_ERR_LEN;
        }
        p->len = b; p->idx = 0; p->sum = b;
        p->st = (b == 0) ? ST_CHK : ST_PAYLOAD;
        return PR_NONE;
    case ST_PAYLOAD:
        p->buf[p->idx++] = b;
        p->sum = (uint8_t)(p->sum + b);
        if (p->idx == p->len) p->st = ST_CHK;
        return PR_NONE;
    case ST_CHK:
        p->st = ST_IDLE;
        if (b != p->sum) { p->errors++; return PR_ERR_CHK; }
        p->frames++;
        return PR_FRAME;
    }
    p->st = ST_IDLE;                            /* corrupted state: recover */
    return PR_NONE;
}
/* --- tests --- */
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* feed a byte stream, count frames/errors, remember last frame */
static void feed(parser_t *p, const uint8_t *s, size_t n, int *nf, int *ne,
                 uint8_t *last, uint8_t *last_len)
{
    *nf = *ne = 0;
    for (size_t i = 0; i < n; i++) {
        presult_t r = parser_feed(p, s[i]);
        if (r == PR_FRAME) { (*nf)++; memcpy(last, p->buf, p->len); *last_len = p->len; }
        else if (r != PR_NONE) (*ne)++;
    }
}

int main(void)
{
    parser_t p; int nf, ne; uint8_t last[MAX_LEN]; uint8_t ll = 0;

    /* normal frame: AA 03 10 20 30 CHK=(3+0x10+0x20+0x30)=0x63 */
    const uint8_t f1[] = {0xAA, 0x03, 0x10, 0x20, 0x30, 0x63};
    parser_init(&p);
    feed(&p, f1, sizeof f1, &nf, &ne, last, &ll);
    assert(nf == 1 && ne == 0 && ll == 3 && last[0] == 0x10 && last[2] == 0x30);

    /* leading garbage + two back-to-back frames, payload contains 0xAA (must not resync) */
    const uint8_t f2[] = {0x00, 0x55, 0xAA, 0x02, 0xAA, 0x01, 0xAD,   /* 2+AA+01 = AD */
                          0xAA, 0x00, 0x00};                        /* LEN 0, CHK 0 */
    parser_init(&p);
    feed(&p, f2, sizeof f2, &nf, &ne, last, &ll);
    assert(nf == 2 && ne == 0 && ll == 0);

    /* bad checksum -> error, then the next good frame still parses */
    const uint8_t f3[] = {0xAA, 0x01, 0x05, 0x00,  0xAA, 0x01, 0x05, 0x06};
    parser_init(&p);
    feed(&p, f3, sizeof f3, &nf, &ne, last, &ll);
    assert(nf == 1 && ne == 1 && p.errors == 1 && p.frames == 1 && last[0] == 0x05);

    /* LEN too big -> error; doubled SOF "AA AA" resyncs on the 2nd AA */
    const uint8_t f4[] = {0xAA, 0x40,  0xAA, 0xAA, 0x01, 0x07, 0x08};
    parser_init(&p);
    feed(&p, f4, sizeof f4, &nf, &ne, last, &ll);
    assert(nf == 1 && ne == 2 && last[0] == 0x07);

    /* MAX_LEN boundary: 32 bytes of 0xFF, sum = 32 + 32*0xFF = 0x20 + 0x1FE0 -> 0x00 */
    uint8_t f5[2 + MAX_LEN + 1];
    f5[0] = 0xAA; f5[1] = MAX_LEN;
    memset(&f5[2], 0xFF, MAX_LEN);
    f5[2 + MAX_LEN] = (uint8_t)(MAX_LEN + MAX_LEN * 0xFFu);
    parser_init(&p);
    feed(&p, f5, sizeof f5, &nf, &ne, last, &ll);
    assert(nf == 1 && ne == 0 && ll == MAX_LEN && last[31] == 0xFF);

    /* LEN = 33 rejected */
    const uint8_t f6[] = {0xAA, 33};
    parser_init(&p);
    feed(&p, f6, sizeof f6, &nf, &ne, last, &ll);
    assert(nf == 0 && ne == 1 && p.st == ST_IDLE);

    /* empty stream / pure noise */
    parser_init(&p);
    feed(&p, (const uint8_t *)"hello", 5, &nf, &ne, last, &ll);
    assert(nf == 0 && ne == 0 && p.st == ST_IDLE);

    printf("G2 PASS\n");
    return 0;
}
