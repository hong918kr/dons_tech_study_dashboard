/*
 * V10  Ethernet MAC Loopback Test: frame build, FCS check, counters
 *
 * Implement the SW side of a MAC loopback test (no preamble/SFD: the MAC adds/strips those).
 *
 *   eth_crc32        CRC-32 IEEE 802.3: reflected poly 0xEDB88320, init 0xFFFFFFFF, final XOR
 *                    0xFFFFFFFF. Check value: eth_crc32("123456789", 9) == 0xCBF43926.
 *   eth_build_frame  DA(6) SA(6) [TPID 0x8100 + TCI(2) if vlan != NULL] EtherType(2) payload,
 *                    zero-pad so the frame is at least 60 bytes BEFORE the FCS (64 with FCS, tagged
 *                    or not), then append the 4-byte FCS computed over DA..pad.
 *                    The FCS goes on the wire LEAST significant byte first: out[n] = fcs & 0xFF ...
 *                    TCI = PCP(3) | DEI(1) | VID(12), big-endian like every other header field.
 *                    Returns the frame length (with FCS). payload > 1500 -> -EMSGSIZE,
 *                    out too small -> -ENOSPC, bad args (NULL, vid > 4095, pcp > 7) -> -EINVAL.
 *   eth_check_frame  Classify a received frame (DA..FCS) and update the counters:
 *                    len < 64 -> ETH_RUNT; len > 1518 (1522 if bytes 12..13 are 0x8100) -> ETH_OVERSIZE;
 *                    FCS mismatch -> ETH_FCS_ERR; else ETH_OK. Length checks come first.
 *                    On ETH_OK also count octets, VLAN-tagged, broadcast and the RMON size bucket.
 *   eth_loopback_test  Send n frames through the provided loopback (lb_xmit / lb_recv). Frame k has
 *                    DA = LB_DA, SA = LB_SA, type 0x88B5, no tag, and a payload of payload_len bytes:
 *                    bytes 0..3 = k (big-endian), byte i >= 4 = (uint8_t)(k * 7 + i).
 *                    After every lb_xmit drain lb_recv until it returns 0; drain once more at the end.
 *                    For every received frame: eth_check_frame; if OK, parse the sequence number and
 *                    compare the payload (mismatch or seq >= n -> data_mismatch); an OK frame whose
 *                    seq is lower than the highest seq seen so far is out_of_order.
 *                    Report: sent, received, good, corrupted (= received - good, any non-OK status),
 *                    lost (= sent - received), out_of_order, data_mismatch.
 *                    Return 0 if every frame came back good, in order and intact, else -EIO.
 *   No malloc.
 *
 * Reference solution.  Build & run:  ./run.sh V10 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define ETH_ALEN        6
#define ETH_HLEN        14      /* DA + SA + EtherType          */
#define ETH_VLAN_HLEN   4       /* TPID + TCI                   */
#define ETH_FCS_LEN     4
#define ETH_ZLEN        60      /* min frame without FCS        */
#define ETH_MIN_FRAME   64      /* min frame with FCS           */
#define ETH_MAX_FRAME   1518    /* max untagged frame with FCS  */
#define ETH_MAX_VLAN    1522    /* max tagged frame with FCS    */
#define ETH_DATA_LEN    1500    /* max payload (MTU)            */
#define ETH_P_8021Q     0x8100u

typedef enum { ETH_OK = 0, ETH_RUNT, ETH_OVERSIZE, ETH_FCS_ERR } EthStatus;

typedef struct { uint8_t pcp; uint8_t dei; uint16_t vid; } EthVlan;

/* RMON-like receive counters. hist: 64, 65-127, 128-255, 256-511, 512-1023, 1024-1518, 1519-1522 */
typedef struct {
    uint32_t frames_ok, octets_ok, vlan_ok, bcast_ok;
    uint32_t runt, oversize, fcs_err;
    uint32_t hist[7];
} EthCounters;

typedef struct {
    uint32_t sent, received, good, corrupted, lost, out_of_order, data_mismatch;
    EthCounters c;
} LbReport;

/* ---------------- loopback HAL (provided, fake at the bottom) ---------------- */
int    lb_xmit(const uint8_t *frame, size_t len);   /* 0 or -ENOBUFS               */
size_t lb_recv(uint8_t *buf, size_t cap);           /* frame length, 0 = nothing   */

static const uint8_t LB_DA[ETH_ALEN] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
static const uint8_t LB_SA[ETH_ALEN] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
#define LB_ETHERTYPE 0x88B5u    /* IEEE local experimental */
#define LB_MAX_FRAMES 256

/* ---------------- your code ---------------- */

uint32_t eth_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];                                     /* LSB-first: data bit 0 goes in first */
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get_be16(const uint8_t *p)  { return (uint16_t)((p[0] << 8) | p[1]); }

