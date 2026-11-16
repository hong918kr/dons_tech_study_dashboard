/* PCIE-B10 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 코덱(tlp_check/encode/decode, tlp_be, tlp_split, cpl_byte_count, cpl_split)을 그대로 넣고
 * 그 위에 연습 네 개를 푼다.
 *   연습 1: 정렬 분할(tlp_split_aligned) vs 앞에서부터 최대 분할 — 64B 캐시 라인이 두 TLP 로 쪼개지는 수
 *   연습 2: Byte Count 표 전체와 유효한 (First BE, Last BE) 조합 수
 *   연습 3: DEVCTL(MPS/MRRS/Extended Tag) 디코드, 태그 수로 막히는 읽기 대역폭
 *   연습 4: 요청자 쪽 완료 조립기 — tag 표, Unexpected Completion, BC/LA 불일치, UR
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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

/* ======================= 추가 연습 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static tlp_hdr mk_req(tlp_kind k, uint16_t rid, uint8_t tag, uint64_t addr, uint32_t len) {
    tlp_hdr h; memset(&h, 0, sizeof h);
    tlp_span sp; memset(&sp, 0, sizeof sp);
    h.kind = k; h.req_id = rid; h.tag = tag;
    if (tlp_be(addr, len, &sp) == TLP_OK) { h.addr = sp.dw_addr; h.len_dw = sp.len_dw; h.first_be = sp.first_be; h.last_be = sp.last_be; }
    return h;
}

/* ---- 연습 1: 정렬 분할 — 첫 조각만 짧게, 이후 조각은 max 정렬 주소에서 시작 ---- */
static int tlp_split_aligned(uint64_t addr, uint64_t len, uint32_t max_bytes, tlp_chunk *out, size_t cap) {
    if (len == 0 || !pow2_in(max_bytes, 128u, 4096u)) return TLP_E_FIELD;
    size_t n = 0;
    uint64_t cur = addr, end = addr + len;
    while (cur < end) {
        uint64_t e = (cur & ~(uint64_t)(max_bytes - 1u)) + max_bytes;   /* 다음 max 정렬 경계 (4KB 도 포함) */
        if (e > end) e = end;
        if (n >= cap) return TLP_E_CAP;
        out[n].addr = cur; out[n].len = (uint32_t)(e - cur); n++;
        cur = e;
    }
    return (int)n;
}
/* 64B 캐시 라인 중 두 TLP 에 걸쳐 쪼개진 라인 수 (= 조각 경계가 라인 중간에 있는 횟수) */
static int split_lines(const tlp_chunk *c, int n) {
    int k = 0;
    for (int i = 1; i < n; i++) if (c[i].addr & 63u) k++;
    return k;
}

/* ---- 연습 3: DEVCTL 디코드와 태그 수 한계 ---- */
#define PCI_EXP_DEVCTL_RELAX_EN   0x0010
#define PCI_EXP_DEVCTL_EXT_TAG    0x0100
#define PCI_EXP_DEVCTL_NOSNOOP_EN 0x0800
typedef struct { uint32_t mps, mrrs, tags; bool ro, ns; } devctl_info;
static int devctl_decode(uint16_t v, devctl_info *o) {
    unsigned p = (v & PCI_EXP_DEVCTL_PAYLOAD) >> 5, r = (v & PCI_EXP_DEVCTL_READRQ) >> 12;
    if (p > 5 || r > 5) return TLP_E_FIELD;                     /* 110b, 111b = reserved */
    o->mps = 128u << p; o->mrrs = 128u << r;
    o->tags = (v & PCI_EXP_DEVCTL_EXT_TAG) ? 256u : 32u;        /* 8-bit tag vs 5-bit tag (10-bit tag 는 DEVCTL2) */
    o->ro = (v & PCI_EXP_DEVCTL_RELAX_EN) != 0;
    o->ns = (v & PCI_EXP_DEVCTL_NOSNOOP_EN) != 0;
    return TLP_OK;
}
/* 링크를 채우려면 동시에 몇 개의 MRd 가 날아가 있어야 하나: ceil(BW × RTT / MRRS) */
static uint32_t tags_needed(uint32_t link_MBps, uint32_t rtt_ns, uint32_t mrrs) {
    uint64_t num = (uint64_t)link_MBps * rtt_ns, den = 1000ull * mrrs;    /* MB/s × ns = 10^-3 B */
    return (uint32_t)((num + den - 1u) / den);
}
/* 태그 수가 한계일 때 읽기 대역폭 (MB/s): min(링크, tags × MRRS / RTT) */
static uint32_t read_bw_MBps(uint32_t tags, uint32_t mrrs, uint32_t rtt_ns, uint32_t link_MBps) {
    uint64_t bw = (uint64_t)tags * mrrs * 1000u / rtt_ns;
    return bw < link_MBps ? (uint32_t)bw : link_MBps;
}

