/* COMM-M15 demo_fuzz.c — 변이 퍼징 300만 건: 견고한 파서의 오류 분포 vs "순진한" 워커가 멈추거나(HANG) 범위 밖을 읽는(OOB) 비율
 *
 * 예시 configuration 3개 × 1,000,000 변이 (비트 뒤집기 / 랜덤 바이트 / 0x00 / 0xFF / 잘라내기 / wTotalLength 바꾸기, 1~3회).
 * 순진한 워커 = wTotalLength 를 믿고 bLength 를 검사 없이 더하는 흔한 코드 — 실제로 넘겨 읽지는 않고 "읽었을 위치"만 센다.
 * 파서·예시 배열은 solution.c 에서 그대로 복사.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_fuzz.c -o demo_fuzz && ./demo_fuzz
 */
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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


static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- 흔한 "순진한" 워커: wTotalLength 를 믿고 bLength 를 검사 없이 더한다.
 *      실제로 범위 밖을 읽지는 않고, 읽었을 위치를 세기만 한다 (실험이 UB 가 되지 않게). ---- */
enum { NV_CLEAN, NV_HANG, NV_OOB };
static int naive_walk(const uint8_t *buf, size_t n) {
    if (n < 4) return NV_OOB;                          /* wTotalLength 읽기부터 범위 밖 */
    size_t total = le16(buf + 2), off = 0;
    for (unsigned steps = 0; off < total; steps++) {
        if (off + 1 >= n) return NV_OOB;               /* bLength / bDescriptorType 읽기 */
        uint8_t bl = buf[off];
        if (bl == 0) return NV_HANG;                    /* off += 0 → 영원히 같은 자리 */
        if (off + bl > n) return NV_OOB;               /* 디스크립터 필드 읽기 */
        off += bl;
        if (steps > 100000) return NV_HANG;
    }
    return NV_CLEAN;
}

static size_t mutate(uint8_t *buf, size_t L, uint32_t *seed) {
    int nmut = 1 + (int)(xs32(seed) % 3u);
    for (int k = 0; k < nmut; k++) {
        uint32_t q = xs32(seed);
        size_t pos = q % L;
        switch ((q >> 8) % 6u) {
        case 0: buf[pos] ^= (uint8_t)(1u << ((q >> 16) % 8u)); break;
        case 1: buf[pos] = (uint8_t)(q >> 16); break;
        case 2: buf[pos] = 0; break;
        case 3: buf[pos] = 0xFF; break;
        case 4: L = 1 + (q >> 16) % L; break;
        default: buf[2] = (uint8_t)(q >> 16); buf[3] = (uint8_t)((q >> 24) & 1u); break;
        }
    }
    return L;
}

int main(void) {
    const char *name[3] = { "CDC-ACM+IAD (75 B)", "HID mouse (34 B)", "UAC2-like (97 B)" };
    const uint8_t *src[3] = { CDC_CFG, MOUSE_CFG, UAC_CFG };
    const size_t len[3] = { sizeof CDC_CFG, sizeof MOUSE_CFG, sizeof UAC_CFG };
    const unsigned N = 1000000;
    unsigned all_hist[19] = { 0 };
    unsigned long long cross[3][2] = { { 0 } };          /* naive 결과 × (robust OK / robust 오류) */
    clock_t t0 = clock();
    for (int w = 0; w < 3; w++) {
        unsigned hist[19] = { 0 }, nv[3] = { 0 };
        uint32_t seed = 0xDEC0DE00u + (uint32_t)w;
        for (unsigned t = 0; t < N; t++) {
            uint8_t buf[128];
            memcpy(buf, src[w], len[w]);
            size_t L = mutate(buf, len[w], &seed);
            usb_cfg c;
            int e = usb_parse_config(buf, L, &c);
            hist[-e]++; all_hist[-e]++;
            int v = naive_walk(buf, L);
            nv[v]++;
            cross[v][e == USB_OK ? 0 : 1]++;
        }
        printf("%-20s OK %6.2f%% | naive: clean %6.2f%%  HANG %5.2f%%  OOB-read %6.2f%%\n", name[w],
               100.0 * hist[0] / N, 100.0 * nv[NV_CLEAN] / N, 100.0 * nv[NV_HANG] / N, 100.0 * nv[NV_OOB] / N);
    }
    double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("\nrobust parser result histogram over %u mutants:\n", 3 * N);
    for (int i = 0; i < 19; i++)
        if (all_hist[i]) printf("  %-13s %8u  (%5.2f%%)\n", usb_err_name(-i), all_hist[i], 100.0 * all_hist[i] / (3.0 * N));
    printf("\ncross table (rows = naive walker, cols = robust parser):\n");
    printf("  %-10s %12s %12s\n", "", "robust OK", "robust ERR");
    const char *rn[3] = { "clean", "HANG", "OOB-read" };
    for (int i = 0; i < 3; i++) printf("  %-10s %12llu %12llu\n", rn[i], cross[i][0], cross[i][1]);
    printf("\n%u parses (robust + naive) in %.2f s CPU\n", 3 * N, sec);
    (void)usb_parse_device; (void)usb_string_to_utf8; (void)usb_langids; (void)ep_sync; (void)ep_usage; (void)ep_mult;
    (void)ep_interval_us; (void)ep_type; (void)ep_mps; (void)CDC_DEV; (void)MOUSE_DEV; (void)UAC_DEV; (void)STR_LANG; (void)STR_KO;
    return 0;
}
