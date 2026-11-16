/* PCIE-B10 TLP 헤더 인코딩/디코딩: MRd, MWr, CplD
 *
 * 헤더는 "바이트 0 이 먼저" 인 big-endian DW 묶음이다 (스펙 그림 그대로: byte 0 = Fmt/Type).
 *   DW0  byte0 = Fmt[2:0]<<5 | Type[4:0]
 *        byte1 = TC[2:0]<<4 | Attr[2](IDO)<<2 | TH            (bit7 T9, bit3 T8, bit1 LN 은 이 레슨에서 0)
 *        byte2 = TD<<7 | EP<<6 | Attr[1:0]<<4 (RO=bit5, NS=bit4) | AT<<2 | Length[9:8]
 *        byte3 = Length[7:0]                                   (Length 는 DW 단위, 필드 0 = 1024 DW)
 *   요청(MRd/MWr)  DW1 = Requester ID | Tag | Last DW BE<<4 | First DW BE
 *                  DW2 = Addr[31:2]<<2 (3DW)  또는  DW2 = Addr[63:32], DW3 = Addr[31:2]<<2 (4DW)
 *   완료(Cpl/CplD) DW1 = Completer ID | Status<<13 | BCM<<12 | Byte Count[11:0] (0 = 4096)
 *                  DW2 = Requester ID | Tag | R | Lower Address[6:0]
 *   byte0: MRd32 0x00, MRd64 0x20, MWr32 0x40, MWr64 0x60, Cpl 0x0A, CplD 0x4A.
 *
 * Part A: tlp_encode / tlp_decode (3DW·4DW MRd, MWr, Cpl, CplD) — 손으로 계산한 벡터 + 무작위 왕복
 * Part B: tlp_check (BE 규칙, 4KB 경계, 4DW-iff-4GB, 필드 범위), tlp_be (주소·길이 → Length/BE),
 *         tlp_split (4KB 경계 + MPS/MRRS 로 요청 분할) — 오류 주입 + 브루트포스 비교
 * Part C: cpl_byte_count / cpl_lower_addr / cpl_split (RCB 64/128 로 나뉜 CplD 의 Byte Count·Lower Address),
 *         tlp_describe (Linux AER "TLP Header" DW 로그 → 한 줄 해석), SSD 가 호스트 메모리를 읽는 시나리오
 *
 * 테스트: 손 계산 바이트 벡터, tlp_be 브루트포스 전수(시작 오프셋 0..15 × 길이 0..600),
 *         고정 시드 xorshift32 무작위 헤더 4000개 왕복, 분할·완료 성질 검사.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Linux include/uapi/linux/pci_regs.h 의 이름·값 (Mac 에서도 빌드되게 직접 정의) */
#define PCI_EXP_DEVCTL_PAYLOAD 0x00e0   /* MPS  = 128 << ((DEVCTL & 0x00e0) >> 5)  */
#define PCI_EXP_DEVCTL_READRQ  0x7000   /* MRRS = 128 << ((DEVCTL & 0x7000) >> 12) */
#define PCI_EXP_LNKCTL_RCB     0x0008   /* 0 = RCB 64B, 1 = RCB 128B               */

#define TLP_TYPE_MEM 0x00u
#define TLP_TYPE_CPL 0x0Au

#define TLP_ATTR_NS  0x1u   /* Attr[0] No Snoop          → byte2 bit4 */
#define TLP_ATTR_RO  0x2u   /* Attr[1] Relaxed Ordering  → byte2 bit5 */
#define TLP_ATTR_IDO 0x4u   /* Attr[2] ID-based Ordering → byte1 bit2 */

#define CPL_SC  0u          /* Successful Completion */
#define CPL_UR  1u          /* Unsupported Request   */
#define CPL_CRS 2u          /* Configuration Request Retry Status (최신 스펙 이름 RRS) */
#define CPL_CA  4u          /* Completer Abort       */

enum { TLP_OK = 0, TLP_E_CAP = -1, TLP_E_TRUNC = -2, TLP_E_UNSUP = -3, TLP_E_FIELD = -4,
       TLP_E_ADDR = -5, TLP_E_BE = -6, TLP_E_4K = -7 };

typedef enum { TLP_MRD, TLP_MWR, TLP_CPL, TLP_CPLD } tlp_kind;

typedef struct {
    tlp_kind kind;
    bool     is4dw;       /* decode 가 채운다. encode 는 무시하고 addr 로 정한다 (Cpl/CplD 는 항상 3DW) */
    uint8_t  tc;          /* 0..7 */
    uint8_t  attr;        /* TLP_ATTR_* 조합 (0..7) */
    bool     th, td, ep;
    uint8_t  at;          /* 0..3 */
    uint16_t len_dw;      /* MRd/MWr/CplD: 1..1024.  Cpl(데이터 없음): 0 (필드는 reserved, 0 으로 보냄) */
    uint16_t req_id;      /* bus<<8 | dev<<3 | fn */
    uint8_t  tag;
    /* 요청 */
    uint8_t  first_be, last_be;
    uint64_t addr;        /* DW 정렬 (bit1:0 = 0) */
    /* 완료 */
    uint16_t cpl_id;
    uint8_t  status;      /* CPL_SC / CPL_UR / CPL_CRS / CPL_CA */
    bool     bcm;
    uint16_t byte_count;  /* 1..4096 (필드 0 = 4096) */
    uint8_t  lower_addr;  /* 0..0x7F */
} tlp_hdr;

typedef struct { uint64_t dw_addr; uint16_t len_dw; uint8_t first_be, last_be; } tlp_span;
typedef struct { uint64_t addr; uint32_t len; } tlp_chunk;
typedef struct { uint64_t addr; uint16_t len_dw, byte_count; uint8_t lower_addr; } cpl_chunk;

