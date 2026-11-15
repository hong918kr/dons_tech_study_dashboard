/* COMM-M15 USB 기초: 디스크립터 파싱과 enumeration 흐름
 *
 * Part A: 디스크립터 파서 — device(18) / configuration(9, wTotalLength) / interface(9) / endpoint(7) / IAD(8) /
 *         string(UTF-16LE → UTF-8). 모르는 클래스 디스크립터(HID 0x21, CS_INTERFACE 0x24, CS_ENDPOINT 0x25 ...)는
 *         bLength 만큼 건너뛰고 목록에만 남긴다. 엔드포인트 해석: 방향 bit7, 전송 타입 bits1:0,
 *         iso sync bits3:2 / usage bits5:4, wMaxPacketSize bits10:0 + 추가 트랜잭션 bits12:11, bInterval → 주기(µs).
 * Part B: 견고성 — bLength 0(무한 루프 함정), bLength > 남은 바이트, wTotalLength > 버퍼 / < 9, 타입 불일치,
 *         bNumInterfaces / bNumEndpoints 불일치, 고아 엔드포인트, IAD 범위, 홀수 길이 문자열, 짝 없는 서로게이트
 *         → 각각 다른 오류 코드. 고정 시드 xorshift32 변이 퍼징 (정확한 크기의 힙 버퍼라 ASan 이 1바이트 초과도 잡는다).
 * Part C: 8바이트 SETUP 패킷 디코더 + 디스크립터 배열로 답하는 mock 장치 + 호스트 enumeration 순서
 *         (reset → GET_DESCRIPTOR(device,64) → reset → SET_ADDRESS → device 18 → config 9 → config wTotalLength →
 *          string 0(LANGID) → strings → SET_CONFIGURATION), wLength 잘림, EP0 패킷 분할 + ZLP, 결함 주입.
 *
 * 예시 덤프 3개(CDC-ACM+IAD, HID 마우스, UAC2 비슷한 iso)는 이 레슨이 만든 것(constructed)이다. 단, HID 마우스의
 * device 디스크립터 18바이트·인터페이스 필드·문자열·HID report 길이(46)는 이 Mac 에 꽂힌 실제 Logitech 마우스의
 * ioreg 필드에서 가져왔다 (md §1). 펌웨어 쪽 코드는 malloc 없음 — 표는 고정 크기, 출력 버퍼는 호출자가 용량과 함께 준다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     (덤프: ./solution dump, 로그: ./solution enum)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>   /* 테스트 하네스의 "정확한 크기" 퍼징 버퍼에만 malloc — 파서·mock 은 쓰지 않는다 */
#include <string.h>

/* ---------------- 상수 (USB 2.0 ch.9 Table 9-4/9-5 + 클래스 스펙) ---------------- */
enum {
    DT_DEVICE = 0x01, DT_CONFIG = 0x02, DT_STRING = 0x03, DT_INTERFACE = 0x04, DT_ENDPOINT = 0x05,
    DT_DEVICE_QUALIFIER = 0x06, DT_OTHER_SPEED = 0x07, DT_IAD = 0x0B, DT_BOS = 0x0F,
    DT_HID = 0x21, DT_REPORT = 0x22, DT_CS_INTERFACE = 0x24, DT_CS_ENDPOINT = 0x25
};
enum { REQ_GET_STATUS = 0, REQ_CLEAR_FEATURE = 1, REQ_SET_FEATURE = 3, REQ_SET_ADDRESS = 5,
       REQ_GET_DESCRIPTOR = 6, REQ_SET_DESCRIPTOR = 7, REQ_GET_CONFIGURATION = 8, REQ_SET_CONFIGURATION = 9,
       REQ_GET_INTERFACE = 10, REQ_SET_INTERFACE = 11, REQ_SYNCH_FRAME = 12 };
enum { EP_CONTROL = 0, EP_ISO = 1, EP_BULK = 2, EP_INTERRUPT = 3 };
typedef enum { USB_LS, USB_FS, USB_HS } usb_speed;

typedef enum {
    USB_OK = 0,
    USB_E_SHORT = -1,         /* 고정 필드를 담기에 버퍼가 짧다 (device n<18, config n<9, 꼬리 1바이트) */
    USB_E_BLEN_ZERO = -2,     /* bLength < 2 — 0 이면 off += 0 으로 영원히 제자리 (무한 루프 함정) */
    USB_E_BLEN_OVERRUN = -3,  /* bLength > 남은 바이트 */
    USB_E_BLEN_SMALL = -4,    /* bLength 가 그 타입의 표준 크기보다 작다 (interface<9, endpoint<7, IAD<8 ...) */
    USB_E_TYPE = -5,          /* 기대한 타입이 아니다 (첫 바이트 타입, config 안의 DEVICE/CONFIG) */
    USB_E_TOTAL_SMALL = -6,   /* wTotalLength < 9 (또는 config bLength 보다 작다) */
    USB_E_TOTAL_GT_BUF = -7,  /* wTotalLength > 받은 바이트 수 */
    USB_E_NUM_IF = -8,        /* bNumInterfaces ≠ 서로 다른 bInterfaceNumber 개수 */
    USB_E_NUM_EP = -9,        /* bNumEndpoints ≠ 그 interface 디스크립터 뒤 endpoint 개수 */
    USB_E_EP_ORPHAN = -10,    /* interface 보다 먼저 나온 endpoint */
    USB_E_EP_ADDR = -11,      /* endpoint 번호 0 또는 예약 비트(6:4) 세트 */
    USB_E_IAD_RANGE = -12,    /* IAD 가 없는 interface 를 가리키거나 bInterfaceCount 0 */
    USB_E_STR_ODD = -13,      /* string bLength 홀수 */
    USB_E_STR_UTF16 = -14,    /* 짝 없는 서로게이트 */
    USB_E_MPS0 = -15,         /* bMaxPacketSize0 ∉ {8,16,32,64} (USB 3.x 는 9 = 2^9) */
    USB_E_CAP = -16,          /* 고정 표 또는 출력 버퍼 용량 초과 */
    USB_E_STALL = -17,        /* 장치가 STALL (요청 거부) */
    USB_E_NORESP = -18        /* 장치 무응답 (타임아웃) */
} usb_err;
#define USB_ERR_MIN (-18)