/* ---- 연습 4: 요청자 쪽 완료 조립기 (tag 표) ---- */
enum { RQ_PART = 1, RQ_DONE = 2, RQ_E_BUSY = -20, RQ_E_UNEXP = -21, RQ_E_BC = -22, RQ_E_LA = -23, RQ_E_STATUS = -24 };
#define RQ_TAGS 32
typedef struct { bool busy; uint64_t first; uint16_t bc0, remaining; } rq_slot;
typedef struct { uint16_t my_id; rq_slot s[RQ_TAGS]; uint32_t unexpected; } requester;

static int rq_issue(requester *r, const tlp_hdr *rd) {
    if (rd->kind != TLP_MRD || rd->tag >= RQ_TAGS || tlp_check(rd) != TLP_OK) return TLP_E_FIELD;
    rq_slot *q = &r->s[rd->tag];
    if (q->busy) return RQ_E_BUSY;
    q->busy = true;
    q->first = rd->addr + (rd->first_be ? (unsigned)be_lo(rd->first_be) : 0u);
    q->bc0 = q->remaining = cpl_byte_count(rd->len_dw, rd->first_be, rd->last_be);
    return TLP_OK;
}
/* 받은 완료 헤더 바이트 하나를 처리 */
static int rq_complete(requester *r, const uint8_t *bytes, size_t n) {
    tlp_hdr c;
    int rc = tlp_decode(bytes, n, &c);
    if (rc < 0) return rc;
    if (!(c.kind == TLP_CPL || c.kind == TLP_CPLD) || c.req_id != r->my_id || c.tag >= RQ_TAGS || !r->s[c.tag].busy) {
        r->unexpected++;                                        /* AER: Unexpected Completion */
        return RQ_E_UNEXP;
    }
    rq_slot *q = &r->s[c.tag];
    if (c.status != CPL_SC || c.kind == TLP_CPL) { q->busy = false; return RQ_E_STATUS; }   /* UR/CA: 요청 끝, 오류 */
    if (c.byte_count != q->remaining) return RQ_E_BC;
    if (c.lower_addr != (uint8_t)((q->first + (q->bc0 - q->remaining)) & 0x7Fu)) return RQ_E_LA;
    uint32_t room = 4u * c.len_dw - (c.lower_addr & 3u);
    if (c.byte_count <= room) { q->remaining = 0; q->busy = false; return RQ_DONE; }
    q->remaining = (uint16_t)(q->remaining - room);
    return RQ_PART;
}
static int enc_cpl(const cpl_chunk *k, uint16_t rid, uint8_t tag, uint8_t *o) {
    tlp_hdr h; memset(&h, 0, sizeof h);
    h.kind = TLP_CPLD; h.status = CPL_SC; h.req_id = rid; h.tag = tag;
    h.len_dw = k->len_dw; h.byte_count = k->byte_count; h.lower_addr = k->lower_addr;
    return tlp_encode(&h, o, 16);
}

