/* SIV-B12 추가 연습 — 한 번에 검증
 *   연습 1: 마진 있는 SCL 설정에서는 LCNT off-by-one 을 rule=PROG 만 잡는다 (허용오차 TOL 의 의미)
 *   연습 2: 우선순위 게이팅 — plan_verdict(): BLOCK / INCOMPLETE / WARN / PASS
 *   연습 3: 플랜 lint — ID 중복, 측정 불가능한 pass 기준, 빠진 분류(정상/경계/에러/스트레스)
 *   연습 4: 랜덤 스트레스 + 스코어보드 (TC-11 축소판) — 시드로 재현되는 실패 리포트
 * mock·드라이버·scl_measure 는 solution.c 와 같다 (자기완결을 위해 복사).
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

/* ================= 연습 1: 허용오차와 rule=PROG ================= */
/* 마진을 둔 Fm 설정: 380 kHz 목표, tLOW 1500 / tHIGH 1140 ns — UM10204 한계에서 멀다 */
static const scl_mode_t MODES_MARGIN[] = {
    { "Fm380", 380000u, 400000u, 1300u, 600u, 74u, 56u },
};
/* modes 를 차례로 측정해 처음 어긋난 rule 이름을 돌려준다 (모두 통과면 NULL). *mode_out 에 모드 이름. */
static const char *tc03_rule(const scl_mode_t *modes, unsigned nm, uint32_t tol_ns, const char **mode_out) {
    for (unsigned k = 0; k < nm; k++) {
        const scl_mode_t *md = &modes[k];
        *mode_out = md->name;
        xi2c_init((md->hcnt << 16) | md->lcnt);
        uint8_t p = 0;
        if (xi2c_xfer(EEP_ADDR, false, &p, 1) != XFER_OK) return "XFER";
        edge_t e[LA_MAX]; scl_meas_t s;
        if (!scl_measure(e, la_capture(e, LA_MAX), &s)) return "CAPTURE";
        uint32_t pl = (md->lcnt + 1u) * PCLK_NS, ph = (md->hcnt + 1u) * PCLK_NS;
        if (s.tlow_min < md->tlow_min_ns || s.thigh_min < md->thigh_min_ns || s.f_hz > md->f_max_hz)
            return "UM10204";
        if (s.f_hz < md->f_nom_hz / 10u * 9u) return "REQ";
        if (absdiff(s.tlow_min, pl) > tol_ns || absdiff(s.tlow_max, pl) > tol_ns ||
            absdiff(s.thigh_min, ph) > tol_ns || absdiff(s.thigh_max, ph) > tol_ns)
            return "PROG";
    }
    return NULL;
}

/* ================= 연습 2·3: 플랜 데이터, 게이팅, lint ================= */
typedef enum { CAT_NORMAL, CAT_BOUNDARY, CAT_ERROR, CAT_STRESS } cat_t;
typedef struct { const char *id, *feature; cat_t cat; int prio; const char *pass; } tc_t;
typedef enum { TR_NOT_RUN, TR_PASS, TR_FAIL } tr_t;
typedef enum { V_PASS, V_WARN, V_INCOMPLETE, V_BLOCK } verdict_t;

/* 강한 쪽이 이긴다: P0 FAIL → BLOCK, P0 NOT_RUN → INCOMPLETE, P1 FAIL → WARN, 그 외 PASS */
static verdict_t plan_verdict(const tc_t *plan, const tr_t *st, size_t n) {
    verdict_t v = V_PASS;
    for (size_t i = 0; i < n; i++) {
        verdict_t x = V_PASS;
        if (plan[i].prio == 0 && st[i] == TR_FAIL) x = V_BLOCK;
        else if (plan[i].prio == 0 && st[i] == TR_NOT_RUN) x = V_INCOMPLETE;
        else if (st[i] == TR_FAIL) x = V_WARN;
        if (x > v) v = x;
    }
    return v;
}