static const char *usb_err_name(int e) {
    static const char *n[] = { "OK", "SHORT", "BLEN_ZERO", "BLEN_OVERRUN", "BLEN_SMALL", "TYPE", "TOTAL_SMALL",
        "TOTAL_GT_BUF", "NUM_IF", "NUM_EP", "EP_ORPHAN", "EP_ADDR", "IAD_RANGE", "STR_ODD", "STR_UTF16", "MPS0",
        "CAP", "STALL", "NORESP" };
    return (e <= 0 && e >= USB_ERR_MIN) ? n[-e] : "?";
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* ---------------- 파싱 결과 (고정 크기 표) ---------------- */
#define USB_MAX_IF    16   /* interface 디스크립터 수 (alternate setting 포함) */
#define USB_MAX_EP    32
#define USB_MAX_IAD    8
#define USB_MAX_EXTRA 32   /* 클래스·벤더 디스크립터 (건너뛰고 기록만) */

typedef struct { uint16_t bcd_usb, vid, pid, bcd_dev; uint8_t cls, sub, proto, mps0, imfr, iprod, iser, ncfg; } usb_dev;
typedef struct { uint8_t addr, attr, interval, blen, iface; uint16_t wmps; } usb_ep;
typedef struct { uint8_t num, alt, nep_decl, nep_found, cls, sub, proto, istr, ep_first; } usb_if;
typedef struct { uint8_t first, count, cls, sub, proto, istr; } usb_iad;
typedef struct { uint8_t type, len, subtype; int8_t owner; uint16_t off; } usb_extra;
typedef struct {
    uint16_t total;
    uint8_t  num_if, cfg_value, icfg, attr, max_power, n_if_distinct;
    usb_if    ifs[USB_MAX_IF];     uint8_t n_if;
    usb_ep    eps[USB_MAX_EP];     uint8_t n_ep;
    usb_iad   iads[USB_MAX_IAD];   uint8_t n_iad;
    usb_extra ext[USB_MAX_EXTRA];  uint8_t n_ext;
} usb_cfg;

/* ================= Part A: device 디스크립터 ================= */

/* 18바이트 device 디스크립터. 반환 USB_OK 또는 오류 (out 은 성공 때만 의미 있음). */
static int usb_parse_device(const uint8_t *d, size_t n, usb_dev *out) {
    if (d == NULL || n < 2) return USB_E_SHORT;
    if (d[0] < 2) return USB_E_BLEN_ZERO;
    if (d[1] != DT_DEVICE) return USB_E_TYPE;
    if (d[0] < 18) return USB_E_BLEN_SMALL;
    if (n < 18) return USB_E_SHORT;
    uint16_t bcd = le16(d + 2);
    uint8_t mps0 = d[7];
    bool ok = mps0 == 8 || mps0 == 16 || mps0 == 32 || mps0 == 64 || (bcd >= 0x0300 && mps0 == 9);
    if (!ok) return USB_E_MPS0;
    out->bcd_usb = bcd;   out->cls = d[4];  out->sub = d[5];  out->proto = d[6];  out->mps0 = mps0;
    out->vid = le16(d + 8);  out->pid = le16(d + 10);  out->bcd_dev = le16(d + 12);
    out->imfr = d[14];  out->iprod = d[15];  out->iser = d[16];  out->ncfg = d[17];
    return USB_OK;
}

/* ================= Part A: endpoint 필드 해석 ================= */

static int      ep_type(uint8_t attr)  { return attr & 3; }
static int      ep_sync(uint8_t attr)  { return (attr >> 2) & 3; }   /* iso 만: 0 none 1 async 2 adaptive 3 sync */
static int      ep_usage(uint8_t attr) { return (attr >> 4) & 3; }   /* iso 만: 0 data 1 feedback 2 implicit-fb data */
static unsigned ep_mps(uint16_t w)     { return w & 0x7FFu; }
static unsigned ep_mult(uint16_t w)    { unsigned m = (w >> 11) & 3u; return m == 3u ? 0u : m + 1u; }  /* 0 = 예약값 */

/* bInterval → 서비스 주기 (µs). 0 = 주기 없음(control/bulk), -1 = 그 타입·속도에서 불가능한 값.
 * 규칙 (USB 2.0 §9.6.6, 이 모델의 정의):
 *   interrupt LS/FS: bInterval 1..255 ms          interrupt HS: 2^(bInterval-1) × 125 µs, bInterval 1..16
 *   iso FS:          2^(bInterval-1) × 1 ms, 1..16  iso HS:       2^(bInterval-1) × 125 µs, 1..16
 *   LS 에는 iso/bulk 가 없다 → -1.  control/bulk → 0 (HS bulk/control OUT 의 bInterval 은 최대 NAK rate) */
static long ep_interval_us(int type, usb_speed sp, uint8_t b) {
    if (sp == USB_LS && (type == EP_ISO || type == EP_BULK)) return -1;
    switch (type) {
    case EP_INTERRUPT:
        if (sp == USB_HS) return (b >= 1 && b <= 16) ? 125L << (b - 1) : -1;
        return b >= 1 ? 1000L * b : -1;
    case EP_ISO:
        if (b < 1 || b > 16) return -1;
        return (sp == USB_HS ? 125L : 1000L) << (b - 1);
    default:
        return 0;
    }
}

/* ================= Part A+B: 경계 검사 반복자와 configuration 파서 ================= */

/* buf[off..total) 의 디스크립터 하나. 호출자는 off < total 을 보장한다.
 * 남은 < 2 → SHORT, bLength < 2 → BLEN_ZERO, bLength > 남은 → BLEN_OVERRUN. 성공하면 *blen, *btype. */
static int desc_next(const uint8_t *buf, size_t total, size_t off, uint8_t *blen, uint8_t *btype) {
    size_t rem = total - off;
    if (rem < 2) return USB_E_SHORT;
    uint8_t b = buf[off];
    if (b < 2) return USB_E_BLEN_ZERO;
    if (b > rem) return USB_E_BLEN_OVERRUN;
    *blen = b;
    *btype = buf[off + 1];
    return USB_OK;
}

static unsigned popcount32(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }

/* configuration 디스크립터 묶음 전체 (GET_DESCRIPTOR(CONFIG, wTotalLength) 의 응답).
 * n = 실제 받은 바이트. wTotalLength 까지만 본다 (뒤 꼬리는 무시). 반환 USB_OK 또는 첫 오류. */
static int usb_parse_config(const uint8_t *buf, size_t n, usb_cfg *c) {
    memset(c, 0, sizeof *c);
    if (buf == NULL || n < 2) return USB_E_SHORT;
    if (buf[0] < 2) return USB_E_BLEN_ZERO;
    if (buf[1] != DT_CONFIG) return USB_E_TYPE;
    if (buf[0] < 9) return USB_E_BLEN_SMALL;
    if (n < 9) return USB_E_SHORT;
    uint16_t total = le16(buf + 2);
    if (total < 9 || total < buf[0]) return USB_E_TOTAL_SMALL;
    if (total > n) return USB_E_TOTAL_GT_BUF;
    c->total = total;  c->num_if = buf[4];  c->cfg_value = buf[5];  c->icfg = buf[6];
    c->attr = buf[7];  c->max_power = buf[8];

    uint32_t present[8] = { 0 };          /* bInterfaceNumber 0..255 비트맵 */
    int cur = -1;                         /* 현재 interface 디스크립터 인덱스 */
    size_t off = buf[0];
    while (off < total) {                 /* 매번 off 가 2 이상 늘어난다 → 최대 total/2 회 */
        uint8_t bl, bt;
        int e = desc_next(buf, total, off, &bl, &bt);
        if (e != USB_OK) return e;
        const uint8_t *d = buf + off;
        switch (bt) {
        case DT_INTERFACE: {
            if (bl < 9) return USB_E_BLEN_SMALL;
            if (cur >= 0 && c->ifs[cur].nep_found != c->ifs[cur].nep_decl) return USB_E_NUM_EP;
            if (c->n_if >= USB_MAX_IF) return USB_E_CAP;
            cur = c->n_if++;
            usb_if *f = &c->ifs[cur];
            f->num = d[2]; f->alt = d[3]; f->nep_decl = d[4]; f->cls = d[5]; f->sub = d[6]; f->proto = d[7];
            f->istr = d[8]; f->nep_found = 0; f->ep_first = c->n_ep;
            present[d[2] >> 5] |= 1u << (d[2] & 31u);
            break;
        }
        case DT_ENDPOINT: {
            if (bl < 7) return USB_E_BLEN_SMALL;
            if (cur < 0) return USB_E_EP_ORPHAN;
            if ((d[2] & 0x0F) == 0 || (d[2] & 0x70) != 0) return USB_E_EP_ADDR;
            if (c->n_ep >= USB_MAX_EP) return USB_E_CAP;
            usb_ep *ep = &c->eps[c->n_ep++];
            ep->addr = d[2]; ep->attr = d[3]; ep->wmps = le16(d + 4); ep->interval = d[6];
            ep->blen = bl; ep->iface = (uint8_t)cur;
            c->ifs[cur].nep_found++;
            break;
        }
        case DT_IAD: {
            if (bl < 8) return USB_E_BLEN_SMALL;
            if (c->n_iad >= USB_MAX_IAD) return USB_E_CAP;
            usb_iad *a = &c->iads[c->n_iad++];
            a->first = d[2]; a->count = d[3]; a->cls = d[4]; a->sub = d[5]; a->proto = d[6]; a->istr = d[7];
            break;
        }
        case DT_DEVICE:
        case DT_CONFIG:
            return USB_E_TYPE;
        default: {                        /* HID 0x21, CS_INTERFACE 0x24, CS_ENDPOINT 0x25, 벤더 ... 건너뛴다 */
            if (c->n_ext >= USB_MAX_EXTRA) return USB_E_CAP;
            usb_extra *x = &c->ext[c->n_ext++];
            x->type = bt; x->len = bl; x->subtype = bl >= 3 ? d[2] : 0; x->owner = (int8_t)cur; x->off = (uint16_t)off;
            break;
        }
        }
        off += bl;
    }
    if (cur >= 0 && c->ifs[cur].nep_found != c->ifs[cur].nep_decl) return USB_E_NUM_EP;
    unsigned distinct = 0;
    for (int i = 0; i < 8; i++) distinct += popcount32(present[i]);
    if (distinct != c->num_if) return USB_E_NUM_IF;
    c->n_if_distinct = (uint8_t)distinct;
    for (unsigned i = 0; i < c->n_iad; i++) {
        const usb_iad *a = &c->iads[i];
        if (a->count == 0) return USB_E_IAD_RANGE;
        for (unsigned k = a->first; k < (unsigned)a->first + a->count; k++)
            if (k > 255u || !(present[k >> 5] & (1u << (k & 31u)))) return USB_E_IAD_RANGE;
    }
    return USB_OK;
}

/* ================= Part A+B: string 디스크립터 (UTF-16LE → UTF-8) ================= */

/* d[0..n) 의 string 디스크립터를 out 에 UTF-8 + NUL 로. 반환: 쓴 바이트 수(NUL 제외) 또는 오류 (오류면 out = "").
 * 검사 순서: SHORT, BLEN_ZERO, TYPE, BLEN_OVERRUN, STR_ODD, 서로게이트 짝 STR_UTF16, 용량 CAP. index 0(LANGID)에는 쓰지 않는다. */
static int usb_string_to_utf8(const uint8_t *d, size_t n, char *out, size_t cap) {
    if (out == NULL || cap == 0) return USB_E_CAP;
    out[0] = '\0';
    if (d == NULL || n < 2) return USB_E_SHORT;
    uint8_t bl = d[0];
    if (bl < 2) return USB_E_BLEN_ZERO;
    if (d[1] != DT_STRING) return USB_E_TYPE;
    if (bl > n) return USB_E_BLEN_OVERRUN;
    if (bl & 1u) return USB_E_STR_ODD;
    size_t o = 0;
    for (size_t i = 2; i < bl; i += 2) {
        uint32_t u = le16(d + i);
        if (u >= 0xD800u && u <= 0xDBFFu) {                 /* 상위 서로게이트 → 하위가 바로 뒤에 있어야 */
            if (i + 4 > bl) { out[0] = '\0'; return USB_E_STR_UTF16; }
            uint32_t lo = le16(d + i + 2);
            if (lo < 0xDC00u || lo > 0xDFFFu) { out[0] = '\0'; return USB_E_STR_UTF16; }
            u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
            i += 2;
        } else if (u >= 0xDC00u && u <= 0xDFFFu) {
            out[0] = '\0'; return USB_E_STR_UTF16;
        }
        uint8_t t[4]; size_t k;
        if (u < 0x80u)        { t[0] = (uint8_t)u; k = 1; }
        else if (u < 0x800u)  { t[0] = (uint8_t)(0xC0u | (u >> 6)); t[1] = (uint8_t)(0x80u | (u & 0x3Fu)); k = 2; }
        else if (u < 0x10000u){ t[0] = (uint8_t)(0xE0u | (u >> 12)); t[1] = (uint8_t)(0x80u | ((u >> 6) & 0x3Fu));
                                t[2] = (uint8_t)(0x80u | (u & 0x3Fu)); k = 3; }
        else                  { t[0] = (uint8_t)(0xF0u | (u >> 18)); t[1] = (uint8_t)(0x80u | ((u >> 12) & 0x3Fu));
                                t[2] = (uint8_t)(0x80u | ((u >> 6) & 0x3Fu)); t[3] = (uint8_t)(0x80u | (u & 0x3Fu)); k = 4; }
        if (o + k + 1 > cap) { out[0] = '\0'; return USB_E_CAP; }
        memcpy(out + o, t, k);
        o += k;
    }
    out[o] = '\0';
    return (int)o;
}

/* string index 0 = LANGID 목록. 반환: 개수 또는 오류 */
static int usb_langids(const uint8_t *d, size_t n, uint16_t *ids, int cap) {
    if (d == NULL || n < 2 || d[0] < 2) return USB_E_SHORT;
    if (d[1] != DT_STRING) return USB_E_TYPE;
    if (d[0] > n) return USB_E_BLEN_OVERRUN;
    if (d[0] & 1u) return USB_E_STR_ODD;
    int k = 0;
    for (size_t i = 2; i + 1 < d[0]; i += 2) { if (k >= cap) return USB_E_CAP; ids[k++] = le16(d + i); }
    return k;
}

/* ================= 사람이 읽는 덤프 (주어짐 — 파싱이 성공한 버퍼만 받는다) ================= */

typedef struct { char *buf; size_t cap, len; bool overflow; } sbuf;
static void sb_init(sbuf *s, char *buf, size_t cap) { s->buf = buf; s->cap = cap; s->len = 0; s->overflow = false; if (cap) buf[0] = '\0'; }
static void sb_printf(sbuf *s, const char *fmt, ...) {
    if (s->cap == 0 || s->overflow) { s->overflow = true; return; }
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= s->cap - s->len) { s->overflow = true; s->len = s->cap - 1; s->buf[s->len] = '\0'; return; }
    s->len += (size_t)k;
}