int main(void) {
    /* 연습 1 */
    {
        tlp_chunk g[64], a[64];
        int ng = tlp_split(0x1004, 4096, 256, g, 64), na = tlp_split_aligned(0x1004, 4096, 256, a, 64);
        printf("ex1 greedy : %d TLPs, first=%#llx+%u, split 64B lines=%d\n", ng, (unsigned long long)g[0].addr, g[0].len, split_lines(g, ng));
        printf("ex1 aligned: %d TLPs, first=%#llx+%u, split 64B lines=%d\n", na, (unsigned long long)a[0].addr, a[0].len, split_lines(a, na));
        CHECK(ng == 17 && na == 17 && split_lines(g, ng) == 15 && split_lines(a, na) == 0);
        CHECK(a[0].len == 252 && a[1].addr == 0x1100 && a[16].addr == 0x2000 && a[16].len == 4);
        int bad = 0;   /* 정렬 분할도 규칙(4KB, Length×4 ≤ max)을 지키고 덮는다 */
        for (uint32_t off = 0; off < 4096; off += 3)
            for (uint32_t mx = 128; mx <= 4096; mx <<= 1) {
                int n = tlp_split_aligned(0x40000000u + off, 5000, mx, a, 64);
                uint64_t cur = 0x40000000u + off;
                for (int i = 0; i < n; i++) {
                    tlp_hdr q = mk_req(TLP_MWR, 0x0300, 0, a[i].addr, a[i].len);
                    uint8_t o[16];
                    if (a[i].addr != cur || 4u * q.len_dw > mx || tlp_encode(&q, o, 16) != 12 ||
                        (i > 0 && (a[i].addr % mx) != 0)) bad++;
                    cur += a[i].len;
                }
                if (n <= 0 || cur != 0x40000000u + off + 5000) bad++;
            }
        CHECK(bad == 0);
    }
    /* 연습 2: Byte Count 표와 유효 BE 조합 수 */
    {
        int n1 = 0, n3 = 0, n2qw = 0, n2 = 0;
        for (unsigned f = 0; f < 16; f++)
            for (unsigned l = 0; l < 16; l++) {
                tlp_hdr q = mk_req(TLP_MRD, 0x0100, 0, 0x1000, 4);
                q.first_be = (uint8_t)f; q.last_be = (uint8_t)l;
                q.len_dw = 1; n1 += tlp_check(&q) == TLP_OK;
                q.len_dw = 3; n3 += tlp_check(&q) == TLP_OK;
                q.len_dw = 2; n2qw += tlp_check(&q) == TLP_OK;
                q.addr = 0x1004; n2 += tlp_check(&q) == TLP_OK;
            }
        printf("ex2 valid (First,Last) pairs: len1=%d len3=%d len2(QW aligned)=%d len2(not QW)=%d\n", n1, n3, n2qw, n2);
        CHECK(n1 == 16 && n3 == 16 && n2qw == 225 && n2 == 16);
        printf("ex2 Length 1 :");
        for (unsigned f = 0; f < 16; f++) printf(" %X%X%X%X=%u", f >> 3 & 1, f >> 2 & 1, f >> 1 & 1, f & 1, cpl_byte_count(1, (uint8_t)f, 0));
        printf("\nex2 Length>1 : BC = 4L -");
        static const uint8_t fs[4] = { 0xF, 0xE, 0xC, 0x8 }, ls[4] = { 0xF, 0x7, 0x3, 0x1 };
        int bad = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
            unsigned k = 4u * 10 - cpl_byte_count(10, fs[i], ls[j]);
            printf(" [%X/%X]%u", fs[i], ls[j], k);
            if (k != (unsigned)(i + j)) bad++;              /* 앞에서 꺼진 i + 뒤에서 꺼진 j */
        }
        printf("\n");
        CHECK(bad == 0 && cpl_lower_addr(0x1004, 0x8) == 0x07 && cpl_byte_count(1, 0x9, 0) == 4 && cpl_byte_count(1, 0x0, 0) == 1 && cpl_byte_count(2, 0x8, 0x1) == 2);
    }
    /* 연습 3 */
    {
        devctl_info di; memset(&di, 0, sizeof di);
        CHECK(devctl_decode(0x2930, &di) == TLP_OK && di.mps == 256 && di.mrrs == 512 && di.tags == 256 && di.ro && di.ns);
        CHECK(devctl_decode(0x2830, &di) == TLP_OK && di.tags == 32 && di.ro);
        CHECK(devctl_decode(0x00C0, &di) == TLP_E_FIELD && devctl_decode(0x6000, &di) == TLP_E_FIELD);
        uint32_t link = 7877;   /* Gen4 x4: 16 GT/s × 4 × 128/130 / 8 (PCIE-B02) */
        printf("ex3 tags_needed(Gen4x4, RTT 1us/2us/4us, MRRS 512) = %u/%u/%u\n",
               tags_needed(link, 1000, 512), tags_needed(link, 2000, 512), tags_needed(link, 4000, 512));
        printf("ex3 read_bw RTT 4us: 32 tags MRRS 512 = %u MB/s, 32 tags MRRS 128 = %u MB/s, 256 tags MRRS 512 = %u MB/s\n",
               read_bw_MBps(32, 512, 4000, link), read_bw_MBps(32, 128, 4000, link), read_bw_MBps(256, 512, 4000, link));
        CHECK(tags_needed(link, 4000, 512) == 62 && tags_needed(link, 1000, 512) == 16);
        CHECK(read_bw_MBps(32, 512, 4000, link) == 4096 && read_bw_MBps(32, 128, 4000, link) == 1024 &&
              read_bw_MBps(256, 512, 4000, link) == link);
    }
    /* 연습 4: SSD 시나리오의 CplD 를 tag 가 섞인 순서로 받는다 (같은 tag 안에서는 주소 순서) */
    {
        requester r; memset(&r, 0, sizeof r);
        r.my_id = bdf_id(3, 0, 0);
        tlp_chunk rq[4]; cpl_chunk cp[3][8]; int nc[3];
        int nr = tlp_split(0xABCD0E70u, 1024, 512, rq, 4);
        CHECK(nr == 3);
        for (int i = 0; i < 3 && i < nr; i++) {
            tlp_hdr rd = mk_req(TLP_MRD, 0x0300, (uint8_t)i, rq[i].addr, rq[i].len);
            CHECK(rq_issue(&r, &rd) == TLP_OK);
            nc[i] = cpl_split(&rd, 64, 256, cp[i], 8);
        }
        tlp_hdr again = mk_req(TLP_MRD, 0x0300, 1, 0x1000, 4);
        CHECK(rq_issue(&r, &again) == RQ_E_BUSY);                     /* 아직 끝나지 않은 tag 재사용 금지 */
        static const int order[5][2] = { {1,0}, {0,0}, {2,0}, {1,1}, {0,1} };   /* (tag, k) */
        int res[5], ok = 1;
        for (int s = 0; s < 5; s++) {
            int t = order[s][0], k = order[s][1];
            uint8_t o[16];
            if (k >= nc[t] || enc_cpl(&cp[t][k], 0x0300, (uint8_t)t, o) != 12) { ok = 0; continue; }
            res[s] = rq_complete(&r, o, 12);
        }
        printf("ex4 results: %d %d %d %d %d (1=PART 2=DONE)\n", res[0], res[1], res[2], res[3], res[4]);
        CHECK(ok && res[0] == RQ_PART && res[1] == RQ_PART && res[2] == RQ_DONE && res[3] == RQ_DONE && res[4] == RQ_DONE);
        CHECK(!r.s[0].busy && !r.s[1].busy && !r.s[2].busy);
        /* 오류 주입 */
        uint8_t o[16];
        enc_cpl(&cp[0][1], 0x0300, 0, o);
        CHECK(rq_complete(&r, o, 12) == RQ_E_UNEXP);                  /* 끝난 tag 에 또 옴 */
        enc_cpl(&cp[0][0], 0x0300, 7, o);
        CHECK(rq_complete(&r, o, 12) == RQ_E_UNEXP);                  /* 낸 적 없는 tag */
        tlp_hdr rd = mk_req(TLP_MRD, 0x0300, 4, 0xABCD0E70u, 400);
        CHECK(rq_issue(&r, &rd) == TLP_OK);
        enc_cpl(&cp[0][0], 0x0400, 4, o);
        CHECK(rq_complete(&r, o, 12) == RQ_E_UNEXP && r.unexpected == 3);   /* 다른 요청자 ID */
        cpl_chunk badbc = cp[0][0]; badbc.byte_count = 396;
        enc_cpl(&badbc, 0x0300, 4, o);
        CHECK(rq_complete(&r, o, 12) == RQ_E_BC);
        cpl_chunk badla = cp[0][0]; badla.lower_addr = 0x74;
        enc_cpl(&badla, 0x0300, 4, o);
        CHECK(rq_complete(&r, o, 12) == RQ_E_LA);
        tlp_hdr ur; memset(&ur, 0, sizeof ur);
        ur.kind = TLP_CPL; ur.status = CPL_UR; ur.req_id = 0x0300; ur.tag = 4; ur.byte_count = 400; ur.lower_addr = 0x70;
        CHECK(tlp_encode(&ur, o, 16) == 12 && rq_complete(&r, o, 12) == RQ_E_STATUS && !r.s[4].busy);
        CHECK(rq_issue(&r, &rd) == TLP_OK);                             /* UR 뒤 tag 는 다시 쓸 수 있다 */
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
