/* TOOL-B11 packet buffer module — shared API for starter.c / solution.c / driver.c
 *
 * Wire frame:  [A5][kind][len_lo][len_hi][payload ... len bytes][crc_lo][crc_hi]
 *              |<---- PKT_HDR_LEN ---->|                       |<-PKT_CRC_LEN->|
 * CRC = CRC-16/CCITT-FALSE over header + payload, stored little endian.
 */
#ifndef TOOL_B11_BUF_H
#define TOOL_B11_BUF_H

#include <stddef.h>
#include <stdint.h>

#define PKT_SYNC        0xA5u
#define PKT_HDR_LEN     4u
#define PKT_CRC_LEN     2u
#define PKT_MAX_PAYLOAD 64u
#define PKT_NKINDS      4u      /* valid kinds: 0..3 */
#define PKT_HEX_BYTES   8u      /* pkt_hex_prefix dumps this many frame bytes */

typedef struct pkt {
    struct pkt *next;           /* tx queue link */
    uint8_t    *frame;          /* heap: header + payload + crc */
    size_t      len;            /* payload bytes */
    uint8_t     kind;
} pkt_t;

typedef struct {
    pkt_t *head, *tail;
    size_t count;
} txq_t;

uint16_t crc16_ccitt(const uint8_t *d, size_t n);
pkt_t   *pkt_new(uint8_t kind, const uint8_t *payload, size_t n); /* NULL: bad kind/len/OOM */
void     pkt_free(pkt_t *p);                                       /* NULL is fine */
size_t   pkt_frame_len(const pkt_t *p);

/* Part A — overflows */
char    *pkt_payload_str(const pkt_t *p);          /* heap copy + '\0'; caller frees */
void     pkt_hex_prefix(const pkt_t *p, char *out, size_t outsz); /* "A5020500..." */
int      pkt_count_kind(unsigned kind);            /* -1 if kind invalid, else new count */
uint32_t pkt_kind_total(unsigned kind);            /* 0 if kind invalid */

/* Part B — lifetime */
void     txq_push(txq_t *q, pkt_t *p);
size_t   txq_drain(txq_t *q);                      /* "send" + free all; returns wire bytes */
void     pkt_reset(pkt_t *p);                      /* drop frame storage, keep struct */
pkt_t   *pkt_from_wire(const uint8_t *raw, size_t n); /* NULL if malformed / CRC error */

#endif