static const char *class_name(uint8_t c) {
    switch (c) {
    case 0x00: return "per-interface"; case 0x01: return "Audio"; case 0x02: return "CDC"; case 0x03: return "HID";
    case 0x08: return "Mass Storage"; case 0x09: return "Hub"; case 0x0A: return "CDC-Data"; case 0x0E: return "Video";
    case 0xEF: return "Misc(IAD)"; case 0xFF: return "Vendor"; default: return "?";
    }
}
static const char *dt_name(uint8_t t) {
    switch (t) {
    case DT_DEVICE: return "DEVICE"; case DT_CONFIG: return "CONFIGURATION"; case DT_STRING: return "STRING";
    case DT_INTERFACE: return "INTERFACE"; case DT_ENDPOINT: return "ENDPOINT"; case DT_DEVICE_QUALIFIER: return "DEVICE_QUALIFIER";
    case DT_OTHER_SPEED: return "OTHER_SPEED_CONFIG"; case DT_IAD: return "IAD"; case DT_BOS: return "BOS";
    case DT_HID: return "HID"; case DT_REPORT: return "REPORT"; case DT_CS_INTERFACE: return "CS_INTERFACE";
    case DT_CS_ENDPOINT: return "CS_ENDPOINT"; default: return "UNKNOWN";
    }
}
static const char *ep_type_name(int t) { static const char *n[] = { "Control", "Isochronous", "Bulk", "Interrupt" }; return n[t & 3]; }
static const char *speed_name(usb_speed s) { return s == USB_LS ? "LS 1.5M" : s == USB_FS ? "FS 12M" : "HS 480M"; }

static void usb_dump_device(const usb_dev *d, sbuf *s) {
    sb_printf(s, "Device: bcdUSB %x.%02x  class 0x%02X (%s) sub 0x%02X proto 0x%02X  bMaxPacketSize0 %u\n",
              d->bcd_usb >> 8, d->bcd_usb & 0xFF, d->cls, class_name(d->cls), d->sub, d->proto, d->mps0);
    sb_printf(s, "        idVendor 0x%04X idProduct 0x%04X bcdDevice %x.%02x  iMfr %u iProd %u iSerial %u  bNumConfigurations %u\n",
              d->vid, d->pid, d->bcd_dev >> 8, d->bcd_dev & 0xFF, d->imfr, d->iprod, d->iser, d->ncfg);
}

static void dump_ep(const usb_ep *e, usb_speed sp, sbuf *s) {
    int t = ep_type(e->attr);
    sb_printf(s, "    EP 0x%02X %-3s %-11s wMaxPacketSize 0x%04X = %u B x%u", e->addr, (e->addr & 0x80) ? "IN" : "OUT",
              ep_type_name(t), e->wmps, ep_mps(e->wmps), ep_mult(e->wmps));
    if (t == EP_ISO) {
        static const char *sy[] = { "no-sync", "async", "adaptive", "sync" };
        static const char *us[] = { "data", "feedback", "implicit-fb-data", "reserved" };
        sb_printf(s, " [%s, %s]", sy[ep_sync(e->attr)], us[ep_usage(e->attr)]);
    }
    long us_ = ep_interval_us(t, sp, e->interval);
    if (us_ > 0)       sb_printf(s, "  bInterval %u -> every %ld us\n", e->interval, us_);
    else if (us_ == 0) sb_printf(s, "  bInterval %u (no period)\n", e->interval);
    else               sb_printf(s, "  bInterval %u INVALID for %s\n", e->interval, speed_name(sp));
}

static void usb_dump_config(const uint8_t *raw, const usb_cfg *c, usb_speed sp, sbuf *s) {
    sb_printf(s, "Configuration %u: wTotalLength %u  bNumInterfaces %u  bmAttributes 0x%02X (%s%s)  MaxPower %u mA\n",
              c->cfg_value, c->total, c->num_if, c->attr, (c->attr & 0x40) ? "self-powered" : "bus-powered",
              (c->attr & 0x20) ? ", remote-wakeup" : "", c->max_power * 2u);
    unsigned ii = 0, ie = 0, ia = 0, ix = 0;
    for (size_t off = raw[0]; off < c->total; off += raw[off]) {   /* 이미 검증된 버퍼만 */
        uint8_t t = raw[off + 1];
        if (t == DT_INTERFACE && ii < c->n_if) {
            const usb_if *f = &c->ifs[ii++];
            sb_printf(s, "  Interface %u alt %u: %u EP  class 0x%02X (%s) sub 0x%02X proto 0x%02X  iInterface %u\n",
                      f->num, f->alt, f->nep_decl, f->cls, class_name(f->cls), f->sub, f->proto, f->istr);
        } else if (t == DT_ENDPOINT && ie < c->n_ep) {
            dump_ep(&c->eps[ie++], sp, s);
        } else if (t == DT_IAD && ia < c->n_iad) {
            const usb_iad *a = &c->iads[ia++];
            sb_printf(s, "  IAD: interfaces %u..%u  function class 0x%02X (%s) sub 0x%02X proto 0x%02X\n",
                      a->first, (unsigned)(a->first + a->count - 1), a->cls, class_name(a->cls), a->sub, a->proto);
        } else if (ix < c->n_ext) {
            const usb_extra *x = &c->ext[ix++];
            if (x->type == DT_CS_INTERFACE || x->type == DT_CS_ENDPOINT)
                sb_printf(s, "    [%s 0x%02X len %u subtype 0x%02X] skipped by bLength\n", dt_name(x->type), x->type, x->len, x->subtype);
            else if (x->type == DT_HID && x->len >= 9)
                sb_printf(s, "    [HID 0x21 len %u: bcdHID %x.%02x, class desc 0x%02X len %u] skipped by bLength\n", x->len,
                          raw[off + 3], raw[off + 2], raw[off + 6], le16(raw + off + 7));
            else
                sb_printf(s, "    [%s 0x%02X len %u] skipped by bLength\n", dt_name(x->type), x->type, x->len);
        }
    }
}

/* ================= Part C: SETUP 패킷 디코더 ================= */

static const char *std_req_name(uint8_t r) {
    switch (r) {
    case REQ_GET_STATUS: return "GET_STATUS"; case REQ_CLEAR_FEATURE: return "CLEAR_FEATURE"; case REQ_SET_FEATURE: return "SET_FEATURE";
    case REQ_SET_ADDRESS: return "SET_ADDRESS"; case REQ_GET_DESCRIPTOR: return "GET_DESCRIPTOR"; case REQ_SET_DESCRIPTOR: return "SET_DESCRIPTOR";
    case REQ_GET_CONFIGURATION: return "GET_CONFIGURATION"; case REQ_SET_CONFIGURATION: return "SET_CONFIGURATION";
    case REQ_GET_INTERFACE: return "GET_INTERFACE"; case REQ_SET_INTERFACE: return "SET_INTERFACE"; case REQ_SYNCH_FRAME: return "SYNCH_FRAME";
    default: return NULL;
    }
}

/* 8바이트 SETUP → 한 줄. 형식 (테스트가 정확히 비교):
 *   "80 06 00 01 00 00 40 00 | IN  STD DEV | GET_DESCRIPTOR(DEVICE, 0) wIndex=0x0000 wLength=64"
 *   방향 IN/OUT(bit7), 타입 STD/CLS/VND/RSV(bits6:5), 수신자 DEV/IF/EP/OTH/R04..(bits4:0)
 *   GET_DESCRIPTOR(<dt_name>, <index>), SET_ADDRESS(<n>), SET_CONFIGURATION(<n>), 다른 표준 요청은 "<NAME> wValue=0x%04X",
 *   표준이 아니면 "bRequest=0x%02X wValue=0x%04X". 반환: 길이 또는 USB_E_CAP(잘림). */
static int setup_decode(const uint8_t s[8], char *out, size_t cap) {
    static const char *ty[] = { "STD", "CLS", "VND", "RSV" };
    static const char *rc[] = { "DEV", "IF", "EP", "OTH" };
    uint8_t rt = s[0], rq = s[1];
    uint16_t wv = le16(s + 2), wi = le16(s + 4), wl = le16(s + 6);
    char rcp[8], req[48];
    if ((rt & 0x1F) < 4) snprintf(rcp, sizeof rcp, "%s", rc[rt & 0x1F]);
    else                 snprintf(rcp, sizeof rcp, "R%02u", (unsigned)(rt & 0x1F));
    const char *nm = ((rt >> 5) & 3) == 0 ? std_req_name(rq) : NULL;
    if (nm && rq == REQ_GET_DESCRIPTOR)          snprintf(req, sizeof req, "GET_DESCRIPTOR(%s, %u)", dt_name((uint8_t)(wv >> 8)), (unsigned)(wv & 0xFF));
    else if (nm && (rq == REQ_SET_ADDRESS || rq == REQ_SET_CONFIGURATION)) snprintf(req, sizeof req, "%s(%u)", nm, (unsigned)wv);
    else if (nm)                                 snprintf(req, sizeof req, "%s wValue=0x%04X", nm, wv);
    else                                         snprintf(req, sizeof req, "bRequest=0x%02X wValue=0x%04X", rq, wv);
    int k = snprintf(out, cap, "%02X %02X %02X %02X %02X %02X %02X %02X | %-3s %s %s | %s wIndex=0x%04X wLength=%u",
                     s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], (rt & 0x80) ? "IN" : "OUT", ty[(rt >> 5) & 3], rcp,
                     req, wi, wl);
    if (k < 0 || (size_t)k >= cap) return USB_E_CAP;
    return k;
}

