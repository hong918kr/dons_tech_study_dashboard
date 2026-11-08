/* SIV-B12 테스트 플랜 작성: Feature → Test Case → Pass Criteria
 *
 * 가상의 I2C 컨트롤러 IP "XI2C" (APB, 7-bit 주소, 8-entry TX/RX FIFO, SCL LCNT/HCNT 카운터)를 mock 으로
 * 만들고, md 의 테스트 플랜 표 중 3개를 실제 코드로 구현한다.
 *   TC-01 (P0, 정상+경계) 데이터 경로: 7바이트 write 두 번 → 길이 1 / 7 / 8(RX FIFO full) read-back 비교
 *   TC-02 (P0, 에러)      주소 NACK: NACK+DONE 보고, BUSY 해제, TX FIFO flush, 그 뒤 정상 전송으로 복구
 *   TC-03 (P0, 경계)      SCL 타이밍: Sm/Fm/Fm+ 로직 분석기 캡처 → ① UM10204 ② 제품 요구 ③ 설정값과 비교
 * mock 에는 결함(fault)을 비트마스크로 주입한다. 좋은 mock 은 모두 PASS, 결함마다 맞는 TC 가 FAIL 해야 한다.
 * 실보드에서는 bus ops(rd/wr)만 MMIO 로, la_capture 만 로직 분석기 export 로 바꾼다 (SIV-B13 참고).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- XI2C 레지스터 맵 (1페이지 스펙 요약은 md §1) ---------------- */
#define XI2C_CTRL    0x00u  /* RW  [0] EN                                                  */
#define XI2C_STATUS  0x04u  /* [0] BUSY (RO), [1] DONE (W1C), [2] NACK (W1C)               */
#define XI2C_TAR     0x08u  /* RW  [6:0] 타깃 주소                                         */
#define XI2C_CMD     0x0Cu  /* WO  [7:0] LEN(1..8), [8] READ — 쓰는 순간 전송 시작         */
#define XI2C_TXDATA  0x10u  /* WO  TX FIFO push                                            */
#define XI2C_RXDATA  0x14u  /* RO  RX FIFO pop (읽기 부작용)                               */
#define XI2C_FIFOLVL 0x18u  /* RO  [3:0] TXLVL, [11:8] RXLVL                               */
#define XI2C_SCLCNT  0x1Cu  /* RW  [15:0] LCNT, [31:16] HCNT; tLOW=(LCNT+1)*tPCLK, tHIGH 동일 */
#define XI2C_ID      0x20u  /* RO  0x12C00102                                              */

#define CTRL_EN      (1u << 0)
#define ST_BUSY      (1u << 0)
#define ST_DONE      (1u << 1)
#define ST_NACK      (1u << 2)
#define CMD_READ     (1u << 8)
#define FIFO_DEPTH   8u
#define PCLK_NS      20u                       /* APB 50 MHz */
#define SCLCNT_RESET ((249u << 16) | 249u)     /* 리셋값 = Sm 100 kHz 설정 */
#define EEP_ADDR     0x50u                     /* 검증 보드의 24C02 류 타깃 */
#define ABSENT_ADDR  0x51u                     /* 보드에 없는 주소 */

/* ---------------- 결함 주입 (fault injection) ---------------- */
#define F_RXFIFO_FULL_DROP (1u << 0) /* RX FIFO 가 가득 차는 8번째 바이트를 버림 (레벨 카운터 off-by-one) */
#define F_NACK_IGNORED     (1u << 1) /* 주소 NACK 을 ACK 로 취급 — 없는 장치에 '성공'                    */
#define F_HANG_AFTER_NACK  (1u << 2) /* NACK 뒤 STOP 을 못 만들고 BUSY 가 영원히 1                       */
#define F_TX_NOT_FLUSHED   (1u << 3) /* NACK abort 때 TX FIFO 를 안 비움 → 다음 전송에 찌꺼기            */
#define F_HCNT_STUCK       (1u << 4) /* SCLCNT.HCNT 는 읽으면 맞지만 SCL 생성기는 리셋값을 씀 (미배선) */
#define F_LCNT_OFF_BY_ONE  (1u << 5) /* tLOW = LCNT*tPCLK (스펙은 LCNT+1) — 카운터 비교 off-by-one      */

/* ---------------- mock IP (주어진 코드 — 고치지 않는다) ---------------- */
typedef struct { uint32_t t_ns; int level; } edge_t;   /* 로직 분석기(LA) 캡처: SCL 에지 */
#define LA_MAX 40u