/* pass 기준이 "측정 가능" 한가: 숫자나 비교 기호(= < >)가 하나라도 있어야 한다 (거친 휴리스틱) */
static bool measurable(const char *s) {
    if (!s) return false;
    for (; *s; s++)
        if ((*s >= '0' && *s <= '9') || *s == '=' || *s == '<' || *s == '>') return true;
    return false;
}
static bool plan_lint(const tc_t *plan, size_t n, char *msg, size_t msz) {
    unsigned cats = 0;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < i; j++)
            if (strcmp(plan[i].id, plan[j].id) == 0) { snprintf(msg, msz, "dup id %s", plan[i].id); return false; }
        if (!measurable(plan[i].pass)) {
            snprintf(msg, msz, "%s pass criteria not measurable: \"%s\"", plan[i].id, plan[i].pass ? plan[i].pass : "");
            return false;
        }
        if (plan[i].prio != 0 && plan[i].prio != 1) { snprintf(msg, msz, "%s prio=%d", plan[i].id, plan[i].prio); return false; }
        cats |= 1u << plan[i].cat;
    }
    static const char *cn[] = { "NORMAL", "BOUNDARY", "ERROR", "STRESS" };
    for (unsigned c = 0; c < 4; c++)
        if (!(cats & (1u << c))) { snprintf(msg, msz, "no %s test in plan", cn[c]); return false; }
    msg[0] = '\0';
    return true;
}

