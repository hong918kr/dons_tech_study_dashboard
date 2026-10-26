/*
 * V9  MIPI CSI-2 Receiver Stream Checker
 *
 * A CSI-2 (v1.x, D-PHY) receiver hands you the merged byte stream of one capture
 * (lane merging and LP/SoT/EoT already removed): packets back to back.
 *
 *   Packet header (4 bytes, sent first to last):
 *     byte0  DI   = VC[7:6] | DT[5:0]
 *     byte1  WC[7:0]     \  16-bit word count, little endian
 *     byte2  WC[15:8]    /  (for a short packet this is the 16-bit data field)
 *     byte3  ECC  = 6-bit Hamming code P5..P0 over D[23:0] = byte0 | byte1<<8 | byte2<<16
 *                   (bits 7:6 are 0 in v1.x)
 *   DT 0x00..0x0F -> short packet (header only):  0x00 FS, 0x01 FE (data = frame number),
 *                                                 0x02 LS, 0x03 LE (data = line number)
 *   DT >= 0x10    -> long packet: header + WC payload bytes + 16-bit CRC (LSB first)
 *                    0x1E YUV422 8-bit (16 bpp), 0x2A RAW8, 0x2B RAW10, 0x2C RAW12
 *
 *   ECC parity equations (XOR of the listed data bits):
 *     P0 = D0 D1 D2 D4 D5 D7 D10 D11 D13 D16 D20 D21 D22 D23
 *     P1 = D0 D1 D3 D4 D6 D8 D10 D12 D14 D17 D20 D21 D22 D23
 *     P2 = D0 D2 D3 D5 D6 D9 D11 D12 D15 D18 D20 D21 D22
 *     P3 = D1 D2 D3 D7 D8 D9 D13 D14 D15 D19 D20 D21 D23
 *     P4 = D4 D5 D6 D7 D8 D9 D16 D17 D18 D19 D20 D22 D23
 *     P5 = D10 D11 D12 D13 D14 D15 D16 D17 D18 D19 D21 D22 D23
 *   CRC-16: poly x^16+x^12+x^5+1, init 0xFFFF, bits processed LSB first (reflected,
 *           0x8408), no final XOR, computed over the payload only.
 *
 * Implement:
 *   csi2_ecc            6-bit ECC of a 24-bit header.
 *   csi2_header_decode  check the ECC; correct a single-bit error (data or ECC bit),
 *                       report a 2-bit error as uncorrectable. Fill *h unless uncorrectable.
 *   csi2_crc16          payload CRC.
 *   csi2_expected_wc    bytes per line for (dt, width); 0 if unknown DT or not byte aligned.
 *   csi2_check_stream   per-VC state machine: FS -> `height` long lines -> FE.
 *                       Count everything in Csi2Report (see field comments). An
 *                       uncorrectable header or a packet running past `len` stops the
 *                       parse (we lost sync): return -1 with stop_off = start of that
 *                       packet. Otherwise return 0 with stop_off = len. At the stop point
 *                       every frame still open counts as missing FE (and a bad frame).
 *   No malloc. Never read past buf[len-1].
 *
 * Reference solution.  Build & run:  ./run.sh V9 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define CSI2_DT_FS        0x00u
#define CSI2_DT_FE        0x01u
#define CSI2_DT_LS        0x02u
#define CSI2_DT_LE        0x03u
#define CSI2_DT_YUV422_8  0x1Eu
#define CSI2_DT_RAW8      0x2Au
#define CSI2_DT_RAW10     0x2Bu
#define CSI2_DT_RAW12     0x2Cu
#define CSI2_NUM_VC       4
#define CSI2_HDR_LEN      4u
#define CSI2_CRC_LEN      2u

typedef struct { uint8_t vc, dt; uint16_t wc; } Csi2Hdr;   /* wc = data field for short pkts */

enum { HDR_OK = 0, HDR_CORRECTED = 1, HDR_UNCORRECTABLE = -1 };

typedef struct {
    uint8_t  dt;       /* expected long-packet DT; 0 = this VC is not configured */
    uint16_t width;    /* pixels per line */
    uint16_t height;   /* lines per frame */
} Csi2VcCfg;