int eth_build_frame(const uint8_t dst[ETH_ALEN], const uint8_t src[ETH_ALEN], const EthVlan *vlan,
                    uint16_t ethertype, const uint8_t *payload, size_t len, uint8_t *out, size_t cap)
{
    if (dst == NULL || src == NULL || out == NULL || (payload == NULL && len > 0)) return -EINVAL;
    if (vlan && (vlan->vid > 0xFFF || vlan->pcp > 7 || vlan->dei > 1)) return -EINVAL;
    if (len > ETH_DATA_LEN) return -EMSGSIZE;

    size_t hdr = ETH_HLEN + (vlan ? ETH_VLAN_HLEN : 0);
    size_t body = hdr + len;
    if (body < ETH_ZLEN) body = ETH_ZLEN;                /* pad to 60 even when tagged */
    size_t total = body + ETH_FCS_LEN;
    if (cap < total) return -ENOSPC;

    memcpy(out, dst, ETH_ALEN);
    memcpy(out + 6, src, ETH_ALEN);
    size_t pos = 12;
    if (vlan) {
        put_be16(out + pos, ETH_P_8021Q);
        put_be16(out + pos + 2, (uint16_t)((vlan->pcp << 13) | (vlan->dei << 12) | vlan->vid));
        pos += 4;
    }
    put_be16(out + pos, ethertype);
    pos += 2;
    if (len) memcpy(out + pos, payload, len);
    memset(out + pos + len, 0, body - pos - len);        /* zero padding */

    uint32_t fcs = eth_crc32(out, body);
    out[body + 0] = (uint8_t)(fcs);                      /* LSB first on the wire */
    out[body + 1] = (uint8_t)(fcs >> 8);
    out[body + 2] = (uint8_t)(fcs >> 16);
    out[body + 3] = (uint8_t)(fcs >> 24);
    return (int)total;
}

static unsigned rmon_bucket(size_t len)
{
    static const uint16_t hi[6] = { 64, 127, 255, 511, 1023, 1518 };
    for (unsigned i = 0; i < 6; i++)
        if (len <= hi[i]) return i;
    return 6;                                            /* 1519..1522 (tagged) */
}

EthStatus eth_check_frame(const uint8_t *f, size_t len, EthCounters *c)
{
    if (len < ETH_MIN_FRAME) { c->runt++; return ETH_RUNT; }
    bool tagged = get_be16(f + 12) == ETH_P_8021Q;
    if (len > (tagged ? ETH_MAX_VLAN : ETH_MAX_FRAME)) { c->oversize++; return ETH_OVERSIZE; }

    size_t body = len - ETH_FCS_LEN;
    uint32_t rx = (uint32_t)f[body] | ((uint32_t)f[body + 1] << 8) |
                  ((uint32_t)f[body + 2] << 16) | ((uint32_t)f[body + 3] << 24);
    if (eth_crc32(f, body) != rx) { c->fcs_err++; return ETH_FCS_ERR; }

    c->frames_ok++;
    c->octets_ok += (uint32_t)len;
    if (tagged) c->vlan_ok++;
    static const uint8_t bcast[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    if (memcmp(f, bcast, ETH_ALEN) == 0) c->bcast_ok++;
    c->hist[rmon_bucket(len)]++;
    return ETH_OK;
}

static uint8_t lb_byte(uint32_t seq, size_t i) { return (uint8_t)(seq * 7u + i); }

int eth_loopback_test(uint16_t n, size_t payload_len, LbReport *r)
{
    if (r == NULL || n > LB_MAX_FRAMES || payload_len < 4 || payload_len > ETH_DATA_LEN) return -EINVAL;
    memset(r, 0, sizeof *r);

    static uint8_t tx[ETH_MAX_VLAN], rx[ETH_MAX_VLAN + 64], pl[ETH_DATA_LEN];
    int32_t highest = -1;

    for (uint32_t k = 0; k <= n; k++) {
        if (k < n) {
            pl[0] = (uint8_t)(k >> 24); pl[1] = (uint8_t)(k >> 16);
            pl[2] = (uint8_t)(k >> 8);  pl[3] = (uint8_t)k;
            for (size_t i = 4; i < payload_len; i++) pl[i] = lb_byte(k, i);
            int len = eth_build_frame(LB_DA, LB_SA, NULL, LB_ETHERTYPE, pl, payload_len, tx, sizeof tx);
            if (len < 0) return len;
            if (lb_xmit(tx, (size_t)len) == 0) r->sent++;
        }
        size_t got;                                      /* drain (k == n: final drain) */
        while ((got = lb_recv(rx, sizeof rx)) != 0) {
            r->received++;
            if (eth_check_frame(rx, got, &r->c) != ETH_OK) continue;
            r->good++;

            const uint8_t *p = rx + ETH_HLEN;
            uint32_t seq = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            bool ok = seq < n && got >= ETH_HLEN + payload_len + ETH_FCS_LEN &&
                      get_be16(rx + 12) == LB_ETHERTYPE;
            for (size_t i = 4; ok && i < payload_len; i++) ok = p[i] == lb_byte(seq, i);
            if (!ok) { r->data_mismatch++; continue; }

            if ((int32_t)seq < highest) r->out_of_order++;
            else highest = (int32_t)seq;
        }
    }
    r->corrupted = r->received - r->good;
    r->lost = r->sent >= r->received ? r->sent - r->received : 0;
    bool pass = r->good == n && r->received == n && r->out_of_order == 0 && r->data_mismatch == 0;
    return pass ? 0 : -EIO;
}

/* ======================================================================
 * Test harness: fake MAC/PHY loopback with fault injection (do not edit)
 *   Faults apply to the k-th transmitted frame (0-based); -1 = off.
 *   drop      : frame disappears
 *   flip      : one bit flipped on the wire (FCS stays the old one)
 *   trunc     : frame cut to trunc_len bytes
 *   swap      : frame k is delivered after frame k+1
 *   refcs     : payload bit flipped INSIDE the MAC before FCS generation (FCS is valid!)
 * ====================================================================== */
#define LBQ 4
static struct {
    uint8_t  q[LBQ][ETH_MAX_VLAN]; size_t qlen[LBQ]; unsigned head, count;
    uint8_t  held[ETH_MAX_VLAN];   size_t held_len;  bool holding;
    int32_t  drop, flip, trunc, swap, refcs;
    size_t   flip_byte, trunc_len; uint8_t flip_bit;
    uint32_t tx_count;
} L;

static void fake_reset(void)
{
    memset(&L, 0, sizeof L);
    L.drop = L.flip = L.trunc = L.swap = L.refcs = -1;
}

static uint32_t fake_crc(const uint8_t *p, size_t n)   /* own copy: the fake never calls your code */
{
    uint32_t c = ~0u;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1; }
    return ~c;
}