/* ================= 연습 4: 랜덤 스트레스 + 스코어보드 ================= */
static uint32_t xs32(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* 첫 불일치의 iteration 번호 (없으면 -1). 메시지에 seed 를 넣어 그대로 재현할 수 있게 한다. */
static int stress(uint32_t seed, unsigned iters, uint32_t faults, char *msg, size_t msz) {
    mock_reset(faults); xi2c_init(SCLCNT_RESET);
    uint8_t ref[256];
    for (unsigned a = 0; a < 256; a += 7) {             /* 배경 패턴: 0 이 결함을 가리지 않게 */
        uint8_t w[FIFO_DEPTH]; w[0] = (uint8_t)a;
        for (unsigned i = 0; i < 7; i++) { w[1 + i] = (uint8_t)(0xA5u ^ (a + i)); ref[(uint8_t)(a + i)] = w[1 + i]; }
        if (xi2c_xfer(EEP_ADDR, false, w, FIFO_DEPTH) != XFER_OK) { snprintf(msg, msz, "background wr failed"); return 0; }
    }
    uint32_t s = seed;
    for (unsigned it = 0; it < iters; it++) {
        uint32_t r = xs32(&s);
        uint8_t off = (uint8_t)(r >> 16), buf[FIFO_DEPTH];
        xfer_t x;
        if (r & 1u) {                                   /* write: 오프셋 + 데이터 1..7 */
            unsigned n = 1u + (r >> 8) % (FIFO_DEPTH - 1u);
            buf[0] = off;
            for (unsigned i = 0; i < n; i++) { buf[1 + i] = (uint8_t)xs32(&s); ref[(uint8_t)(off + i)] = buf[1 + i]; }
            x = xi2c_xfer(EEP_ADDR, false, buf, n + 1u);
        } else {                                        /* read: 1..8 */
            unsigned n = 1u + (r >> 8) % FIFO_DEPTH;
            x = xi2c_xfer(EEP_ADDR, false, &off, 1);
            if (x == XFER_OK) x = xi2c_xfer(EEP_ADDR, true, buf, n);
            for (unsigned i = 0; x == XFER_OK && i < n; i++)
                if (buf[i] != ref[(uint8_t)(off + i)]) {
                    snprintf(msg, msz, "stress seed=0x%08X iter=%u rd off=0x%02X len=%u idx=%u exp=0x%02X act=0x%02X",
                             (unsigned)seed, it, off, n, i, ref[(uint8_t)(off + i)], buf[i]);
                    return (int)it;
                }
        }
        if (x != XFER_OK) {
            snprintf(msg, msz, "stress seed=0x%08X iter=%u xfer=%s", (unsigned)seed, it, xfer_name(x));
            return (int)it;
        }
    }
    msg[0] = '\0';
    return -1;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    const char *mode = NULL, *rule;
    /* ---- 연습 1 ---- */
    mock_reset(0);
    CHECK(tc03_rule(MODES, 3, 10, &mode) == NULL);
    CHECK(tc03_rule(MODES_MARGIN, 1, 10, &mode) == NULL);
    mock_reset(F_LCNT_OFF_BY_ONE);
    rule = tc03_rule(MODES, 3, 10, &mode);
    CHECK(rule && strcmp(rule, "UM10204") == 0 && strcmp(mode, "Sm") == 0);   /* 마진 0: 스펙이 잡는다 */
    rule = tc03_rule(MODES_MARGIN, 1, 10, &mode);
    CHECK(rule && strcmp(rule, "PROG") == 0);                                   /* 마진: PROG 만 잡는다 */
    CHECK(tc03_rule(MODES_MARGIN, 1, 20, &mode) == NULL);                       /* TOL ≥ 결함 크기면 놓친다 */
    mock_reset(F_HCNT_STUCK);
    rule = tc03_rule(MODES_MARGIN, 1, 10, &mode);
    CHECK(rule && strcmp(rule, "REQ") == 0);
    printf("ex1: off-by-one on Fm380 -> rule=PROG (tol 10ns), missed with tol 20ns\n");

    /* ---- 연습 2 ---- */
    static const tc_t P[] = {
        { "TC-01", "F1", CAT_BOUNDARY, 0, "len 1,7,8 readback==written" },
        { "TC-02", "F3", CAT_ERROR,    0, "NACK within 64 polls" },
        { "TC-08", "F6", CAT_ERROR,    1, "ARBL=1" },
        { "TC-11", "F1", CAT_STRESS,   1, "1e6 xfers, 0 mismatch" },
        { "TC-05", "F5", CAT_NORMAL,   0, "SCL held 1ms, data intact" },
    };
    const tr_t all_pass[] = { TR_PASS, TR_PASS, TR_PASS, TR_PASS, TR_PASS };
    const tr_t p1_fail[]  = { TR_PASS, TR_PASS, TR_FAIL, TR_PASS, TR_PASS };
    const tr_t p0_skip[]  = { TR_PASS, TR_PASS, TR_FAIL, TR_PASS, TR_NOT_RUN };
    const tr_t p0_fail[]  = { TR_PASS, TR_FAIL, TR_PASS, TR_NOT_RUN, TR_NOT_RUN };
    CHECK(plan_verdict(P, all_pass, 5) == V_PASS);
    CHECK(plan_verdict(P, p1_fail, 5) == V_WARN);
    CHECK(plan_verdict(P, p0_skip, 5) == V_INCOMPLETE);
    CHECK(plan_verdict(P, p0_fail, 5) == V_BLOCK);

    /* ---- 연습 3 ---- */
    char msg[160];
    CHECK(plan_lint(P, 5, msg, sizeof msg) && msg[0] == '\0');
    static const tc_t dup[] = { { "TC-01", "F1", CAT_NORMAL, 0, "x=1" }, { "TC-01", "F2", CAT_ERROR, 0, "y=2" } };
    CHECK(!plan_lint(dup, 2, msg, sizeof msg) && strcmp(msg, "dup id TC-01") == 0);
    static const tc_t vague[] = { { "TC-01", "F1", CAT_NORMAL, 0, "works fine" } };
    CHECK(!plan_lint(vague, 1, msg, sizeof msg) && strstr(msg, "not measurable") != NULL);
    CHECK(!plan_lint(P, 4, msg, sizeof msg) && strcmp(msg, "no NORMAL test in plan") == 0);
    printf("ex3: %s\n", msg);

    /* ---- 연습 4 ---- */
    char m1[200], m2[200];
    CHECK(stress(0x1234ABCDu, 300, 0, m1, sizeof m1) == -1);
    int i1 = stress(0x1234ABCDu, 300, F_RXFIFO_FULL_DROP, m1, sizeof m1);
    int i2 = stress(0x1234ABCDu, 300, F_RXFIFO_FULL_DROP, m2, sizeof m2);
    CHECK(i1 >= 0 && i1 == i2 && strcmp(m1, m2) == 0);                     /* 같은 시드 = 같은 실패 */
    CHECK(strstr(m1, "seed=0x1234ABCD") && strstr(m1, "len=8 idx=7"));
    CHECK(stress(0x1234ABCDu, 300, F_HCNT_STUCK, m2, sizeof m2) == -1);     /* 데이터 스트레스는 타이밍을 못 본다 */
    printf("ex4: %s\n", m1);
    (void)why;

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