static struct {
    uint32_t faults, ctrl, status, tar, sclcnt;
    uint8_t  tx[FIFO_DEPTH]; unsigned txn;
    uint8_t  rx[FIFO_DEPTH]; unsigned rxn, rxh;
    unsigned busy_left; uint32_t pend; bool hang;
    uint8_t  eep[256]; uint8_t ptr;                    /* 타깃 0x50: 포인터 자동 증가 메모리 */
    edge_t   la[LA_MAX]; unsigned la_n;
    unsigned nacc;                                     /* 버스 접근 횟수 */
} D;

static void mock_reset(uint32_t faults) {
    memset(&D, 0, sizeof D);
    D.faults = faults;
    D.sclcnt = SCLCNT_RESET;
}
static void la_record(void) {                          /* 주소 바이트 + ACK = SCL 9클럭 */
    uint32_t lcnt = D.sclcnt & 0xFFFFu;
    uint32_t hcnt = (D.faults & F_HCNT_STUCK) ? (SCLCNT_RESET >> 16) : (D.sclcnt >> 16);
    uint32_t tl = ((D.faults & F_LCNT_OFF_BY_ONE) ? lcnt : lcnt + 1u) * PCLK_NS;
    uint32_t th = (hcnt + 1u) * PCLK_NS, t = 1000u;
    D.la_n = 0;
    for (int k = 0; k < 9; k++) {
        D.la[D.la_n++] = (edge_t){ t, 0 }; t += tl;
        D.la[D.la_n++] = (edge_t){ t, 1 }; t += th;
    }
    D.la[D.la_n++] = (edge_t){ t, 0 };
}
static void mock_start(uint32_t cmd) {
    unsigned len = cmd & 0xFFu, addr = D.tar & 0x7Fu;
    bool rd = (cmd & CMD_READ) != 0;
    if (!(D.ctrl & CTRL_EN) || (D.status & ST_BUSY) || len == 0 || len > FIFO_DEPTH) return;
    la_record();
    D.status |= ST_BUSY; D.busy_left = 3; D.pend = ST_DONE;
    bool ack = addr == EEP_ADDR || (D.faults & F_NACK_IGNORED);
    if (!ack) {                                        /* 주소 NACK → STOP, abort */
        D.pend = ST_DONE | ST_NACK;
        if (!(D.faults & F_TX_NOT_FLUSHED)) D.txn = 0;
        if (D.faults & F_HANG_AFTER_NACK) { D.hang = true; D.status |= ST_NACK; }
        return;
    }
    if (!rd) {
        unsigned n = len < D.txn ? len : D.txn;
        for (unsigned i = 0; i < n && addr == EEP_ADDR; i++) {
            if (i == 0) D.ptr = D.tx[0]; else D.eep[D.ptr++] = D.tx[i];
        }
        memmove(D.tx, D.tx + n, D.txn - n); D.txn -= n;
    } else {
        for (unsigned i = 0; i < len; i++) {
            uint8_t b = addr == EEP_ADDR ? D.eep[D.ptr++] : 0xFFu;   /* 없는 장치 = 풀업 → 0xFF */
            if ((D.faults & F_RXFIFO_FULL_DROP) && D.rxn - D.rxh == FIFO_DEPTH - 1u) continue;
            if (D.rxn < FIFO_DEPTH) D.rx[D.rxn++] = b;
        }
    }
}
static uint32_t mock_rd(uint32_t off) {
    D.nacc++;
    switch (off) {
    case XI2C_CTRL:   return D.ctrl;
    case XI2C_STATUS: {
        uint32_t v = D.status;
        if ((D.status & ST_BUSY) && !D.hang && --D.busy_left == 0)
            D.status = (D.status & ~ST_BUSY) | D.pend;
        return v;
    }
    case XI2C_TAR:    return D.tar;
    case XI2C_RXDATA: {
        if (D.rxh >= D.rxn) return 0;                  /* underflow: 0 */
        uint32_t v = D.rx[D.rxh++];
        if (D.rxh == D.rxn) D.rxh = D.rxn = 0;
        return v;
    }
    case XI2C_FIFOLVL: return D.txn | ((D.rxn - D.rxh) << 8);
    case XI2C_SCLCNT:  return D.sclcnt;
    case XI2C_ID:      return 0x12C00102u;
    default:           return 0;                       /* WO 레지스터·빈 주소는 0 */
    }
}
static void mock_wr(uint32_t off, uint32_t v) {
    D.nacc++;
    switch (off) {
    case XI2C_CTRL:   D.ctrl = v & CTRL_EN; break;
    case XI2C_STATUS: D.status &= ~(v & (ST_DONE | ST_NACK)); break;      /* W1C */
    case XI2C_TAR:    D.tar = v & 0x7Fu; break;
    case XI2C_CMD:    mock_start(v); break;
    case XI2C_TXDATA: if (D.txn < FIFO_DEPTH) D.tx[D.txn++] = (uint8_t)v; break;
    case XI2C_SCLCNT: D.sclcnt = v; break;
    default: break;                                    /* RO / 빈 주소: 무시 */
    }
}
static unsigned la_capture(edge_t *out, unsigned max) {   /* 실보드: LA 의 CSV export */
    unsigned n = D.la_n < max ? D.la_n : max;
    memcpy(out, D.la, n * sizeof *out);
    return n;
}

