/* COMM-M15 추가 연습 — 한 번에 검증
 *
 * 연습 1: HID report 디스크립터 아이템 워커 — 이 Mac 의 실제 Logitech 마우스 report(46 B, ioreg)로 입력 리포트 32비트 확인
 * 연습 2: 주기 전송 대역폭(바이트/초) + "alt 0 에는 대역폭 있는 iso 엔드포인트 금지" 검사
 * 연습 3: 드라이버 바인딩 — 클래스·alt·타입·방향으로 엔드포인트 찾기, interface → IAD
 * 연습 4: 장치 쪽 configuration 빌더 (wTotalLength·bNumInterfaces 자동) → 손으로 쓴 CDC 배열과 바이트 동일
 * 연습 5: UTF-8 → string 디스크립터 (서로게이트 쌍, 126 유닛 한계, 잘못된 UTF-8 거부) + 랜덤 왕복
 *
 * 본문(solution.c)의 파서·예시 배열을 그대로 복사해 넣었다 (파일 하나로 독립).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
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


/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ======================= 연습 1: HID report 디스크립터 (이 Mac 마우스의 실제 46바이트) ======================= */

/* ioreg 의 AppleUserUSBHostHIDDevice "ReportDescriptor" 값 그대로 (Logitech 0x046D:0xC077) */
static const uint8_t LOGI_REPORT[46] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0 };

enum { HID_E_TRUNC = -1, HID_E_UNBALANCED = -2 };
typedef struct { unsigned items, collections, max_depth, input_items, input_bits; } hid_summary;

/* short item: prefix = bTag(7:4) bType(3:2) bSize(1:0, 3 → 4바이트). long item: prefix 0xFE, bDataSize, bLongItemTag.
 * Global: Report Size tag 7, Report Count tag 9.  Main: Input 8, Collection 0xA, End Collection 0xC. */
static int hid_walk(const uint8_t *r, size_t n, hid_summary *s) {
    memset(s, 0, sizeof *s);
    unsigned rsize = 0, rcount = 0, depth = 0;
    size_t i = 0;
    while (i < n) {
        uint8_t p = r[i];
        if (p == 0xFE) {                                   /* long item */
            if (i + 3 > n || i + 3 + r[i + 1] > n) return HID_E_TRUNC;
            i += 3u + r[i + 1]; s->items++; continue;
        }
        static const uint8_t SZ[4] = { 0, 1, 2, 4 };
        size_t sz = SZ[p & 3];
        if (i + 1 + sz > n) return HID_E_TRUNC;
        uint32_t v = 0;
        for (size_t k = 0; k < sz; k++) v |= (uint32_t)r[i + 1 + k] << (8 * k);
        unsigned type = (p >> 2) & 3u, tag = p >> 4;
        if (type == 1 && tag == 7) rsize = v;
        if (type == 1 && tag == 9) rcount = v;
        if (type == 0 && tag == 8) { s->input_items++; s->input_bits += rsize * rcount; }
        if (type == 0 && tag == 0xA) { s->collections++; depth++; if (depth > s->max_depth) s->max_depth = depth; }
        if (type == 0 && tag == 0xC) { if (depth == 0) return HID_E_UNBALANCED; depth--; }
        s->items++;
        i += 1 + sz;
    }
    return depth == 0 ? 0 : HID_E_UNBALANCED;
}

/* ======================= 연습 2: 주기 전송 대역폭 + alt 0 규칙 ======================= */

/* interrupt/iso 엔드포인트가 예약하는 최대 바이트/초. control/bulk = 0 (보장 없음), 잘못된 bInterval = -1 */
static long ep_bytes_per_sec(const usb_ep *e, usb_speed sp) {
    int t = ep_type(e->attr);
    if (t != EP_INTERRUPT && t != EP_ISO) return 0;
    long us = ep_interval_us(t, sp, e->interval);
    if (us <= 0) return -1;
    return (long)(ep_mps(e->wmps) * ep_mult(e->wmps)) * 1000000L / us;
}

/* "기본(alt 0) 설정에는 0이 아닌 크기의 iso 엔드포인트가 없어야 한다" (USB 2.0 §5.6.3 — 확인 권장) */
static bool iso_alt0_ok(const usb_cfg *c) {
    for (unsigned i = 0; i < c->n_if; i++) {
        if (c->ifs[i].alt != 0) continue;
        for (unsigned k = 0; k < c->ifs[i].nep_found; k++) {
            const usb_ep *e = &c->eps[c->ifs[i].ep_first + k];
            if (ep_type(e->attr) == EP_ISO && ep_mps(e->wmps) > 0) return false;
        }
    }
    return true;
}

