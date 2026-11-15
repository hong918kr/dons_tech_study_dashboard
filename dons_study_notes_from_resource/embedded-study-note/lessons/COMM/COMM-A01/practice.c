/* COMM-A01 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 모델(SIE + EP0 펌웨어 + 호스트 시뮬레이터)을 그대로 담고(자체 완결), 그 위에:
 *   연습 1  ENDPOINT_HALT: SET/CLEAR_FEATURE, GET_STATUS(endpoint), functional STALL vs protocol STALL, 토글 리셋
 *   연습 2  stty 인자 → SET_LINE_CODING 7바이트 (Linux cdc-acm termios 변환 규칙), B0 = hang-up
 *   연습 3  CDC SERIAL_STATE 알림 10 B 를 interrupt EP(mps 8)로: 8 + 2 패킷, 호스트 재조립
 *   연습 4  control 전송 트랜잭션 수 닫힌 식 vs 시뮬레이터, bring-up 전체 트랜잭션 수 (mps 8 vs 64)
 * 본문 std_request 는 std_request_base 로 이름만 바꾸고, 연습 1 의 std_request 가 감싼다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- USB 2.0 ch.9 + CDC 1.2 / PSTN 1.2 상수 ---------------- */
enum { DT_DEVICE = 1, DT_CONFIG = 2, DT_STRING = 3, DT_INTERFACE = 4, DT_ENDPOINT = 5,
       DT_DEVICE_QUALIFIER = 6, DT_OTHER_SPEED = 7 };
enum { REQ_GET_STATUS = 0, REQ_CLEAR_FEATURE = 1, REQ_SET_FEATURE = 3, REQ_SET_ADDRESS = 5,
       REQ_GET_DESCRIPTOR = 6, REQ_GET_CONFIGURATION = 8, REQ_SET_CONFIGURATION = 9,
       REQ_GET_INTERFACE = 10, REQ_SET_INTERFACE = 11 };
enum { CDC_SEND_ENCAPSULATED_COMMAND = 0x00, CDC_GET_ENCAPSULATED_RESPONSE = 0x01,
       CDC_SET_LINE_CODING = 0x20, CDC_GET_LINE_CODING = 0x21, CDC_SET_CONTROL_LINE_STATE = 0x22,
       CDC_SEND_BREAK = 0x23 };
enum { VND_PATTERN = 0x01 };            /* 벤더 루프백: wValue 바이트짜리 패턴을 돌려준다 (펌웨어 테스트 훅) */
#define RT_TYPE(bm)  (((bm) >> 5) & 3)  /* 0 standard, 1 class, 2 vendor */
#define RT_RECIP(bm) ((bm) & 0x1F)      /* 0 device, 1 interface, 2 endpoint */

#define EP0_MPS_MAX 64
#define ENCAP_MAX   64                  /* SEND_ENCAPSULATED_COMMAND 수신 버퍼 */
#define PATTERN_MAX 256
#define CDC_COMM_IF 0

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* ---------------- 장치 상태 ---------------- */
typedef enum { EP0_IDLE, EP0_DATA_IN, EP0_DATA_OUT, EP0_STATUS_IN, EP0_STATUS_OUT, EP0_STALLED } ep0_state;
typedef enum { DEV_DEFAULT, DEV_ADDRESS, DEV_CONFIGURED } dev_state;
typedef enum { ACT_NONE, ACT_SET_ADDRESS, ACT_SET_CONFIG, ACT_LINE_CODING, ACT_CTRL_LINES, ACT_ENCAP } ep0_action;
static const char *const EP0_NAME[] = { "IDLE", "DATA_IN", "DATA_OUT", "STATUS_IN", "STATUS_OUT", "STALLED" };

typedef struct { uint32_t rate; uint8_t stop, parity, bits; } line_coding;   /* bCharFormat, bParityType, bDataBits */

typedef struct {                /* SIE = USB 주변장치 레지스터 + EP0 버퍼 (하드웨어) */
    uint8_t addr, mps;
    bool in_armed, in_stall, out_armed, out_stall;
    uint8_t in_buf[EP0_MPS_MAX], in_len, in_tog, out_tog;
    uint8_t setup[8];
} sie_regs;

typedef struct {
    sie_regs hw;
    /* EP0 펌웨어 */
    ep0_state st;
    dev_state dstate;
    uint8_t bm, breq;
    uint16_t wvalue, windex, wlength;
    const uint8_t *tx; uint16_t tx_left; bool tx_zlp;     /* DATA_IN: 남은 바이트, ZLP 를 더 보내야 하나 */
    uint8_t rx[ENCAP_MAX]; uint16_t rx_len, rx_expect;     /* DATA_OUT */
    ep0_action act; uint16_t act_arg;                      /* status 뒤 commit 할 효과 */
    uint8_t config;                                        /* 0 = unconfigured */
    line_coding lc;
    uint8_t lines;                                         /* bit0 DTR, bit1 RTS */
    uint8_t encap[ENCAP_MAX]; uint16_t encap_len;
    uint8_t txbuf[8];                                      /* 짧은 IN 응답 (GET_STATUS / CONFIGURATION / LINE_CODING) */
    uint8_t dev_desc[18];
    struct { uint32_t setups, xfer_ok, stalls, aborts, early_status, zlp_armed, dup_out, commits; } n;
} usbd;

/* ---------------- 디스크립터 (CDC-ACM, IAD 없는 고전형: device class 0x02) ---------------- */
static const uint8_t DEV_DESC_TMPL[18] = {
    0x12, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, 0x40 /* bMaxPacketSize0: usbd_init 이 덮어씀 */,
    0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x01 };   /* VID 0x1209 PID 0x0001 (pid.codes test), bcdDevice 1.00 */
static const uint8_t CFG_DESC[67] = {
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,   /* CONFIG wTotalLength 67, 2 IF, value 1, bus-powered 100 mA */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,   /* IF0 CDC comm / ACM / AT(V.250), 1 EP */
    0x05, 0x24, 0x00, 0x10, 0x01,                           /*   Header bcdCDC 1.10 */
    0x05, 0x24, 0x01, 0x00, 0x01,                           /*   Call Management: caps 0, data IF 1 */
    0x04, 0x24, 0x02, 0x02,                                 /*   ACM caps 0x02: line coding + ctrl line state + SERIAL_STATE */
    0x05, 0x24, 0x06, 0x00, 0x01,                           /*   Union: master 0, slave 1 */
    0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x10,               /*   EP 0x82 interrupt IN 8 B, 16 ms (알림) */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,   /* IF1 CDC-Data, 2 EP */
    0x07, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00,               /*   EP 0x01 bulk OUT 64 B */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00 };             /*   EP 0x81 bulk IN 64 B */
static uint8_t STR0[4] = { 0x04, 0x03, 0x09, 0x04 };        /* LANGID 0x0409 */
static uint8_t STR1[2 + 2 * 7], STR2[2 + 2 * 31], STR3[2 + 2 * 8];
static const uint8_t *const STRS[4] = { STR0, STR1, STR2, STR3 };
static uint8_t g_pattern[PATTERN_MAX];