/* ================= Part C: EP0 데이터 단계 패킷 분할 ================= */

/* 장치가 보낼 수 있는 바이트 avail, 호스트 요청 wLength, EP0 최대 패킷 mps0 → 패킷 크기들 (0 = ZLP).
 * 규칙: 실제 = min(avail, wLength). mps0 크기로 자르고, 실제 < wLength 인데 마지막 패킷이 꽉 찼으면(또는 실제 0) ZLP 추가
 *       — 짧은 패킷이 "끝"이라는 신호이기 때문. 반환: 패킷 수 또는 USB_E_CAP. */
static int data_stage_packets(size_t avail, uint16_t wlength, uint8_t mps0, uint16_t *sizes, int cap) {
    size_t actual = avail < wlength ? avail : wlength;
    int np = 0;
    size_t left = actual;
    while (left > 0) {
        if (np >= cap) return USB_E_CAP;
        uint16_t k = (uint16_t)(left < mps0 ? left : mps0);
        sizes[np++] = k;
        left -= k;
    }
    if (actual < wlength && (actual == 0 || actual % mps0 == 0)) {
        if (np >= cap) return USB_E_CAP;
        sizes[np++] = 0;
    }
    return np;
}

/* ================= Part C: 디스크립터 배열로 답하는 mock 장치 ================= */

#define MOCK_STALL  (-100)
#define MOCK_NORESP (-101)
typedef struct {
    const uint8_t *dev;                  /* 18바이트 */
    const uint8_t *cfg; size_t cfg_avail;/* configuration 묶음과 실제로 보낼 수 있는 바이트 (결함: wTotalLength 보다 적게) */
    const uint8_t *const *str; size_t n_str;   /* str[0] = LANGID 디스크립터 */
    uint8_t addr, pending_addr, cfg_value;
    bool    addr_pending;
    bool    bug_addr_early;              /* 결함 주입: SET_ADDRESS 를 status 단계 전에 적용 */
    uint32_t n_stall;
} mock_dev;

static void mock_reset(mock_dev *m) { m->addr = 0; m->addr_pending = false; m->cfg_value = 0; }

/* SETUP 하나 처리. 반환: IN 데이터 바이트 수(in 에 씀, 0 = 데이터 없음), MOCK_STALL, MOCK_NORESP.
 * 지원: GET_DESCRIPTOR(DEVICE 0 / CONFIG 0 / STRING i — i>0 이면 wIndex 0x0409 만), SET_ADDRESS(1..127, status 뒤 적용),
 *       SET_CONFIGURATION(0 또는 bConfigurationValue, Address 상태 이후). 나머지·범위 밖은 STALL. 응답 = min(길이, wLength, cap). */
static int mock_setup(mock_dev *m, uint8_t addr, const uint8_t s[8], uint8_t *in, size_t cap) {
    if (addr != m->addr) return MOCK_NORESP;      /* 다른 주소로 온 토큰은 무시 */
    uint8_t rt = s[0], rq = s[1];
    uint16_t wv = le16(s + 2), wi = le16(s + 4), wl = le16(s + 6);
    if (rt == 0x80 && rq == REQ_GET_DESCRIPTOR) {
        uint8_t type = (uint8_t)(wv >> 8), idx = (uint8_t)wv;
        const uint8_t *src = NULL; size_t len = 0;
        if (type == DT_DEVICE && idx == 0)       { src = m->dev; len = 18; }
        else if (type == DT_CONFIG && idx == 0)  { src = m->cfg; len = m->cfg_avail; }
        else if (type == DT_STRING && idx < m->n_str && m->str[idx] != NULL && (idx == 0 || wi == 0x0409)) {
            src = m->str[idx]; len = src[0];
        }
        if (src == NULL) { m->n_stall++; return MOCK_STALL; }
        size_t k = len < wl ? len : wl;
        if (k > cap) k = cap;
        if (k) memcpy(in, src, k);
        return (int)k;
    }
    if (rt == 0x00 && rq == REQ_SET_ADDRESS) {
        if (wv == 0 || wv > 127 || wi != 0 || wl != 0) { m->n_stall++; return MOCK_STALL; }
        if (m->bug_addr_early) m->addr = (uint8_t)wv;               /* 버그: 너무 일찍 바꿈 */
        else { m->pending_addr = (uint8_t)wv; m->addr_pending = true; }
        return 0;
    }
    if (rt == 0x00 && rq == REQ_SET_CONFIGURATION) {
        if (m->addr == 0 || wl != 0 || (wv != 0 && wv != m->cfg[5])) { m->n_stall++; return MOCK_STALL; }
        m->cfg_value = (uint8_t)wv;
        return 0;
    }
    m->n_stall++;
    return MOCK_STALL;
}

/* status 단계 (주어짐): 이 시점에야 SET_ADDRESS 가 효력을 낸다 (USB 2.0 §9.4.6) */
static int mock_status(mock_dev *m, uint8_t addr) {
    if (addr != m->addr) return MOCK_NORESP;
    if (m->addr_pending) { m->addr = m->pending_addr; m->addr_pending = false; }
    return 0;
}

/* ================= Part C: 호스트 쪽 enumeration ================= */

#define ENUM_LOG_LINES 40
#define ENUM_LINE      128
#define ENUM_MAX_SETUP 16
typedef struct {
    mock_dev *dev;
    char     log[ENUM_LOG_LINES][ENUM_LINE]; int n_log; bool log_overflow;
    uint8_t  setups[ENUM_MAX_SETUP][8];      int n_setup;
    uint8_t  buf[256];                       /* device/string 응답 */
    uint8_t  cfgbuf[512];                    /* configuration 묶음 — cfg 의 offset 이 여기를 가리키므로 따로 둔다 */
    uint8_t  mps0;
    usb_dev  d;
    usb_cfg  cfg;
    uint16_t langid;
    char     mfr[64], prod[64], ser[64];
    int      str_missing;
} host_ctx;

static void host_log(host_ctx *h, const char *fmt, ...) {
    if (h->n_log >= ENUM_LOG_LINES) { h->log_overflow = true; return; }
    va_list ap; va_start(ap, fmt);
    vsnprintf(h->log[h->n_log++], ENUM_LINE, fmt, ap);
    va_end(ap);
}

static void mk_setup(uint8_t s[8], uint8_t rt, uint8_t rq, uint16_t wv, uint16_t wi, uint16_t wl) {
    s[0] = rt; s[1] = rq; s[2] = (uint8_t)wv; s[3] = (uint8_t)(wv >> 8);
    s[4] = (uint8_t)wi; s[5] = (uint8_t)(wi >> 8); s[6] = (uint8_t)wl; s[7] = (uint8_t)(wl >> 8);
}

/* control 전송 하나 (주어짐): SETUP → (DATA IN 패킷들) → STATUS. first_pkt_only = 첫 패킷만 받고 데이터 단계를 끝냄.
 * 반환: 받은 바이트 수 또는 USB_E_STALL / USB_E_NORESP / USB_E_CAP. */
static int host_ctrl(host_ctx *h, uint8_t addr, const uint8_t s[8], uint8_t *in, size_t cap, bool first_pkt_only) {
    char line[ENUM_LINE - 16];
    if (h->n_setup < ENUM_MAX_SETUP) memcpy(h->setups[h->n_setup++], s, 8);
    setup_decode(s, line, sizeof line);
    host_log(h, "@%u SETUP %s", addr, line);
    int r = mock_setup(h->dev, addr, s, in, cap);
    if (r == MOCK_NORESP) { host_log(h, "     no response (timeout)"); return USB_E_NORESP; }
    if (r == MOCK_STALL)  { host_log(h, "     STALL"); return USB_E_STALL; }
    if (le16(s + 6) != 0) {
        uint16_t pk[80];
        int np = data_stage_packets((size_t)r, le16(s + 6), h->dev->dev[7], pk, 80);
        if (np < 0) return np;
        char ps[64]; sbuf sb; sb_init(&sb, ps, sizeof ps);
        int show = first_pkt_only ? 1 : np;
        for (int i = 0; i < show && i < 8; i++) sb_printf(&sb, i ? "+%u" : "%u", pk[i]);
        if (show > 8) sb_printf(&sb, "+...");
        if (first_pkt_only) {
            r = np > 0 ? pk[0] : 0;
            host_log(h, "     DATA IN %d B (first packet only: %s), host ends data stage", r, ps);
        } else {
            host_log(h, "     DATA IN %d B in %d pkt (%s)%s", r, np, ps, (np > 0 && pk[np - 1] == 0) ? " ZLP=end" : "");
        }
    }
    if (mock_status(h->dev, addr) == MOCK_NORESP) { host_log(h, "     STATUS: no response (timeout)"); return USB_E_NORESP; }
    host_log(h, "     STATUS %s ZLP ok", le16(s + 6) ? "OUT" : "IN");
    return r;
}

/* 전체 enumeration. 성공하면 h->d, h->cfg, 문자열이 채워지고 장치는 Configured. 반환 USB_OK 또는 첫 치명 오류.
 * 문자열 STALL 은 치명적이지 않다 (str_missing++). */