/* ======================= 연습 3: 드라이버 바인딩 — 엔드포인트 찾기 ======================= */

static int find_ep(const usb_cfg *c, uint8_t if_cls, uint8_t alt, int type, bool dir_in) {
    for (unsigned i = 0; i < c->n_if; i++) {
        const usb_if *f = &c->ifs[i];
        if (f->cls != if_cls || f->alt != alt) continue;
        for (unsigned k = 0; k < f->nep_found; k++) {
            const usb_ep *e = &c->eps[f->ep_first + k];
            if (ep_type(e->attr) == type && ((e->addr & 0x80) != 0) == dir_in) return e->addr;
        }
    }
    return -1;
}
static int iad_for_interface(const usb_cfg *c, uint8_t ifnum) {
    for (unsigned i = 0; i < c->n_iad; i++)
        if (ifnum >= c->iads[i].first && ifnum < c->iads[i].first + c->iads[i].count) return (int)i;
    return -1;
}

/* ======================= 연습 4: 장치 쪽 configuration 빌더 ======================= */

typedef struct { uint8_t *buf; size_t cap, len; int err; uint32_t present[8]; } cfg_builder;

static void cb_put(cfg_builder *b, const uint8_t *d) {
    if (b->err) return;
    if (d[0] < 2) { b->err = USB_E_BLEN_ZERO; return; }
    if (b->len + d[0] > b->cap) { b->err = USB_E_CAP; return; }
    if (b->len + d[0] > 0xFFFFu) { b->err = USB_E_CAP; return; }
    memcpy(b->buf + b->len, d, d[0]);
    if (d[1] == DT_INTERFACE) b->present[d[2] >> 5] |= 1u << (d[2] & 31u);
    b->len += d[0];
}
static void cb_begin(cfg_builder *b, uint8_t *buf, size_t cap, uint8_t value, uint8_t attr, unsigned max_ma) {
    memset(b, 0, sizeof *b);
    b->buf = buf; b->cap = cap;
    uint8_t h[9] = { 9, DT_CONFIG, 0, 0, 0, value, 0, (uint8_t)(0x80u | attr), (uint8_t)((max_ma > 500 ? 500 : max_ma) / 2) };
    cb_put(b, h);
}
static void cb_iad(cfg_builder *b, uint8_t first, uint8_t count, uint8_t cls, uint8_t sub, uint8_t proto) {
    uint8_t d[8] = { 8, DT_IAD, first, count, cls, sub, proto, 0 }; cb_put(b, d);
}
static void cb_iface(cfg_builder *b, uint8_t num, uint8_t alt, uint8_t nep, uint8_t cls, uint8_t sub, uint8_t proto) {
    uint8_t d[9] = { 9, DT_INTERFACE, num, alt, nep, cls, sub, proto, 0 }; cb_put(b, d);
}
static void cb_ep(cfg_builder *b, uint8_t addr, uint8_t attr, uint16_t mps, uint8_t interval) {
    uint8_t d[7] = { 7, DT_ENDPOINT, addr, attr, (uint8_t)mps, (uint8_t)(mps >> 8), interval }; cb_put(b, d);
}
/* wTotalLength 와 bNumInterfaces 를 마지막에 채운다 — 손으로 세다 틀리는 두 필드 */
static int cb_finish(cfg_builder *b) {
    if (b->err) return b->err;
    unsigned nif = 0;
    for (int i = 0; i < 8; i++) nif += popcount32(b->present[i]);
    b->buf[2] = (uint8_t)b->len; b->buf[3] = (uint8_t)(b->len >> 8);
    b->buf[4] = (uint8_t)nif;
    return (int)b->len;
}

static int build_cdc(uint8_t *buf, size_t cap) {
    cfg_builder b;
    cb_begin(&b, buf, cap, 1, 0x00, 100);
    cb_iad(&b, 0, 2, 0x02, 0x02, 0x01);
    cb_iface(&b, 0, 0, 1, 0x02, 0x02, 0x01);
    const uint8_t hdr[5] = { 5, DT_CS_INTERFACE, 0x00, 0x10, 0x01 }, cm[5] = { 5, DT_CS_INTERFACE, 0x01, 0x00, 0x01 };
    const uint8_t acm[4] = { 4, DT_CS_INTERFACE, 0x02, 0x02 }, un[5] = { 5, DT_CS_INTERFACE, 0x06, 0x00, 0x01 };
    cb_put(&b, hdr); cb_put(&b, cm); cb_put(&b, acm); cb_put(&b, un);
    cb_ep(&b, 0x82, EP_INTERRUPT, 8, 16);
    cb_iface(&b, 1, 0, 2, 0x0A, 0x00, 0x00);
    cb_ep(&b, 0x01, EP_BULK, 64, 0);
    cb_ep(&b, 0x81, EP_BULK, 64, 0);
    return cb_finish(&b);
}