static void mk_str(uint8_t *out, size_t cap, const char *a) {   /* ASCII → UTF-16LE string 디스크립터 */
    size_t n = strlen(a);
    if (2 + 2 * n > cap) { out[0] = 0; return; }
    out[0] = (uint8_t)(2 + 2 * n); out[1] = DT_STRING;
    for (size_t i = 0; i < n; i++) { out[2 + 2 * i] = (uint8_t)a[i]; out[3 + 2 * i] = 0; }
}
static void desc_init(void) {
    mk_str(STR1, sizeof STR1, "Don Lab");
    mk_str(STR2, sizeof STR2, "COMM-A01 CDC-ACM Virtual Serial");   /* 31 글자 → 64 B: mps 의 배수 → ZLP 시험대 */
    mk_str(STR3, sizeof STR3, "A01-0001");
    for (int i = 0; i < PATTERN_MAX; i++) g_pattern[i] = (uint8_t)(i * 7 + 3);
}

/* ---------------- 로그 (디버그 UART 대신 고정 버퍼) ---------------- */
static char g_log[24576]; static size_t g_log_len; static bool g_log_on, g_log_trunc;
static void logf_(const char *fmt, ...) {
    if (!g_log_on) return;
    va_list ap; va_start(ap, fmt);
    size_t cap = sizeof g_log - g_log_len;
    int k = vsnprintf(g_log + g_log_len, cap, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= cap) { g_log_trunc = true; g_log_len = sizeof g_log - 1; }
    else g_log_len += (size_t)k;
}

/* ---------------- 펌웨어가 쓰는 "하드웨어" API (주어짐) ---------------- */
static void hw_arm_in(usbd *d, const uint8_t *p, uint16_t n) {
    if (n) memcpy(d->hw.in_buf, p, n);
    d->hw.in_len = (uint8_t)n; d->hw.in_armed = true;
}
static void hw_arm_status_in(usbd *d) { d->hw.in_tog = 1; hw_arm_in(d, NULL, 0); }   /* status 는 항상 DATA1 */
static void hw_arm_out(usbd *d) { d->hw.out_armed = true; }
static void hw_arm_status_out(usbd *d) { d->hw.out_tog = 1; d->hw.out_armed = true; }
static void hw_disarm(usbd *d) { d->hw.in_armed = false; d->hw.out_armed = false; }
static void hw_stall_ep0(usbd *d) { d->hw.in_stall = d->hw.out_stall = true; hw_disarm(d); }
static void hw_set_address(usbd *d, uint8_t a) { logf_("      hw   DADDR %u -> %u\n", d->hw.addr, a); d->hw.addr = a; }

static void ep0_set_state(usbd *d, ep0_state s) {
    if (s == d->st) return;
    if (s == EP0_DATA_IN) logf_("      ep0  %s -> DATA_IN  (send %u B, zlp %s)\n", EP0_NAME[d->st], d->tx_left, d->tx_zlp ? "yes" : "no");
    else if (s == EP0_DATA_OUT) logf_("      ep0  %s -> DATA_OUT (expect %u B)\n", EP0_NAME[d->st], d->rx_expect);
    else logf_("      ep0  %s -> %s\n", EP0_NAME[d->st], EP0_NAME[s]);
    d->st = s;
}

/* 펌웨어 공용 헬퍼 (주어짐) */
static void ep0_stall(usbd *d) { hw_stall_ep0(d); d->n.stalls++; ep0_set_state(d, EP0_STALLED); }
static void ep0_status_only(usbd *d) { hw_arm_status_in(d); ep0_set_state(d, EP0_STATUS_IN); }   /* 데이터 단계 없음 */
static void ep0_recv(usbd *d, uint16_t n) { d->rx_len = 0; d->rx_expect = n; hw_arm_out(d); ep0_set_state(d, EP0_DATA_OUT); }
static void ep0_done(usbd *d) { hw_disarm(d); d->n.xfer_ok++; ep0_set_state(d, EP0_IDLE); }