typedef struct {
    /* counters */
    uint32_t frames_ok;      /* FE seen, exactly `height` good lines, no error inside */
    uint32_t frames_bad;     /* any other frame that ended (FE, FS-without-FE, or stop) */
    uint32_t lines_ok;       /* long packets with right DT, WC and CRC inside a frame */
    uint32_t crc_err;        /* payload CRC mismatch */
    uint32_t wc_err;         /* WC != csi2_expected_wc(cfg.dt, cfg.width) */
    uint32_t dt_err;         /* long packet DT != cfg.dt */
    uint32_t no_fs_pkts;     /* long packet or FE received while no frame is open */
    uint32_t missing_fe;     /* FS while a frame is open, or frame still open at stop */
    uint32_t line_count_err; /* FE arrived with good lines != height */
    uint32_t frame_num_err;  /* FS number not prev+1 (both non-zero), or FE number != FS number */
    /* receiver state (zero-initialised by the caller) */
    bool     in_frame, frame_bad, have_prev;
    uint16_t frame_num, prev_num, lines;
} Csi2VcStats;

typedef struct {
    Csi2VcStats vc[CSI2_NUM_VC];
    uint32_t packets;           /* headers accepted (OK or corrected) */
    uint32_t ecc_corrected;
    uint32_t ecc_uncorrectable;
    uint32_t unexpected_vc;     /* packet on a VC whose cfg.dt == 0 */
    size_t   stop_off;
} Csi2Report;

/* ======================= your code ======================= */

static const uint32_t ECC_MASK[6] = {          /* data bits feeding P0..P5 */
    0xF12CB7u, 0xF2555Bu, 0x749A6Du, 0xB8E38Eu, 0xDF03F0u, 0xEFFC00u
};

static unsigned parity24(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return x & 1u;
}

uint8_t csi2_ecc(uint32_t d24)
{
    uint8_t e = 0;
    for (unsigned i = 0; i < 6; i++)
        e |= (uint8_t)(parity24(d24 & ECC_MASK[i]) << i);
    return e;
}

int csi2_header_decode(const uint8_t raw[4], Csi2Hdr *h)
{
    uint32_t d = (uint32_t)raw[0] | (uint32_t)raw[1] << 8 | (uint32_t)raw[2] << 16;
    uint8_t syn = (uint8_t)((raw[3] ^ csi2_ecc(d)) & 0x3Fu);
    int st = HDR_OK;

    if (syn != 0) {
        if ((syn & (syn - 1)) == 0) {
            st = HDR_CORRECTED;                 /* one ECC bit flipped: data is fine */
        } else {
            int bit = -1;
            for (int i = 0; i < 24; i++)        /* column i of H = ecc of a lone bit i */
                if (csi2_ecc(1u << i) == syn) { bit = i; break; }
            if (bit < 0)
                return HDR_UNCORRECTABLE;        /* even-weight syndrome: >= 2 errors */
            d ^= 1u << bit;
            st = HDR_CORRECTED;
        }
    }
    h->vc = (uint8_t)((d >> 6) & 3u);
    h->dt = (uint8_t)(d & 0x3Fu);
    h->wc = (uint16_t)(d >> 8);
    return st;
}

uint16_t csi2_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

uint16_t csi2_expected_wc(uint8_t dt, uint16_t width)
{
    uint32_t bpp;
    switch (dt) {
    case CSI2_DT_RAW8:     bpp = 8;  break;
    case CSI2_DT_RAW10:    bpp = 10; break;
    case CSI2_DT_RAW12:    bpp = 12; break;
    case CSI2_DT_YUV422_8: bpp = 16; break;
    default: return 0;
    }
    uint32_t bits = (uint32_t)width * bpp;
    if (bits % 8u != 0 || bits / 8u > 0xFFFFu)
        return 0;
    return (uint16_t)(bits / 8u);
}

static void close_frame(Csi2VcStats *s, bool good)
{
    if (good) s->frames_ok++; else s->frames_bad++;
    s->in_frame = false;
}