static uint16_t bdf_id(unsigned bus, unsigned dev, unsigned fn) {
    return (uint16_t)((bus & 0xFFu) << 8 | (dev & 0x1Fu) << 3 | (fn & 7u));
}
static void be32_put(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t be32_get(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static bool is_req(tlp_kind k) { return k == TLP_MRD || k == TLP_MWR; }
static bool pow2_in(uint32_t v, uint32_t lo, uint32_t hi) { return v >= lo && v <= hi && (v & (v - 1u)) == 0; }
static int  be_lo(uint8_t be) { for (int i = 0; i < 4; i++) if (be >> i & 1u) return i; return -1; }   /* 가장 낮은 켜진 바이트 */
static int  be_hi(uint8_t be) { for (int i = 3; i >= 0; i--) if (be >> i & 1u) return i; return -1; }  /* 가장 높은 켜진 바이트 */

/* ---------------- Part B: 규칙 검사 (encode·decode 가 같이 쓴다) ---------------- */

/* 반환 TLP_OK 또는 TLP_E_FIELD / TLP_E_ADDR / TLP_E_4K / TLP_E_BE (is4dw 는 보지 않는다) */
static int tlp_check(const tlp_hdr *h) {
    if (h->tc > 7 || h->attr > 7 || h->at > 3) return TLP_E_FIELD;
    if (is_req(h->kind)) {
        if (h->len_dw < 1 || h->len_dw > 1024 || h->first_be > 0xF || h->last_be > 0xF) return TLP_E_FIELD;
        if (h->addr & 3u) return TLP_E_ADDR;
        if ((h->addr & 0xFFFu) + 4u * h->len_dw > 4096u) return TLP_E_4K;        /* 4KB 경계를 넘으면 안 됨 */
        if (h->len_dw == 1) return h->last_be == 0 ? TLP_OK : TLP_E_BE;         /* 1 DW: Last BE = 0000 */
        if (h->first_be == 0 || h->last_be == 0) return TLP_E_BE;               /* 2 DW 이상: 둘 다 0 이 아님 */
        if (h->len_dw == 2 && (h->addr & 7u) == 0) return TLP_OK;               /* QW 정렬 2 DW: 불연속 허용 */
        bool f_ok = h->first_be == 0xF || h->first_be == 0xE || h->first_be == 0xC || h->first_be == 0x8;
        bool l_ok = h->last_be == 0xF || h->last_be == 0x7 || h->last_be == 0x3 || h->last_be == 0x1;
        return f_ok && l_ok ? TLP_OK : TLP_E_BE;                                /* 그 외: 연속이어야 함 */
    }
    if (h->kind == TLP_CPL ? h->len_dw != 0 : (h->len_dw < 1 || h->len_dw > 1024)) return TLP_E_FIELD;
    if (!(h->status == CPL_SC || h->status == CPL_UR || h->status == CPL_CRS || h->status == CPL_CA)) return TLP_E_FIELD;
    if (h->byte_count < 1 || h->byte_count > 4096 || h->lower_addr > 0x7F) return TLP_E_FIELD;
    return TLP_OK;
}

/* ---------------- Part A: 인코더 / 디코더 ---------------- */

/* 헤더를 out 에 쓴다. 반환: 12(3DW) / 16(4DW) 또는 오류 (오류면 out 을 건드리지 않음).
 * 요청은 addr > 0xFFFFFFFF 일 때만 4DW (64-bit 주소) 형식. */
static int tlp_encode(const tlp_hdr *h, uint8_t *out, size_t cap) {
    int rc = tlp_check(h);
    if (rc != TLP_OK) return rc;
    bool req = is_req(h->kind);
    bool is4 = req && h->addr > 0xFFFFFFFFull;
    size_t n = is4 ? 16u : 12u;
    if (cap < n) return TLP_E_CAP;
    unsigned fmt = (h->kind == TLP_MWR || h->kind == TLP_CPLD ? 2u : 0u) | (is4 ? 1u : 0u);
    unsigned len = h->len_dw & 0x3FFu;                              /* 1024 → 0 */
    out[0] = (uint8_t)(fmt << 5 | (req ? TLP_TYPE_MEM : TLP_TYPE_CPL));
    out[1] = (uint8_t)(h->tc << 4 | ((h->attr & TLP_ATTR_IDO) ? 0x04u : 0u) | (h->th ? 1u : 0u));
    out[2] = (uint8_t)((h->td ? 0x80u : 0u) | (h->ep ? 0x40u : 0u) | (h->attr & 3u) << 4 | h->at << 2 | len >> 8);
    out[3] = (uint8_t)len;
    if (req) {
        out[4] = (uint8_t)(h->req_id >> 8); out[5] = (uint8_t)h->req_id;
        out[6] = h->tag;                    out[7] = (uint8_t)(h->last_be << 4 | h->first_be);
        if (is4) { be32_put(out + 8, (uint32_t)(h->addr >> 32)); be32_put(out + 12, (uint32_t)h->addr & ~3u); }
        else       be32_put(out + 8, (uint32_t)h->addr & ~3u);
    } else {
        unsigned bc = h->byte_count & 0xFFFu;                       /* 4096 → 0 */
        out[4]  = (uint8_t)(h->cpl_id >> 8); out[5] = (uint8_t)h->cpl_id;
        out[6]  = (uint8_t)(h->status << 5 | (h->bcm ? 0x10u : 0u) | bc >> 8);
        out[7]  = (uint8_t)bc;
        out[8]  = (uint8_t)(h->req_id >> 8); out[9] = (uint8_t)h->req_id;
        out[10] = h->tag;                    out[11] = h->lower_addr;
    }
    return (int)n;
}

/* in[0..n) 에서 헤더 하나를 읽는다. 반환: 헤더 바이트 수(12/16) 또는 오류 (오류면 *h 를 건드리지 않음). */
static int tlp_decode(const uint8_t *in, size_t n, tlp_hdr *h) {
    if (n < 4) return TLP_E_TRUNC;
    unsigned fmt = in[0] >> 5, type = in[0] & 0x1Fu;
    tlp_hdr t;
    memset(&t, 0, sizeof t);
    if (type == TLP_TYPE_MEM && fmt <= 3)                    t.kind = (fmt & 2u) ? TLP_MWR : TLP_MRD;
    else if (type == TLP_TYPE_CPL && (fmt == 0 || fmt == 2)) t.kind = fmt ? TLP_CPLD : TLP_CPL;
    else return TLP_E_UNSUP;                                 /* prefix, Cfg/IO/Msg, 4DW Cpl ... */
    t.is4dw = (fmt & 1u) != 0;
    size_t hl = t.is4dw ? 16u : 12u;
    if (n < hl) return TLP_E_TRUNC;
    t.tc   = (uint8_t)(in[1] >> 4 & 7u);
    t.attr = (uint8_t)((in[2] >> 4 & 3u) | ((in[1] & 0x04u) ? TLP_ATTR_IDO : 0u));
    t.th   = (in[1] & 1u) != 0;
    t.td   = (in[2] & 0x80u) != 0;
    t.ep   = (in[2] & 0x40u) != 0;
    t.at   = (uint8_t)(in[2] >> 2 & 3u);
    unsigned len = (unsigned)(in[2] & 3u) << 8 | in[3];
    t.len_dw = t.kind == TLP_CPL ? 0 : (uint16_t)(len ? len : 1024u);
    if (is_req(t.kind)) {
        t.req_id = (uint16_t)(in[4] << 8 | in[5]);
        t.tag = in[6];
        t.first_be = in[7] & 0xFu;
        t.last_be  = in[7] >> 4;
        if (t.is4dw) {
            t.addr = (uint64_t)be32_get(in + 8) << 32 | (be32_get(in + 12) & ~3u);
            if ((t.addr >> 32) == 0) return TLP_E_ADDR;      /* 4GB 미만인데 4DW: 이 모델은 거부 */
        } else {
            t.addr = be32_get(in + 8) & ~3u;
        }
    } else {
        unsigned bc = (unsigned)(in[6] & 0xFu) << 8 | in[7];
        t.cpl_id = (uint16_t)(in[4] << 8 | in[5]);
        t.status = in[6] >> 5;
        t.bcm = (in[6] & 0x10u) != 0;
        t.byte_count = (uint16_t)(bc ? bc : 4096u);
        t.req_id = (uint16_t)(in[8] << 8 | in[9]);
        t.tag = in[10];
        t.lower_addr = in[11] & 0x7Fu;
    }
    int rc = tlp_check(&t);
    if (rc != TLP_OK) return rc;
    *h = t;
    return (int)hl;
}

/* ---------------- Part B: BE 계산과 요청 분할 ---------------- */

/* 바이트 범위 [addr, addr+len) → DW 정렬 주소, Length, First/Last DW BE.
 * len == 0 → zero-length read/write (Length 1, BE 0000/0000). Length > 1024 이면 TLP_E_FIELD. */
static int tlp_be(uint64_t addr, uint32_t len, tlp_span *o) {
    o->dw_addr = addr & ~3ull;
    if (len == 0) { o->len_dw = 1; o->first_be = 0; o->last_be = 0; return TLP_OK; }
    uint64_t end = addr + len;                                      /* exclusive */
    uint64_t ndw = ((end + 3u) >> 2) - (addr >> 2);
    if (ndw > 1024) return TLP_E_FIELD;
    uint8_t fbe = (uint8_t)((0xFu << (addr & 3u)) & 0xFu);         /* 시작 바이트부터 DW 끝까지 */
    uint8_t lbe = (uint8_t)(0xFu >> ((4u - (end & 3u)) & 3u));      /* DW 시작부터 마지막 바이트까지 */
    o->len_dw = (uint16_t)ndw;
    if (ndw == 1) { o->first_be = fbe & lbe; o->last_be = 0; }      /* 1 DW: 둘을 겹쳐 First 에, Last = 0 */
    else          { o->first_be = fbe;       o->last_be = lbe; }
    return TLP_OK;
}

/* 긴 전송 [addr, addr+len) 을 TLP 하나에 담을 수 있는 조각으로 자른다 (앞에서부터 가장 크게).
 * 조각 조건: 4KB 경계를 넘지 않음, DW 범위(Length×4) ≤ max_bytes (MWr 이면 MPS, MRd 이면 MRRS).
 * 반환: 조각 수, 또는 TLP_E_FIELD (len == 0, max_bytes 가 128..4096 2의 거듭제곱 아님) / TLP_E_CAP. */
static int tlp_split(uint64_t addr, uint64_t len, uint32_t max_bytes, tlp_chunk *out, size_t cap) {
    if (len == 0 || !pow2_in(max_bytes, 128u, 4096u)) return TLP_E_FIELD;
    size_t n = 0;
    uint64_t cur = addr, end = addr + len;
    while (cur < end) {
        uint64_t e = end;
        uint64_t lim_mps = (cur & ~3ull) + max_bytes;               /* DW 정렬 시작 기준! */
        uint64_t lim_4k  = (cur | 0xFFFull) + 1u;
        if (lim_mps < e) e = lim_mps;
        if (lim_4k < e) e = lim_4k;
        if (n >= cap) return TLP_E_CAP;
        out[n].addr = cur;
        out[n].len = (uint32_t)(e - cur);
        n++;
        cur = e;
    }
    return (int)n;
}

/* ---------------- Part C: Completion 의 Byte Count / Lower Address ---------------- */

/* MRd 의 (Length, First BE, Last BE) → 첫 CplD 의 Byte Count (= 돌려줄 총 바이트 수) */
static uint16_t cpl_byte_count(uint16_t len_dw, uint8_t first_be, uint8_t last_be) {
    if (len_dw == 1) {
        if (first_be == 0) return 1;                                 /* zero-length read → 1 */
        return (uint16_t)(be_hi(first_be) - be_lo(first_be) + 1);   /* 구멍 포함 */
    }
    return (uint16_t)(4u * len_dw - (unsigned)be_lo(first_be) - (unsigned)(3 - be_hi(last_be)));
}

/* 첫 CplD 의 Lower Address = 첫 번째로 켜진 바이트 주소의 하위 7비트 (BE 0000 이면 DW 주소) */
static uint8_t cpl_lower_addr(uint64_t dw_addr, uint8_t first_be) {
    unsigned off = first_be ? (unsigned)be_lo(first_be) : 0u;
    return (uint8_t)((dw_addr + off) & 0x7Fu);
}

/* MRd 하나에 대한 CplD 목록. 완료자 정책(이 레슨): 각 CplD 는 MPS 이하에서 가장 크게,
 * 마지막이 아니면 RCB 정렬 주소에서 끝난다. 반환: CplD 수 또는 TLP_E_FIELD / TLP_E_CAP. */
static int cpl_split(const tlp_hdr *rd, uint32_t rcb, uint32_t mps, cpl_chunk *out, size_t cap) {
    if (rd->kind != TLP_MRD || tlp_check(rd) != TLP_OK) return TLP_E_FIELD;
    if ((rcb != 64 && rcb != 128) || !pow2_in(mps, 128u, 4096u)) return TLP_E_FIELD;
    uint64_t cur = rd->addr, dw_end = rd->addr + 4u * rd->len_dw;
    uint16_t bc0 = cpl_byte_count(rd->len_dw, rd->first_be, rd->last_be);
    uint64_t first_byte = rd->addr + (rd->first_be ? (unsigned)be_lo(rd->first_be) : 0u);
    uint64_t byte_end = first_byte + bc0;                            /* 마지막 켜진 바이트 + 1 */
    uint64_t start = first_byte;                                     /* 이 CplD 가 돌려주는 첫 유효 바이트 */
    size_t n = 0;
    while (cur < dw_end) {
        uint64_t e = dw_end;
        uint64_t lim = (cur + mps) & ~(uint64_t)(rcb - 1u);          /* MPS 이하 + RCB 경계에서 끝 */
        if (lim < e) e = lim;
        if (n >= cap) return TLP_E_CAP;
        out[n].addr = cur;
        out[n].len_dw = (uint16_t)((e - cur) / 4u);
        out[n].byte_count = (uint16_t)(byte_end - start);            /* 남은 바이트 (이번 것 포함) */
        out[n].lower_addr = (uint8_t)(start & 0x7Fu);
        n++;
        cur = e;
        start = e;
    }
    return (int)n;
}

/* 헤더 한 줄 요약 (AER 로그 해석용). 반환: 쓴 글자 수 또는 TLP_E_CAP (cap 이 모자람). 형식:
 *   요청: "MRd32 01:00.0 tag=0x0c addr=0xf7c01004 len=3 be=c/3" + 플래그
 *   완료: "CplD SC 00:00.0->01:00.0 tag=0x0c len=3 bc=8 la=0x06" + 플래그
 *   플래그(있을 때만, 이 순서): " TC<n>" " RO" " NS" " IDO" " TH" " TD" " EP" " AT<n>" " BCM" */
static int tlp_describe(const tlp_hdr *h, char *buf, size_t cap) {
    static const char *st[8] = { "SC", "UR", "CRS", "rsv3", "CA", "rsv5", "rsv6", "rsv7" };
    char t[192];
    int k;
    if (is_req(h->kind)) {
        bool w64 = h->addr > 0xFFFFFFFFull;
        k = snprintf(t, sizeof t, "%s%s %02x:%02x.%x tag=0x%02x addr=0x%llx len=%u be=%x/%x",
                     h->kind == TLP_MRD ? "MRd" : "MWr", w64 ? "64" : "32",
                     h->req_id >> 8, h->req_id >> 3 & 0x1Fu, h->req_id & 7u, h->tag,
                     (unsigned long long)h->addr, h->len_dw, h->first_be, h->last_be);
    } else {
        k = snprintf(t, sizeof t, "%s %s %02x:%02x.%x->%02x:%02x.%x tag=0x%02x len=%u bc=%u la=0x%02x",
                     h->kind == TLP_CPLD ? "CplD" : "Cpl", st[h->status & 7u],
                     h->cpl_id >> 8, h->cpl_id >> 3 & 0x1Fu, h->cpl_id & 7u,
                     h->req_id >> 8, h->req_id >> 3 & 0x1Fu, h->req_id & 7u,
                     h->tag, h->len_dw, h->byte_count, h->lower_addr);
    }
    if (h->tc) k += snprintf(t + k, sizeof t - (size_t)k, " TC%u", h->tc);
    if (h->attr & TLP_ATTR_RO)  k += snprintf(t + k, sizeof t - (size_t)k, " RO");
    if (h->attr & TLP_ATTR_NS)  k += snprintf(t + k, sizeof t - (size_t)k, " NS");
    if (h->attr & TLP_ATTR_IDO) k += snprintf(t + k, sizeof t - (size_t)k, " IDO");
    if (h->th) k += snprintf(t + k, sizeof t - (size_t)k, " TH");
    if (h->td) k += snprintf(t + k, sizeof t - (size_t)k, " TD");
    if (h->ep) k += snprintf(t + k, sizeof t - (size_t)k, " EP");
    if (h->at) k += snprintf(t + k, sizeof t - (size_t)k, " AT%u", h->at);
    if (h->bcm) k += snprintf(t + k, sizeof t - (size_t)k, " BCM");
    if (k < 0 || (size_t)k + 1u > cap) return TLP_E_CAP;
    memcpy(buf, t, (size_t)k + 1u);
    return k;
}

/* ---------------- 테스트 하네스 ---------------- */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* AER Header Log 처럼 DW 를 u32 로 찍은 값 → 와이어 바이트 (DW 의 bit31:24 = 바이트 0) */
static void tlp_from_dws(const uint32_t *dw, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; i++) be32_put(out + 4 * i, dw[i]);
}

static bool hdr_eq(const tlp_hdr *a, const tlp_hdr *b) {
    if (a->kind != b->kind || a->tc != b->tc || a->attr != b->attr || a->th != b->th || a->td != b->td ||
        a->ep != b->ep || a->at != b->at || a->len_dw != b->len_dw || a->req_id != b->req_id || a->tag != b->tag)
        return false;
    if (is_req(a->kind))
        return a->first_be == b->first_be && a->last_be == b->last_be && a->addr == b->addr;
    return a->cpl_id == b->cpl_id && a->status == b->status && a->bcm == b->bcm &&
           a->byte_count == b->byte_count && a->lower_addr == b->lower_addr;
}

static void hex(const char *tag, const uint8_t *b, int n) {
    printf("%s", tag);
    for (int i = 0; i < n; i++) printf("%s%02X", (i && i % 4 == 0) ? " | " : " ", b[i]);
    printf("\n");
}

static tlp_hdr mk_req(tlp_kind k, uint16_t rid, uint8_t tag, uint64_t addr, uint32_t len) {
    tlp_hdr h; memset(&h, 0, sizeof h);
    tlp_span sp; memset(&sp, 0, sizeof sp);
    h.kind = k; h.req_id = rid; h.tag = tag;
    if (tlp_be(addr, len, &sp) == TLP_OK) { h.addr = sp.dw_addr; h.len_dw = sp.len_dw; h.first_be = sp.first_be; h.last_be = sp.last_be; }
    h.is4dw = h.addr > 0xFFFFFFFFull;
    return h;
}
static tlp_hdr mk_cpl(tlp_kind k, uint16_t cid, uint8_t st, uint16_t rid, uint8_t tag, uint16_t len, uint16_t bc, uint8_t la) {
    tlp_hdr h; memset(&h, 0, sizeof h);
    h.kind = k; h.cpl_id = cid; h.status = st; h.req_id = rid; h.tag = tag; h.len_dw = len; h.byte_count = bc; h.lower_addr = la;
    return h;
}

/* 브루트포스 BE: 바이트마다 "범위 안인가" 를 보고 첫/끝 DW 의 비트를 켠다 */
static void be_brute(uint64_t addr, uint32_t len, tlp_span *o) {
    o->dw_addr = addr & ~3ull;
    if (len == 0) { o->len_dw = 1; o->first_be = o->last_be = 0; return; }
    uint64_t d0 = addr >> 2, d1 = (addr + len - 1u) >> 2;
    o->len_dw = (uint16_t)(d1 - d0 + 1u);
    uint8_t f = 0, l = 0;
    for (unsigned b = 0; b < 4; b++) {
        uint64_t x = d0 * 4u + b, y = d1 * 4u + b;
        if (x >= addr && x < addr + len) f |= (uint8_t)(1u << b);
        if (y >= addr && y < addr + len) l |= (uint8_t)(1u << b);
    }
    o->first_be = f; o->last_be = o->len_dw == 1 ? 0 : l;
}

static tlp_hdr rand_hdr(uint32_t *s) {
    tlp_kind k = (tlp_kind)(xs32(s) % 4u);
    tlp_hdr h;
    if (is_req(k)) {
        uint64_t addr = xs32(s);
        if (xs32(s) & 1u) addr |= (uint64_t)(xs32(s) | 1u) << 32;   /* 절반은 4GB 이상 */
        uint32_t room = 4096u - (uint32_t)(addr & 0xFFFu);
        h = mk_req(k, (uint16_t)xs32(s), (uint8_t)xs32(s), addr, xs32(s) % (room + 1u));
    } else {
        static const uint8_t sts[4] = { CPL_SC, CPL_UR, CPL_CRS, CPL_CA };
        uint16_t len = k == TLP_CPLD ? (uint16_t)(1u + xs32(s) % 1024u) : 0;
        h = mk_cpl(k, (uint16_t)xs32(s), sts[xs32(s) % 4u], (uint16_t)xs32(s), (uint8_t)xs32(s), len,
                   (uint16_t)(1u + xs32(s) % 4096u), (uint8_t)(xs32(s) & 0x7Fu));
        h.bcm = xs32(s) & 1u;
    }
    uint32_t r = xs32(s);
    h.tc = r & 7u; h.attr = r >> 3 & 7u; h.th = r >> 6 & 1u; h.td = r >> 7 & 1u; h.ep = r >> 8 & 1u; h.at = r >> 9 & 3u;
    return h;
}

int main(void) {
    uint8_t b[32];
    tlp_hdr h, d;
    int rc;

    /* ===== Part A: 손으로 계산한 벡터 ===== */
    /* V1: MRd32 01:00.0 tag 0x0c, 바이트 0xF7C01006..0xF7C0100D (8B) → Length 3, First BE 1100, Last BE 0011 */
    const uint8_t v1[12] = { 0x00,0x00,0x00,0x03, 0x01,0x00,0x0C,0x3C, 0xF7,0xC0,0x10,0x04 };
    h = mk_req(TLP_MRD, bdf_id(1, 0, 0), 0x0C, 0xF7C01006u, 8);
    memset(b, 0xEE, sizeof b);
    rc = tlp_encode(&h, b, sizeof b);
    hex("V1 MRd32 :", b, 12);
    CHECK(rc == 12 && memcmp(b, v1, 12) == 0);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(v1, 12, &d) == 12 && d.kind == TLP_MRD && !d.is4dw && d.addr == 0xF7C01004u &&
          d.len_dw == 3 && d.first_be == 0xC && d.last_be == 0x3 && d.req_id == 0x0100 && d.tag == 0x0C);

    /* V2: MWr64 03:00.0, addr 0x12_3456_7800, 4B, RO → 4DW, byte2 bit5 */
    const uint8_t v2[16] = { 0x60,0x00,0x20,0x01, 0x03,0x00,0x00,0x0F, 0x00,0x00,0x00,0x12, 0x34,0x56,0x78,0x00 };
    h = mk_req(TLP_MWR, bdf_id(3, 0, 0), 0x00, 0x1234567800ull, 4);
    h.attr = TLP_ATTR_RO;
    memset(b, 0xEE, sizeof b);
    rc = tlp_encode(&h, b, sizeof b);
    hex("V2 MWr64 :", b, 16);
    CHECK(rc == 16 && memcmp(b, v2, 16) == 0);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(v2, 16, &d) == 16 && d.kind == TLP_MWR && d.is4dw && d.addr == 0x1234567800ull &&
          d.attr == TLP_ATTR_RO && d.len_dw == 1 && d.first_be == 0xF && d.last_be == 0);

    /* V3: V1 에 대한 CplD (Root 00:00.0 → 01:00.0), Byte Count 8, Lower Address 0x06 */
    const uint8_t v3[12] = { 0x4A,0x00,0x00,0x03, 0x00,0x00,0x00,0x08, 0x01,0x00,0x0C,0x06 };
    h = mk_cpl(TLP_CPLD, bdf_id(0, 0, 0), CPL_SC, bdf_id(1, 0, 0), 0x0C, 3, 8, 0x06);
    rc = tlp_encode(&h, b, sizeof b);
    hex("V3 CplD  :", b, 12);
    CHECK(rc == 12 && memcmp(b, v3, 12) == 0);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(v3, 12, &d) == 12 && hdr_eq(&d, &h));

    /* V4: Cpl(데이터 없음) UR — 없는 함수에 CfgRd → Root Port 00:01.0 이 UR, 요청자 00:00.0 tag 5, BC 4 */
    const uint8_t v4[12] = { 0x0A,0x00,0x00,0x00, 0x00,0x08,0x20,0x04, 0x00,0x00,0x05,0x00 };
    h = mk_cpl(TLP_CPL, bdf_id(0, 1, 0), CPL_UR, bdf_id(0, 0, 0), 0x05, 0, 4, 0x00);
    rc = tlp_encode(&h, b, sizeof b);
    hex("V4 Cpl UR:", b, 12);
    CHECK(rc == 12 && memcmp(b, v4, 12) == 0);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(v4, 12, &d) == 12 && d.kind == TLP_CPL && d.status == CPL_UR && d.len_dw == 0 && d.cpl_id == 0x0008);

    /* V5: Length 1024 DW 는 필드 0, Byte Count 4096 도 필드 0 */
    h = mk_req(TLP_MRD, bdf_id(1, 0, 0), 0x01, 0x80000000u, 4096);
    rc = tlp_encode(&h, b, sizeof b);
    CHECK(rc == 12 && b[0] == 0x00 && (b[2] & 3u) == 0 && b[3] == 0x00 && b[7] == 0xFF);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(b, 12, &d) == 12 && d.len_dw == 1024);
    h = mk_cpl(TLP_CPLD, 0, CPL_SC, bdf_id(1, 0, 0), 0x01, 64, 4096, 0x00);
    rc = tlp_encode(&h, b, sizeof b);
    CHECK(rc == 12 && (b[6] & 0x0Fu) == 0 && b[7] == 0 && b[3] == 64);
    memset(&d, 0, sizeof d);
    CHECK(tlp_decode(b, 12, &d) == 12 && d.byte_count == 4096);

    /* 모든 필드 비트 위치: TC7, RO|NS|IDO, TH, TD, EP, AT 3 → DW0 = 70 07 FC 01 (위 V2 모양 4DW) */
    h = mk_req(TLP_MWR, bdf_id(3, 0, 0), 0, 0x1234567800ull, 4);
    h.tc = 7; h.attr = TLP_ATTR_RO | TLP_ATTR_NS | TLP_ATTR_IDO; h.th = true; h.td = true; h.ep = true; h.at = 3;
    rc = tlp_encode(&h, b, sizeof b);
    CHECK(rc == 16 && b[0] == 0x60 && b[1] == 0x75 && b[2] == 0xFC && b[3] == 0x01);

    /* 무작위 왕복 4000개 (고정 시드) */
    {
        uint32_t s = 0xC0FFEEu; int bad = 0, n4 = 0;
        for (int i = 0; i < 4000; i++) {
            tlp_hdr r = rand_hdr(&s);
            uint8_t o[16]; tlp_hdr q; memset(&q, 0, sizeof q);
            int n = tlp_encode(&r, o, sizeof o);
            int m = n > 0 ? tlp_decode(o, (size_t)n, &q) : n;
            if (n <= 0 || m != n || !hdr_eq(&r, &q) || q.is4dw != r.is4dw) bad++;
            if (n == 16) n4++;
        }
        printf("A random round trip: 4000 headers, 4DW=%d, bad=%d\n", n4, bad);
        CHECK(bad == 0 && n4 > 500);
    }

    /* ===== Part B: 오류 주입 (decode) ===== */
    CHECK(tlp_decode(v1, 11, &d) == TLP_E_TRUNC);                    /* 3DW 인데 11바이트 */
    CHECK(tlp_decode(v1, 3, &d) == TLP_E_TRUNC);
    CHECK(tlp_decode(v2, 12, &d) == TLP_E_TRUNC);                    /* Fmt=011 → 16바이트 필요 */
    {
        uint8_t x[16];
        memcpy(x, v1, 12); x[0] = 0x80;  CHECK(tlp_decode(x, 12, &d) == TLP_E_UNSUP);   /* Fmt 100 = TLP Prefix */
        memcpy(x, v1, 12); x[0] = 0x04;  CHECK(tlp_decode(x, 12, &d) == TLP_E_UNSUP);   /* CfgRd0: 이 코덱 범위 밖 */
        memcpy(x, v3, 12); x[0] = 0x2A;  CHECK(tlp_decode(x, 16, &d) == TLP_E_UNSUP);   /* Cpl 은 4DW 없음 */
        const uint8_t low4[16] = { 0x20,0x00,0x00,0x01, 0x01,0x00,0x00,0x0F, 0x00,0x00,0x00,0x00, 0x12,0x34,0x56,0x78 };
        CHECK(tlp_decode(low4, 16, &d) == TLP_E_ADDR);              /* 4DW 인데 상위 32비트 0 */
        const uint8_t x4k[12] = { 0x00,0x00,0x00,0x02, 0x01,0x00,0x00,0xFF, 0x00,0x00,0x0F,0xFC };
        CHECK(tlp_decode(x4k, 12, &d) == TLP_E_4K);                 /* 0xFFC + 8B → 4KB 경계 넘음 */
        const uint8_t l1[12] = { 0x00,0x00,0x00,0x01, 0x01,0x00,0x00,0xFF, 0x00,0x00,0x10,0x00 };
        CHECK(tlp_decode(l1, 12, &d) == TLP_E_BE);                  /* Length 1 인데 Last BE ≠ 0 */
        const uint8_t f0[12] = { 0x00,0x00,0x00,0x02, 0x01,0x00,0x00,0xF0, 0x00,0x00,0x10,0x00 };
        CHECK(tlp_decode(f0, 12, &d) == TLP_E_BE);                  /* Length 2 인데 First BE = 0 */
        const uint8_t nc3[12] = { 0x00,0x00,0x00,0x03, 0x01,0x00,0x00,0xF5, 0x00,0x00,0x10,0x00 };
        CHECK(tlp_decode(nc3, 12, &d) == TLP_E_BE);                 /* Length 3, First BE 0101 불연속 */
        const uint8_t qw2[12] = { 0x00,0x00,0x00,0x02, 0x01,0x00,0x00,0xA5, 0x00,0x00,0x10,0x08 };
        CHECK(tlp_decode(qw2, 12, &d) == 12 && d.first_be == 0x5 && d.last_be == 0xA);   /* QW 정렬 2DW: 허용 */
        memcpy(x, qw2, 12); x[11] = 0x04;
        CHECK(tlp_decode(x, 12, &d) == TLP_E_BE);                   /* 같은 BE 인데 QW 정렬 아님 */
        memcpy(x, v3, 12); x[6] = 0x60;
        CHECK(tlp_decode(x, 12, &d) == TLP_E_FIELD);                /* Completion Status 011 = reserved */
        const uint8_t big[12] = { 0x00,0x00,0x00,0x00, 0x01,0x00,0x07,0xFF, 0x80,0x00,0x00,0x04 };
        CHECK(tlp_decode(big, 12, &d) == TLP_E_4K);                 /* 1024 DW 인데 시작이 4KB 정렬 아님 */
    }
    /* 오류 주입 (encode): 오류면 out 을 건드리지 않는다 */
    {
        tlp_hdr e = mk_req(TLP_MRD, 0x0100, 0, 0x1000, 4);
        e.tc = 8;           CHECK(tlp_encode(&e, b, sizeof b) == TLP_E_FIELD);
        e.tc = 0; e.len_dw = 0;    CHECK(tlp_encode(&e, b, sizeof b) == TLP_E_FIELD);
        e.len_dw = 1025;           CHECK(tlp_encode(&e, b, sizeof b) == TLP_E_FIELD);
        e.len_dw = 1; e.addr = 0x1002;  CHECK(tlp_encode(&e, b, sizeof b) == TLP_E_ADDR);
        e = mk_req(TLP_MRD, 0x0100, 0, 0x1000, 4);
        memset(b, 0xEE, sizeof b);
        CHECK(tlp_encode(&e, b, 11) == TLP_E_CAP && b[0] == 0xEE);
        e = mk_req(TLP_MWR, 0x0100, 0, 0x100000000ull, 4);
        CHECK(tlp_encode(&e, b, 15) == TLP_E_CAP && b[0] == 0xEE && tlp_encode(&e, b, 16) == 16 && b[0] == 0x60);
        tlp_hdr c = mk_cpl(TLP_CPLD, 0, CPL_SC, 0x0100, 0, 1, 4, 0x80);
        CHECK(tlp_encode(&c, b, sizeof b) == TLP_E_FIELD);          /* Lower Address 는 7비트 */
        c.lower_addr = 0; c.byte_count = 0;     CHECK(tlp_encode(&c, b, sizeof b) == TLP_E_FIELD);
        c.byte_count = 4097;                    CHECK(tlp_encode(&c, b, sizeof b) == TLP_E_FIELD);
        c = mk_cpl(TLP_CPL, 0, CPL_SC, 0x0100, 0, 3, 4, 0);
        CHECK(tlp_encode(&c, b, sizeof b) == TLP_E_FIELD);          /* Cpl(데이터 없음) 에 Length */
    }

    /* tlp_be: 손 계산 + 브루트포스 전수 */
    {
        tlp_span sp; memset(&sp, 0, sizeof sp);
        CHECK(tlp_be(0xF7C01006u, 8, &sp) == TLP_OK && sp.dw_addr == 0xF7C01004u && sp.len_dw == 3 && sp.first_be == 0xC && sp.last_be == 0x3);
        CHECK(tlp_be(0x1000, 4, &sp) == TLP_OK && sp.len_dw == 1 && sp.first_be == 0xF && sp.last_be == 0);
        CHECK(tlp_be(0x1001, 2, &sp) == TLP_OK && sp.len_dw == 1 && sp.first_be == 0x6 && sp.last_be == 0);
        CHECK(tlp_be(0x1003, 2, &sp) == TLP_OK && sp.len_dw == 2 && sp.first_be == 0x8 && sp.last_be == 0x1);
        CHECK(tlp_be(0x1002, 0, &sp) == TLP_OK && sp.dw_addr == 0x1000 && sp.len_dw == 1 && sp.first_be == 0 && sp.last_be == 0);
        CHECK(tlp_be(0x1000, 4096, &sp) == TLP_OK && sp.len_dw == 1024 && sp.first_be == 0xF && sp.last_be == 0xF);
        CHECK(tlp_be(0x1001, 4096, &sp) == TLP_E_FIELD);            /* 1025 DW */
        CHECK(tlp_be(0x1000, 4097, &sp) == TLP_E_FIELD);
        int bad = 0;
        for (uint32_t off = 0; off < 16; off++)
            for (uint32_t len = 0; len <= 600; len++) {
                tlp_span a, e; memset(&a, 0, sizeof a);
                be_brute(0x10000000u + off, len, &e);
                if (tlp_be(0x10000000u + off, len, &a) != TLP_OK || a.dw_addr != e.dw_addr || a.len_dw != e.len_dw ||
                    a.first_be != e.first_be || a.last_be != e.last_be) bad++;
            }
        CHECK(bad == 0);
    }

    /* tlp_split: 손 계산 벡터 */
    {
        tlp_chunk c[64];
        CHECK(tlp_split(0xABCD0E70u, 1024, 512, c, 64) == 3 &&
              c[0].addr == 0xABCD0E70u && c[0].len == 400 && c[1].addr == 0xABCD1000u && c[1].len == 512 &&
              c[2].addr == 0xABCD1200u && c[2].len == 112);
        CHECK(tlp_split(0x1002, 256, 256, c, 64) == 2 && c[0].len == 254 && c[1].addr == 0x1100 && c[1].len == 2);   /* MPS 함정 */
        CHECK(tlp_split(0xFFE, 4, 4096, c, 64) == 2 && c[0].len == 2 && c[1].addr == 0x1000 && c[1].len == 2);
        CHECK(tlp_split(0x1004, 4096, 256, c, 64) == 17 && c[15].addr == 0x1F04 && c[15].len == 252 && c[16].addr == 0x2000 && c[16].len == 4);
        CHECK(tlp_split(0x1000, 4096, 256, c, 15) == TLP_E_CAP);     /* 16 조각인데 15칸 */
        CHECK(tlp_split(0x1000, 0, 256, c, 64) == TLP_E_FIELD);
        CHECK(tlp_split(0x1000, 64, 192, c, 64) == TLP_E_FIELD);     /* 2의 거듭제곱 아님 */
        CHECK(tlp_split(0x1000, 64, 64, c, 64) == TLP_E_FIELD);      /* MPS/MRRS 최소 128 */
    }
    /* tlp_split: 성질 — 빈틈 없이 덮음, Length×4 ≤ max, 4KB 안 넘음, 각 조각이 진짜 TLP 로 인코딩됨, 앞에서부터 최대 */
    {
        static const uint32_t lens[] = { 1, 3, 4, 127, 128, 129, 511, 512, 513, 4095, 4096, 8192, 10000 };
        static const uint32_t maxs[] = { 128, 256, 512, 4096 };
        tlp_chunk c[128]; int bad = 0, cases = 0;
        for (uint32_t off = 0; off < 4096; off += 13)
            for (size_t li = 0; li < sizeof lens / sizeof lens[0]; li++)
                for (size_t mi = 0; mi < 4; mi++) {
                    uint64_t a = 0x7F0000000ull + off;   /* 4GB 이상 → 4DW 로 인코딩 */
                    int n = tlp_split(a, lens[li], maxs[mi], c, 128);
                    cases++;
                    if (n <= 0) { bad++; continue; }
                    uint64_t cur = a;
                    for (int i = 0; i < n; i++) {
                        tlp_span sp; memset(&sp, 0, sizeof sp);
                        uint64_t e = c[i].addr + c[i].len;
                        tlp_hdr q = mk_req(TLP_MWR, 0x0300, 0, c[i].addr, c[i].len);
                        uint8_t o[16];
                        bool maximal = (i == n - 1) || e == (c[i].addr & ~3ull) + maxs[mi] || (e & 0xFFFu) == 0;
                        if (c[i].addr != cur || c[i].len == 0 || tlp_be(c[i].addr, c[i].len, &sp) != TLP_OK ||
                            4u * sp.len_dw > maxs[mi] || (c[i].addr >> 12) != ((e - 1u) >> 12) ||
                            tlp_encode(&q, o, sizeof o) != 16 || !maximal) bad++;
                        cur = e;
                    }
                    if (cur != a + lens[li]) bad++;
                }
        printf("B split properties: %d cases, bad=%d\n", cases, bad);
        CHECK(bad == 0);
    }

    /* ===== Part C: Completion Byte Count / Lower Address ===== */
    CHECK(cpl_byte_count(1, 0xF, 0) == 4 && cpl_byte_count(1, 0x9, 0) == 4 && cpl_byte_count(1, 0x6, 0) == 2);
    CHECK(cpl_byte_count(1, 0x0, 0) == 1 && cpl_byte_count(1, 0x8, 0) == 1 && cpl_byte_count(1, 0x7, 0) == 3);
    CHECK(cpl_byte_count(3, 0xC, 0x3) == 8 && cpl_byte_count(2, 0x8, 0x1) == 2 && cpl_byte_count(1024, 0xF, 0xF) == 4096);
    CHECK(cpl_lower_addr(0xF7C01004u, 0xC) == 0x06 && cpl_lower_addr(0x20000040u, 0) == 0x40 && cpl_lower_addr(0x3000010Cu, 0x8) == 0x0F);
    {
        int bad = 0;   /* 브루트포스: Length 1 전부, Length 2..5 × 연속 BE 조합 */
        for (uint8_t f = 0; f < 16; f++) {
            int lo = be_lo(f), hi = be_hi(f);
            if (cpl_byte_count(1, f, 0) != (f ? hi - lo + 1 : 1)) bad++;
        }
        static const uint8_t fs[4] = { 0xF, 0xE, 0xC, 0x8 }, ls[4] = { 0x1, 0x3, 0x7, 0xF };
        for (uint16_t len = 2; len <= 5; len++)
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
                tlp_hdr q = mk_req(TLP_MRD, 0, 0, 0x1000, 4);
                q.len_dw = len; q.first_be = fs[i]; q.last_be = ls[j];
                int first = 0x1000 + be_lo(fs[i]), last = 0x1000 + 4 * (len - 1) + be_hi(ls[j]);
                if (cpl_byte_count(len, fs[i], ls[j]) != last - first + 1) bad++;
            }
        CHECK(bad == 0);
    }
    {
        cpl_chunk c[64];
        tlp_hdr rd = mk_req(TLP_MRD, bdf_id(1, 0, 0), 0x00, 0xABCD0E70u, 400);
        int n = cpl_split(&rd, 64, 256, c, 64);
        CHECK(n == 2 && c[0].len_dw == 52 && c[0].byte_count == 400 && c[0].lower_addr == 0x70 &&
              c[1].addr == 0xABCD0F40u && c[1].len_dw == 48 && c[1].byte_count == 192 && c[1].lower_addr == 0x40);
        n = cpl_split(&rd, 128, 256, c, 64);
        CHECK(n == 2 && c[0].len_dw == 36 && c[0].byte_count == 400 && c[0].lower_addr == 0x70 &&
              c[1].addr == 0xABCD0F00u && c[1].len_dw == 64 && c[1].byte_count == 256 && c[1].lower_addr == 0x00);
        rd = mk_req(TLP_MRD, bdf_id(1, 0, 0), 0x0C, 0xF7C01006u, 8);          /* V1: 한 번에 */
        n = cpl_split(&rd, 64, 128, c, 64);
        CHECK(n == 1 && c[0].len_dw == 3 && c[0].byte_count == 8 && c[0].lower_addr == 0x06);
        rd = mk_req(TLP_MRD, 0x0100, 0, 0x20000040u, 0);                       /* zero-length read */
        n = cpl_split(&rd, 64, 128, c, 64);
        CHECK(n == 1 && c[0].len_dw == 1 && c[0].byte_count == 1 && c[0].lower_addr == 0x40);
        rd = mk_req(TLP_MRD, 0x0100, 0, 0x30000107u, 154);                    /* BE 8 / 1, 40 DW */
        CHECK(rd.len_dw == 40 && rd.first_be == 0x8 && rd.last_be == 0x1);
        n = cpl_split(&rd, 64, 128, c, 64);
        CHECK(n == 2 && c[0].len_dw == 31 && c[0].byte_count == 154 && c[0].lower_addr == 0x07 &&
              c[1].len_dw == 9 && c[1].byte_count == 33 && c[1].lower_addr == 0x00);
        rd = mk_req(TLP_MRD, 0x0100, 0, 0x80000000u, 4096);                   /* 1024 DW, RCB128, MPS256 → 16개 */
        n = cpl_split(&rd, 128, 256, c, 64);
        CHECK(n == 16 && c[0].byte_count == 4096 && c[1].byte_count == 3840 && c[15].byte_count == 256 && c[15].lower_addr == 0);
        CHECK(cpl_split(&rd, 128, 256, c, 15) == TLP_E_CAP && cpl_split(&rd, 32, 256, c, 64) == TLP_E_FIELD &&
              cpl_split(&rd, 64, 96, c, 64) == TLP_E_FIELD);
        tlp_hdr wr = mk_req(TLP_MWR, 0x0100, 0, 0x1000, 4);
        CHECK(cpl_split(&wr, 64, 128, c, 64) == TLP_E_FIELD);                 /* MWr 은 Completion 없음 (posted) */
    }
    /* 완료 성질 (무작위): 바이트 합 = BC0, 마지막 아니면 RCB 경계에서 끝, Length×4 ≤ MPS, LA 일치, "마지막" 판정식 */
    {
        uint32_t s = 0xBADC0DEu; int bad = 0;
        for (int it = 0; it < 3000; it++) {
            uint64_t a = xs32(&s);
            uint32_t room = 4096u - (uint32_t)(a & 0xFFFu);
            tlp_hdr rd = mk_req(TLP_MRD, 0x0100, 0, a, xs32(&s) % (room + 1u));
            uint32_t rcb = (xs32(&s) & 1u) ? 128u : 64u, mps = 128u << (xs32(&s) % 3u);
            cpl_chunk c[64];
            int n = cpl_split(&rd, rcb, mps, c, 64);
            if (n <= 0) { bad++; continue; }
            uint32_t bc0 = cpl_byte_count(rd.len_dw, rd.first_be, rd.last_be), got = 0;
            for (int i = 0; i < n; i++) {
                uint64_t e = c[i].addr + 4u * c[i].len_dw;
                bool last = c[i].byte_count <= 4u * c[i].len_dw - (c[i].lower_addr & 3u);   /* 요청자가 쓰는 판정식 */
                uint32_t mine = last ? c[i].byte_count : 4u * c[i].len_dw - (c[i].lower_addr & 3u);
                if (c[i].byte_count != bc0 - got || 4u * c[i].len_dw > mps || c[i].len_dw == 0 ||
                    (i < n - 1 && (e % rcb) != 0) || last != (i == n - 1) ||
                    c[i].lower_addr != (i == 0 ? cpl_lower_addr(rd.addr, rd.first_be) : (uint8_t)(c[i].addr & 0x7Fu)))
                    bad++;
                got += mine;
            }
            if (got != bc0) bad++;
        }
        CHECK(bad == 0);
    }

    /* AER "TLP Header" 로그 (DW 를 u32 로 찍은 값) → 해석 */
    {
        static const uint32_t L1[3] = { 0x4a000003, 0x00000008, 0x01000c06 };
        static const uint32_t L2[4] = { 0x20000080, 0x0300ffff, 0x00000001, 0x23456000 };
        static const uint32_t L3[3] = { 0x40004001, 0x0300000f, 0xfe001000 };
        static const uint32_t L4[3] = { 0x0a000000, 0x00082004, 0x00000500 };
        uint8_t x[16]; char s[160] = "";
        tlp_from_dws(L1, 3, x); memset(&d, 0, sizeof d);
        CHECK(tlp_decode(x, 12, &d) == 12 && tlp_describe(&d, s, sizeof s) > 0 &&
              strcmp(s, "CplD SC 00:00.0->01:00.0 tag=0x0c len=3 bc=8 la=0x06") == 0);
        printf("C AER L1: %s\n", s);
        tlp_from_dws(L2, 4, x); memset(&d, 0, sizeof d);
        CHECK(tlp_decode(x, 16, &d) == 16 && tlp_describe(&d, s, sizeof s) > 0 &&
              strcmp(s, "MRd64 03:00.0 tag=0xff addr=0x123456000 len=128 be=f/f") == 0);
        printf("C AER L2: %s\n", s);
        tlp_from_dws(L3, 3, x); memset(&d, 0, sizeof d);
        CHECK(tlp_decode(x, 12, &d) == 12 && tlp_describe(&d, s, sizeof s) > 0 &&
              strcmp(s, "MWr32 03:00.0 tag=0x00 addr=0xfe001000 len=1 be=f/0 EP") == 0);
        printf("C AER L3: %s\n", s);
        tlp_from_dws(L4, 3, x); memset(&d, 0, sizeof d);
        CHECK(tlp_decode(x, 12, &d) == 12 && tlp_describe(&d, s, sizeof s) > 0 &&
              strcmp(s, "Cpl UR 00:01.0->00:00.0 tag=0x05 len=0 bc=4 la=0x00") == 0);
        printf("C AER L4: %s\n", s);
        h = mk_req(TLP_MWR, bdf_id(3, 0, 0), 0, 0x1234567800ull, 4);
        h.tc = 7; h.attr = 7; h.th = h.td = h.ep = true; h.at = 3;
        CHECK(tlp_describe(&h, s, sizeof s) > 0 &&
              strcmp(s, "MWr64 03:00.0 tag=0x00 addr=0x1234567800 len=1 be=f/0 TC7 RO NS IDO TH TD EP AT3") == 0);
        memset(s, 'Z', sizeof s);
        CHECK(tlp_describe(&h, s, 20) == TLP_E_CAP && s[0] == 'Z');           /* 모자라면 안 씀 */
    }

    /* SSD 시나리오: NVMe 컨트롤러(03:00.0)가 호스트 메모리 0xABCD0E70 에서 1024B 를 읽는다.
     * MRRS 512 → MRd 3개 (tag 0..2), 호스트 Root Complex 가 RCB 64 / MPS 256 으로 CplD 를 돌려준다. */
    {
        tlp_chunk rq[8]; cpl_chunk cp[16];
        uint8_t seen[1024]; memset(seen, 0, sizeof seen);
        int nr = tlp_split(0xABCD0E70u, 1024, 512, rq, 8), total_cpl = 0, bad = 0;
        CHECK(nr == 3);
        for (int i = 0; i < nr && i < 8; i++) {
            tlp_hdr rd = mk_req(TLP_MRD, bdf_id(3, 0, 0), (uint8_t)i, rq[i].addr, rq[i].len);
            uint8_t o[16]; memset(&d, 0, sizeof d);
            int n = tlp_encode(&rd, o, sizeof o);
            if (n != 12 || tlp_decode(o, 12, &d) != 12 || !hdr_eq(&d, &rd)) bad++;
            if (n == 12) hex(i == 0 ? "C SSD MRd tag0:" : i == 1 ? "C SSD MRd tag1:" : "C SSD MRd tag2:", o, 12);
            int nc = cpl_split(&rd, 64, 256, cp, 16);
            uint64_t first = rq[i].addr;                        /* 요청자: tag → 요청 첫 바이트 주소 */
            for (int k = 0; k < nc; k++) {
                tlp_hdr cd = mk_cpl(TLP_CPLD, bdf_id(0, 0, 0), CPL_SC, bdf_id(3, 0, 0), (uint8_t)i,
                                    cp[k].len_dw, cp[k].byte_count, cp[k].lower_addr);
                uint8_t w[16]; tlp_hdr r; memset(&r, 0, sizeof r);
                if (tlp_encode(&cd, w, sizeof w) != 12 || tlp_decode(w, 12, &r) != 12) { bad++; continue; }
                /* 받은 CplD 만으로 위치 계산: 이 데이터의 첫 유효 바이트 = first + (BC0 − BC) */
                uint64_t at = first + (rq[i].len - r.byte_count);
                if ((at & 0x7Fu) != r.lower_addr) bad++;
                uint32_t cnt = r.byte_count <= 4u * r.len_dw - (r.lower_addr & 3u) ? r.byte_count : 4u * r.len_dw - (r.lower_addr & 3u);
                for (uint32_t j = 0; j < cnt; j++) {
                    uint64_t off = at + j - 0xABCD0E70u;
                    if (off >= 1024 || seen[off]++) bad++;
                }
                printf("C SSD   CplD tag%d len=%2u DW bc=%3u la=0x%02x\n", i, r.len_dw, r.byte_count, r.lower_addr);
                total_cpl++;
            }
        }
        int covered = 0;
        for (int j = 0; j < 1024; j++) covered += seen[j] == 1;
        CHECK(bad == 0 && total_cpl == 5 && covered == 1024);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