static void lc_to_bytes(const line_coding *lc, uint8_t b[7]) {
    b[0] = (uint8_t)lc->rate; b[1] = (uint8_t)(lc->rate >> 8); b[2] = (uint8_t)(lc->rate >> 16); b[3] = (uint8_t)(lc->rate >> 24);
    b[4] = lc->stop; b[5] = lc->parity; b[6] = lc->bits;
}
static line_coding lc_from_bytes(const uint8_t b[7]) {
    line_coding lc = { (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24, b[4], b[5], b[6] };
    return lc;
}
static const line_coding LC_DEFAULT = { 115200, 0, 0, 8 };
static const char *lc_str(const line_coding *lc, char buf[32]) {   /* "9600 7E2" 형식 */
    static const char par[] = "NOEMS";
    snprintf(buf, 32, "%u %u%c%s", (unsigned)lc->rate, lc->bits, lc->parity < 5 ? par[lc->parity] : '?',
             lc->stop == 0 ? "1" : lc->stop == 1 ? "1.5" : "2");
    return buf;
}

static int get_descriptor(usbd *d, uint8_t type, uint8_t idx, const uint8_t **p, uint16_t *n) {
    switch (type) {
    case DT_DEVICE: if (idx) return -1; *p = d->dev_desc; *n = 18; return 0;
    case DT_CONFIG: if (idx) return -1; *p = CFG_DESC; *n = sizeof CFG_DESC; return 0;
    case DT_STRING: if (idx > 3) return -1; *p = STRS[idx]; *n = STRS[idx][0]; return 0;   /* LANGID(wIndex)는 무시 */
    default: return -1;   /* DEVICE_QUALIFIER / OTHER_SPEED: FS 전용 장치는 STALL 해야 한다 */
    }
}

/* ======================= TODO 영역: EP0 펌웨어 ======================= */
static void ep0_commit(usbd *d);
static bool line_coding_ok(const uint8_t b[7]);
static int std_request(usbd *d);
static int cdc_request(usbd *d);
static int vendor_request(usbd *d);

/* TODO A1: 다음 IN 패킷 하나를 무장. min(tx_left, mps) 바이트, 0 이면 그것이 ZLP (tx_zlp 를 내린다) */
static void ep0_arm_next_in(usbd *d) {
    uint16_t k = d->tx_left < d->hw.mps ? d->tx_left : d->hw.mps;
    if (k == 0) { d->tx_zlp = false; d->n.zlp_armed++; }  /* 지금 무장하는 것이 ZLP */
    hw_arm_in(d, d->tx, k);
    if (k) { d->tx += k; d->tx_left = (uint16_t)(d->tx_left - k); }
}

/* TODO A1: IN 데이터 단계 시작. 보낼 길이 = min(avail, wLength), ZLP 필요 = (길이 < wLength && 길이 % mps == 0).
 *          wLength == 0 이면 데이터 단계가 없다 → status IN. 호스트가 데이터를 덜 읽고 status 로 올 수 있게 OUT 도 미리 무장. */
static void ep0_send(usbd *d, const uint8_t *p, uint16_t avail) {
    if (d->wlength == 0) { ep0_status_only(d); return; }        /* 데이터 단계 없음 → status 는 IN */
    uint16_t n = avail < d->wlength ? avail : d->wlength;      /* 절대 wLength 를 넘지 않는다 */
    d->tx = p; d->tx_left = n;
    d->tx_zlp = n < d->wlength && n % d->hw.mps == 0;          /* 꽉 찬 마지막 패킷 + 짧은 응답 → ZLP 로 끝 표시 */
    hw_arm_status_out(d);                                      /* 조기 status(OUT ZLP, DATA1)를 언제든 받을 준비 */
    ep0_set_state(d, EP0_DATA_IN);
    ep0_arm_next_in(d);
}

/* TODO A2: 호스트가 IN 패킷을 ACK 했다 (SIE 가 부름).
 *          DATA_IN: 남은 데이터나 ZLP 가 있으면 다음 패킷, 없으면 STATUS_OUT 으로.
 *          STATUS_IN: status 완료 → ep0_commit → ep0_done.  그 밖: 무시. */
static void ep0_on_in_done(usbd *d) {
    if (d->st == EP0_DATA_IN) {
        if (d->tx_left > 0 || d->tx_zlp) ep0_arm_next_in(d);
        else ep0_set_state(d, EP0_STATUS_OUT);                 /* 데이터 다 ACK 됨 → 호스트의 OUT ZLP 대기 (이미 무장) */
    } else if (d->st == EP0_STATUS_IN) {
        ep0_commit(d);                                         /* 효과는 status ACK 뒤에만 */
        ep0_done(d);
    }
}

/* TODO A3: OUT 패킷 수신 (토글이 맞는 새 패킷만 온다).
 *          DATA_OUT: wLength 초과 → STALL, 누적, 짧은 패킷이거나 wLength 도달 → ep0_out_complete 검증
 *                    (실패 → STALL) → OUT 재무장(초과 데이터 감시) + status IN 무장 → STATUS_IN. 아니면 OUT 재무장.
 *          DATA_IN(조기 status, early_status++) / STATUS_OUT: 길이 0 이면 ep0_done, 아니면 STALL.
 *          그 밖(STATUS_IN 의 초과 데이터 등): STALL. */
static int ep0_out_complete(usbd *d);
static void ep0_on_out(usbd *d, const uint8_t *p, uint16_t n) {
    if (d->st == EP0_DATA_OUT) {
        if (n > d->rx_expect - d->rx_len) { ep0_stall(d); return; }          /* 호스트가 wLength 보다 많이 보냄 */
        if (n) memcpy(d->rx + d->rx_len, p, n);
        d->rx_len = (uint16_t)(d->rx_len + n);
        if (n < d->hw.mps || d->rx_len == d->rx_expect) {                    /* 데이터 단계 끝 */
            if (ep0_out_complete(d) < 0) { ep0_stall(d); return; }           /* 내용 불량 → status 단계에서 STALL */
            hw_arm_out(d);                                                   /* 초과 OUT 이 오면 아래 else 에서 STALL */
            hw_arm_status_in(d);
            ep0_set_state(d, EP0_STATUS_IN);
        } else {
            hw_arm_out(d);
        }
    } else if (d->st == EP0_DATA_IN || d->st == EP0_STATUS_OUT) {
        if (n != 0) { ep0_stall(d); return; }                                /* status 는 0바이트여야 한다 */
        if (d->st == EP0_DATA_IN) d->n.early_status++;                       /* 호스트가 데이터를 덜 읽고 끝냄 */
        ep0_done(d);                                                         /* 무장된 IN(다음 패킷)도 회수 */
    } else {
        ep0_stall(d);
    }
}

/* TODO A4: SETUP 수신 (SIE 가 이미 STALL 해제·NAK·토글 DATA1). 필드 디코드(LE16), 진행 중이던 전송 상태를
 *          버리고(IDLE/STALLED 가 아니었다면 aborts++), act = NONE, 타입별 핸들러 → 음수면 ep0_stall. */
static void ep0_on_setup(usbd *d) {
    const uint8_t *s = d->hw.setup;
    if (d->st != EP0_IDLE && d->st != EP0_STALLED) d->n.aborts++;   /* 새 SETUP 이 진행 중 전송을 끊는다 */
    d->n.setups++;
    d->bm = s[0]; d->breq = s[1];
    d->wvalue = le16(s + 2); d->windex = le16(s + 4); d->wlength = le16(s + 6);
    d->tx_left = 0; d->tx_zlp = false; d->rx_len = 0; d->rx_expect = 0;
    d->act = ACT_NONE; d->act_arg = 0;                              /* 끊긴 요청의 효과는 버린다 (SET_ADDRESS 포함) */
    ep0_set_state(d, EP0_IDLE);
    int r;
    switch (RT_TYPE(d->bm)) {
    case 0: r = std_request(d); break;
    case 1: r = cdc_request(d); break;
    case 2: r = vendor_request(d); break;
    default: r = -1; break;
    }
    if (r < 0) ep0_stall(d);
}

/* TODO B1: 표준 요청. 성공하면 ep0_send / ep0_status_only 로 단계를 시작하고 0, 거부는 -1(→ STALL).
 *   GET_DESCRIPTOR  bm 0x80, get_descriptor(wValue 상위=타입, 하위=index)
 *   SET_ADDRESS     bm 0x00, wValue ≤ 127, wIndex = wLength = 0, Configured 가 아님 → act = SET_ADDRESS, status only
 *   SET_CONFIGURATION bm 0x00, wLength 0, Default 상태 아님, wValue ∈ {0, 1} → act = SET_CONFIG, status only
 *   GET_CONFIGURATION bm 0x80 → config 1바이트
 *   GET_STATUS      bm 0x80(장치) / 0x81(IF 0..1, Configured) / 0x82(EP 0x00·0x80, Configured 면 0x01·0x81·0x82) → 00 00
 *   그 밖 (SET/CLEAR_FEATURE, SET_INTERFACE, ...) → -1 */
static int std_request_base(usbd *d) {
    switch (d->breq) {
    case REQ_GET_DESCRIPTOR: {
        const uint8_t *p; uint16_t n;
        if (d->bm != 0x80) return -1;
        if (get_descriptor(d, (uint8_t)(d->wvalue >> 8), (uint8_t)d->wvalue, &p, &n) < 0) return -1;
        ep0_send(d, p, n);
        return 0;
    }
    case REQ_SET_ADDRESS:
        if (d->bm != 0x00 || d->wvalue > 127 || d->windex || d->wlength || d->dstate == DEV_CONFIGURED) return -1;
        d->act = ACT_SET_ADDRESS; d->act_arg = d->wvalue;       /* 아직 바꾸지 않는다: status IN 은 옛 주소로 온다 */
        ep0_status_only(d);
        return 0;
    case REQ_SET_CONFIGURATION:
        if (d->bm != 0x00 || d->wlength || d->windex || d->dstate == DEV_DEFAULT || d->wvalue > 1) return -1;
        d->act = ACT_SET_CONFIG; d->act_arg = d->wvalue;
        ep0_status_only(d);
        return 0;
    case REQ_GET_CONFIGURATION:
        if (d->bm != 0x80) return -1;
        d->txbuf[0] = d->config;
        ep0_send(d, d->txbuf, 1);
        return 0;
    case REQ_GET_STATUS: {
        bool ok = false;
        uint16_t ep = d->windex;
        if (d->bm == 0x80) ok = true;                                            /* self-powered 0, remote wakeup 0 */
        else if (d->bm == 0x81) ok = d->dstate == DEV_CONFIGURED && d->windex < 2;
        else if (d->bm == 0x82) ok = ep == 0x00 || ep == 0x80 ||
                                     (d->dstate == DEV_CONFIGURED && (ep == 0x01 || ep == 0x81 || ep == 0x82));
        if (!ok) return -1;
        d->txbuf[0] = 0; d->txbuf[1] = 0;
        ep0_send(d, d->txbuf, 2);
        return 0;
    }
    default:
        return -1;
    }
}

/* TODO B2: status 완료 후 효과 적용. SET_ADDRESS → hw_set_address + dstate(0 이면 DEFAULT, 아니면 ADDRESS),
 *          SET_CONFIG → config + dstate(1 → CONFIGURED, 0 → ADDRESS), LINE_CODING → lc = rx,
 *          CTRL_LINES → lines = act_arg & 3, ENCAP → encap = rx. 적용했으면 commits++, act = NONE. */
static void ep0_commit(usbd *d) {
    switch (d->act) {
    case ACT_NONE: return;
    case ACT_SET_ADDRESS:
        hw_set_address(d, (uint8_t)d->act_arg);
        d->dstate = d->act_arg ? DEV_ADDRESS : DEV_DEFAULT;
        break;
    case ACT_SET_CONFIG:
        d->config = (uint8_t)d->act_arg;
        d->dstate = d->config ? DEV_CONFIGURED : DEV_ADDRESS;
        break;
    case ACT_LINE_CODING: d->lc = lc_from_bytes(d->rx); break;
    case ACT_CTRL_LINES:  d->lines = (uint8_t)(d->act_arg & 3); break;
    case ACT_ENCAP:       memcpy(d->encap, d->rx, d->rx_len); d->encap_len = d->rx_len; break;
    }
    d->n.commits++;
    d->act = ACT_NONE;
}

/* TODO C1: CDC-ACM 클래스 요청 (수신자 = 통신 IF 0, Configured 상태에서만).
 *   SET_LINE_CODING 0x21/0x20 wLength 7 → act = LINE_CODING, ep0_recv(7)
 *   GET_LINE_CODING 0xA1/0x21 → 현재 lc 7바이트
 *   SET_CONTROL_LINE_STATE 0x21/0x22 wLength 0 → act = CTRL_LINES(wValue), status only (예약 비트 15:2 는 무시)
 *   SEND_ENCAPSULATED_COMMAND 0x21/0x00 wLength 1..ENCAP_MAX → act = ENCAP, ep0_recv(wLength)
 *   GET_ENCAPSULATED_RESPONSE 0xA1/0x01 → encap 버퍼 (없으면 0바이트)
 *   그 밖 (SEND_BREAK 은 ACM caps 에 광고 안 함) → -1 */
static int cdc_request(usbd *d) {
    if (d->dstate != DEV_CONFIGURED || RT_RECIP(d->bm) != 1 || d->windex != CDC_COMM_IF) return -1;
    switch (d->breq) {
    case CDC_SET_LINE_CODING:
        if (d->bm != 0x21 || d->wlength != 7) return -1;
        d->act = ACT_LINE_CODING;
        ep0_recv(d, 7);
        return 0;
    case CDC_GET_LINE_CODING:
        if (d->bm != 0xA1) return -1;
        lc_to_bytes(&d->lc, d->txbuf);
        ep0_send(d, d->txbuf, 7);
        return 0;
    case CDC_SET_CONTROL_LINE_STATE:
        if (d->bm != 0x21 || d->wlength) return -1;
        d->act = ACT_CTRL_LINES; d->act_arg = d->wvalue;
        ep0_status_only(d);
        return 0;
    case CDC_SEND_ENCAPSULATED_COMMAND:
        if (d->bm != 0x21 || d->wlength == 0 || d->wlength > ENCAP_MAX) return -1;   /* 버퍼보다 크면 SETUP 에서 거절 */
        d->act = ACT_ENCAP;
        ep0_recv(d, d->wlength);
        return 0;
    case CDC_GET_ENCAPSULATED_RESPONSE:
        if (d->bm != 0xA1) return -1;
        ep0_send(d, d->encap, d->encap_len);
        return 0;
    default:
        return -1;
    }
}

/* TODO C2: 라인 코딩 검증 (PSTN 1.2 Table 17 을 이 모델의 정의로): dwDTERate ≠ 0, bCharFormat 0..2
 *          (1 / 1.5 / 2 stop), bParityType 0..4 (none/odd/even/mark/space), bDataBits ∈ {5,6,7,8,16} */
static bool line_coding_ok(const uint8_t b[7]) {
    uint32_t rate = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    uint8_t bits = b[6];
    return rate != 0 && b[4] <= 2 && b[5] <= 4 && ((bits >= 5 && bits <= 8) || bits == 16);
}
/* ======================= TODO 영역 끝 ======================= */

/* 데이터 OUT 단계 끝의 내용 검증 (주어짐) */
static int ep0_out_complete(usbd *d) {
    if (d->act == ACT_LINE_CODING) return (d->rx_len == 7 && line_coding_ok(d->rx)) ? 0 : -1;
    if (d->act == ACT_ENCAP) return 0;
    return -1;
}

static int vendor_request(usbd *d) {   /* 주어짐: 0xC0/0x01, wValue = 장치가 "가진" 바이트 수 */
    if (d->bm != 0xC0 || d->breq != VND_PATTERN || d->wvalue > PATTERN_MAX) return -1;
    ep0_send(d, g_pattern, d->wvalue);
    return 0;
}

/* ---------------- 장치 초기화 / 버스 리셋 (주어짐) ---------------- */
static void usbd_bus_reset(usbd *d) {
    logf_("RESET  (Default state, address 0)\n");
    d->hw.addr = 0; d->hw.in_stall = d->hw.out_stall = false; hw_disarm(d); d->hw.in_tog = d->hw.out_tog = 0;
    d->st = EP0_IDLE; d->dstate = DEV_DEFAULT; d->config = 0; d->lines = 0; d->act = ACT_NONE;
    d->lc = LC_DEFAULT; d->encap_len = 0; d->tx_left = 0; d->tx_zlp = false;
}
static void usbd_init(usbd *d, uint8_t mps) {
    memset(d, 0, sizeof *d);
    d->hw.mps = mps;
    memcpy(d->dev_desc, DEV_DESC_TMPL, 18);
    d->dev_desc[7] = mps;
    usbd_bus_reset(d);
}

/* ---------------- SIE: 호스트 트랜잭션 → 펌웨어 이벤트 (주어짐) ---------------- */
typedef enum { H_ACK, H_NAK, H_STALL, H_NORESP, H_DATA } handshake;

static handshake sie_setup(usbd *d, uint8_t addr, const uint8_t s[8]) {
    if (addr != d->hw.addr) return H_NORESP;
    memcpy(d->hw.setup, s, 8);
    d->hw.in_stall = d->hw.out_stall = false;    /* protocol STALL 은 다음 SETUP 에서 풀린다 */
    hw_disarm(d);                                /* 양방향 NAK */
    d->hw.in_tog = d->hw.out_tog = 1;            /* SETUP 이 DATA0 → 다음은 DATA1 */
    ep0_on_setup(d);
    return H_ACK;
}
/* ack_lost: 호스트가 보낸 ACK 가 장치에 도착하지 않음 → 장치는 같은 패킷·같은 토글을 계속 들고 있다 */
static handshake sie_in(usbd *d, uint8_t addr, uint8_t *buf, uint8_t *len, uint8_t *pid, bool ack_lost) {
    if (addr != d->hw.addr) return H_NORESP;
    if (d->hw.in_stall) { logf_("  IN   STALL\n"); return H_STALL; }
    if (!d->hw.in_armed) { logf_("  IN   NAK\n"); return H_NAK; }
    *len = d->hw.in_len; *pid = d->hw.in_tog;
    if (*len) memcpy(buf, d->hw.in_buf, *len);
    if (*len) logf_("  IN   DATA%u %2u B%s\n", *pid, *len, ack_lost ? "   (host ACK lost)" : "");
    else      logf_("  IN   DATA%u ZLP%s\n", *pid, ack_lost ? "   (host ACK lost)" : "");
    if (!ack_lost) { d->hw.in_armed = false; d->hw.in_tog ^= 1; ep0_on_in_done(d); }
    return H_DATA;
}
static handshake sie_out(usbd *d, uint8_t addr, const uint8_t *p, uint8_t len, uint8_t pid) {
    if (addr != d->hw.addr) return H_NORESP;
    if (d->hw.out_stall) { logf_("  OUT  DATA%u %2u B -> STALL\n", pid, len); return H_STALL; }
    if (pid != d->hw.out_tog) { d->n.dup_out++; logf_("  OUT  DATA%u %2u B -> ACK (duplicate, dropped)\n", pid, len); return H_ACK; }
    if (!d->hw.out_armed) { logf_("  OUT  DATA%u %2u B -> NAK\n", pid, len); return H_NAK; }
    if (len > d->hw.mps) return H_NORESP;        /* babble: 버퍼 초과 패킷 — 핸드셰이크 없음 */
    if (len) logf_("  OUT  DATA%u %2u B -> ACK\n", pid, len); else logf_("  OUT  DATA%u ZLP -> ACK\n", pid);
    d->hw.out_armed = false; d->hw.out_tog ^= 1;
    ep0_on_out(d, p, len);
    return H_ACK;
}

/* ---------------- 호스트 시뮬레이터 (주어짐) ---------------- */
typedef struct {
    int stop_in_after;       /* >0: IN 데이터 단계에서 이만큼 받고 status 로 (Windows 첫 GET_DESCRIPTOR 식 조기 종료) */
    int abandon_after;       /* >0: 데이터 패킷 이만큼 뒤 전송 포기 (status 없음 → 다음 SETUP 이 끊는다) */
    uint32_t in_ack_lost;    /* bit k: k번째 데이터 IN 트랜잭션에서 호스트 ACK 유실 */
    uint32_t out_ack_lost;   /* bit k: k번째 OUT 데이터 트랜잭션에서 장치 ACK 유실 → 같은 토글로 재전송 */
    uint16_t extra_out;      /* 호스트 버그: wLength 보다 이만큼 더 보냄 */
    bool status_ack_lost;    /* status IN ZLP 에 대한 호스트 ACK 유실 */
} xopt;
typedef struct { int rc, stage, pkts, zlps, dup_in, naks; uint16_t actual; } xres;
enum { X_OK = 0, X_STALL = -1, X_TIMEOUT = -2, X_BABBLE = -3, X_ABANDONED = -4 };
enum { STG_SETUP = 0, STG_DATA = 1, STG_STATUS = 2 };
#define NAK_LIMIT 16
static const char *x_name(int rc) {
    static const char *n[] = { "OK", "STALL", "TIMEOUT", "BABBLE", "ABANDONED" };
    return (rc <= 0 && rc >= -4) ? n[-rc] : "?";
}
static const char *const STG_NAME[] = { "setup", "data", "status" };

static const char *req_name(const uint8_t s[8]) {
    switch (RT_TYPE(s[0])) {
    case 0: switch (s[1]) {
        case REQ_GET_STATUS: return "GET_STATUS"; case REQ_CLEAR_FEATURE: return "CLEAR_FEATURE";
        case REQ_SET_FEATURE: return "SET_FEATURE"; case REQ_SET_ADDRESS: return "SET_ADDRESS";
        case REQ_GET_DESCRIPTOR:
            switch (s[3]) { case DT_DEVICE: return "GET_DESCRIPTOR(DEVICE)"; case DT_CONFIG: return "GET_DESCRIPTOR(CONFIG)";
                            case DT_STRING: return "GET_DESCRIPTOR(STRING)"; case DT_DEVICE_QUALIFIER: return "GET_DESCRIPTOR(DEVICE_QUALIFIER)";
                            default: return "GET_DESCRIPTOR(?)"; }
        case REQ_GET_CONFIGURATION: return "GET_CONFIGURATION"; case REQ_SET_CONFIGURATION: return "SET_CONFIGURATION";
        case REQ_GET_INTERFACE: return "GET_INTERFACE"; case REQ_SET_INTERFACE: return "SET_INTERFACE";
        default: return "STD(?)"; }
    case 1: switch (s[1]) {
        case CDC_SEND_ENCAPSULATED_COMMAND: return "SEND_ENCAPSULATED_COMMAND"; case CDC_GET_ENCAPSULATED_RESPONSE: return "GET_ENCAPSULATED_RESPONSE";
        case CDC_SET_LINE_CODING: return "SET_LINE_CODING"; case CDC_GET_LINE_CODING: return "GET_LINE_CODING";
        case CDC_SET_CONTROL_LINE_STATE: return "SET_CONTROL_LINE_STATE"; case CDC_SEND_BREAK: return "SEND_BREAK";
        default: return "CLASS(?)"; }
    case 2: return s[1] == VND_PATTERN ? "VENDOR_PATTERN" : "VENDOR(?)";
    default: return "RESERVED";
    }
}

static void mk_setup(uint8_t s[8], uint8_t bm, uint8_t req, uint16_t wv, uint16_t wi, uint16_t wl) {
    s[0] = bm; s[1] = req; s[2] = (uint8_t)wv; s[3] = (uint8_t)(wv >> 8);
    s[4] = (uint8_t)wi; s[5] = (uint8_t)(wi >> 8); s[6] = (uint8_t)wl; s[7] = (uint8_t)(wl >> 8);
}

/* 한 번의 control 전송. out = OUT 데이터(wLength + extra_out 바이트), in = IN 데이터 받을 곳(in_cap) */
static int host_xfer(usbd *d, uint8_t addr, const uint8_t s[8], const uint8_t *out, uint8_t *in, size_t in_cap,
                     const xopt *o, xres *r) {
    static const xopt none;
    if (!o) o = &none;
    memset(r, 0, sizeof *r);
    uint16_t wl = le16(s + 6);
    uint8_t mps = d->hw.mps, buf[EP0_MPS_MAX], len = 0, pid = 0;
    handshake h;
    uint8_t cfg0 = d->config, lines0 = d->lines; line_coding lc0 = d->lc;
    logf_("@%u SETUP %02X %02X %02X %02X %02X %02X %02X %02X  %s wValue=0x%04X wIndex=0x%04X wLength=%u\n", addr,
          s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], req_name(s), le16(s + 2), le16(s + 4), wl);
    if (sie_setup(d, addr, s) != H_ACK) { logf_("  SETUP no response\n"); r->rc = X_TIMEOUT; goto out; }
    r->stage = STG_DATA;
    if (wl > 0 && (s[0] & 0x80)) {                         /* ---- control read: IN 데이터 → OUT status ---- */
        uint8_t tog = 1; int k = 0;
        for (;;) {
            if (o->abandon_after && r->pkts >= o->abandon_after) { logf_("  (host abandons transfer)\n"); r->rc = X_ABANDONED; goto out; }
            if (o->stop_in_after && r->pkts >= o->stop_in_after) { logf_("  (host stops data stage early)\n"); break; }
            bool lost = k < 32 && ((o->in_ack_lost >> k) & 1u);
            int naks = 0;
            while ((h = sie_in(d, addr, buf, &len, &pid, lost)) == H_NAK)
                if (++naks >= NAK_LIMIT) { r->naks += naks; r->rc = X_TIMEOUT; goto out; }
            r->naks += naks;
            if (h == H_STALL) { r->rc = X_STALL; goto out; }
            if (h == H_NORESP) { r->rc = X_TIMEOUT; goto out; }
            k++;
            if (pid != tog) { r->dup_in++; continue; }        /* 이미 받은 패킷의 재전송 → 버리고 ACK */
            if (len > mps || r->actual + len > wl) { r->rc = X_BABBLE; goto out; }
            if (len && r->actual + len <= in_cap) memcpy(in + r->actual, buf, len);
            r->actual = (uint16_t)(r->actual + len); r->pkts++; tog ^= 1;
            if (len == 0) r->zlps++;
            if (len < mps || r->actual == wl) break;            /* 짧은 패킷 또는 wLength 도달 = 데이터 단계 끝 */
        }
        r->stage = STG_STATUS;
        int naks = 0;
        while ((h = sie_out(d, addr, buf, 0, 1)) == H_NAK)
            if (++naks >= NAK_LIMIT) { r->naks += naks; r->rc = X_TIMEOUT; goto out; }
        r->naks += naks;
        r->rc = h == H_ACK ? X_OK : h == H_STALL ? X_STALL : X_TIMEOUT;
        goto out;
    }
    if (wl > 0) {                                           /* ---- control write: OUT 데이터 → IN status ---- */
        uint32_t total = (uint32_t)wl + o->extra_out, sent = 0;
        uint8_t tog = 1; int k = 0;
        while (sent < total) {
            if (o->abandon_after && r->pkts >= o->abandon_after) { logf_("  (host abandons transfer)\n"); r->rc = X_ABANDONED; goto out; }
            uint8_t c = (uint8_t)(total - sent < mps ? total - sent : mps);
            bool lost = k < 32 && ((o->out_ack_lost >> k) & 1u);
            int naks = 0;
            while ((h = sie_out(d, addr, out + sent, c, tog)) == H_NAK)
                if (++naks >= NAK_LIMIT) { r->naks += naks; r->rc = X_TIMEOUT; goto out; }
            r->naks += naks;
            k++;
            if (h == H_STALL) { r->rc = X_STALL; goto out; }
            if (h == H_NORESP) { r->rc = X_TIMEOUT; goto out; }
            if (lost) { logf_("       (device ACK lost -> host resends DATA%u)\n", tog); continue; }
            sent += c; r->pkts++; tog ^= 1;
        }
        if (o->abandon_after && r->pkts >= o->abandon_after) { logf_("  (host abandons transfer)\n"); r->rc = X_ABANDONED; goto out; }
    }
    r->stage = STG_STATUS;                                  /* ---- status IN (데이터 없음 또는 control write) ---- */
    {
        int naks = 0;
        while ((h = sie_in(d, addr, buf, &len, &pid, o->status_ack_lost)) == H_NAK)
            if (++naks >= NAK_LIMIT) { r->naks += naks; r->rc = X_TIMEOUT; goto out; }
        r->naks += naks;
        if (h == H_STALL) r->rc = X_STALL;
        else if (h == H_NORESP) r->rc = X_TIMEOUT;
        else r->rc = (len == 0 && pid == 1) ? X_OK : X_BABBLE;
    }
out:
    if (r->rc == X_OK && wl == 0) logf_("  => OK  (no data stage)\n");
    else if (r->rc == X_OK && !(s[0] & 0x80)) logf_("  => OK  %u B OUT in %d pkt\n", wl, r->pkts);
    else if (r->rc == X_OK) logf_("  => OK  %u B IN in %d pkt%s\n", r->actual, r->pkts, r->zlps ? " (incl. ZLP)" : "");
    else logf_("  => %s at %s stage\n", x_name(r->rc), STG_NAME[r->stage]);
    if (d->config != cfg0) logf_("     dev  configuration %u -> %u\n", cfg0, d->config);
    if (d->lines != lines0) logf_("     dev  DTR %u RTS %u -> DTR %u RTS %u\n", lines0 & 1, lines0 >> 1, d->lines & 1, d->lines >> 1);
    if (d->lc.rate != lc0.rate || d->lc.stop != lc0.stop || d->lc.parity != lc0.parity || d->lc.bits != lc0.bits) {
        char a[32], b[32];
        logf_("     dev  line coding %s -> %s\n", lc_str(&lc0, a), lc_str(&d->lc, b));
    }
    return r->rc;
}