static void on_packet(Csi2VcStats *s, const Csi2VcCfg *c, const Csi2Hdr *h, const uint8_t *payload)
{
    if (h->dt == CSI2_DT_FS) {
        if (s->in_frame) {                       /* previous frame never got its FE */
            s->missing_fe++;
            close_frame(s, false);
        }
        uint16_t fn = h->wc;
        if (fn != 0 && s->have_prev && s->prev_num != 0) {
            uint16_t want = (s->prev_num == 0xFFFFu) ? 1u : (uint16_t)(s->prev_num + 1u);
            if (fn != want) s->frame_num_err++;  /* dropped frame upstream */
        }
        s->prev_num = fn; s->have_prev = true;
        s->frame_num = fn; s->lines = 0;
        s->in_frame = true; s->frame_bad = false;
    } else if (h->dt == CSI2_DT_FE) {
        if (!s->in_frame) { s->no_fs_pkts++; return; }
        if (h->wc != s->frame_num) { s->frame_num_err++; s->frame_bad = true; }
        if (s->lines != c->height) { s->line_count_err++; s->frame_bad = true; }
        close_frame(s, !s->frame_bad);
    } else if (h->dt >= 0x10u) {                 /* long packet */
        if (!s->in_frame) { s->no_fs_pkts++; return; }
        bool ok = true;
        if (h->dt != c->dt) { s->dt_err++; ok = false; }
        if (h->wc != csi2_expected_wc(c->dt, c->width)) { s->wc_err++; ok = false; }
        uint16_t rx = (uint16_t)(payload[h->wc] | payload[h->wc + 1u] << 8);
        if (rx != csi2_crc16(payload, h->wc)) { s->crc_err++; ok = false; }
        if (ok) { s->lines++; s->lines_ok++; } else s->frame_bad = true;
    }
    /* LS/LE and generic short packets: accepted, not checked in this version */
}

int csi2_check_stream(const uint8_t *buf, size_t len, const Csi2VcCfg cfg[CSI2_NUM_VC],
                      Csi2Report *r)
{
    size_t off = 0;
    int rc = 0;

    while (off < len) {
        if (len - off < CSI2_HDR_LEN) { rc = -1; break; }          /* truncated header */
        Csi2Hdr h;
        int st = csi2_header_decode(&buf[off], &h);
        if (st == HDR_UNCORRECTABLE) { r->ecc_uncorrectable++; rc = -1; break; }
        size_t plen = (h.dt >= 0x10u) ? (size_t)h.wc + CSI2_CRC_LEN : 0;
        if (len - off - CSI2_HDR_LEN < plen) { rc = -1; break; }   /* runs past buffer */
        if (st == HDR_CORRECTED) r->ecc_corrected++;
        r->packets++;
        if (cfg[h.vc].dt == 0) r->unexpected_vc++;
        else on_packet(&r->vc[h.vc], &cfg[h.vc], &h, &buf[off + CSI2_HDR_LEN]);
        off += CSI2_HDR_LEN + plen;
    }
    r->stop_off = off;
    for (int v = 0; v < CSI2_NUM_VC; v++)
        if (r->vc[v].in_frame) { r->vc[v].missing_fe++; close_frame(&r->vc[v], false); }
    return rc;
}

/* ============ provided: test pattern generator (an independent CSI-2 TX model) ============ */
/* In the lab this is the sensor TPG or an FPGA pattern source. Do not edit. */

static const uint8_t TPG_COL[24] = {   /* H-matrix column (P5..P0) for each data bit D0..D23 */
    0x07, 0x0B, 0x0D, 0x0E, 0x13, 0x15, 0x16, 0x19, 0x1A, 0x1C, 0x23, 0x25,
    0x26, 0x29, 0x2A, 0x2C, 0x31, 0x32, 0x34, 0x38, 0x1F, 0x2F, 0x37, 0x3B
};