static int host_enumerate(host_ctx *h, uint8_t new_addr) {
    uint8_t s[8];
    int r;
    host_log(h, "RESET (device -> Default state, address 0)");
    mock_reset(h->dev);
    mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 64);
    r = host_ctrl(h, 0, s, h->buf, sizeof h->buf, true);
    if (r < 0) return r;
    if (r < 8) return USB_E_SHORT;
    h->mps0 = h->buf[7];                                     /* 이것 하나 알려고 읽었다 */
    host_log(h, "RESET again (bMaxPacketSize0 = %u learned)", h->mps0);
    mock_reset(h->dev);
    mk_setup(s, 0x00, REQ_SET_ADDRESS, new_addr, 0, 0);
    r = host_ctrl(h, 0, s, NULL, 0, false);
    if (r < 0) return r;
    mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18);
    r = host_ctrl(h, new_addr, s, h->buf, sizeof h->buf, false);
    if (r < 0) return r;
    if ((r = usb_parse_device(h->buf, (size_t)r, &h->d)) != USB_OK) return r;
    mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_CONFIG << 8, 0, 9);
    r = host_ctrl(h, new_addr, s, h->cfgbuf, sizeof h->cfgbuf, false);
    if (r < 0) return r;
    if (r < 9) return USB_E_SHORT;
    if (h->cfgbuf[1] != DT_CONFIG) return USB_E_TYPE;
    uint16_t total = le16(h->cfgbuf + 2);
    if (total < 9) return USB_E_TOTAL_SMALL;
    if (total > sizeof h->cfgbuf) return USB_E_CAP;
    mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_CONFIG << 8, 0, total);
    r = host_ctrl(h, new_addr, s, h->cfgbuf, sizeof h->cfgbuf, false);
    if (r < 0) return r;
    if ((r = usb_parse_config(h->cfgbuf, (size_t)r, &h->cfg)) != USB_OK) return r;

    h->langid = 0;
    mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_STRING << 8, 0, 255);
    r = host_ctrl(h, new_addr, s, h->buf, sizeof h->buf, false);
    if (r == USB_E_NORESP) return r;
    if (r >= 4) {
        uint16_t ids[8];
        int k = usb_langids(h->buf, (size_t)r, ids, 8);
        for (int i = 0; i < k; i++) if (ids[i] == 0x0409) h->langid = 0x0409;
        if (h->langid == 0 && k > 0) h->langid = ids[0];
    }
    uint8_t idx[3] = { h->d.imfr, h->d.iprod, h->d.iser };
    char *dst[3] = { h->mfr, h->prod, h->ser };
    for (int i = 0; i < 3; i++) {
        dst[i][0] = '\0';
        if (idx[i] == 0) continue;
        if (h->langid == 0) { h->str_missing++; continue; }
        mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, (uint16_t)(DT_STRING << 8 | idx[i]), h->langid, 255);
        r = host_ctrl(h, new_addr, s, h->buf, sizeof h->buf, false);
        if (r == USB_E_NORESP) return r;
        if (r < 0 || usb_string_to_utf8(h->buf, (size_t)r, dst[i], 64) < 0) h->str_missing++;
    }
    mk_setup(s, 0x00, REQ_SET_CONFIGURATION, h->cfg.cfg_value, 0, 0);
    r = host_ctrl(h, new_addr, s, NULL, 0, false);
    if (r < 0) return r;
    host_log(h, "CONFIGURED: addr %u, config %u, %u interface(s), %u endpoint(s)", new_addr, h->cfg.cfg_value,
             h->cfg.num_if, h->cfg.n_ep);
    return USB_OK;
}

/* ======================= 예시 디스크립터 ======================= */

/* (1) CDC-ACM + IAD, FS — constructed. VID 0x1209 = pid.codes(오픈소스 공유 VID), PID 0x0001 은 테스트용 (확인 권장) */
static const uint8_t CDC_DEV[18] = {
    0x12, 0x01, 0x00, 0x02,  0xEF, 0x02, 0x01,  0x40,  0x09, 0x12,  0x01, 0x00,  0x00, 0x01,  1, 2, 3,  1 };
static const uint8_t CDC_CFG[75] = {
    0x09, 0x02, 0x4B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,     /* config: 75 B, 2 IF, value 1, bus-powered, 100 mA */
    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,           /* IAD: IF 0..1, CDC / ACM / AT */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,     /* IF0 alt0: 1 EP, CDC comm / ACM / AT V.250 */
    0x05, 0x24, 0x00, 0x10, 0x01,                             /*   CS Header, bcdCDC 1.10 */
    0x05, 0x24, 0x01, 0x00, 0x01,                             /*   CS Call Management, data IF 1 */
    0x04, 0x24, 0x02, 0x02,                                   /*   CS ACM, capabilities 0x02 */
    0x05, 0x24, 0x06, 0x00, 0x01,                             /*   CS Union, control 0 / subordinate 1 */
    0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x10,                 /*   EP 0x82 IN interrupt 8 B, 16 ms (notification) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,     /* IF1 alt0: 2 EP, CDC-Data */
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,                 /*   EP 0x01 OUT bulk 64 B */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00 };               /*   EP 0x81 IN  bulk 64 B */

/* (2) HID 마우스, LS — device 18바이트는 이 Mac 의 Logitech USB Optical Mouse ioreg 필드에서 재구성
 *     (bcdUSB 512=0x0200, bMaxPacketSize0 8, idVendor 1133=0x046D, idProduct 49271=0xC077, bcdDevice 29184=0x7200,
 *      iManufacturer 1, iProduct 2, iSerialNumber 0, bNumConfigurations 1, class 0/0/0).
 *     interface 0 (class 3 / sub 1 boot / proto 2 mouse, 1 EP, iInterface 0), bConfigurationValue 1, report 길이 46 도 ioreg.
 *     config bmAttributes/MaxPower, bcdHID, EP wMaxPacketSize 4 / bInterval 10 은 constructed (ioreg 에 없음). */
static const uint8_t MOUSE_DEV[18] = {
    0x12, 0x01, 0x00, 0x02,  0x00, 0x00, 0x00,  0x08,  0x6D, 0x04,  0x77, 0xC0,  0x00, 0x72,  1, 2, 0,  1 };
static const uint8_t MOUSE_CFG[34] = {
    0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,     /* config: 34 B, 1 IF, bus-powered + remote wakeup, 100 mA */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,     /* IF0: 1 EP, HID / boot / mouse */
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x2E, 0x00,     /*   HID 1.11, 1 report descriptor, 46 B */
    0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x0A };               /*   EP 0x81 IN interrupt 4 B, 10 ms */

/* (3) UAC2 비슷한 HS 오디오 출력 — constructed. 클래스 디스크립터는 길이·타입만 의미 (실제 UAC2 는 Clock Source /
 *     Terminal 디스크립터가 더 있다 — 이 레슨 파서는 어차피 bLength 로 건너뛴다). */
static const uint8_t UAC_DEV[18] = {
    0x12, 0x01, 0x00, 0x02,  0xEF, 0x02, 0x01,  0x40,  0x09, 0x12,  0x02, 0x00,  0x00, 0x01,  1, 2, 0,  1 };
static const uint8_t UAC_CFG[97] = {
    0x09, 0x02, 0x61, 0x00, 0x02, 0x01, 0x00, 0x80, 0xFA,     /* config: 97 B, 2 IF, 500 mA */
    0x08, 0x0B, 0x00, 0x02, 0x01, 0x00, 0x20, 0x00,           /* IAD: IF 0..1, Audio, protocol 0x20 (UAC2) */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x20, 0x00,     /* IF0: AudioControl, 0 EP */
    0x09, 0x24, 0x01, 0x00, 0x02, 0x08, 0x09, 0x00, 0x00,     /*   CS AC Header (축약) */
    0x09, 0x04, 0x01, 0x00, 0x00, 0x01, 0x02, 0x20, 0x00,     /* IF1 alt0: AudioStreaming, 0 EP (대역폭 0) */
    0x09, 0x04, 0x01, 0x01, 0x02, 0x01, 0x02, 0x20, 0x00,     /* IF1 alt1: 2 EP */
    0x10, 0x24, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, /* CS AS General */
    0x06, 0x24, 0x02, 0x01, 0x04, 0x18,                       /*   CS Format Type I: 4 B subslot, 24 bit */
    0x07, 0x05, 0x01, 0x05, 0x38, 0x00, 0x01,                 /*   EP 0x01 OUT iso async data 56 B, every microframe */
    0x08, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,           /*   CS AS iso data endpoint */
    0x07, 0x05, 0x81, 0x11, 0x04, 0x00, 0x04 };               /*   EP 0x81 IN iso feedback 4 B, 2^3 x 125 us = 1 ms */

/* 문자열: index 0 = LANGID 0x0409 (English US) */
static const uint8_t STR_LANG[4] = { 0x04, 0x03, 0x09, 0x04 };
static const uint8_t STR_KO[12] = { 0x0C, 0x03, 0x5C, 0xD5, 0x00, 0xAE, 0x20, 0x00, 0x3D, 0xD8, 0x0C, 0xDD }; /* "한글 🔌" */

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ASCII → string 디스크립터 (테스트·mock 용) */
static size_t mk_str(uint8_t *out, size_t cap, const char *a) {
    size_t n = strlen(a), bl = 2 + 2 * n;
    if (bl > 254 || bl > cap) return 0;
    out[0] = (uint8_t)bl; out[1] = DT_STRING;
    for (size_t i = 0; i < n; i++) { out[2 + 2 * i] = (uint8_t)a[i]; out[3 + 2 * i] = 0; }
    return bl;
}

static uint8_t g_s_logi[64], g_s_mouse[64], g_s_don[64], g_s_cdc[64], g_s_ser[64];
static const uint8_t *g_mouse_str[3], *g_cdc_str[4];

static void init_strings(void) {
    mk_str(g_s_logi, sizeof g_s_logi, "Logitech");
    mk_str(g_s_mouse, sizeof g_s_mouse, "USB Optical Mouse");
    mk_str(g_s_don, sizeof g_s_don, "Don Lab");
    mk_str(g_s_cdc, sizeof g_s_cdc, "COMM-M15 CDC Demo");
    mk_str(g_s_ser, sizeof g_s_ser, "0001");
    g_mouse_str[0] = STR_LANG; g_mouse_str[1] = g_s_logi; g_mouse_str[2] = g_s_mouse;
    g_cdc_str[0] = STR_LANG; g_cdc_str[1] = g_s_don; g_cdc_str[2] = g_s_cdc; g_cdc_str[3] = g_s_ser;
}