/* ======================= 추가 연습 ======================= */

/* ---- 연습 1: functional STALL (ENDPOINT_HALT) vs protocol STALL ----
 * bulk/interrupt EP 3개(0x01, 0x81, 0x82)의 halt 비트와 데이터 토글. SET_FEATURE(ENDPOINT_HALT) 로 halt,
 * CLEAR_FEATURE 로 해제 + 토글 DATA0. GET_STATUS(endpoint) bit0 = halt. EP0 의 halt 는 이 모델에서 지원 안 함(STALL).
 * 효과는 SETUP 단계에서 바로 적용 (TinyUSB·Linux gadget 도 대개 그렇다 — 본문의 commit 규칙으로 옮겨도 된다). */
#define FEAT_ENDPOINT_HALT 0
typedef struct { uint8_t addr; bool halt; uint8_t tog; } bulk_ep;
static bulk_ep g_eps[3] = { { 0x01, false, 0 }, { 0x81, false, 0 }, { 0x82, false, 0 } };
static bulk_ep *find_ep(uint16_t a) {
    for (int i = 0; i < 3; i++) if (g_eps[i].addr == a) return &g_eps[i];
    return NULL;
}
static int std_request(usbd *d) {
    if (d->bm == 0x02 && (d->breq == REQ_SET_FEATURE || d->breq == REQ_CLEAR_FEATURE)) {
        bulk_ep *e = find_ep(d->windex);
        if (d->wvalue != FEAT_ENDPOINT_HALT || d->wlength || d->dstate != DEV_CONFIGURED || !e) return -1;
        if (d->breq == REQ_SET_FEATURE) e->halt = true;
        else { e->halt = false; e->tog = 0; }      /* CLEAR_FEATURE(HALT) 는 halt 가 아니었어도 토글을 DATA0 으로 */
        ep0_status_only(d);
        return 0;
    }
    if (d->bm == 0x82 && d->breq == REQ_GET_STATUS) {
        bulk_ep *e = find_ep(d->windex);
        if (e && d->dstate == DEV_CONFIGURED) {
            d->txbuf[0] = e->halt ? 1 : 0; d->txbuf[1] = 0;
            ep0_send(d, d->txbuf, 2);
            return 0;
        }
    }
    return std_request_base(d);
}
/* bulk IN 토큰 하나 (데이터는 생략, 핸드셰이크와 토글만) */
static handshake bulk_in(usbd *d, uint8_t ep, uint8_t *pid) {
    bulk_ep *e = find_ep(ep);
    if (!e || d->dstate != DEV_CONFIGURED) return H_NORESP;
    if (e->halt) return H_STALL;
    *pid = e->tog; e->tog ^= 1;
    return H_DATA;
}