/* ---------------- 접근 계층 (SIV-B02 의 bus ops 를 축약) ---------------- */
typedef struct { uint32_t (*rd)(uint32_t off); void (*wr)(uint32_t off, uint32_t v); } bus_ops_t;
static const bus_ops_t MOCK_BUS = { mock_rd, mock_wr };
static const bus_ops_t *g_bus = &MOCK_BUS;            /* 실보드: *(volatile uint32_t *)(BASE + off) */
static uint32_t reg_read32(uint32_t off)              { return g_bus->rd(off); }
static void     reg_write32(uint32_t off, uint32_t v) { g_bus->wr(off, v); }

/* ---------------- 실패 메시지: 첫 실패 하나만 남긴다 ---------------- */
static char g_why[200];
static void why(const char *fmt, ...) {
    if (g_why[0]) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_why, sizeof g_why, fmt, ap);
    va_end(ap);
}

/* ---------------- 테스트 펌웨어의 최소 드라이버 ---------------- */
typedef enum { XFER_OK, XFER_NACK, XFER_TIMEOUT, XFER_BADARG } xfer_t;
static const char *xfer_name(xfer_t r) {
    static const char *n[] = { "OK", "NACK", "TIMEOUT", "BADARG" };
    return n[r];
}
#define POLL_MAX 64u
static uint32_t g_last_st;   /* 마지막으로 읽은 STATUS (타임아웃·에러 리포트용) */
static unsigned g_rxlvl;     /* read 전송에서 drain 직전의 RXLVL */
static void xi2c_init(uint32_t sclcnt) {
    reg_write32(XI2C_CTRL, CTRL_EN);
    reg_write32(XI2C_SCLCNT, sclcnt);
}

/* A1: 전송 하나. len 1..FIFO_DEPTH 가 아니면 버스 접근 없이 BADARG.
 *     ① 이전 W1C 상태 정리 ② TAR ③ (write 면) TX FIFO 채우기 ④ CMD ⑤ STATUS 를 최대 POLL_MAX 번 폴링 —
 *     "BUSY=0 이고 DONE=1" 이 완료다 ⑥ 본 DONE/NACK 만 W1C 로 ack ⑦ NACK 이면 XFER_NACK
 *     ⑧ (read 면) RXLVL 을 g_rxlvl 에 기록하고 len 바이트를 RXDATA 에서 꺼낸다. */
static xfer_t xi2c_xfer(uint8_t addr, bool rd, uint8_t *buf, unsigned len) {
    if (len == 0 || len > FIFO_DEPTH) return XFER_BADARG;
    reg_write32(XI2C_STATUS, ST_DONE | ST_NACK);
    reg_write32(XI2C_TAR, addr);
    if (!rd)
        for (unsigned i = 0; i < len; i++) reg_write32(XI2C_TXDATA, buf[i]);
    reg_write32(XI2C_CMD, len | (rd ? CMD_READ : 0u));
    unsigned n;
    for (n = 0; n < POLL_MAX; n++) {
        g_last_st = reg_read32(XI2C_STATUS);
        if (!(g_last_st & ST_BUSY) && (g_last_st & ST_DONE)) break;
    }
    if (n == POLL_MAX) return XFER_TIMEOUT;
    reg_write32(XI2C_STATUS, g_last_st & (ST_DONE | ST_NACK));
    if (g_last_st & ST_NACK) return XFER_NACK;
    if (rd) {
        g_rxlvl = (reg_read32(XI2C_FIFOLVL) >> 8) & 0xFu;
        for (unsigned i = 0; i < len; i++) buf[i] = (uint8_t)reg_read32(XI2C_RXDATA);
    }
    return XFER_OK;
}