/* 정확한 크기의 힙 사본으로 파싱 → ASan 이 n 바이트를 1바이트라도 넘는 읽기를 잡는다 */
static int parse_cfg_exact(const uint8_t *src, size_t n, usb_cfg *c) {
    uint8_t *p = malloc(n ? n : 1);
    if (!p) return USB_E_CAP;
    if (n) memcpy(p, src, n);
    int r = usb_parse_config(p, n, c);
    free(p);
    return r;
}

static bool cfg_invariants(const usb_cfg *c, size_t n) {
    if (c->total < 9 || c->total > n) return false;
    unsigned sum = 0;
    for (unsigned i = 0; i < c->n_if; i++) {
        if (c->ifs[i].nep_found != c->ifs[i].nep_decl) return false;
        if (c->ifs[i].ep_first + c->ifs[i].nep_found > c->n_ep) return false;
        sum += c->ifs[i].nep_found;
    }
    if (sum != c->n_ep || c->n_if_distinct != c->num_if) return false;
    for (unsigned i = 0; i < c->n_ext; i++) if (c->ext[i].off + c->ext[i].len > c->total) return false;
    return true;
}

static host_ctx g_h;
static mock_dev g_m;
static void host_setup(const uint8_t *dev, const uint8_t *cfg, size_t cfg_avail, const uint8_t *const *str, size_t n_str) {
    memset(&g_h, 0, sizeof g_h); memset(&g_m, 0, sizeof g_m);
    g_m.dev = dev; g_m.cfg = cfg; g_m.cfg_avail = cfg_avail; g_m.str = str; g_m.n_str = n_str;
    g_h.dev = &g_m;
}
static bool setup_is(int i, const uint8_t exp[8]) { return i < g_h.n_setup && memcmp(g_h.setups[i], exp, 8) == 0; }
static bool log_has(const char *needle) {
    for (int i = 0; i < g_h.n_log; i++) if (strstr(g_h.log[i], needle)) return true;
    return false;
}
static void print_log(void) { for (int i = 0; i < g_h.n_log; i++) printf("%s\n", g_h.log[i]); }

static char g_dump[8192];
static void dump_one(const char *title, const uint8_t *dev, const uint8_t *cfg, size_t n, usb_speed sp) {
    usb_dev d; usb_cfg c; sbuf sb;
    sb_init(&sb, g_dump, sizeof g_dump);
    printf("==== %s (%s) ====\n", title, speed_name(sp));
    if (usb_parse_device(dev, 18, &d) == USB_OK) usb_dump_device(&d, &sb);
    int r = usb_parse_config(cfg, n, &c);
    if (r == USB_OK) usb_dump_config(cfg, &c, sp, &sb);
    else sb_printf(&sb, "config parse error %s\n", usb_err_name(r));
    fputs(g_dump, stdout);
}

int main(int argc, char **argv) {
    init_strings();
    if (argc > 1 && strcmp(argv[1], "dump") == 0) {
        dump_one("CDC-ACM + IAD (constructed)", CDC_DEV, CDC_CFG, sizeof CDC_CFG, USB_FS);
        dump_one("HID mouse (device desc from this Mac's ioreg)", MOUSE_DEV, MOUSE_CFG, sizeof MOUSE_CFG, USB_LS);
        dump_one("UAC2-like iso (constructed)", UAC_DEV, UAC_CFG, sizeof UAC_CFG, USB_HS);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "enum") == 0) {
        host_setup(MOUSE_DEV, MOUSE_CFG, sizeof MOUSE_CFG, g_mouse_str, 3);
        int r = host_enumerate(&g_h, 5);
        print_log();
        printf("result %s  \"%s\" / \"%s\"\n", usb_err_name(r), g_h.mfr, g_h.prod);
        return 0;
    }

    /* ---------------- Part A ---------------- */
    usb_dev d;
    usb_cfg c;
    CHECK(usb_parse_device(MOUSE_DEV, 18, &d) == USB_OK);
    CHECK(d.vid == 0x046D && d.pid == 0xC077 && d.bcd_usb == 0x0200 && d.bcd_dev == 0x7200);
    CHECK(d.mps0 == 8 && d.cls == 0 && d.imfr == 1 && d.iprod == 2 && d.iser == 0 && d.ncfg == 1);
    CHECK(usb_parse_device(CDC_DEV, 18, &d) == USB_OK && d.cls == 0xEF && d.sub == 0x02 && d.proto == 0x01 && d.mps0 == 64);

    CHECK(usb_parse_config(CDC_CFG, sizeof CDC_CFG, &c) == USB_OK);
    CHECK(c.total == 75 && c.num_if == 2 && c.n_if == 2 && c.n_ep == 3 && c.n_iad == 1 && c.n_ext == 4);
    CHECK(c.iads[0].first == 0 && c.iads[0].count == 2 && c.iads[0].cls == 0x02);
    CHECK(c.ext[0].type == DT_CS_INTERFACE && c.ext[0].subtype == 0x00 && c.ext[3].subtype == 0x06 && c.ext[3].owner == 0);
    CHECK(c.eps[0].addr == 0x82 && ep_type(c.eps[0].attr) == EP_INTERRUPT && c.eps[0].iface == 0);
    CHECK(c.eps[1].addr == 0x01 && ep_type(c.eps[1].attr) == EP_BULK && ep_mps(c.eps[1].wmps) == 64 && c.eps[2].addr == 0x81);
    CHECK(c.ifs[1].cls == 0x0A && c.ifs[1].ep_first == 1 && c.ifs[1].nep_found == 2);

    CHECK(usb_parse_config(MOUSE_CFG, sizeof MOUSE_CFG, &c) == USB_OK);
    CHECK(c.total == 34 && c.n_ext == 1 && c.ext[0].type == DT_HID && c.ext[0].len == 9 && le16(MOUSE_CFG + c.ext[0].off + 7) == 46);
    CHECK(c.n_ep == 1 && c.eps[0].addr == 0x81 && ep_mps(c.eps[0].wmps) == 4 && ep_interval_us(EP_INTERRUPT, USB_LS, c.eps[0].interval) == 10000);

    CHECK(usb_parse_config(UAC_CFG, sizeof UAC_CFG, &c) == USB_OK);
    CHECK(c.total == 97 && c.num_if == 2 && c.n_if == 3 && c.n_if_distinct == 2 && c.n_ep == 2 && c.n_ext == 4);
    CHECK(c.ifs[1].num == 1 && c.ifs[1].alt == 0 && c.ifs[1].nep_found == 0 && c.ifs[2].alt == 1 && c.ifs[2].nep_found == 2);
    CHECK(ep_type(c.eps[0].attr) == EP_ISO && ep_sync(c.eps[0].attr) == 1 && ep_usage(c.eps[0].attr) == 0 && ep_mps(c.eps[0].wmps) == 56);
    CHECK(ep_interval_us(EP_ISO, USB_HS, c.eps[0].interval) == 125 && ep_usage(c.eps[1].attr) == 1);
    CHECK(ep_interval_us(EP_ISO, USB_HS, c.eps[1].interval) == 1000 && c.ext[3].type == DT_CS_ENDPOINT);

    /* wMaxPacketSize bits 12:11 (high-bandwidth) */
    CHECK(ep_mps(0x1400) == 1024 && ep_mult(0x1400) == 3);
    CHECK(ep_mps(0x0B00) == 768 && ep_mult(0x0B00) == 2 && ep_mult(0x0040) == 1 && ep_mult(0x1800) == 0);
    /* bInterval 표 */
    CHECK(ep_interval_us(EP_INTERRUPT, USB_FS, 1) == 1000 && ep_interval_us(EP_INTERRUPT, USB_FS, 255) == 255000);
    CHECK(ep_interval_us(EP_INTERRUPT, USB_FS, 0) == -1 && ep_interval_us(EP_INTERRUPT, USB_HS, 0) == -1);
    CHECK(ep_interval_us(EP_INTERRUPT, USB_HS, 1) == 125 && ep_interval_us(EP_INTERRUPT, USB_HS, 4) == 1000);
    CHECK(ep_interval_us(EP_INTERRUPT, USB_HS, 16) == 4096000L && ep_interval_us(EP_INTERRUPT, USB_HS, 17) == -1);
    CHECK(ep_interval_us(EP_ISO, USB_FS, 1) == 1000 && ep_interval_us(EP_ISO, USB_FS, 4) == 8000 && ep_interval_us(EP_ISO, USB_FS, 17) == -1);
    CHECK(ep_interval_us(EP_BULK, USB_FS, 0) == 0 && ep_interval_us(EP_BULK, USB_HS, 255) == 0 && ep_interval_us(EP_BULK, USB_LS, 0) == -1);
    CHECK(ep_interval_us(EP_ISO, USB_LS, 1) == -1 && ep_interval_us(EP_CONTROL, USB_LS, 0) == 0);
    {   /* 전수: 모든 타입 × 속도 × bInterval 0..255 → 결과가 -1, 0, 또는 125 의 배수이고, HS 는 2의 거듭제곱 × 125 */
        bool ok = true;
        for (int t = 0; t < 4; t++) for (int sp = 0; sp < 3; sp++) for (int b = 0; b < 256; b++) {
            long v = ep_interval_us(t, (usb_speed)sp, (uint8_t)b);
            if (v < -1 || (v > 0 && v % 125 != 0)) ok = false;
            if (v > 0 && sp == USB_HS) { long q = v / 125; if (q & (q - 1)) ok = false; }
        }
        CHECK(ok);
    }

    /* 문자열 */
    char s8[128];
    CHECK(usb_string_to_utf8(g_s_logi, g_s_logi[0], s8, sizeof s8) == 8 && strcmp(s8, "Logitech") == 0);
    CHECK(usb_string_to_utf8(g_s_mouse, g_s_mouse[0], s8, sizeof s8) == 17 && strcmp(s8, "USB Optical Mouse") == 0);
    CHECK(usb_string_to_utf8(STR_KO, sizeof STR_KO, s8, sizeof s8) == 11 &&
          memcmp(s8, "\xED\x95\x9C\xEA\xB8\x80\x20\xF0\x9F\x94\x8C", 12) == 0);   /* "한글 🔌" + NUL */
    { uint16_t ids[4]; CHECK(usb_langids(STR_LANG, 4, ids, 4) == 1 && ids[0] == 0x0409); }
    { const uint8_t empty[2] = { 0x02, 0x03 }; CHECK(usb_string_to_utf8(empty, 2, s8, sizeof s8) == 0 && s8[0] == '\0'); }

    /* 덤프 */
    {
        sbuf sb; sb_init(&sb, g_dump, sizeof g_dump);
        CHECK(usb_parse_config(CDC_CFG, sizeof CDC_CFG, &c) == USB_OK);
        usb_dump_config(CDC_CFG, &c, USB_FS, &sb);
        CHECK(!sb.overflow && strstr(g_dump, "IAD: interfaces 0..1") && strstr(g_dump, "EP 0x82 IN  Interrupt") &&
              strstr(g_dump, "bInterval 16 -> every 16000 us") && strstr(g_dump, "[CS_INTERFACE 0x24 len 4 subtype 0x02]"));
        char tiny[40]; sb_init(&sb, tiny, sizeof tiny);
        usb_dump_config(CDC_CFG, &c, USB_FS, &sb);
        CHECK(sb.overflow && strlen(tiny) == sizeof tiny - 1);
    }

    /* ---------------- Part B: 결함 주입 ---------------- */
    uint8_t m[128];