static size_t tpg_hdr(uint8_t *b, size_t off, uint8_t vc, uint8_t dt, uint16_t wc)
{
    uint32_t d = (uint32_t)(vc << 6 | dt) | (uint32_t)wc << 8;
    uint8_t e = 0;
    for (int i = 0; i < 24; i++) if (d >> i & 1u) e ^= TPG_COL[i];
    b[off] = (uint8_t)d; b[off + 1] = (uint8_t)(d >> 8); b[off + 2] = (uint8_t)(d >> 16); b[off + 3] = e;
    return off + 4;
}

static uint16_t tpg_crc(const uint8_t *p, size_t n)        /* bit-serial, like the RTL */
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 8; k++) {
            unsigned in = (p[i] >> k & 1u) ^ (c & 1u);
            c >>= 1;
            if (in) c ^= 0x8408;
        }
    return c;
}

static size_t tpg_short(uint8_t *b, size_t off, uint8_t vc, uint8_t dt, uint16_t data)
{
    return tpg_hdr(b, off, vc, dt, data);
}

static size_t tpg_long(uint8_t *b, size_t off, uint8_t vc, uint8_t dt, uint16_t wc, uint8_t seed)
{
    off = tpg_hdr(b, off, vc, dt, wc);
    for (uint16_t i = 0; i < wc; i++) b[off + i] = (uint8_t)(seed + i * 7u);
    uint16_t c = tpg_crc(&b[off], wc);
    off += wc;
    b[off] = (uint8_t)c; b[off + 1] = (uint8_t)(c >> 8);
    return off + 2;
}

/* FS(fn), `lines` long packets, FE(fn) on one VC */
static size_t tpg_frame(uint8_t *b, size_t off, uint8_t vc, uint8_t dt, uint16_t fn,
                        uint16_t wc, int lines)
{
    off = tpg_short(b, off, vc, CSI2_DT_FS, fn);
    for (int l = 0; l < lines; l++) off = tpg_long(b, off, vc, dt, wc, (uint8_t)(fn * 16 + l));
    return tpg_short(b, off, vc, CSI2_DT_FE, fn);
}

static int run(const uint8_t *b, size_t len, const Csi2VcCfg *cfg, Csi2Report *r)
{
    memset(r, 0, sizeof *r);
    return csi2_check_stream(b, len, cfg, r);
}