/* ---- 연습 2: stty 인자 → 라인 코딩 (Linux cdc-acm 의 termios 변환 규칙을 이 모델의 정의로) ----
 * 숫자 = baud, cs5..cs8, [-]parenb, [-]parodd, [-]cmspar, [-]cstopb. parity = PARENB ? (PARODD ? 1 : 2) + (CMSPAR ? 2 : 0) : 0.
 * 반환: 0 성공, 1 = baud 0 (hang-up: Linux 는 DTR 을 내리고 라인 코딩 rate 는 유지), -1 = 모르는 토큰/범위 밖. */
typedef struct { uint32_t baud; int csize; bool parenb, parodd, cmspar, cstopb; } termios_lite;
static termios_lite tl_from_lc(const line_coding *lc) {
    termios_lite t = { lc->rate, lc->bits, lc->parity != 0, lc->parity == 1 || lc->parity == 3, lc->parity >= 3, lc->stop == 2 };
    return t;
}
static line_coding lc_from_tl(const termios_lite *t) {
    line_coding lc = { t->baud, (uint8_t)(t->cstopb ? 2 : 0),
                       (uint8_t)(t->parenb ? (t->parodd ? 1 : 2) + (t->cmspar ? 2 : 0) : 0), (uint8_t)t->csize };
    return lc;
}
static int stty_apply(line_coding *lc, const char *args) {
    termios_lite t = tl_from_lc(lc);
    char tok[16];
    bool hangup = false;
    const char *p = args;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;
        size_t n = 0;
        while (p[n] && p[n] != ' ') n++;
        if (n >= sizeof tok) return -1;
        memcpy(tok, p, n); tok[n] = 0; p += n;
        bool neg = tok[0] == '-';
        const char *w = neg ? tok + 1 : tok;
        if (!neg && w[0] >= '0' && w[0] <= '9') {
            uint32_t v = 0;
            for (const char *q = w; *q; q++) {
                if (*q < '0' || *q > '9' || v > 400000000u) return -1;
                v = v * 10 + (uint32_t)(*q - '0');
            }
            if (v == 0) hangup = true; else t.baud = v;
        } else if (!neg && w[0] == 'c' && w[1] == 's' && w[2] >= '5' && w[2] <= '8' && !w[3]) t.csize = w[2] - '0';
        else if (!strcmp(w, "parenb")) t.parenb = !neg;
        else if (!strcmp(w, "parodd")) t.parodd = !neg;
        else if (!strcmp(w, "cmspar")) t.cmspar = !neg;
        else if (!strcmp(w, "cstopb")) t.cstopb = !neg;
        else return -1;
    }
    if (hangup) return 1;
    *lc = lc_from_tl(&t);
    return 0;
}

