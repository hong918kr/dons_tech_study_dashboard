/* COMM-A01 demo_bugs.c — EP0 펌웨어의 흔한 버그 4종이 "호스트 눈에" 얼마나 자주 보이는가 (실측)
 *
 * solution.c 의 모델(SIE + EP0 펌웨어 + 호스트 시뮬레이터)을 그대로 복사하고 g_bug 스위치만 넣었다 (자체 완결).
 * 각 버그 × EP0 mps(8, 64) × 세 가지 호스트 조건:
 *   clean   : 벤더 패턴 요청, 실제 길이 0..3·mps × wLength 1..3·mps+1, 255, 0xFFFF 전수
 *   ackloss : 같은 행렬, 호스트 ACK 를 트랜잭션마다 5% 확률로 잃음 (xorshift32 고정 시드)
 *   win1st  : Windows 식 첫 GET_DESCRIPTOR(DEVICE, 64) 를 첫 패킷만 읽고 status
 * 집계: host-fail = 호스트가 OK 가 아니거나 데이터가 틀림, dev-anom = 호스트는 OK 인데 장치가 조기 status 로 끝냄.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BUG_NONE, BUG_NO_ZLP, BUG_ZLP_LE, BUG_EMPTY_NO_ZLP, BUG_LATE_STATUS_OUT, BUG_N };
static int g_bug;   /* demo 전용 스위치: 펌웨어에 흔한 버그 하나를 켠다 */

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
    if (g_bug == BUG_NO_ZLP) d->tx_zlp = false;                                  /* ZLP 를 아예 모름 */
    if (g_bug == BUG_ZLP_LE) d->tx_zlp = n <= d->wlength && n % d->hw.mps == 0;  /* wLength 를 꽉 채워도 ZLP */
    if (g_bug != BUG_LATE_STATUS_OUT) hw_arm_status_out(d);                                      /* 조기 status(OUT ZLP, DATA1)를 언제든 받을 준비 */
    if (g_bug == BUG_EMPTY_NO_ZLP && n == 0) { ep0_set_state(d, EP0_STATUS_OUT); return; }   /* "보낼 게 없네" → 데이터 ZLP 생략 */
    ep0_set_state(d, EP0_DATA_IN);
    ep0_arm_next_in(d);
}

/* TODO A2: 호스트가 IN 패킷을 ACK 했다 (SIE 가 부름).
 *          DATA_IN: 남은 데이터나 ZLP 가 있으면 다음 패킷, 없으면 STATUS_OUT 으로.
 *          STATUS_IN: status 완료 → ep0_commit → ep0_done.  그 밖: 무시. */
static void ep0_on_in_done(usbd *d) {
    if (d->st == EP0_DATA_IN) {
        if (d->tx_left > 0 || d->tx_zlp) ep0_arm_next_in(d);
        else {
            if (g_bug == BUG_LATE_STATUS_OUT) hw_arm_status_out(d);   /* 데이터를 다 보낸 뒤에야 status OUT 준비 */
            ep0_set_state(d, EP0_STATUS_OUT);
        }
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
static int std_request(usbd *d) {
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


static usbd g_d;
static uint8_t g_in[4096];
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { long combos, host_fail, timeouts, dev_anom; } tally;

static void run_matrix(uint8_t mps, bool loss, tally *t) {
    uint32_t seed = 0xD3B0u + mps;
    memset(t, 0, sizeof *t);
    for (int n = 0; n <= 3 * mps; n++)
        for (int wi = 1; wi <= 3 * mps + 3; wi++) {
            int wl = wi <= 3 * mps + 1 ? wi : wi == 3 * mps + 2 ? 255 : 0xFFFF;
            xopt o = { 0 };
            if (loss) for (int k = 0; k < 32; k++) if (xs32(&seed) % 100 < 5) o.in_ack_lost |= 1u << k;
            uint8_t s[8]; xres r;
            usbd_init(&g_d, mps);
            mk_setup(s, 0xC0, VND_PATTERN, (uint16_t)n, 0, (uint16_t)wl);
            int rc = host_xfer(&g_d, 0, s, NULL, g_in, sizeof g_in, &o, &r);
            int want = n < wl ? n : wl;
            int T = r.pkts + r.dup_in;
            int last_lost = T >= 1 && T <= 32 && ((o.in_ack_lost >> (T - 1)) & 1u);
            t->combos++;
            if (rc != X_OK || r.actual != want || memcmp(g_in, g_pattern, (size_t)want) != 0) {
                t->host_fail++; if (rc == X_TIMEOUT) t->timeouts++;
            } else if ((int)g_d.n.early_status != last_lost) t->dev_anom++;
        }
}

int main(void) {
    static const char *const BUG_NAME[BUG_N] = { "correct", "no ZLP at all", "ZLP also when n == wLength",
                                                 "0-byte reply: no data ZLP", "status-OUT armed late" };
    desc_init();
    printf("%-31s %4s | %-26s | %-26s | %s\n", "firmware", "mps", "clean: host-fail / dev-anom", "5% ACK loss: fail / anom", "win 1st read");
    for (int b = 0; b < BUG_N; b++) {
        g_bug = b;
        for (int mi = 0; mi < 2; mi++) {
            uint8_t mps = mi ? 64 : 8;
            tally c, l; xres r; uint8_t s[8];
            run_matrix(mps, false, &c);
            run_matrix(mps, true, &l);
            xopt early = { .stop_in_after = 1 };
            usbd_init(&g_d, mps);
            mk_setup(s, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 64);
            int rc = host_xfer(&g_d, 0, s, NULL, g_in, sizeof g_in, &early, &r);
            printf("%-31s %4u | %5ld /%5ld  of %5ld    | %5ld /%5ld  of %5ld    | %s\n", BUG_NAME[b], mps,
                   c.host_fail, c.dev_anom, c.combos, l.host_fail, l.dev_anom, l.combos, x_name(rc));
        }
    }
    return 0;
}
