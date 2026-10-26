/* T2 byte stream -> packets -- "0xAA, len, payload, checksum = low 8 bits of sum(len+payload)" */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYNC 0xAA
#define MAX_PAYLOAD 64
#define MAX_FRAME (3 + MAX_PAYLOAD)

typedef void (*on_packet_fn)(const uint8_t *payload, size_t len, void *ctx);

typedef struct {
    uint8_t buf[MAX_FRAME];   /* never holds more than one frame */
    size_t len;
    uint32_t dropped, bad_chk;
} parser_t;

static void drop_front(parser_t *p, size_t n)
{
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

static void process(parser_t *p, on_packet_fn cb, void *ctx)
{
    for (;;) {
        size_t i = 0;
        while (i < p->len && p->buf[i] != SYNC)
            i++;
        p->dropped += (uint32_t)i;           /* garbage before sync */
        drop_front(p, i);
        if (p->len < 2)                      /* need the length byte */
            return;
        size_t n = p->buf[1];
        if (n > MAX_PAYLOAD) {               /* fake sync -> drop ONE byte, retry */
            drop_front(p, 1);
            p->dropped++;
            continue;
        }
        if (p->len < 3 + n)                  /* partial: wait for more */
            return;
        uint8_t sum = 0;
        for (size_t k = 1; k < 2 + n; k++)
            sum += p->buf[k];                /* uint8_t wraps = low 8 bits */
        if (sum != p->buf[2 + n]) {
            drop_front(p, 1);                /* resync from the NEXT byte */
            p->bad_chk++;
            p->dropped++;
            continue;
        }
        cb(p->buf + 2, n, ctx);
        drop_front(p, 3 + n);
    }
}

void parser_feed(parser_t *p, const uint8_t *chunk, size_t n, on_packet_fn cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {        /* one byte in, then process: buf can't overflow */
        p->buf[p->len++] = chunk[i];
        process(p, cb, ctx);
    }
}

/* ---- tests ---- */
typedef struct { uint8_t data[256]; size_t n; int packets; } sink_t;

static void collect(const uint8_t *payload, size_t len, void *ctx)
{
    sink_t *s = ctx;
    s->data[s->n++] = (uint8_t)len;          /* record "len, payload..." */
    memcpy(s->data + s->n, payload, len);
    s->n += len;
    s->packets++;
}

static size_t frame(uint8_t *out, const uint8_t *payload, uint8_t n)
{
    uint8_t sum = n;
    out[0] = SYNC;
    out[1] = n;
    for (uint8_t i = 0; i < n; i++) {
        out[2 + i] = payload[i];
        sum += payload[i];
    }
    out[2 + n] = sum;
    return 3u + n;
}

int main(void)
{
    uint8_t stream[64];
    size_t len = 0;
    stream[len++] = 0x00;                                  /* garbage */
    len += frame(stream + len, (const uint8_t *)"A", 1);
    len += frame(stream + len, NULL, 0);                   /* len 0 */
    len += frame(stream + len, (const uint8_t *)"BC", 2);
    const uint8_t want[] = {1, 'A', 0, 2, 'B', 'C'};

    parser_t p = {0};
    sink_t s = {0};
    parser_feed(&p, stream, len, collect, &s);             /* all in one chunk */
    assert(s.packets == 3 && s.n == sizeof want && memcmp(s.data, want, s.n) == 0);

    parser_t q = {0};
    sink_t t = {0};
    for (size_t i = 0; i < len; i++)                       /* one byte at a time */
        parser_feed(&q, stream + i, 1, collect, &t);
    assert(t.packets == 3 && memcmp(t.data, want, t.n) == 0);

    uint8_t bad[16];
    size_t bl = frame(bad, (const uint8_t *)"XY", 2);
    bad[bl - 1] ^= 0xFF;                                    /* bad checksum */
    bl += frame(bad + bl, (const uint8_t *)"Z", 1);
    parser_t r = {0};
    sink_t u = {0};
    parser_feed(&r, bad, bl, collect, &u);
    assert(u.packets == 1 && u.data[1] == 'Z' && r.bad_chk == 1);

    uint8_t junk[1000];
    memset(junk, 0x55, sizeof junk);
    parser_feed(&r, junk, sizeof junk, collect, &u);        /* garbage never grows the buffer */
    assert(r.len == 0);
    puts("t2 OK");
    return 0;
}