/* ---- 연습 3: SERIAL_STATE 알림 (interrupt IN EP 0x82) — 10 B, mps 8 이면 8 + 2 두 패킷 ---- */
enum { SS_DCD = 1, SS_DSR = 2, SS_BREAK = 4, SS_RING = 8, SS_FRAMING = 16, SS_PARITY = 32, SS_OVERRUN = 64 };
static size_t serial_state_build(uint8_t out[10], uint16_t iface, uint16_t bits) {
    uint8_t h[10] = { 0xA1, 0x20, 0x00, 0x00, (uint8_t)iface, (uint8_t)(iface >> 8), 0x02, 0x00,
                      (uint8_t)bits, (uint8_t)(bits >> 8) };
    memcpy(out, h, 10);
    return 10;
}
static uint16_t ep_mps_from_cfg(uint8_t ep) {   /* CFG_DESC 를 bLength 로 걸어 EP 의 wMaxPacketSize */
    for (size_t off = 0; off + 2 <= sizeof CFG_DESC && CFG_DESC[off] >= 2; off += CFG_DESC[off])
        if (CFG_DESC[off + 1] == DT_ENDPOINT && off + 7 <= sizeof CFG_DESC && CFG_DESC[off + 2] == ep)
            return (uint16_t)(le16(CFG_DESC + off + 4) & 0x7FF);
    return 0;
}
/* 호스트 쪽 재조립: 패킷들을 이어 붙여 10 B 가 되면 해석. 반환 = 비트맵, 형식 오류 -1 */
static int serial_state_reassemble(const uint8_t *const pk[], const uint8_t len[], int npk) {
    uint8_t buf[16]; size_t n = 0;
    for (int i = 0; i < npk; i++) {
        if (n + len[i] > sizeof buf) return -1;
        memcpy(buf + n, pk[i], len[i]); n += len[i];
    }
    if (n != 10 || buf[0] != 0xA1 || buf[1] != 0x20 || le16(buf + 6) != 2) return -1;
    return le16(buf + 8);
}