/* ======================= 연습 5: UTF-8 → string 디스크립터 (장치 쪽) ======================= */

#define PR_E_UTF8     (-20)   /* 잘못된 UTF-8 (overlong, 끊긴 연속 바이트, 서로게이트, > U+10FFFF) */
#define PR_E_TOO_LONG (-21)   /* bLength 가 255 를 넘는다 (UTF-16 코드 유닛 126개 초과) */

static int usb_string_from_utf8(const char *s, uint8_t *out, size_t cap) {
    const uint8_t *p = (const uint8_t *)s;
    size_t units = 0, o = 2;
    uint8_t tmp[256];
    while (*p) {
        uint32_t u; size_t k;
        if (p[0] < 0x80)              { u = p[0]; k = 1; }
        else if ((p[0] & 0xE0) == 0xC0) { u = p[0] & 0x1Fu; k = 2; }
        else if ((p[0] & 0xF0) == 0xE0) { u = p[0] & 0x0Fu; k = 3; }
        else if ((p[0] & 0xF8) == 0xF0) { u = p[0] & 0x07u; k = 4; }
        else return PR_E_UTF8;
        for (size_t j = 1; j < k; j++) {
            if ((p[j] & 0xC0) != 0x80) return PR_E_UTF8;       /* NUL 도 여기서 걸린다 → 넘어 읽지 않음 */
            u = (u << 6) | (p[j] & 0x3Fu);
        }
        static const uint32_t MIN[5] = { 0, 0, 0x80, 0x800, 0x10000 };
        if (u < MIN[k] || u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) return PR_E_UTF8;
        size_t need = u >= 0x10000 ? 2 : 1;
        if (units + need > 126) return PR_E_TOO_LONG;
        if (need == 2) {
            uint32_t v = u - 0x10000;
            uint16_t hi = (uint16_t)(0xD800 + (v >> 10)), lo = (uint16_t)(0xDC00 + (v & 0x3FF));
            tmp[o++] = (uint8_t)hi; tmp[o++] = (uint8_t)(hi >> 8); tmp[o++] = (uint8_t)lo; tmp[o++] = (uint8_t)(lo >> 8);
        } else {
            tmp[o++] = (uint8_t)u; tmp[o++] = (uint8_t)(u >> 8);
        }
        units += need;
        p += k;
    }
    if (o > cap) return USB_E_CAP;
    tmp[0] = (uint8_t)o; tmp[1] = DT_STRING;
    memcpy(out, tmp, o);
    return (int)o;
}