/* ---------------- TC-01: 데이터 경로 (정상 + FIFO 경계) ---------------- */
static uint8_t pat(unsigned i) { return (uint8_t)(0x5Au + 37u * i); }

/* A2: ① 오프셋 0x20, 0x27 에 7바이트씩 write (오프셋 1 + 데이터 7 = TX FIFO 가득)
 *     ② 길이 1, FIFO_DEPTH-1, FIFO_DEPTH 각각: 포인터를 0x20 으로 (1바이트 write) → len 바이트 read → pat(i) 비교
 *     ③ 매 read 뒤 STATUS == 0, FIFOLVL == 0
 *     실패 메시지 예: "TC-01 rd len=8 idx=7 exp=0x5D act=0x00 (RXLVL=7)" */
static bool tc01_data_path(void) {
    xi2c_init(SCLCNT_RESET);
    for (unsigned c = 0; c < 2; c++) {
        uint8_t w[FIFO_DEPTH];
        w[0] = (uint8_t)(0x20u + 7u * c);
        for (unsigned i = 0; i < 7; i++) w[1 + i] = pat(7u * c + i);
        xfer_t r = xi2c_xfer(EEP_ADDR, false, w, FIFO_DEPTH);
        if (r != XFER_OK) {
            why("TC-01 wr chunk=%u xfer=%s STATUS=0x%08X", c, xfer_name(r), (unsigned)g_last_st);
            return false;
        }
    }
    static const unsigned lens[] = { 1u, FIFO_DEPTH - 1u, FIFO_DEPTH };
    for (unsigned k = 0; k < 3; k++) {
        unsigned len = lens[k];
        uint8_t p = 0x20, b[FIFO_DEPTH] = { 0 };
        xfer_t r = xi2c_xfer(EEP_ADDR, false, &p, 1);
        if (r == XFER_OK) r = xi2c_xfer(EEP_ADDR, true, b, len);
        if (r != XFER_OK) {
            why("TC-01 rd len=%u xfer=%s STATUS=0x%08X", len, xfer_name(r), (unsigned)g_last_st);
            return false;
        }
        for (unsigned i = 0; i < len; i++)
            if (b[i] != pat(i)) {
                why("TC-01 rd len=%u idx=%u exp=0x%02X act=0x%02X (RXLVL=%u)",
                    len, i, pat(i), b[i], g_rxlvl);
                return false;
            }
        uint32_t st = reg_read32(XI2C_STATUS), lvl = reg_read32(XI2C_FIFOLVL);
        if (st != 0 || lvl != 0) {
            why("TC-01 rd len=%u after: STATUS=0x%08X FIFOLVL=0x%08X exp 0/0", len,
                (unsigned)st, (unsigned)lvl);
            return false;
        }
    }
    return true;
}

/* ---------------- TC-02: 주소 NACK 과 복구 (에러) ---------------- */
/* B1: ① 없는 주소 0x51 에 2바이트 {0x30,0xEE} write → 반드시 XFER_NACK
 *        (TIMEOUT 이면 "timeout" + 마지막 STATUS, OK 면 "exp NACK" 을 메시지에)
 *     ② ack 뒤 STATUS == 0, TXLVL == 0 (찌꺼기가 다음 전송에 섞이면 안 된다)
 *     ③ 복구: 0x50 에 {0x30,0xC3} write → 포인터 0x30 → 1바이트 read == 0xC3 */
