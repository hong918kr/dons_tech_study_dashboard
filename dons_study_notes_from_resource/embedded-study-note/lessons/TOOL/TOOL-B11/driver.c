/* TOOL-B11 test driver — one bug class per case, one case per process.
 *   ./t heap | stack | global | uaf | dfree | leak
 * Prints exactly one result line; check.sh compares it with the expected line
 * and greps stderr for "ERROR: AddressSanitizer".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

/* noinline: keep case_xxx as its own frame in ASan stack traces */
#define CASE static __attribute__((noinline)) int

CASE case_heap(void)
{
    static const char txt[] = "TEMP=23.5C";
    pkt_t *p = pkt_new(1, (const uint8_t *)txt, strlen(txt));
    char *s = pkt_payload_str(p);
    printf("heap: str=\"%s\" strlen=%zu\n", s, strlen(s));
    free(s);
    pkt_free(p);
    return 0;
}

CASE case_stack(void)
{
    static const uint8_t pl[] = { 1, 2, 3, 4, 5 };
    pkt_t *p = pkt_new(2, pl, sizeof pl);
    char out[32];
    pkt_hex_prefix(p, out, sizeof out);
    printf("stack: hex=%s\n", out);
    pkt_free(p);
    return 0;
}

CASE case_global(void)
{
    static const unsigned rx_kinds[] = { 0, 1, 2, 3, 3, 4, 7 };   /* 4, 7: line noise */
    int rejected = 0;
    for (size_t i = 0; i < sizeof rx_kinds / sizeof rx_kinds[0]; i++)
        if (pkt_count_kind(rx_kinds[i]) < 0)
            rejected++;
    printf("global: counts=%u,%u,%u,%u rejected=%d\n", (unsigned)pkt_kind_total(0),
           (unsigned)pkt_kind_total(1), (unsigned)pkt_kind_total(2),
           (unsigned)pkt_kind_total(3), rejected);
    return 0;
}

CASE case_uaf(void)
{
    static const uint8_t a[] = { 'h', 'i', '!' }, c[10] = { 0 };
    txq_t q = { NULL, NULL, 0 };
    txq_push(&q, pkt_new(0, a, sizeof a));          /* 4 + 3 + 2 = 9 */
    txq_push(&q, pkt_new(3, NULL, 0));              /* 4 + 0 + 2 = 6 */
    txq_push(&q, pkt_new(1, c, sizeof c));          /* 4 + 10 + 2 = 16 */
    size_t queued = q.count;
    size_t bytes = txq_drain(&q);
    printf("uaf: drained=%zu bytes=%zu left=%zu\n", queued, bytes, q.count);
    return 0;
}

CASE case_dfree(void)
{
    static const uint8_t pl[] = { 'a', 'b', 'c' };
    pkt_t *p = pkt_new(0, pl, sizeof pl);
    pkt_reset(p);
    printf("dfree: len=%zu frame=%s\n", p->len, p->frame == NULL ? "null" : "dangling");
    pkt_free(p);
    return 0;
}

CASE case_leak(void)
{
    static const uint8_t pl[] = { 0x10, 0x20, 0x30, 0x40 };
    pkt_t *tx = pkt_new(2, pl, sizeof pl);
    uint8_t wire[PKT_HDR_LEN + sizeof pl + PKT_CRC_LEN];
    memcpy(wire, tx->frame, sizeof wire);
    pkt_free(tx);

    int good = 0, bad = 0;
    for (int round = 0; round < 3; round++) {
        pkt_t *rx = pkt_from_wire(wire, sizeof wire);           /* CRC ok */
        good += rx != NULL;
        pkt_free(rx);
        wire[sizeof wire - 1] ^= 0x01u;                          /* corrupt CRC */
        rx = pkt_from_wire(wire, sizeof wire);
        bad += rx == NULL;
        pkt_free(rx);
        wire[sizeof wire - 1] ^= 0x01u;                          /* restore */
    }
    printf("leak: good=%d bad_rejected=%d\n", good, bad);
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* ASan/LSan may _exit(): keep the line */
    const char *c = argc > 1 ? argv[1] : "";
    if (strcmp(c, "heap") == 0)   return case_heap();
    if (strcmp(c, "stack") == 0)  return case_stack();
    if (strcmp(c, "global") == 0) return case_global();
    if (strcmp(c, "uaf") == 0)    return case_uaf();
    if (strcmp(c, "dfree") == 0)  return case_dfree();
    if (strcmp(c, "leak") == 0)   return case_leak();
    fprintf(stderr, "usage: %s heap|stack|global|uaf|dfree|leak\n", argv[0]);
    return 2;
}