static void fake_push(const uint8_t *f, size_t len)
{
    if (L.count == LBQ) return;
    unsigned t = (L.head + L.count) % LBQ;
    memcpy(L.q[t], f, len); L.qlen[t] = len; L.count++;
}

int lb_xmit(const uint8_t *frame, size_t len)
{
    if (len > ETH_MAX_VLAN || L.count == LBQ) return -ENOBUFS;
    int32_t k = (int32_t)L.tx_count++;
    uint8_t w[ETH_MAX_VLAN];
    memcpy(w, frame, len);
    if (k == L.drop) return 0;
    if (k == L.refcs) {                                  /* corrupt, then MAC regenerates FCS */
        w[ETH_HLEN + 5] ^= 0x10;
        uint32_t c = fake_crc(w, len - 4);
        w[len - 4] = (uint8_t)c; w[len - 3] = (uint8_t)(c >> 8);
        w[len - 2] = (uint8_t)(c >> 16); w[len - 1] = (uint8_t)(c >> 24);
    }
    if (k == L.flip)  w[L.flip_byte] ^= (uint8_t)(1u << L.flip_bit);
    if (k == L.trunc) len = L.trunc_len;
    if (k == L.swap) { memcpy(L.held, w, len); L.held_len = len; L.holding = true; return 0; }
    fake_push(w, len);
    if (L.holding && k == L.swap + 1) { fake_push(L.held, L.held_len); L.holding = false; }
    return 0;
}

size_t lb_recv(uint8_t *buf, size_t cap)
{
    if (L.count == 0) return 0;
    size_t len = L.qlen[L.head];
    if (len > cap) len = cap;
    memcpy(buf, L.q[L.head], len);
    L.head = (L.head + 1) % LBQ; L.count--;
    return len;
}