int main(void)
{
    static uint8_t b[512];
    Csi2Report r;
    Csi2Hdr h;
    /* VC0: RAW10, 8 px/line (WC = 10), 3 lines/frame. Other VCs unused. */
    const Csi2VcCfg cfg[CSI2_NUM_VC] = { { CSI2_DT_RAW10, 8, 3 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
    size_t n, n1;

    /* 1. building blocks: ECC, CRC, word count */
    CHECK(csi2_ecc(0x000000) == 0x00);
    CHECK(csi2_ecc(0x000A2B) == 0x2E);               /* VC0 RAW10 WC=10 */
    CHECK(csi2_ecc(0x000100) == 0x1A);               /* FS, frame number 1 */
    CHECK(csi2_ecc(0x09602B) == 0x1D);        /* RAW10 WC=2400 (0x0960) */
    CHECK(csi2_crc16((const uint8_t *)"123456789", 9) == 0x6F91);
    const uint8_t spec1[24] = { 0xFF,0x00,0x00,0x02,0xB9,0xDC,0xF3,0x72,0xBB,0xD4,0xB8,0x5A,
                                0xC8,0x75,0xC2,0x7C,0x81,0xF8,0x05,0xDF,0xFF,0x00,0x00,0x01 };
    const uint8_t spec2[24] = { 0xFF,0x00,0x00,0x00,0x1E,0xF0,0x1E,0xC7,0x4F,0x82,0x78,0xC5,
                                0x82,0xE0,0x8C,0x70,0xD2,0x3C,0x78,0xE9,0xFF,0x00,0x00,0x01 };
    CHECK(csi2_crc16(spec1, 24) == 0x00F0);
    CHECK(csi2_crc16(spec2, 24) == 0xE569);
    CHECK(csi2_expected_wc(CSI2_DT_RAW10, 1920) == 2400);
    CHECK(csi2_expected_wc(CSI2_DT_RAW12, 1920) == 2880);
    CHECK(csi2_expected_wc(CSI2_DT_YUV422_8, 1920) == 3840);
    CHECK(csi2_expected_wc(CSI2_DT_RAW10, 1918) == 0);   /* 19180 bits: not byte aligned */
    CHECK(csi2_expected_wc(0x30, 1920) == 0);            /* user-defined DT: unknown */

    /* 2. header decode: clean, every single-bit flip corrected, every 2-bit flip detected */
    uint8_t hdr[4] = { 0x6B, 0x0A, 0x00, 0x00 };          /* VC1 RAW10 WC=10 */
    hdr[3] = csi2_ecc(0x000A6B);
    CHECK(csi2_header_decode(hdr, &h) == HDR_OK && h.vc == 1 && h.dt == 0x2B && h.wc == 10);
    int fixed = 0, caught = 0;
    for (int i = 0; i < 30; i++) {                        /* 24 data + 6 ECC bits */
        uint8_t t[4]; memcpy(t, hdr, 4);
        t[i / 8] ^= (uint8_t)(1u << (i % 8));
        memset(&h, 0, sizeof h);
        if (csi2_header_decode(t, &h) == HDR_CORRECTED && h.vc == 1 && h.dt == 0x2B && h.wc == 10)
            fixed++;
        for (int j = i + 1; j < 30; j++) {
            uint8_t u[4]; memcpy(u, t, 4);
            u[j / 8] ^= (uint8_t)(1u << (j % 8));
            if (csi2_header_decode(u, &h) == HDR_UNCORRECTABLE) caught++;
        }
    }
    CHECK(fixed == 30);
    CHECK(caught == 435);                                  /* C(30,2) */

    /* 3. clean stream: 2 frames x 3 lines, 112 bytes */
    n = tpg_frame(b, 0, 0, CSI2_DT_RAW10, 1, 10, 3);
    n1 = n;
    n = tpg_frame(b, n, 0, CSI2_DT_RAW10, 2, 10, 3);
    CHECK(n1 == 56 && n == 112);
    CHECK(run(b, n, cfg, &r) == 0 && r.stop_off == 112);
    CHECK(r.vc[0].frames_ok == 2 && r.vc[0].frames_bad == 0 && r.vc[0].lines_ok == 6);
    CHECK(r.packets == 10 && r.ecc_corrected == 0 && r.vc[0].frame_num_err == 0);

    /* 4. single-bit error in a line header (WC bit 3: 10 -> 2) is corrected, sync kept */
    b[20 + 1] ^= 0x08;                                     /* frame 1, line 2 header at 20 */
    CHECK(run(b, n, cfg, &r) == 0 && r.stop_off == 112);
    CHECK(r.ecc_corrected == 1 && r.vc[0].frames_ok == 2 && r.vc[0].lines_ok == 6);
    b[20 + 1] ^= 0x08;

    /* 5. 2-bit error in frame 2, line 1 header (offset 60): parse stops there */
    b[60 + 0] ^= 0x01; b[60 + 2] ^= 0x10;
    CHECK(run(b, n, cfg, &r) == -1 && r.stop_off == 60);
    CHECK(r.ecc_uncorrectable == 1 && r.vc[0].frames_ok == 1);
    CHECK(r.vc[0].missing_fe == 1 && r.vc[0].frames_bad == 1);   /* frame 2 left open */
    b[60 + 0] ^= 0x01; b[60 + 2] ^= 0x10;

    /* 6. payload CRC error: one payload byte of frame 1 line 1 flipped */
    b[4 + 4 + 5] ^= 0x40;
    CHECK(run(b, n, cfg, &r) == 0);
    CHECK(r.vc[0].crc_err == 1 && r.vc[0].lines_ok == 5);
    CHECK(r.vc[0].frames_ok == 1 && r.vc[0].frames_bad == 1 && r.vc[0].line_count_err == 1);
    b[4 + 4 + 5] ^= 0x40;

    /* 7. short frame: frame 2 has only 2 lines */
    n = tpg_frame(b, 0, 0, CSI2_DT_RAW10, 1, 10, 3);
    n = tpg_frame(b, n, 0, CSI2_DT_RAW10, 2, 10, 2);
    CHECK(n == 96 && run(b, n, cfg, &r) == 0);
    CHECK(r.vc[0].frames_ok == 1 && r.vc[0].frames_bad == 1 && r.vc[0].line_count_err == 1);
    CHECK(r.vc[0].lines_ok == 5 && r.vc[0].crc_err == 0);

    /* 8. missing FE on frame 1, then frame number jumps 2 -> 4 (frame 3 dropped) */
    n = tpg_frame(b, 0, 0, CSI2_DT_RAW10, 1, 10, 3) - 4;   /* cut off FE(1) */
    n = tpg_frame(b, n, 0, CSI2_DT_RAW10, 2, 10, 3);
    n = tpg_frame(b, n, 0, CSI2_DT_RAW10, 4, 10, 3);
    CHECK(n == 164 && run(b, n, cfg, &r) == 0);
    CHECK(r.vc[0].missing_fe == 1 && r.vc[0].frame_num_err == 1);
    CHECK(r.vc[0].frames_ok == 2 && r.vc[0].frames_bad == 1 && r.vc[0].lines_ok == 9);

    /* 9. WC mismatch (12 instead of 10, valid ECC and CRC): flagged, parse continues */
    n = tpg_short(b, 0, 0, CSI2_DT_FS, 1);
    n = tpg_long(b, n, 0, CSI2_DT_RAW10, 10, 0x11);
    n = tpg_long(b, n, 0, CSI2_DT_RAW10, 12, 0x22);
    n = tpg_long(b, n, 0, CSI2_DT_RAW10, 10, 0x33);
    n = tpg_short(b, n, 0, CSI2_DT_FE, 1);
    CHECK(n == 58 && run(b, n, cfg, &r) == 0 && r.stop_off == 58);
    CHECK(r.vc[0].wc_err == 1 && r.vc[0].crc_err == 0 && r.vc[0].lines_ok == 2);
    CHECK(r.vc[0].frames_bad == 1 && r.vc[0].line_count_err == 1);

    /* 10. two VCs interleaved: VC0 RAW10 8x2, VC1 YUV422 4x2 (WC 8, frame 7), + stray VC2 */
    const Csi2VcCfg cfg2[CSI2_NUM_VC] = { { CSI2_DT_RAW10, 8, 2 }, { CSI2_DT_YUV422_8, 4, 2 },
                                          { 0, 0, 0 }, { 0, 0, 0 } };
    n = tpg_short(b, 0, 0, CSI2_DT_FS, 1);
    n = tpg_short(b, n, 1, CSI2_DT_FS, 7);
    n = tpg_long(b, n, 0, CSI2_DT_RAW10, 10, 0xA0);
    n = tpg_long(b, n, 1, CSI2_DT_YUV422_8, 8, 0xB0);
    n = tpg_short(b, n, 2, CSI2_DT_LS, 1);                   /* VC2 not configured */
    n = tpg_long(b, n, 0, CSI2_DT_RAW10, 10, 0xA1);
    n = tpg_long(b, n, 1, CSI2_DT_YUV422_8, 8, 0xB1);
    n = tpg_short(b, n, 0, CSI2_DT_FE, 1);
    n = tpg_short(b, n, 1, CSI2_DT_FE, 7);
    CHECK(n == 80 && run(b, n, cfg2, &r) == 0);
    CHECK(r.vc[0].frames_ok == 1 && r.vc[0].lines_ok == 2 && r.vc[0].frames_bad == 0);
    CHECK(r.vc[1].frames_ok == 1 && r.vc[1].lines_ok == 2 && r.vc[1].frames_bad == 0);
    CHECK(r.unexpected_vc == 1 && r.packets == 9);

    /* 11. truncated long packet at the end of the buffer: stop, no over-read */
    n = tpg_frame(b, 0, 0, CSI2_DT_RAW10, 1, 10, 3);
    CHECK(run(b, 30, cfg, &r) == -1 && r.stop_off == 20);   /* line 2 needs 16 B, 10 left */
    CHECK(r.vc[0].lines_ok == 1 && r.vc[0].missing_fe == 1);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