static bool tc02_nack_recovery(void) {
    xi2c_init(SCLCNT_RESET);
    uint8_t w[2] = { 0x30, 0xEE };
    xfer_t r = xi2c_xfer(ABSENT_ADDR, false, w, 2);
    if (r == XFER_TIMEOUT) {
        why("TC-02 tar=0x%02X timeout after %u polls, last STATUS=0x%08X (BUSY stuck)",
            ABSENT_ADDR, POLL_MAX, (unsigned)g_last_st);
        return false;
    }
    if (r != XFER_NACK) {
        why("TC-02 tar=0x%02X exp NACK act=%s STATUS=0x%08X (bit2 NACK=0)",
            ABSENT_ADDR, xfer_name(r), (unsigned)g_last_st);
        return false;
    }
    uint32_t st = reg_read32(XI2C_STATUS), lvl = reg_read32(XI2C_FIFOLVL);
    if (st != 0) { why("TC-02 after ack STATUS=0x%08X exp=0x00000000", (unsigned)st); return false; }
    if (lvl & 0xFu) {
        why("TC-02 after NACK TXLVL=%u exp=0 (TX FIFO not flushed)", (unsigned)(lvl & 0xFu));
        return false;
    }
    uint8_t w2[2] = { 0x30, 0xC3 }, p = 0x30, b = 0;
    if (xi2c_xfer(EEP_ADDR, false, w2, 2) != XFER_OK || xi2c_xfer(EEP_ADDR, false, &p, 1) != XFER_OK ||
        xi2c_xfer(EEP_ADDR, true, &b, 1) != XFER_OK) {
        why("TC-02 recovery xfer failed STATUS=0x%08X", (unsigned)g_last_st);
        return false;
    }
    if (b != 0xC3) { why("TC-02 recovery off=0x30 exp=0xC3 act=0x%02X", b); return false; }
    return true;
}

/* ---------------- TC-03: SCL 타이밍 (경계) ---------------- */
typedef struct {
    const char *name;
    uint32_t f_nom_hz, f_max_hz, tlow_min_ns, thigh_min_ns;   /* UM10204 표 */
    uint32_t lcnt, hcnt;                                       /* IP 프로그래밍 가이드 권장값 */
} scl_mode_t;
static const scl_mode_t MODES[] = {
    { "Sm",   100000u,  100000u, 4700u, 4000u, 249u, 249u },   /* 5000 + 5000 ns */
    { "Fm",   400000u,  400000u, 1300u,  600u,  69u,  54u },   /* 1400 + 1100 ns */
    { "Fm+", 1000000u, 1000000u,  500u,  260u,  27u,  21u },   /*  560 +  440 ns */
};
typedef struct { uint32_t tlow_min, tlow_max, thigh_min, thigh_max, f_hz; } scl_meas_t;
#define TOL_NS 10u                                             /* LA 100 MS/s 샘플 1개 */

/* B2: 에지 배열에서 LOW 구간(0→1), HIGH 구간(1→0) 길이의 min/max 와 fSCL 을 구한다.
 *     fSCL = (하강 에지 수 - 1) * 1e9 / (마지막 하강 - 첫 하강). 레벨이 번갈아 나오지 않거나(글리치),
 *     n < 3 이거나, 하강 에지가 2개 미만이면 false. */
static bool scl_measure(const edge_t *e, unsigned n, scl_meas_t *m) {
    if (n < 3) return false;
    *m = (scl_meas_t){ UINT32_MAX, 0, UINT32_MAX, 0, 0 };
    unsigned falls = 0; uint32_t first = 0, last = 0;
    for (unsigned i = 0; i < n; i++) {
        if (i + 1 < n) {
            if (e[i].level == e[i + 1].level) return false;
            uint32_t d = e[i + 1].t_ns - e[i].t_ns;
            if (e[i].level == 0) {
                if (d < m->tlow_min) m->tlow_min = d;
                if (d > m->tlow_max) m->tlow_max = d;
            } else {
                if (d < m->thigh_min) m->thigh_min = d;
                if (d > m->thigh_max) m->thigh_max = d;
            }
        }
        if (e[i].level == 0) { if (falls++ == 0) first = e[i].t_ns; last = e[i].t_ns; }
    }
    if (falls < 2 || last == first || m->tlow_max == 0 || m->thigh_max == 0) return false;
    m->f_hz = (uint32_t)((uint64_t)(falls - 1u) * 1000000000u / (last - first));
    return true;
}
static uint32_t absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

/* B3: 모드마다 SCLCNT = HCNT<<16 | LCNT 로 init → 0x50 에 1바이트 write → la_capture → scl_measure.
 *     판정 순서: rule=UM10204 (tLOW/tHIGH min, f ≤ fmax) → rule=REQ (f ≥ 90% nominal)
 *               → rule=PROG (tLOW/tHIGH 의 min·max 가 (CNT+1)*PCLK_NS 와 TOL_NS 이내)
 *     메시지: "TC-03 mode=Fm rule=REQ f=156250Hz tLOW=1400/1400ns tHIGH=5000/1100ns (meas/prog)" */