int main(void)
{
    static uint8_t f[ETH_MAX_VLAN + 8];
    static uint8_t big[ETH_DATA_LEN + 1];
    const uint8_t bc[6]  = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    const uint8_t sa[6]  = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
    EthCounters c;
    LbReport r;

    /* 1. CRC-32 check value */
    CHECK(eth_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);

    /* 2. short payload: padded to 60 + FCS = 64, known FCS, LSB first, round-trip OK */
    memset(&c, 0, sizeof c);
    memset(f, 0xAA, sizeof f);
    int n = eth_build_frame(bc, sa, NULL, 0x0800, hello, 5, f, sizeof f);
    CHECK(n == 64);
    CHECK(f[12] == 0x08 && f[13] == 0x00 && f[14] == 'h' && f[18] == 'o');
    CHECK(f[19] == 0 && f[59] == 0);                                    /* zero padding */
    CHECK(eth_crc32(f, 60) == 0x32DD79F1u);
    CHECK(f[60] == 0xF1 && f[61] == 0x79 && f[62] == 0xDD && f[63] == 0x32);
    CHECK(f[64] == 0xAA);                                               /* nothing past the frame */
    CHECK(eth_check_frame(f, 64, &c) == ETH_OK);
    CHECK(c.frames_ok == 1 && c.octets_ok == 64 && c.bcast_ok == 1 && c.hist[0] == 1);
    CHECK(eth_crc32(f, 64) == 0x2144DF1Cu);                             /* CRC over data+FCS = magic residue */

    /* 3. no padding needed at 46 bytes; normal size */
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, big, 46, f, sizeof f) == 64);
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, big, 100, f, sizeof f) == 118);
    CHECK(eth_check_frame(f, 118, &c) == ETH_OK && c.hist[1] == 1);
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, big, 100, f, 117) == -ENOSPC);

    /* 4. runt: a truncated frame */
    memset(&c, 0, sizeof c);
    CHECK(eth_check_frame(f, 40, &c) == ETH_RUNT && c.runt == 1 && c.frames_ok == 0);

    /* 5. max sizes and oversize */
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, big, 1500, f, sizeof f) == 1518);
    CHECK(eth_check_frame(f, 1518, &c) == ETH_OK && c.hist[5] == 1);
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, big, 1501, f, sizeof f) == -EMSGSIZE);
    const EthVlan v100 = { 5, 0, 100 };
    CHECK(eth_build_frame(bc, sa, &v100, 0x0800, big, 1500, f, sizeof f) == 1522);
    CHECK(eth_check_frame(f, 1522, &c) == ETH_OK && c.vlan_ok == 1 && c.hist[6] == 1);
    f[12] = 0x08; f[13] = 0x00;                         /* same 1522 bytes, now untagged */
    uint32_t fx = eth_crc32(f, 1518);
    f[1518] = (uint8_t)fx; f[1519] = (uint8_t)(fx >> 8); f[1520] = (uint8_t)(fx >> 16); f[1521] = (uint8_t)(fx >> 24);
    CHECK(eth_check_frame(f, 1522, &c) == ETH_OVERSIZE && c.oversize == 1);

    /* 6. one flipped bit -> FCS error */
    memset(&c, 0, sizeof c);
    CHECK(eth_build_frame(bc, sa, NULL, 0x0800, hello, 5, f, sizeof f) == 64);
    f[30] ^= 0x04;
    CHECK(eth_check_frame(f, 64, &c) == ETH_FCS_ERR && c.fcs_err == 1 && c.frames_ok == 0);

    /* 7. VLAN tag: TCI layout, still padded to 64 */
    const EthVlan bad = { 0, 0, 4096 };
    CHECK(eth_build_frame(bc, sa, &bad, 0x0800, hello, 5, f, sizeof f) == -EINVAL);
    CHECK(eth_build_frame(bc, sa, &v100, 0x0800, hello, 5, f, sizeof f) == 64);
    CHECK(f[12] == 0x81 && f[13] == 0x00 && f[14] == 0xA0 && f[15] == 0x64);  /* PCP 5, VID 100 */
    CHECK(f[16] == 0x08 && f[17] == 0x00 && f[18] == 'h');
    memset(&c, 0, sizeof c);
    CHECK(eth_check_frame(f, 64, &c) == ETH_OK && c.vlan_ok == 1);

    /* 8. clean loopback */
    fake_reset();
    CHECK(eth_loopback_test(10, 50, &r) == 0);
    CHECK(r.sent == 10 && r.received == 10 && r.good == 10 && r.lost == 0);
    CHECK(r.corrupted == 0 && r.out_of_order == 0 && r.data_mismatch == 0);
    CHECK(r.c.frames_ok == 10 && r.c.octets_ok == 680 && r.c.hist[1] == 10);

    /* 9. drop + bit flip + truncation + reorder */
    fake_reset();
    L.drop = 2; L.flip = 4; L.flip_byte = 30; L.flip_bit = 3; L.trunc = 6; L.trunc_len = 30; L.swap = 7;
    CHECK(eth_loopback_test(10, 50, &r) == -EIO);
    CHECK(r.sent == 10 && r.received == 9 && r.good == 7 && r.lost == 1);
    CHECK(r.corrupted == 2 && r.c.fcs_err == 1 && r.c.runt == 1);
    CHECK(r.out_of_order == 1 && r.data_mismatch == 0);

    /* 10. corruption inside the MAC before FCS generation: FCS passes, payload compare catches it */
    fake_reset();
    L.refcs = 3;
    CHECK(eth_loopback_test(10, 50, &r) == -EIO);
    CHECK(r.good == 10 && r.c.fcs_err == 0 && r.data_mismatch == 1);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