/* ---- 연습 4: control 전송 한 번의 트랜잭션 수 (닫힌 식) ---- */
static int ctrl_transactions(int avail, int wl, int mps) {
    int a = avail < wl ? avail : wl;
    int data = wl == 0 ? 0 : a < wl ? a / mps + 1 : (a + mps - 1) / mps;
    return 1 + data + 1;   /* SETUP + DATA 패킷 + STATUS */
}

/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static usbd g_d;
static uint8_t g_in[4096];
static int xfer(usbd *d, uint8_t addr, uint8_t bm, uint8_t req, uint16_t wv, uint16_t wi, uint16_t wl,
                const uint8_t *out, const xopt *o, xres *r) {
    uint8_t s[8]; mk_setup(s, bm, req, wv, wi, wl);
    return host_xfer(d, addr, s, out, g_in, sizeof g_in, o, r);
}
static bool bring_to_configured(usbd *d, uint8_t mps, uint8_t a) {
    xres r;
    usbd_init(d, mps);
    for (int i = 0; i < 3; i++) { g_eps[i].halt = false; g_eps[i].tog = 0; }
    if (xfer(d, 0, 0x00, REQ_SET_ADDRESS, a, 0, 0, NULL, NULL, &r) != X_OK) return false;
    return xfer(d, a, 0x00, REQ_SET_CONFIGURATION, 1, 0, 0, NULL, NULL, &r) == X_OK;
}

static void practice1(void) {
    xres r; uint8_t pid = 9;
    CHECK(bring_to_configured(&g_d, 64, 4));
    CHECK(bulk_in(&g_d, 0x81, &pid) == H_DATA && pid == 0 && bulk_in(&g_d, 0x81, &pid) == H_DATA && pid == 1);
    CHECK(xfer(&g_d, 4, 0x02, REQ_SET_FEATURE, FEAT_ENDPOINT_HALT, 0x81, 0, NULL, NULL, &r) == X_OK);
    CHECK(bulk_in(&g_d, 0x81, &pid) == H_STALL && bulk_in(&g_d, 0x81, &pid) == H_STALL);
    /* EP0 에 다른 요청이 오가도 functional STALL 은 남는다 (protocol STALL 과의 차이) */
    CHECK(xfer(&g_d, 4, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18, NULL, NULL, &r) == X_OK);
    CHECK(bulk_in(&g_d, 0x81, &pid) == H_STALL && bulk_in(&g_d, 0x01, &pid) == H_DATA);
    CHECK(xfer(&g_d, 4, 0x82, REQ_GET_STATUS, 0, 0x81, 2, NULL, NULL, &r) == X_OK && r.actual == 2 && g_in[0] == 1 && g_in[1] == 0);
    CHECK(xfer(&g_d, 4, 0x02, REQ_CLEAR_FEATURE, FEAT_ENDPOINT_HALT, 0x81, 0, NULL, NULL, &r) == X_OK);
    CHECK(xfer(&g_d, 4, 0x82, REQ_GET_STATUS, 0, 0x81, 2, NULL, NULL, &r) == X_OK && g_in[0] == 0);
    CHECK(bulk_in(&g_d, 0x81, &pid) == H_DATA && pid == 0);                       /* 토글 DATA0 으로 리셋 */
    CHECK(bulk_in(&g_d, 0x01, &pid) == H_DATA && pid == 1);                       /* 0x01 은 영향 없음 */
    CHECK(xfer(&g_d, 4, 0x02, REQ_CLEAR_FEATURE, FEAT_ENDPOINT_HALT, 0x01, 0, NULL, NULL, &r) == X_OK);
    CHECK(bulk_in(&g_d, 0x01, &pid) == H_DATA && pid == 0);                       /* halt 아니어도 토글 리셋 */
    CHECK(xfer(&g_d, 4, 0x02, REQ_SET_FEATURE, FEAT_ENDPOINT_HALT, 0x00, 0, NULL, NULL, &r) == X_STALL);   /* EP0 */
    CHECK(xfer(&g_d, 4, 0x02, REQ_SET_FEATURE, FEAT_ENDPOINT_HALT, 0x83, 0, NULL, NULL, &r) == X_STALL);   /* 없는 EP */
    CHECK(xfer(&g_d, 4, 0x02, REQ_SET_FEATURE, 1, 0x81, 0, NULL, NULL, &r) == X_STALL);                    /* 다른 feature */
    CHECK(xfer(&g_d, 4, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18, NULL, NULL, &r) == X_OK);        /* EP0 는 회복 */
    usbd_init(&g_d, 64);
    CHECK(xfer(&g_d, 0, 0x00, REQ_SET_ADDRESS, 4, 0, 0, NULL, NULL, &r) == X_OK);
    CHECK(xfer(&g_d, 4, 0x02, REQ_SET_FEATURE, FEAT_ENDPOINT_HALT, 0x81, 0, NULL, NULL, &r) == X_STALL);   /* 구성 전 */
}