static bool tc03_scl_timing(void) {
    for (unsigned k = 0; k < sizeof MODES / sizeof MODES[0]; k++) {
        const scl_mode_t *md = &MODES[k];
        xi2c_init((md->hcnt << 16) | md->lcnt);
        uint8_t p = 0x00;
        xfer_t r = xi2c_xfer(EEP_ADDR, false, &p, 1);
        if (r != XFER_OK) { why("TC-03 mode=%s xfer=%s", md->name, xfer_name(r)); return false; }
        edge_t e[LA_MAX]; scl_meas_t s;
        unsigned n = la_capture(e, LA_MAX);
        if (!scl_measure(e, n, &s)) { why("TC-03 mode=%s bad capture n=%u", md->name, n); return false; }
        uint32_t pl = (md->lcnt + 1u) * PCLK_NS, ph = (md->hcnt + 1u) * PCLK_NS;
        const char *rule = NULL;
        if (s.tlow_min < md->tlow_min_ns || s.thigh_min < md->thigh_min_ns || s.f_hz > md->f_max_hz)
            rule = "UM10204";
        else if (s.f_hz < md->f_nom_hz / 10u * 9u)
            rule = "REQ";
        else if (absdiff(s.tlow_min, pl) > TOL_NS || absdiff(s.tlow_max, pl) > TOL_NS ||
                 absdiff(s.thigh_min, ph) > TOL_NS || absdiff(s.thigh_max, ph) > TOL_NS)
            rule = "PROG";
        if (rule) {
            why("TC-03 mode=%s rule=%s f=%uHz tLOW=%u/%uns tHIGH=%u/%uns (meas/prog)", md->name, rule,
                (unsigned)s.f_hz, (unsigned)s.tlow_min, (unsigned)pl, (unsigned)s.thigh_min, (unsigned)ph);
            return false;
        }
    }
    return true;
}

/* ---------------- Part C: 플랜 = 데이터, 실행기 ---------------- */
typedef enum { CAT_NORMAL, CAT_BOUNDARY, CAT_ERROR, CAT_STRESS } cat_t;
typedef bool (*tc_fn)(void);
typedef struct { const char *id, *feature; cat_t cat; int prio; const char *pass; tc_fn fn; } tc_t;
static const tc_t PLAN[] = {   /* md §5 의 플랜 표와 같은 ID. fn == NULL 은 아직 구현 안 됨 */
    { "TC-01", "F1 data/F2 FIFO", CAT_BOUNDARY, 0, "readback==written len 1,7,8; STATUS=0 FIFOLVL=0", tc01_data_path },
    { "TC-02", "F3 NACK",         CAT_ERROR,    0, "NACK+DONE<=64 polls; TXLVL=0; next xfer OK",       tc02_nack_recovery },
    { "TC-03", "F4 SCL timing",   CAT_BOUNDARY, 0, "UM10204 min/max; f>=90% nom; |meas-prog|<=10ns",  tc03_scl_timing },
    { "TC-04", "F3 NACK",         CAT_ERROR,    0, "data NACK at byte k: abort, NACK, TXLVL=0",        NULL },
    { "TC-05", "F5 stretching",   CAT_NORMAL,   0, "target holds SCL 1ms: data intact, no timeout",    NULL },
    { "TC-06", "F7 interrupt",    CAT_NORMAL,   0, "IRQ on DONE/NACK, masked when IE=0, W1C clears",   NULL },
    { "TC-07", "F9 registers",    CAT_NORMAL,   0, "reset values + RW per access type (B03/B04)",      NULL },
    { "TC-08", "F6 arbitration",  CAT_ERROR,    1, "2nd master wins: ARBL=1, no bus hang",             NULL },
    { "TC-09", "F8 bus clear",    CAT_ERROR,    1, "SDA stuck low: 9 SCL pulses free the bus",         NULL },
    { "TC-10", "F4 spike filter", CAT_BOUNDARY, 1, "40ns glitch ignored, data intact",                 NULL },
    { "TC-11", "F1 data",         CAT_STRESS,   1, "1e6 random xfers vs scoreboard, 0 mismatch",       NULL },
    { "TC-12", "F4 SCL timing",   CAT_STRESS,   1, "TC-01/03 pass at Vmin/Vmax x -40/25/125C",         NULL },
};
#define PLAN_N (sizeof PLAN / sizeof PLAN[0])
#define PLAN_MAX 16u
typedef enum { TR_NOT_RUN, TR_PASS, TR_FAIL } tr_t;
typedef struct {
    unsigned run, pass, fail, not_run;
    uint32_t fail_mask;            /* bit i = PLAN[i] FAIL */
    bool     blocking;             /* P0 FAIL 이 하나라도 있으면 true */
    tr_t     st[PLAN_MAX];
    char     why[PLAN_MAX][200];
} plan_result_t;