#define MUT(src) (memcpy(m, (src), sizeof(src)), sizeof(src))
    size_t n;
    n = MUT(CDC_CFG); m[17] = 0;                       /* IF0 bLength 0 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_BLEN_ZERO);
    n = MUT(CDC_CFG); m[26] = 1;                       /* CS header bLength 1 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_BLEN_ZERO);
    n = MUT(CDC_CFG); m[68] = 0x20;                    /* 마지막 EP bLength 32 > 남은 7 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_BLEN_OVERRUN);
    CHECK(parse_cfg_exact(CDC_CFG, 40, &c) == USB_E_TOTAL_GT_BUF);       /* 잘린 버퍼 */
    n = MUT(CDC_CFG); m[2] = 5; m[3] = 0;              /* wTotalLength 5 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_TOTAL_SMALL);
    n = MUT(CDC_CFG); m[2] = 0x4C;                     /* wTotalLength 76 > 75 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_TOTAL_GT_BUF);
    CHECK(parse_cfg_exact(CDC_CFG, 8, &c) == USB_E_SHORT);
    CHECK(parse_cfg_exact(CDC_CFG, 1, &c) == USB_E_SHORT);
    n = MUT(CDC_CFG); m[1] = DT_DEVICE;                /* 첫 타입이 config 아님 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_TYPE);
    n = MUT(CDC_CFG); m[10] = DT_CONFIG;               /* IAD 자리에 CONFIG 타입 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_TYPE);
    n = MUT(CDC_CFG); m[4] = 3;                        /* bNumInterfaces 3 vs 실제 2 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_NUM_IF);
    n = MUT(CDC_CFG); m[21] = 2;                       /* IF0 bNumEndpoints 2 vs 1 (다음 IF 에서 검출) */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_NUM_EP);
    n = MUT(CDC_CFG); m[56] = 3;                       /* IF1 bNumEndpoints 3 vs 2 (끝에서 검출) */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_NUM_EP);
    n = MUT(CDC_CFG); m[21] = 0;                       /* IF0 bNumEndpoints 0 인데 EP 1개 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_NUM_EP);
    {   /* config 바로 뒤에 EP (interface 없음) */
        uint8_t o[16] = { 0x09, 0x02, 0x10, 0x00, 0x00, 0x01, 0x00, 0x80, 0x32, 0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x0A };
        CHECK(parse_cfg_exact(o, sizeof o, &c) == USB_E_EP_ORPHAN);
    }
    n = MUT(MOUSE_CFG); m[29] = 0x80;                  /* EP 번호 0 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_EP_ADDR);
    n = MUT(MOUSE_CFG); m[29] = 0x91;                  /* 예약 비트 4 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_EP_ADDR);
    n = MUT(CDC_CFG); m[12] = 3;                       /* IAD 가 IF 0..2 를 가리킴 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_IAD_RANGE);
    n = MUT(CDC_CFG); m[12] = 0;                       /* IAD count 0 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_IAD_RANGE);
    {   /* interface bLength 5: 5바이트짜리 "interface" 뒤를 4바이트 벤더 블롭으로 채워 길이 합은 맞춘다 */
        uint8_t o[18] = { 0x09, 0x02, 0x12, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32, 0x05, 0x04, 0x00, 0x00, 0x00, 0x04, 0xFF, 0x00, 0x00 };
        CHECK(parse_cfg_exact(o, sizeof o, &c) == USB_E_BLEN_SMALL);
    }
    n = MUT(CDC_CFG); m[0] = 7;                        /* config bLength 7 */
    CHECK(parse_cfg_exact(m, n, &c) == USB_E_BLEN_SMALL);
    {   /* UAC1 처럼 9바이트 endpoint(bRefresh, bSynchAddress) 는 허용: 앞 7바이트만 해석 */
        uint8_t o[27] = { 0x09, 0x02, 0x1B, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
                          0x09, 0x04, 0x00, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,
                          0x09, 0x05, 0x01, 0x09, 0xC0, 0x00, 0x01, 0x00, 0x00 };
        CHECK(parse_cfg_exact(o, sizeof o, &c) == USB_OK && c.eps[0].blen == 9 && ep_mps(c.eps[0].wmps) == 192 &&
              ep_sync(c.eps[0].attr) == 2);
    }
    {   /* wTotalLength 뒤 꼬리는 무시 */
        uint8_t o[40]; memcpy(o, MOUSE_CFG, 34); memset(o + 34, 0, 6);
        CHECK(parse_cfg_exact(o, sizeof o, &c) == USB_OK && c.total == 34);
    }
    {   /* 표 용량: interface 1개에 endpoint 33개 → CAP */
        static uint8_t o[9 + 9 + 33 * 7];
        size_t k = 0;
        uint16_t tot = (uint16_t)sizeof o;
        uint8_t hdr[9] = { 0x09, 0x02, (uint8_t)tot, (uint8_t)(tot >> 8), 0x01, 0x01, 0x00, 0x80, 0x32 };
        memcpy(o, hdr, 9); k = 9;
        uint8_t itf[9] = { 0x09, 0x04, 0x00, 0x00, 33, 0xFF, 0x00, 0x00, 0x00 };
        memcpy(o + k, itf, 9); k += 9;
        for (int i = 0; i < 33; i++) {
            uint8_t e[7] = { 0x07, 0x05, (uint8_t)((i & 1 ? 0x80 : 0) | (1 + i % 15)), 0x02, 0x40, 0x00, 0x00 };
            memcpy(o + k, e, 7); k += 7;
        }
        CHECK(k == sizeof o && parse_cfg_exact(o, sizeof o, &c) == USB_E_CAP);
    }
    /* device 디스크립터 결함 */
    n = MUT(MOUSE_DEV); m[7] = 12;
    CHECK(usb_parse_device(m, n, &d) == USB_E_MPS0);
    n = MUT(MOUSE_DEV); m[2] = 0x20; m[3] = 0x03; m[7] = 9;   /* bcdUSB 3.20 이면 9 (=512) 허용 */
    CHECK(usb_parse_device(m, n, &d) == USB_OK && d.mps0 == 9);
    CHECK(usb_parse_device(MOUSE_DEV, 8, &d) == USB_E_SHORT);   /* 첫 8바이트만 받은 경우 */
    n = MUT(MOUSE_DEV); m[0] = 0;
    CHECK(usb_parse_device(m, n, &d) == USB_E_BLEN_ZERO);
    n = MUT(MOUSE_DEV); m[1] = DT_CONFIG;
    CHECK(usb_parse_device(m, n, &d) == USB_E_TYPE);
    n = MUT(MOUSE_DEV); m[0] = 9;
    CHECK(usb_parse_device(m, n, &d) == USB_E_BLEN_SMALL);
    /* 문자열 결함 */
    {
        uint8_t odd[7] = { 0x07, 0x03, 'A', 0, 'B', 0, 'C' };
        CHECK(usb_string_to_utf8(odd, sizeof odd, s8, sizeof s8) == USB_E_STR_ODD && s8[0] == '\0');
        uint8_t lone_hi[6] = { 0x06, 0x03, 0x3D, 0xD8, 'A', 0 };       /* D83D 다음이 'A' */
        CHECK(usb_string_to_utf8(lone_hi, sizeof lone_hi, s8, sizeof s8) == USB_E_STR_UTF16);
        uint8_t end_hi[4] = { 0x04, 0x03, 0x3D, 0xD8 };                /* 끝에서 상위만 */
        CHECK(usb_string_to_utf8(end_hi, sizeof end_hi, s8, sizeof s8) == USB_E_STR_UTF16);
        uint8_t lone_lo[4] = { 0x04, 0x03, 0x0C, 0xDD };
        CHECK(usb_string_to_utf8(lone_lo, sizeof lone_lo, s8, sizeof s8) == USB_E_STR_UTF16);
        CHECK(usb_string_to_utf8(g_s_mouse, 20, s8, sizeof s8) == USB_E_BLEN_OVERRUN);   /* bLength 36 > 받은 20 */
        CHECK(usb_string_to_utf8(g_s_logi, g_s_logi[0], s8, 8) == USB_E_CAP && s8[0] == '\0');  /* 8글자 + NUL > 8 */
        CHECK(usb_string_to_utf8(g_s_logi, g_s_logi[0], s8, 9) == 8);
        CHECK(usb_string_to_utf8(STR_KO, sizeof STR_KO, s8, 11) == USB_E_CAP);
        CHECK(usb_string_to_utf8(MOUSE_DEV, 18, s8, sizeof s8) == USB_E_TYPE);
        uint8_t z[2] = { 0x00, 0x03 };
        CHECK(usb_string_to_utf8(z, 2, s8, sizeof s8) == USB_E_BLEN_ZERO);
    }
    /* 퍼징: 예시 3개 × 6000 변이, 정확한 크기 버퍼. 결과는 정의된 코드, OK 면 불변식 성립 */
    {
        const uint8_t *src[3] = { CDC_CFG, MOUSE_CFG, UAC_CFG };
        const size_t   len[3] = { sizeof CDC_CFG, sizeof MOUSE_CFG, sizeof UAC_CFG };
        uint32_t seed = 0x15B0C0DEu, hist[19] = { 0 }, bad = 0, ok_inv_fail = 0, trials = 0;
        for (int which = 0; which < 3; which++) {
            for (int t = 0; t < 6000; t++, trials++) {
                uint8_t buf[128];
                size_t L = len[which];
                memcpy(buf, src[which], L);
                uint32_t r = xs32(&seed);
                int nmut = 1 + (int)(r % 3u);
                for (int k = 0; k < nmut; k++) {
                    uint32_t q = xs32(&seed);
                    size_t pos = q % L;
                    switch ((q >> 8) % 6u) {
                    case 0: buf[pos] ^= (uint8_t)(1u << ((q >> 16) % 8u)); break;     /* 비트 뒤집기 */
                    case 1: buf[pos] = (uint8_t)(q >> 16); break;                    /* 랜덤 바이트 */
                    case 2: buf[pos] = 0; break;
                    case 3: buf[pos] = 0xFF; break;
                    case 4: L = 1 + (q >> 16) % L; break;                            /* 잘라내기 */
                    default: buf[2] = (uint8_t)(q >> 16); buf[3] = (uint8_t)((q >> 24) & 1u); break;  /* wTotalLength */
                    }
                }
                int e = parse_cfg_exact(buf, L, &c);
                if (e > 0 || e < USB_ERR_MIN) bad++;
                else hist[-e]++;
                if (e == USB_OK && !cfg_invariants(&c, L)) ok_inv_fail++;
                char dummy[2048]; sbuf sb; sb_init(&sb, dummy, sizeof dummy);
                if (e == USB_OK) usb_dump_config(buf, &c, USB_HS, &sb);              /* 덤프도 안전해야 */
            }
        }
        unsigned kinds = 0;
        for (int i = 1; i < 19; i++) kinds += hist[i] ? 1u : 0u;
        printf("B fuzz: %u mutants  OK=%u  SHORT=%u BLEN_ZERO=%u OVERRUN=%u SMALL=%u TYPE=%u TOTAL_SMALL=%u GT_BUF=%u "
               "NUM_IF=%u NUM_EP=%u ORPHAN=%u EP_ADDR=%u IAD=%u  (%u error kinds)\n",
               trials, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8], hist[9], hist[10],
               hist[11], hist[12], kinds);
        CHECK(bad == 0 && ok_inv_fail == 0);
        CHECK(hist[0] > 0 && kinds >= 8);
    }

    /* ---------------- Part C ---------------- */
    {
        char line[128];
        const uint8_t s1[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 };
        CHECK(setup_decode(s1, line, sizeof line) > 0 &&
              strcmp(line, "80 06 00 01 00 00 40 00 | IN  STD DEV | GET_DESCRIPTOR(DEVICE, 0) wIndex=0x0000 wLength=64") == 0);
        const uint8_t s2[8] = { 0x00, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
        CHECK(setup_decode(s2, line, sizeof line) > 0 &&
              strcmp(line, "00 05 05 00 00 00 00 00 | OUT STD DEV | SET_ADDRESS(5) wIndex=0x0000 wLength=0") == 0);
        const uint8_t s3[8] = { 0x80, 0x06, 0x02, 0x03, 0x09, 0x04, 0xFF, 0x00 };
        CHECK(setup_decode(s3, line, sizeof line) > 0 &&
              strcmp(line, "80 06 02 03 09 04 FF 00 | IN  STD DEV | GET_DESCRIPTOR(STRING, 2) wIndex=0x0409 wLength=255") == 0);
        const uint8_t s4[8] = { 0x21, 0x20, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00 };   /* CDC SET_LINE_CODING */
        CHECK(setup_decode(s4, line, sizeof line) > 0 &&
              strcmp(line, "21 20 00 00 00 00 07 00 | OUT CLS IF | bRequest=0x20 wValue=0x0000 wIndex=0x0000 wLength=7") == 0);
        const uint8_t s5[8] = { 0x81, 0x06, 0x00, 0x22, 0x00, 0x00, 0x2E, 0x00 };   /* HID report descriptor 요청 */
        CHECK(setup_decode(s5, line, sizeof line) > 0 &&
              strcmp(line, "81 06 00 22 00 00 2E 00 | IN  STD IF | GET_DESCRIPTOR(REPORT, 0) wIndex=0x0000 wLength=46") == 0);
        CHECK(setup_decode(s1, line, 20) == USB_E_CAP);
    }
    {
        uint16_t pk[16];
        CHECK(data_stage_packets(18, 64, 8, pk, 16) == 3 && pk[0] == 8 && pk[1] == 8 && pk[2] == 2);
        CHECK(data_stage_packets(16, 255, 8, pk, 16) == 3 && pk[0] == 8 && pk[1] == 8 && pk[2] == 0);   /* ZLP */
        CHECK(data_stage_packets(16, 16, 8, pk, 16) == 2);                                              /* wLength 도달 */
        CHECK(data_stage_packets(75, 75, 64, pk, 16) == 2 && pk[0] == 64 && pk[1] == 11);
        CHECK(data_stage_packets(64, 255, 64, pk, 16) == 2 && pk[1] == 0);
        CHECK(data_stage_packets(18, 9, 64, pk, 16) == 1 && pk[0] == 9);
        CHECK(data_stage_packets(0, 255, 8, pk, 16) == 1 && pk[0] == 0);
        CHECK(data_stage_packets(255, 255, 8, pk, 16) == USB_E_CAP);                                   /* 32 패킷 > 16 */
    }
    /* C3: 마우스 enumeration — SETUP 바이트 순서를 정확히 */
    {
        host_setup(MOUSE_DEV, MOUSE_CFG, sizeof MOUSE_CFG, g_mouse_str, 3);
        CHECK(host_enumerate(&g_h, 5) == USB_OK);
        static const uint8_t E[9][8] = {
            { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00 }, { 0x00, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00 }, { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x09, 0x00 },
            { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x22, 0x00 }, { 0x80, 0x06, 0x00, 0x03, 0x00, 0x00, 0xFF, 0x00 },
            { 0x80, 0x06, 0x01, 0x03, 0x09, 0x04, 0xFF, 0x00 }, { 0x80, 0x06, 0x02, 0x03, 0x09, 0x04, 0xFF, 0x00 },
            { 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 } };
        bool seq = g_h.n_setup == 9;
        for (int i = 0; i < 9 && seq; i++) seq = setup_is(i, E[i]);
        CHECK(seq);
        CHECK(g_m.addr == 5 && g_m.cfg_value == 1 && g_h.mps0 == 8 && g_h.d.vid == 0x046D && g_h.langid == 0x0409);
        CHECK(strcmp(g_h.mfr, "Logitech") == 0 && strcmp(g_h.prod, "USB Optical Mouse") == 0 && g_h.ser[0] == '\0');
        CHECK(log_has("DATA IN 8 B (first packet only: 8)") && log_has("DATA IN 34 B in 5 pkt (8+8+8+8+2)"));
        CHECK(g_h.cfg.n_ep == 1 && g_h.str_missing == 0 && !g_h.log_overflow);
    }
    /* C4: CDC (mps0 64) — config 75 B 를 64+11 로, 문자열 3개 */
    {
        host_setup(CDC_DEV, CDC_CFG, sizeof CDC_CFG, g_cdc_str, 4);
        CHECK(host_enumerate(&g_h, 9) == USB_OK);
        CHECK(g_h.n_setup == 10 && g_h.cfg.n_iad == 1 && g_h.d.cls == 0xEF && g_m.cfg_value == 1);
        CHECK(log_has("DATA IN 75 B in 2 pkt (64+11)") && log_has("DATA IN 18 B (first packet only: 18)"));
        CHECK(strcmp(g_h.ser, "0001") == 0 && strcmp(g_h.prod, "COMM-M15 CDC Demo") == 0);
    }
    /* C5: 결함 — iSerial 이 없는 문자열을 가리킴 → STALL, 그래도 Configured */
    {
        host_setup(CDC_DEV, CDC_CFG, sizeof CDC_CFG, g_cdc_str, 3);   /* str[3] 없음 */
        CHECK(host_enumerate(&g_h, 9) == USB_OK && g_h.str_missing == 1 && g_h.ser[0] == '\0' && g_m.cfg_value == 1);
        CHECK(log_has("STALL") && g_m.n_stall == 1);
    }
    /* C6: 결함 — wTotalLength 는 80 이라 해 놓고 75 B 만 보냄 → TOTAL_GT_BUF, 설정 안 됨 */
    {
        static uint8_t bad[80];
        memcpy(bad, CDC_CFG, sizeof CDC_CFG); bad[2] = 80;
        host_setup(CDC_DEV, bad, sizeof CDC_CFG, g_cdc_str, 4);
        CHECK(host_enumerate(&g_h, 9) == USB_E_TOTAL_GT_BUF && g_m.cfg_value == 0);
    }
    /* C7: 결함 — 장치가 SET_ADDRESS 를 status 단계 전에 적용 → status 무응답 */
    {
        host_setup(MOUSE_DEV, MOUSE_CFG, sizeof MOUSE_CFG, g_mouse_str, 3);
        g_m.bug_addr_early = true;
        CHECK(host_enumerate(&g_h, 5) == USB_E_NORESP && log_has("STATUS: no response"));
    }
    /* C8: LANGID 가 없는(string 0 STALL) 장치 → 문자열 없이 Configured */
    {
        host_setup(MOUSE_DEV, MOUSE_CFG, sizeof MOUSE_CFG, g_mouse_str, 0);
        CHECK(host_enumerate(&g_h, 5) == USB_OK && g_h.langid == 0 && g_h.str_missing == 2 && g_m.cfg_value == 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