static void practice2(void) {
    line_coding lc = LC_DEFAULT; uint8_t b[7]; char s[32]; xres r;
    static const uint8_t want[7] = { 0x80, 0x25, 0x00, 0x00, 2, 2, 7 };
    CHECK(stty_apply(&lc, "9600 cs7 parenb -parodd cstopb") == 0);
    lc_to_bytes(&lc, b);
    CHECK(memcmp(b, want, 7) == 0 && strcmp(lc_str(&lc, s), "9600 7E2") == 0);
    CHECK(stty_apply(&lc, "parodd cmspar") == 0 && lc.parity == 3 && strcmp(lc_str(&lc, s), "9600 7M2") == 0);
    CHECK(stty_apply(&lc, "-parodd") == 0 && lc.parity == 4);
    CHECK(stty_apply(&lc, "-parenb -cstopb cs8 115200") == 0 && strcmp(lc_str(&lc, s), "115200 8N1") == 0);
    CHECK(stty_apply(&lc, "0") == 1 && lc.rate == 115200);                       /* B0 = hang-up */
    CHECK(stty_apply(&lc, "cs9") == -1 && stty_apply(&lc, "crtscts") == -1 && stty_apply(&lc, "96OO") == -1);
    CHECK(stty_apply(&lc, "99999999999") == -1 && lc.rate == 115200);
    printf("practice 2: \"9600 cs7 parenb -parodd cstopb\" -> %02X %02X %02X %02X %02X %02X %02X\n",
           want[0], want[1], want[2], want[3], want[4], want[5], want[6]);
    /* 랜덤 stty → SET_LINE_CODING → GET_LINE_CODING 왕복 */
    static const char *const tok[] = { "parenb", "-parenb", "parodd", "-parodd", "cmspar", "-cmspar", "cstopb", "-cstopb",
                                       "cs5", "cs6", "cs7", "cs8", "1200", "9600", "57600", "115200", "921600", "3000000" };
    uint32_t seed = 0x5771u; long bad = 0;
    CHECK(bring_to_configured(&g_d, 8, 2));
    for (int t = 0; t < 2000; t++) {
        char cmd[128] = ""; size_t len = 0;
        int k = 1 + (int)(xs32(&seed) % 5);
        for (int i = 0; i < k; i++) {
            int w = snprintf(cmd + len, sizeof cmd - len, "%s ", tok[xs32(&seed) % 18]);
            if (w > 0) len += (size_t)w;
        }
        line_coding want_lc = g_d.lc;
        if (stty_apply(&want_lc, cmd) != 0) { bad++; continue; }
        lc_to_bytes(&want_lc, b);
        if (xfer(&g_d, 2, 0x21, CDC_SET_LINE_CODING, 0, CDC_COMM_IF, 7, b, NULL, &r) != X_OK ||
            xfer(&g_d, 2, 0xA1, CDC_GET_LINE_CODING, 0, CDC_COMM_IF, 7, NULL, NULL, &r) != X_OK ||
            memcmp(g_in, b, 7) != 0) bad++;
    }
    CHECK(bad == 0);
}

static void practice3(void) {
    uint8_t note[10];
    uint16_t mps = ep_mps_from_cfg(0x82);
    CHECK(mps == 8 && ep_mps_from_cfg(0x81) == 64 && ep_mps_from_cfg(0x05) == 0);
    CHECK(serial_state_build(note, CDC_COMM_IF, SS_DCD | SS_DSR) == 10);
    CHECK(note[0] == 0xA1 && note[1] == 0x20 && note[6] == 2 && note[8] == 0x03 && note[9] == 0);
    int bad = 0, npk_seen = 0;
    for (uint16_t bits = 0; bits < 128; bits++) {
        serial_state_build(note, CDC_COMM_IF, bits);
        const uint8_t *pk[4]; uint8_t len[4]; int npk = 0;
        for (size_t off = 0; off < 10; off += mps) { pk[npk] = note + off; len[npk] = (uint8_t)(10 - off < mps ? 10 - off : mps); npk++; }
        npk_seen = npk;
        if (serial_state_reassemble(pk, len, npk) != bits) bad++;
        if (serial_state_reassemble(pk, len, npk - 1) != -1) bad++;   /* 첫 패킷만 보고 해석하면 안 된다 */
    }
    CHECK(bad == 0 && npk_seen == 2);
    printf("practice 3: SERIAL_STATE 10 B over EP 0x82 (wMaxPacketSize %u) -> %d packets (8 + 2)\n", mps, npk_seen);
}

static void practice4(void) {
    long bad = 0; xres r;
    for (int mi = 0; mi < 4; mi++) {
        uint8_t mps = (uint8_t)(8 << mi);
        usbd_init(&g_d, mps);
        for (int n = 0; n <= 2 * mps; n++)
            for (int wl = 0; wl <= 2 * mps + 1; wl++) {
                if (xfer(&g_d, 0, 0xC0, VND_PATTERN, (uint16_t)n, 0, (uint16_t)wl, NULL, NULL, &r) != X_OK) { bad++; continue; }
                if (1 + r.pkts + 1 != ctrl_transactions(n, wl, mps)) bad++;
            }
    }
    CHECK(bad == 0);
    /* bring-up 요청 목록 (본문 Part C 와 같은 순서; 첫 GET_DESCRIPTOR 는 첫 패킷만) */
    static const int req[][2] = { { 18, 18 }, { 0, 0 }, { 18, 18 }, { 9, 9 }, { 67, 67 }, { 4, 255 },
                                  { 16, 255 }, { 64, 255 }, { 18, 255 }, { 0, 0 }, { 0, 0 }, { 7, 7 }, { 7, 7 }, { 7, 7 }, { 0, 0 } };
    int tot[2] = { 0, 0 };
    for (int mi = 0; mi < 2; mi++) {
        int mps = mi ? 64 : 8;
        for (size_t i = 0; i < sizeof req / sizeof req[0]; i++)
            tot[mi] += i == 0 ? 3 : ctrl_transactions(req[i][0], req[i][1], mps);   /* 조기 종료: SETUP + 1 + status */
    }
    printf("practice 4: bring-up control transactions (excl. STALLed qualifier) mps 8 = %d, mps 64 = %d\n", tot[0], tot[1]);
    /* 손 계산: 첫 읽기 3 + 나머지 14개 × (SETUP+STATUS) 28 + 데이터 패킷 (mps 64: 12, mps 8: 33) */
    CHECK(tot[1] == 3 + 28 + 12 && tot[0] == 3 + 28 + 33);
}

int main(void) {
    desc_init();
    practice1();
    practice2();
    practice3();
    practice4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