/* C1: plan 의 각 항목: fn 이 없으면 NOT_RUN. 있으면 mock_reset(faults) (테스트 격리), g_why 비우고 실행,
 *     결과·메시지 기록, fail_mask / blocking 갱신. r 은 먼저 0 으로 초기화한다. */
static void run_plan(const tc_t *plan, size_t n, uint32_t faults, plan_result_t *r) {
    memset(r, 0, sizeof *r);
    for (size_t i = 0; i < n && i < PLAN_MAX; i++) {
        if (!plan[i].fn) { r->st[i] = TR_NOT_RUN; r->not_run++; continue; }
        mock_reset(faults);
        g_why[0] = '\0';
        r->run++;
        if (plan[i].fn()) { r->st[i] = TR_PASS; r->pass++; continue; }
        r->st[i] = TR_FAIL; r->fail++;
        r->fail_mask |= 1u << i;
        if (plan[i].prio == 0) r->blocking = true;
        snprintf(r->why[i], sizeof r->why[i], "%s", g_why);
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define WHY_HAS(s) (strstr(g_why, (s)) != NULL)

static bool run_one(tc_fn fn, uint32_t faults) { mock_reset(faults); g_why[0] = '\0'; return fn(); }

int main(void) {
    /* ---- Part A: 드라이버 ---- */
    mock_reset(0); xi2c_init(SCLCNT_RESET);
    uint8_t w[2] = { 0x10, 0xAB }, p = 0x10, b = 0;
    CHECK(xi2c_xfer(EEP_ADDR, false, w, 2) == XFER_OK);
    CHECK(D.eep[0x10] == 0xAB && D.txn == 0);
    CHECK(reg_read32(XI2C_STATUS) == 0);                           /* DONE 은 드라이버가 W1C 로 ack */
    CHECK(xi2c_xfer(EEP_ADDR, false, &p, 1) == XFER_OK && xi2c_xfer(EEP_ADDR, true, &b, 1) == XFER_OK);
    CHECK(b == 0xAB && g_rxlvl == 1);
    unsigned acc = D.nacc;
    CHECK(xi2c_xfer(EEP_ADDR, false, w, 0) == XFER_BADARG);
    CHECK(xi2c_xfer(EEP_ADDR, true, w, FIFO_DEPTH + 1) == XFER_BADARG);
    CHECK(D.nacc == acc);                                          /* 잘못된 인자는 버스 접근 0 */
    mock_reset(0);                                                 /* EN=0: CMD 가 무시된다 */
    CHECK(xi2c_xfer(EEP_ADDR, true, &b, 1) == XFER_TIMEOUT);       /* "BUSY=0" 만 보면 OK 로 착각 */
    CHECK(g_last_st == 0);

    /* ---- Part A: TC-01 ---- */
    CHECK(run_one(tc01_data_path, 0) && g_why[0] == '\0');
    CHECK(D.eep[0x2D] == pat(13) && D.eep[0x2E] == 0);            /* 14바이트만 쓰였다 */
    CHECK(!run_one(tc01_data_path, F_RXFIFO_FULL_DROP));
    CHECK(WHY_HAS("TC-01 rd len=8 idx=7 exp=0x5D act=0x00 (RXLVL=7)"));
    CHECK(run_one(tc01_data_path, F_HCNT_STUCK));                  /* 데이터 경로는 타이밍 결함을 못 본다 */

    /* ---- Part B: TC-02 ---- */
    CHECK(run_one(tc02_nack_recovery, 0) && g_why[0] == '\0');
    CHECK(!run_one(tc02_nack_recovery, F_NACK_IGNORED));
    CHECK(WHY_HAS("tar=0x51 exp NACK act=OK STATUS=0x00000002"));
    CHECK(!run_one(tc02_nack_recovery, F_HANG_AFTER_NACK));
    CHECK(WHY_HAS("timeout after 64 polls, last STATUS=0x00000005"));
    CHECK(!run_one(tc02_nack_recovery, F_TX_NOT_FLUSHED));
    CHECK(WHY_HAS("TXLVL=2 exp=0"));

    /* ---- Part B: scl_measure ---- */
    scl_meas_t s;
    const edge_t e1[] = { { 0, 0 }, { 1400, 1 }, { 2500, 0 }, { 3900, 1 }, { 5000, 0 } };
    CHECK(scl_measure(e1, 5, &s) && s.tlow_min == 1400 && s.thigh_max == 1100 && s.f_hz == 400000u);
    const edge_t e2[] = { { 0, 0 }, { 500, 1 }, { 900, 1 }, { 1000, 0 } };     /* 1→1: 글리치/누락 */
    CHECK(!scl_measure(e2, 4, &s));
    CHECK(!scl_measure(e1, 2, &s));

    /* ---- Part B: TC-03 ---- */
    CHECK(run_one(tc03_scl_timing, 0) && g_why[0] == '\0');
    mock_reset(F_HCNT_STUCK); reg_write32(XI2C_SCLCNT, 0x00360045u);
    CHECK(reg_read32(XI2C_SCLCNT) == 0x00360045u);                 /* RW 테스트(SIV-B04)는 통과한다 */
    CHECK(!run_one(tc03_scl_timing, F_HCNT_STUCK));
    CHECK(WHY_HAS("mode=Fm rule=REQ f=156250Hz tLOW=1400/1400ns tHIGH=5000/1100ns"));
    CHECK(!run_one(tc03_scl_timing, F_LCNT_OFF_BY_ONE));
    CHECK(WHY_HAS("mode=Sm rule=UM10204 f=100200Hz tLOW=4980/5000ns"));

    /* ---- Part C: 결함 캠페인 — 결함마다 정확히 한 TC 가 잡아야 한다 ---- */
    static const struct { const char *name; uint32_t f, exp_mask; unsigned idx; const char *sub; } CAMP[] = {
        { "none",             0,                  0,       0, ""                    },
        { "RXFIFO_FULL_DROP", F_RXFIFO_FULL_DROP, 1u << 0, 0, "len=8 idx=7"         },
        { "NACK_IGNORED",     F_NACK_IGNORED,     1u << 1, 1, "exp NACK act=OK"     },
        { "HANG_AFTER_NACK",  F_HANG_AFTER_NACK,  1u << 1, 1, "timeout"             },
        { "TX_NOT_FLUSHED",   F_TX_NOT_FLUSHED,   1u << 1, 1, "TXLVL=2"             },
        { "HCNT_STUCK",       F_HCNT_STUCK,       1u << 2, 2, "mode=Fm rule=REQ"    },
        { "LCNT_OFF_BY_ONE",  F_LCNT_OFF_BY_ONE,  1u << 2, 2, "mode=Sm rule=UM10204" },
    };
    static plan_result_t R[sizeof CAMP / sizeof CAMP[0]];
    for (unsigned c = 0; c < sizeof CAMP / sizeof CAMP[0]; c++) {
        run_plan(PLAN, PLAN_N, CAMP[c].f, &R[c]);
        CHECK(R[c].run == 3 && R[c].not_run == PLAN_N - 3);
        CHECK(R[c].fail_mask == CAMP[c].exp_mask);
        CHECK(R[c].blocking == (CAMP[c].exp_mask != 0));
        CHECK(strstr(R[c].why[CAMP[c].idx], CAMP[c].sub) != NULL);
    }
    printf("\n%-17s TC-01 TC-02 TC-03  verdict\n", "fault");
    for (unsigned c = 0; c < sizeof CAMP / sizeof CAMP[0]; c++) {
        printf("%-17s", CAMP[c].name);
        for (unsigned i = 0; i < 3; i++) printf(" %-5s", R[c].st[i] == TR_PASS ? "pass" : R[c].st[i] == TR_FAIL ? "FAIL" : "-");
        printf("  %s\n", R[c].blocking ? "BLOCK" : "ok");
    }
    for (unsigned c = 1; c < sizeof CAMP / sizeof CAMP[0]; c++)
        for (unsigned i = 0; i < 3; i++)
            if (R[c].st[i] == TR_FAIL) printf("[%s] FAIL %s\n", CAMP[c].name, R[c].why[i]);
    printf("plan: %u tests, %u run, %u not run\n\n", (unsigned)PLAN_N, R[0].run, R[0].not_run);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