int main(void) {
    usb_cfg c;
    usb_dev dv;
    uint16_t ids[2];
    /* 연습 3 의 전제: class 0xEF/0x02/0x01 이면 "IAD 를 보라"는 뜻 */
    CHECK(usb_parse_device(CDC_DEV, 18, &dv) == USB_OK && dv.cls == 0xEF && usb_parse_device(UAC_DEV, 18, &dv) == USB_OK &&
          dv.cls == 0xEF && usb_parse_device(MOUSE_DEV, 18, &dv) == USB_OK && dv.cls == 0x00);
    CHECK(usb_langids(STR_LANG, sizeof STR_LANG, ids, 2) == 1 && ids[0] == 0x0409);
    /* ---- 연습 1 ---- */
    {
        hid_summary s;
        CHECK(hid_walk(LOGI_REPORT, sizeof LOGI_REPORT, &s) == 0);
        printf("practice 1: items=%u collections=%u depth=%u input_items=%u input_bits=%u (%u bytes)\n",
               s.items, s.collections, s.max_depth, s.input_items, s.input_bits, s.input_bits / 8);
        CHECK(s.items == 24 && s.collections == 2 && s.max_depth == 2 && s.input_items == 2);
        CHECK(s.input_bits == 32);                                  /* ioreg MaxInputReportSize = 4 와 일치 */
        CHECK(hid_walk(LOGI_REPORT, 45, &s) == HID_E_UNBALANCED);    /* 마지막 End Collection 없음 */
        CHECK(hid_walk(LOGI_REPORT, 43, &s) == HID_E_TRUNC);         /* 81 06 의 데이터 바이트 잘림 */
        const uint8_t extra_end[3] = { 0xA1, 0x01, 0xC0 }, two_end[2] = { 0xC0, 0xC0 };
        CHECK(hid_walk(extra_end, 3, &s) == 0 && hid_walk(two_end, 2, &s) == HID_E_UNBALANCED);
        const uint8_t longi[5] = { 0xFE, 0x02, 0x10, 0xAA, 0xBB };
        CHECK(hid_walk(longi, 5, &s) == 0 && s.items == 1 && hid_walk(longi, 4, &s) == HID_E_TRUNC);
        CHECK(le16(MOUSE_CFG + 25) == sizeof LOGI_REPORT);           /* HID 디스크립터의 wDescriptorLength */
    }
    /* ---- 연습 2 ---- */
    {
        CHECK(usb_parse_config(UAC_CFG, sizeof UAC_CFG, &c) == USB_OK);
        long d = ep_bytes_per_sec(&c.eps[0], USB_HS), fb = ep_bytes_per_sec(&c.eps[1], USB_HS);
        CHECK(d == 448000 && fb == 4000 && iso_alt0_ok(&c));
        printf("practice 2: UAC data %ld B/s, feedback %ld B/s", d, fb);
        CHECK(usb_parse_config(MOUSE_CFG, sizeof MOUSE_CFG, &c) == USB_OK && ep_bytes_per_sec(&c.eps[0], USB_LS) == 400);
        CHECK(usb_parse_config(CDC_CFG, sizeof CDC_CFG, &c) == USB_OK && ep_bytes_per_sec(&c.eps[0], USB_FS) == 500 &&
              ep_bytes_per_sec(&c.eps[1], USB_FS) == 0);
        usb_ep hb = { 0x81, 0x05, 1, 7, 0, 0x1400 };                 /* HS high-bandwidth iso 3 x 1024 B */
        printf(", mouse 400 B/s, CDC notify 500 B/s, HS hi-bw iso %ld B/s\n", ep_bytes_per_sec(&hb, USB_HS));
        CHECK(ep_bytes_per_sec(&hb, USB_HS) == 24576000L);
        usb_ep bad = { 0x81, 0x03, 0, 7, 0, 8 };                     /* interrupt bInterval 0 */
        CHECK(ep_bytes_per_sec(&bad, USB_FS) == -1);
        uint8_t o[25] = { 0x09, 0x02, 0x19, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
                          0x09, 0x04, 0x00, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00,
                          0x07, 0x05, 0x81, 0x05, 0xC0, 0x00, 0x01 };   /* alt 0 에 iso 192 B */
        CHECK(usb_parse_config(o, sizeof o, &c) == USB_OK && !iso_alt0_ok(&c));
        o[22] = 0;                                                  /* 크기 0 이면 허용 */
        CHECK(usb_parse_config(o, sizeof o, &c) == USB_OK && iso_alt0_ok(&c));
    }
    /* ---- 연습 3 ---- */
    {
        CHECK(usb_parse_config(CDC_CFG, sizeof CDC_CFG, &c) == USB_OK);
        CHECK(find_ep(&c, 0x0A, 0, EP_BULK, true) == 0x81 && find_ep(&c, 0x0A, 0, EP_BULK, false) == 0x01);
        CHECK(find_ep(&c, 0x02, 0, EP_INTERRUPT, true) == 0x82 && find_ep(&c, 0x03, 0, EP_INTERRUPT, true) == -1);
        CHECK(iad_for_interface(&c, 1) == 0 && iad_for_interface(&c, 2) == -1);
        CHECK(usb_parse_config(UAC_CFG, sizeof UAC_CFG, &c) == USB_OK);
        CHECK(find_ep(&c, 0x01, 1, EP_ISO, false) == 0x01 && find_ep(&c, 0x01, 0, EP_ISO, false) == -1);
        CHECK(find_ep(&c, 0x01, 1, EP_ISO, true) == 0x81 && ep_usage(c.eps[1].attr) == 1);
    }
    /* ---- 연습 4 ---- */
    {
        uint8_t buf[128];
        int n = build_cdc(buf, sizeof buf);
        CHECK(n == 75 && memcmp(buf, CDC_CFG, 75) == 0);              /* 손으로 쓴 배열과 바이트 단위로 같다 */
        CHECK(usb_parse_config(buf, (size_t)n, &c) == USB_OK);
        int small = build_cdc(buf, 74);
        CHECK(small == USB_E_CAP && build_cdc(buf, 9) == USB_E_CAP);
        printf("practice 4: builder -> %d bytes, identical to CDC_CFG; cap 74 -> %s\n", n, usb_err_name(small));
    }
    /* ---- 연습 5 ---- */
    {
        uint8_t d[256]; char s8[512];
        CHECK(usb_string_from_utf8("\xED\x95\x9C\xEA\xB8\x80 \xF0\x9F\x94\x8C", d, sizeof d) == 12 && memcmp(d, STR_KO, 12) == 0);
        CHECK(usb_string_from_utf8("", d, sizeof d) == 2 && d[0] == 2 && d[1] == DT_STRING);
        char a[200];
        memset(a, 'A', 126); a[126] = '\0';
        CHECK(usb_string_from_utf8(a, d, sizeof d) == 254);
        memset(a, 'A', 127); a[127] = '\0';
        CHECK(usb_string_from_utf8(a, d, sizeof d) == PR_E_TOO_LONG);
        char e[4 * 64 + 1]; size_t k = 0;
        for (int i = 0; i < 63; i++) { memcpy(e + k, "\xF0\x9F\x94\x8C", 4); k += 4; }
        e[k] = '\0';
        CHECK(usb_string_from_utf8(e, d, sizeof d) == 254);         /* 63 x 서로게이트 쌍 = 126 유닛 */
        memcpy(e + k, "\xF0\x9F\x94\x8C", 4); e[k + 4] = '\0';
        CHECK(usb_string_from_utf8(e, d, sizeof d) == PR_E_TOO_LONG);
        CHECK(usb_string_from_utf8("\xC0\x80", d, sizeof d) == PR_E_UTF8);          /* overlong NUL */
        CHECK(usb_string_from_utf8("\xED\xA0\x80", d, sizeof d) == PR_E_UTF8);      /* 서로게이트를 UTF-8 로 */
        CHECK(usb_string_from_utf8("\xF4\x90\x80\x80", d, sizeof d) == PR_E_UTF8);  /* > U+10FFFF */
        CHECK(usb_string_from_utf8("ab\xE2\x82", d, sizeof d) == PR_E_UTF8);        /* 끊김 */
        CHECK(usb_string_from_utf8("Logitech", d, 17) == USB_E_CAP && usb_string_from_utf8("Logitech", d, 18) == 18);
        /* 랜덤 왕복: 코드 포인트 → UTF-16 디스크립터 → UTF-8(본문 디코더) → 다시 디스크립터, 바이트 동일 */
        uint32_t seed = 0x00C077u; int ok = 0, trials = 3000;
        for (int t = 0; t < trials; t++) {
            uint8_t src[256]; size_t o = 2, units = 0, maxu = 1 + xs32(&seed) % 126u;
            while (units < maxu) {
                uint32_t r = xs32(&seed), u;
                switch (r % 4u) {
                case 0: u = 0x20 + (r >> 8) % 0x5F; break;
                case 1: u = 0x80 + (r >> 8) % 0x780; break;
                case 2: u = 0x800 + (r >> 8) % (0x10000 - 0x800); if (u >= 0xD800 && u <= 0xDFFF) u -= 0x800; break;
                default: u = 0x10000 + (r >> 8) % 0x100000; break;
                }
                if (u >= 0x10000 && units + 2 > maxu) break;
                if (u >= 0x10000) {
                    uint32_t v = u - 0x10000;
                    uint16_t hi = (uint16_t)(0xD800 + (v >> 10)), lo = (uint16_t)(0xDC00 + (v & 0x3FF));
                    src[o++] = (uint8_t)hi; src[o++] = (uint8_t)(hi >> 8); src[o++] = (uint8_t)lo; src[o++] = (uint8_t)(lo >> 8);
                    units += 2;
                } else { src[o++] = (uint8_t)u; src[o++] = (uint8_t)(u >> 8); units++; }
            }
            src[0] = (uint8_t)o; src[1] = DT_STRING;
            if (usb_string_to_utf8(src, o, s8, sizeof s8) < 0) continue;
            if (usb_string_from_utf8(s8, d, sizeof d) == (int)o && memcmp(d, src, o) == 0) ok++;
        }
        printf("practice 5: random round trips %d/%d\n", ok, trials);
        CHECK(ok == trials);
    }
    (void)ep_sync;
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
