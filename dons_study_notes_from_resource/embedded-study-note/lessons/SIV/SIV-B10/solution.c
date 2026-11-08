/* SIV-B10 IP 블록 Bring-up 시퀀스
 *
 * first silicon 이 보드에 올라오면 validation 엔지니어가 IP 블록마다 가장 먼저 돌리는 것:
 *   ① 클럭 enable → ② 리셋 해제 → ③ 살아 있나(scratch) → ④ ID → ⑤ 버전 → ⑥ 기본 기능(loopback)
 * 각 단계는 체크포인트다. 실패하면 "어디서 발견됐나(step)" 와 "원인 추정(cause)" 을 따로 남긴다.
 *
 * 호스트(맥)에는 SoC 가 없으므로 rd32()/wr32() 뒤에 mock SoC 를 둔다:
 *   CRU(clock & reset unit) + interconnect 버스 에러 로거 + 가상의 UART0 IP.
 * SIV-B02 의 mock 레지스터 파일을 이 레슨에 필요한 만큼만 축약해 넣었고, fault_t 로 단계별 결함을
 * 결정적으로 주입한다. 실제 보드에서는 rd32/wr32 가 *(volatile uint32_t *)(addr) 접근
 * (또는 JTAG/OpenOCD 의 mdw/mww)으로 바뀔 뿐 bring-up 코드는 그대로다 (SIV-B13).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 주소 맵 (SIV-B01 기술서에서 생성됐다고 가정한 축약판) ---------------- */
#define CRU_CLK_EN      0x0000u  /* RW  reset 0x1 : bit n = IP n 클럭 게이트 열기 (bootrom 이 TIMER0 만 켬) */
#define CRU_CLK_STAT    0x0004u  /* RO           : bit n = IP n 클럭이 실제로 돌고 있음                  */
#define CRU_RST_CTRL    0x0008u  /* RW  reset 0xE : bit n = 1 이면 IP n 을 리셋에 붙잡아 둠(assert)       */
#define CRU_RST_STAT    0x000Cu  /* RO           : bit n = 1 이면 IP n 리셋 해제 완료                    */
#define CRU_BUSERR      0x0010u  /* W1C bit0     : interconnect 가 버스 에러(SLVERR/DECERR)를 포착함       */
#define CRU_BUSERR_ADDR 0x0014u  /* RO           : 마지막 버스 에러 주소                                  */

enum { IP_TIMER0 = 0, IP_SPI0 = 1, IP_I2C0 = 2, IP_UART0 = 3 };
#define UART0_BIT (1u << IP_UART0)

#define UART0_BASE   0x1000u
#define UART_ID      0x00u   /* RO  tie-off 상수 0x55415254 ("UART")            */
#define UART_VER     0x04u   /* RO  [23:16] major [15:8] minor [7:0] patch       */
#define UART_CTRL    0x08u   /* RW  [0] EN  [1] LOOP                             */
#define UART_BAUD    0x0Cu   /* RW  [15:0] 분주비                                */
#define UART_STAT    0x10u   /* RO  [0] TXE  [1] RXNE                            */
#define UART_TXD     0x14u   /* WO  쓰면 송신 (LOOP 면 RX 로 돌아옴)             */
#define UART_RXD     0x18u   /* RO  읽으면 RX 비움                               */
#define UART_SCRATCH 0x1Cu   /* RW  기능 없는 32비트 — 버스/리셋 확인용          */
#define UART_SIZE    0x20u

#define UART_ID_VAL   0x55415254u
#define UART_VER_VAL  0x00010200u   /* v1.2.0 */
#define CTRL_EN   (1u << 0)
#define CTRL_LOOP (1u << 1)
#define STAT_TXE  (1u << 0)
#define STAT_RXNE (1u << 1)
#define BUS_ERR_DATA 0xDEADBEEFu    /* 이 mock interconnect 가 에러 때 돌려주는 읽기 데이터 */

/* ---------------- mock SoC (주어진 코드 — 고치지 않는다) ---------------- */
typedef struct {
    bool     clk_dead;      /* 게이트가 안 열린다: CLK_EN 을 써도 클럭 없음, CLK_STAT 도 0           */
    bool     clk_stat_lies; /* CLK_STAT 은 CLK_EN 을 따라 1 이 되지만 IP 에는 클럭이 닿지 않음        */
    int      clk_settle;    /* 클럭이 안정될 때까지 CLK_STAT 이 0 을 읽히는 횟수 (정상 지연)          */
    bool     rst_stuck;     /* 리셋이 안 풀린다: RST_STAT 도 0                                        */
    bool     rst_stat_lies; /* RST_STAT 은 1 이지만 IP 는 여전히 리셋 중 (리셋 배선/동기화기 결함)    */
    uint32_t rdata_stuck0;  /* UART 읽기 데이터 버스에서 0 으로 고착된 비트                           */
    uint32_t id_xor;        /* ID tie-off 오류 (뒤집힌 비트)                                          */
    uint32_t ver;           /* 0 이 아니면 VERSION 값 대체                                            */
    uint8_t  rx_stuck0;     /* UART RX 데이터 경로에서 0 으로 고착된 비트                              */
} fault_t;

static fault_t F;
static struct {
    uint32_t clk_en, rst_ctrl, buserr, buserr_addr;
    int      settle_left;
    uint32_t ctrl, baud, scratch;
    bool     rx_full;
    uint8_t  rx;
} S;

typedef struct { char op; uint32_t addr; uint32_t val; } acc_t;   /* 버스 접근 기록 */
#define LOG_MAX 512
static acc_t g_log[LOG_MAX];
static int   g_nlog;

static void reset_soc(const fault_t *f) {   /* 전원 인가(POR) 직후 상태 + 결함 주입 */
    memset(&S, 0, sizeof S);
    memset(&F, 0, sizeof F);
    if (f) F = *f;
    S.clk_en   = 1u << IP_TIMER0;                                   /* bootrom 이 TIMER0 만 켬 */
    S.rst_ctrl = (1u << IP_SPI0) | (1u << IP_I2C0) | UART0_BIT;     /* 나머지는 리셋 유지     */
    g_nlog = 0;
}
static bool uart_clk_running(void) {
    return (S.clk_en & UART0_BIT) && S.settle_left == 0 && !F.clk_dead && !F.clk_stat_lies;
}
static bool uart_in_reset(void) {
    return (S.rst_ctrl & UART0_BIT) || F.rst_stuck || F.rst_stat_lies;
}
static uint32_t bus_error(uint32_t addr) {
    S.buserr |= 1u;
    S.buserr_addr = addr;
    return BUS_ERR_DATA;
}
static uint32_t cru_read(uint32_t addr) {
    uint32_t v;
    bool rep;
    switch (addr) {
    case CRU_CLK_EN:   return S.clk_en;
    case CRU_CLK_STAT:
        rep = !F.clk_dead && (S.clk_en & UART0_BIT) && S.settle_left == 0;
        if ((S.clk_en & UART0_BIT) && S.settle_left > 0) S.settle_left--;
        return (S.clk_en & ~UART0_BIT) | (rep ? UART0_BIT : 0u);
    case CRU_RST_CTRL: return S.rst_ctrl;
    case CRU_RST_STAT:
        v = ~S.rst_ctrl & 0xFu & ~UART0_BIT;
        rep = F.rst_stat_lies ? !(S.rst_ctrl & UART0_BIT) : !uart_in_reset();
        return v | (rep ? UART0_BIT : 0u);
    case CRU_BUSERR:      return S.buserr;
    case CRU_BUSERR_ADDR: return S.buserr_addr;
    default:              return bus_error(addr);
    }
}
static void cru_write(uint32_t addr, uint32_t v) {
    switch (addr) {
    case CRU_CLK_EN:
        if (!(S.clk_en & UART0_BIT) && (v & UART0_BIT)) S.settle_left = F.clk_settle;
        S.clk_en = v & 0xFu;
        break;
    case CRU_RST_CTRL: S.rst_ctrl = v & 0xFu; break;
    case CRU_BUSERR:   S.buserr &= ~(v & 1u); break;                 /* W1C */
    case CRU_CLK_STAT: case CRU_RST_STAT: case CRU_BUSERR_ADDR: break; /* RO: 무시 */
    default:           (void)bus_error(addr); break;
    }
}
static uint32_t uart_read(uint32_t off) {
    uint32_t v = 0;
    if (uart_in_reset()) { S.ctrl = S.baud = S.scratch = 0; S.rx_full = false; }
    switch (off) {
    case UART_ID:      v = UART_ID_VAL ^ F.id_xor; break;
    case UART_VER:     v = F.ver ? F.ver : UART_VER_VAL; break;
    case UART_CTRL:    v = S.ctrl; break;
    case UART_BAUD:    v = S.baud; break;
    case UART_STAT:    v = uart_in_reset() ? 0u : (STAT_TXE | (S.rx_full ? STAT_RXNE : 0u)); break;
    case UART_RXD:     v = S.rx; if (!uart_in_reset()) S.rx_full = false; break;
    case UART_SCRATCH: v = S.scratch; break;
    default:           v = 0; break;                                  /* TXD 등: 읽으면 0 */
    }
    return v & ~F.rdata_stuck0;
}
static void uart_write(uint32_t off, uint32_t v) {
    if (uart_in_reset()) { S.ctrl = S.baud = S.scratch = 0; S.rx_full = false; return; } /* 쓰기 무시 */
    switch (off) {
    case UART_CTRL:    S.ctrl = v & (CTRL_EN | CTRL_LOOP); break;
    case UART_BAUD:    S.baud = v & 0xFFFFu; break;
    case UART_TXD:
        if ((S.ctrl & (CTRL_EN | CTRL_LOOP)) == (CTRL_EN | CTRL_LOOP)) {
            S.rx = (uint8_t)(v & ~(uint32_t)F.rx_stuck0);
            S.rx_full = true;
        }
        break;
    case UART_SCRATCH: S.scratch = v; break;
    default: break;                                                    /* RO: 무시 */
    }
}
static uint32_t rd32(uint32_t addr) {
    uint32_t v;
    if (addr < 0x1000u)                                     v = cru_read(addr);
    else if (addr >= UART0_BASE && addr < UART0_BASE + UART_SIZE)
        v = uart_clk_running() ? uart_read(addr - UART0_BASE) : bus_error(addr);
    else                                                    v = bus_error(addr);
    if (g_nlog < LOG_MAX) g_log[g_nlog++] = (acc_t){ 'r', addr, v };
    return v;
}
static void wr32(uint32_t addr, uint32_t v) {
    if (g_nlog < LOG_MAX) g_log[g_nlog++] = (acc_t){ 'w', addr, v };
    if (addr < 0x1000u)                                     cru_write(addr, v);
    else if (addr >= UART0_BASE && addr < UART0_BASE + UART_SIZE) {
        if (uart_clk_running()) uart_write(addr - UART0_BASE, v);
        else                    (void)bus_error(addr);
    } else                                                  (void)bus_error(addr);
}

/* ---------------- bring-up 결과와 리포트 (주어진 코드) ---------------- */
typedef enum { STEP_NONE, STEP_CLK, STEP_RST, STEP_ALIVE, STEP_ID, STEP_VER, STEP_FUNC, STEP_DONE } step_t;
typedef enum { CAUSE_OK, CAUSE_CLK, CAUSE_RST, CAUSE_BUS, CAUSE_ID, CAUSE_VER, CAUSE_FUNC } cause_t;
static const char *const STEP_NAME[]  = { "-", "CLK", "RST", "ALIVE", "ID", "VER", "FUNC", "DONE" };
static const char *const CAUSE_NAME[] = { "OK", "CLOCK_NOT_APPLIED", "RESET_NOT_RELEASED", "BUS_DATA_PATH",
                                          "ID_MISMATCH", "VERSION_UNSUPPORTED", "FUNC_LOOPBACK" };
#define MAX_POLLS 8                 /* 실시간 타임아웃은 SIV-B09. 여기선 결정적인 "읽기 횟수" 한도 */
#define REQ_MAJOR 1u                /* 이 드라이버/테스트가 아는 major                             */
#define REQ_MINOR 2u                /* 필요한 최소 minor (예: v1.2 에서 LOOP 비트 추가)             */
#define LOOP_BYTE 0xA5u
#define BAUD_115200_AT_12M 104u     /* 12 MHz / 115200 ≈ 104.2                                     */

typedef struct {
    step_t   step;      /* 실패를 '발견'한 단계 (성공이면 STEP_DONE)       */
    cause_t  cause;     /* 추정 '원인'                                      */
    uint32_t done;      /* 통과한 체크포인트: bit (1u << step)              */
    uint32_t addr, exp, act, mask;
    int      polls;     /* 마지막 폴링 단계의 읽기 횟수                     */
    uint32_t id, ver;   /* 읽은 ID / VERSION                                */
    char     msg[256];
} bu_result_t;

static const char *reg_name(uint32_t a) {
    switch (a) {
    case CRU_CLK_EN: return "CRU.CLK_EN";     case CRU_CLK_STAT: return "CRU.CLK_STAT";
    case CRU_RST_CTRL: return "CRU.RST_CTRL"; case CRU_RST_STAT: return "CRU.RST_STAT";
    case UART0_BASE + UART_ID: return "UART0.ID";       case UART0_BASE + UART_VER: return "UART0.VER";
    case UART0_BASE + UART_CTRL: return "UART0.CTRL";   case UART0_BASE + UART_BAUD: return "UART0.BAUD";
    case UART0_BASE + UART_STAT: return "UART0.STAT";   case UART0_BASE + UART_RXD: return "UART0.RXD";
    case UART0_BASE + UART_SCRATCH: return "UART0.SCRATCH";
    default: return "?";
    }
}
static void appendf(char *buf, size_t cap, const char *fmt, ...) {
    size_t n = strlen(buf);
    if (n >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, cap - n, fmt, ap);
    va_end(ap);
}
static void bu_pass(bu_result_t *r, step_t st) { r->done |= 1u << st; }
/* 실패 기록: mask != 0 이면 exp/act/diff/bits 를, 0 이면 act 만 찍는다. 항상 false 를 돌려준다. */
static bool bu_fail(bu_result_t *r, step_t st, cause_t c, uint32_t addr,
                    uint32_t exp, uint32_t act, uint32_t mask, const char *note) {
    r->step = st; r->cause = c; r->addr = addr; r->exp = exp; r->act = act; r->mask = mask;
    r->msg[0] = '\0';
    appendf(r->msg, sizeof r->msg, "FAIL step=%s cause=%s reg=%s addr=0x%04X",
            STEP_NAME[st], CAUSE_NAME[c], reg_name(addr), (unsigned)addr);
    if (mask) {
        uint32_t d = (exp ^ act) & mask;
        appendf(r->msg, sizeof r->msg, " exp=0x%08X act=0x%08X diff=0x%08X bits=[",
                (unsigned)exp, (unsigned)act, (unsigned)d);
        int shown = 0, total = 0;
        for (int b = 31; b >= 0; b--) {
            if (!(d & (1u << b))) continue;
            if (++total <= 8) appendf(r->msg, sizeof r->msg, shown++ ? ",%d" : "%d", b);
        }
        if (total > 8) appendf(r->msg, sizeof r->msg, ",...+%d", total - 8);   /* 너무 많으면 줄임 */
        appendf(r->msg, sizeof r->msg, "]");
    } else {
        appendf(r->msg, sizeof r->msg, " act=0x%08X", (unsigned)act);
    }
    if (st == STEP_CLK || st == STEP_RST) appendf(r->msg, sizeof r->msg, " polls=%d", r->polls);
    appendf(r->msg, sizeof r->msg, " | %s", note);
    return false;
}

/* ================= Part A: 클럭 → 리셋 → ID ================= */
/* Part B 에서 채울 함수 (Part A 의 bu_check_id 가 사용) */
static bool bu_read_checked(bu_result_t *r, step_t st, uint32_t addr, uint32_t *out);

/* UART0 클럭 게이트를 열고 CLK_STAT 이 1 이 될 때까지 최대 MAX_POLLS 번 읽는다.
 * 다른 IP 의 CLK_EN 비트는 건드리지 않는다. 성공: r->polls = 읽은 횟수, CLK 체크포인트 통과. */
static bool bu_clock_enable(bu_result_t *r) {
    wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) | UART0_BIT);          /* 공유 레지스터: RMW 로 내 비트만 */
    uint32_t s = 0;
    for (int i = 1; i <= MAX_POLLS; i++) {
        s = rd32(CRU_CLK_STAT);
        if (s & UART0_BIT) { r->polls = i; bu_pass(r, STEP_CLK); return true; }
    }
    r->polls = MAX_POLLS;
    return bu_fail(r, STEP_CLK, CAUSE_CLK, CRU_CLK_STAT, UART0_BIT, s, UART0_BIT,
                   "CLK_EN.UART0 written but clock never reported running (gate/PLL/clock tree)");
}
/* UART0 리셋을 해제(RST_CTRL 비트 0)하고 RST_STAT 이 1 이 될 때까지 최대 MAX_POLLS 번 읽는다. */
static bool bu_reset_release(bu_result_t *r) {
    wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) & ~UART0_BIT);     /* SPI0/I2C0 리셋은 그대로 유지 */
    uint32_t s = 0;
    for (int i = 1; i <= MAX_POLLS; i++) {
        s = rd32(CRU_RST_STAT);
        if (s & UART0_BIT) { r->polls = i; bu_pass(r, STEP_RST); return true; }
    }
    r->polls = MAX_POLLS;
    return bu_fail(r, STEP_RST, CAUSE_RST, CRU_RST_STAT, UART0_BIT, s, UART0_BIT,
                   "RST_CTRL.UART0 deasserted but reset-done never set (reset wiring/synchronizer)");
}
/* UART0.ID 를 (버스 에러 검사와 함께) 읽어 UART_ID_VAL 과 비교. r->id 에 저장. */
static bool bu_check_id(bu_result_t *r) {
    uint32_t id;
    if (!bu_read_checked(r, STEP_ID, UART0_BASE + UART_ID, &id)) return false;
    r->id = id;
    if (id != UART_ID_VAL)
        return bu_fail(r, STEP_ID, CAUSE_ID, UART0_BASE + UART_ID, UART_ID_VAL, id, 0xFFFFFFFFu,
                       "wrong IP at this base or ID tie-off error");
    bu_pass(r, STEP_ID);
    return true;
}

/* ================= Part B: 증상 → 원인 ================= */
/* 버스 에러 로거로 확인하는 읽기. 이전(stale) 에러를 먼저 지우고(W1C), 읽고, 에러가 잡혔으면
 * cause=CLK 로 실패 기록(mask=0, act=읽은 값) 후 로거를 다시 깨끗이 해 두고 false. */
static bool bu_read_checked(bu_result_t *r, step_t st, uint32_t addr, uint32_t *out) {
    wr32(CRU_BUSERR, 1u);                                     /* stale 에러 제거 (W1C) */
    *out = rd32(addr);
    if (!(rd32(CRU_BUSERR) & 1u)) return true;
    uint32_t ea = rd32(CRU_BUSERR_ADDR);
    wr32(CRU_BUSERR, 1u);                                     /* 다음 테스트를 위해 정리 */
    char note[128];
    snprintf(note, sizeof note, "bus error captured at 0x%04X although CLK_STAT.UART0=1: "
             "clock not reaching IP", (unsigned)ea);
    return bu_fail(r, st, CAUSE_CLK, addr, 0, *out, 0, note);
}
/* SCRATCH 에 0xA5A5A5A5, 0x5A5A5A5A 를 차례로 쓰고 읽어 확인 (ALIVE 체크포인트).
 *   버스 에러          → CLK (bu_read_checked 가 기록)
 *   읽은 값 == 0(리셋값) → RST : 쓰기가 무시됨 = IP 가 아직 리셋 중
 *   그 밖의 불일치      → BUS : 데이터 경로 결함 (mask 0xFFFFFFFF 로 비트 리포트) */
static bool bu_check_alive(bu_result_t *r) {
    static const uint32_t pat[2] = { 0xA5A5A5A5u, 0x5A5A5A5Au };
    const uint32_t a = UART0_BASE + UART_SCRATCH;
    for (int i = 0; i < 2; i++) {
        uint32_t v;
        wr32(a, pat[i]);
        if (!bu_read_checked(r, STEP_ALIVE, a, &v)) return false;
        if (v == pat[i]) continue;
        if (v == 0u)
            return bu_fail(r, STEP_ALIVE, CAUSE_RST, a, pat[i], v, 0xFFFFFFFFu,
                           "write ignored, reads reset value although RST_STAT.UART0=1: IP still in reset");
        return bu_fail(r, STEP_ALIVE, CAUSE_BUS, a, pat[i], v, 0xFFFFFFFFu,
                       "scratch readback corrupted: data-path bit(s) stuck");
    }
    bu_pass(r, STEP_ALIVE);
    return true;
}

/* ================= Part C: 버전 · 기능 · 전체 시퀀스 ================= */
/* VERSION: major == REQ_MAJOR && minor >= REQ_MINOR 이어야 통과. r->ver 에 저장.
 * 실패 시 exp=REQ 버전(patch 0), mask=0x00FFFF00. */
static bool bu_check_version(bu_result_t *r) {
    uint32_t v;
    if (!bu_read_checked(r, STEP_VER, UART0_BASE + UART_VER, &v)) return false;
    r->ver = v;
    uint32_t major = (v >> 16) & 0xFFu, minor = (v >> 8) & 0xFFu;
    if (major != REQ_MAJOR || minor < REQ_MINOR)
        return bu_fail(r, STEP_VER, CAUSE_VER, UART0_BASE + UART_VER,
                       (REQ_MAJOR << 16) | (REQ_MINOR << 8), v, 0x00FFFF00u,
                       "IP version older than required (need 1.2+): wrong netlist/IP drop?");
    bu_pass(r, STEP_VER);
    return true;
}
/* 기본 기능: BAUD=104 쓰고 되읽기 → CTRL=EN|LOOP → STAT.TXE 확인 → TXD=0xA5 →
 * STAT.RXNE 를 최대 MAX_POLLS 번 폴링 → RXD 비교(mask 0xFF) → CTRL=EN (loopback 끄고 사용 가능 상태로). */
static bool bu_func_loopback(bu_result_t *r) {
    const uint32_t b = UART0_BASE;
    uint32_t v;
    wr32(b + UART_BAUD, BAUD_115200_AT_12M);
    if (!bu_read_checked(r, STEP_FUNC, b + UART_BAUD, &v)) return false;
    if (v != BAUD_115200_AT_12M)
        return bu_fail(r, STEP_FUNC, CAUSE_FUNC, b + UART_BAUD, BAUD_115200_AT_12M, v, 0xFFFFu,
                       "BAUD readback mismatch");
    wr32(b + UART_CTRL, CTRL_EN | CTRL_LOOP);
    if (!bu_read_checked(r, STEP_FUNC, b + UART_STAT, &v)) return false;
    if (!(v & STAT_TXE))
        return bu_fail(r, STEP_FUNC, CAUSE_FUNC, b + UART_STAT, STAT_TXE, v, STAT_TXE, "TX not empty after enable");
    wr32(b + UART_TXD, LOOP_BYTE);
    bool rxne = false;
    for (int i = 1; i <= MAX_POLLS && !rxne; i++) {
        if (!bu_read_checked(r, STEP_FUNC, b + UART_STAT, &v)) return false;
        rxne = (v & STAT_RXNE) != 0;
    }
    if (!rxne)
        return bu_fail(r, STEP_FUNC, CAUSE_FUNC, b + UART_STAT, STAT_RXNE, v, STAT_RXNE,
                       "loopback byte never arrived (RXNE=0)");
    if (!bu_read_checked(r, STEP_FUNC, b + UART_RXD, &v)) return false;
    if ((v & 0xFFu) != LOOP_BYTE)
        return bu_fail(r, STEP_FUNC, CAUSE_FUNC, b + UART_RXD, LOOP_BYTE, v, 0xFFu,
                       "loopback data mismatch: TX/RX data path bit(s)");
    wr32(b + UART_CTRL, CTRL_EN);
    bu_pass(r, STEP_FUNC);
    return true;
}
/* 전체 시퀀스: r 을 0 으로 초기화하고 CLK → RST → ALIVE → ID → VER → FUNC 을 차례로.
 * 첫 실패에서 멈춘다(그 뒤 단계는 접근조차 하지 않는다). 성공하면 step=DONE, cause=OK,
 * msg = "PASS UART0 bring-up: ID=0x55415254 VER=1.2.0 checkpoints=6/6" */
static bool uart_bringup(bu_result_t *r) {
    memset(r, 0, sizeof *r);
    if (!bu_clock_enable(r) || !bu_reset_release(r) || !bu_check_alive(r) ||
        !bu_check_id(r) || !bu_check_version(r) || !bu_func_loopback(r))
        return false;
    r->step = STEP_DONE;
    r->cause = CAUSE_OK;
    snprintf(r->msg, sizeof r->msg, "PASS UART0 bring-up: ID=0x%08X VER=%u.%u.%u checkpoints=6/6",
             (unsigned)r->id, (unsigned)((r->ver >> 16) & 0xFFu), (unsigned)((r->ver >> 8) & 0xFFu),
             (unsigned)(r->ver & 0xFFu));
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define HAS(s) (strstr(r.msg, (s)) != NULL)
#define B(st) (1u << (st))

static int first_acc(char op, uint32_t addr) {
    for (int i = 0; i < g_nlog; i++) if (g_log[i].op == op && g_log[i].addr == addr) return i;
    return -1;
}
static int uart_accesses(void) {
    int n = 0;
    for (int i = 0; i < g_nlog; i++) if (g_log[i].addr >= UART0_BASE) n++;
    return n;
}

int main(void) {
    bu_result_t r;
    /* starter 의 빈 TODO 에서 -Wunused-function 이 나지 않게 참조만 해 둔다 (solution 에서도 무해) */
    (void)wr32; (void)bu_pass; (void)bu_fail; (void)bu_read_checked; (void)bu_check_version; (void)bu_func_loopback;

    /* ---- Part A: 정상 mock ---- */
    reset_soc(NULL); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && r.polls == 1);
    CHECK(S.clk_en == ((1u << IP_TIMER0) | UART0_BIT));               /* TIMER0 클럭 보존 */
    CHECK(bu_reset_release(&r));
    CHECK(S.rst_ctrl == ((1u << IP_SPI0) | (1u << IP_I2C0)));         /* SPI0/I2C0 는 계속 리셋 */
    CHECK(bu_check_id(&r) && r.id == UART_ID_VAL);
    CHECK(r.done == (B(STEP_CLK) | B(STEP_RST) | B(STEP_ID)));
    CHECK(first_acc('w', CRU_CLK_EN) >= 0 && first_acc('w', CRU_CLK_EN) < first_acc('w', CRU_RST_CTRL));
    CHECK(first_acc('w', CRU_RST_CTRL) < first_acc('r', UART0_BASE + UART_ID));
    CHECK(S.buserr == 0);
    /* ---- Part A: 결함 주입 ---- */
    reset_soc(&(fault_t){ .clk_dead = true }); memset(&r, 0, sizeof r);
    CHECK(!bu_clock_enable(&r) && r.step == STEP_CLK && r.cause == CAUSE_CLK);
    CHECK(r.addr == CRU_CLK_STAT && r.polls == MAX_POLLS && HAS("bits=[3]"));
    reset_soc(&(fault_t){ .rst_stuck = true }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r));
    CHECK(!bu_reset_release(&r) && r.step == STEP_RST && r.cause == CAUSE_RST && r.addr == CRU_RST_STAT);
    CHECK(r.done == B(STEP_CLK) && HAS("reg=CRU.RST_STAT"));
    reset_soc(&(fault_t){ .id_xor = 0x100u }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r));
    CHECK(!bu_check_id(&r) && r.cause == CAUSE_ID && r.act == 0x55415354u && HAS("bits=[8]"));

    /* ---- Part B: 증상 ≠ 원인 ---- */
    reset_soc(&(fault_t){ .clk_stat_lies = true }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r));               /* 상태 레지스터는 거짓말을 한다 */
    CHECK(!bu_check_id(&r) && r.step == STEP_ID && r.cause == CAUSE_CLK); /* ID 불일치가 아니다 */
    CHECK(r.act == BUS_ERR_DATA && HAS("bus error") && S.buserr == 0);   /* 로거 정리까지 */
    reset_soc(NULL); memset(&r, 0, sizeof r);
    S.buserr = 1u; S.buserr_addr = 0x3000u;                            /* 이전 테스트가 남긴 stale 에러 */
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r) && bu_check_id(&r));
    reset_soc(&(fault_t){ .rst_stat_lies = true }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r) && bu_check_id(&r)); /* ID 는 리셋 중에도 읽힌다! */
    CHECK(!bu_check_alive(&r) && r.step == STEP_ALIVE && r.cause == CAUSE_RST);
    CHECK(r.addr == UART0_BASE + UART_SCRATCH && r.act == 0u);
    reset_soc(&(fault_t){ .rdata_stuck0 = 0x80u }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r));
    CHECK(!bu_check_alive(&r) && r.cause == CAUSE_BUS && r.act == 0xA5A5A525u && HAS("bits=[7]"));
    reset_soc(NULL); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r) && bu_check_alive(&r) && (r.done & B(STEP_ALIVE)));
    reset_soc(&(fault_t){ .clk_settle = MAX_POLLS - 1 }); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && r.polls == MAX_POLLS);               /* 정상 지연은 통과 (경계) */
    reset_soc(&(fault_t){ .clk_settle = MAX_POLLS }); memset(&r, 0, sizeof r);
    CHECK(!bu_clock_enable(&r) && r.cause == CAUSE_CLK);              /* 한도 초과는 실패 */
    reset_soc(NULL); memset(&r, 0, sizeof r);
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r));
    CHECK(bu_clock_enable(&r) && bu_reset_release(&r) && r.polls == 1);   /* 두 번 돌려도 안전 */
    CHECK(S.clk_en == ((1u << IP_TIMER0) | UART0_BIT) && S.rst_ctrl == ((1u << IP_SPI0) | (1u << IP_I2C0)));

    /* ---- Part C: 전체 시퀀스 + 결함 매트릭스 ---- */
    reset_soc(NULL);
    CHECK(uart_bringup(&r) && r.step == STEP_DONE && r.cause == CAUSE_OK);
    CHECK(r.done == (B(STEP_CLK) | B(STEP_RST) | B(STEP_ALIVE) | B(STEP_ID) | B(STEP_VER) | B(STEP_FUNC)));
    CHECK(S.ctrl == CTRL_EN && S.baud == BAUD_115200_AT_12M && S.buserr == 0);
    CHECK(strcmp(r.msg, "PASS UART0 bring-up: ID=0x55415254 VER=1.2.0 checkpoints=6/6") == 0);

    static const struct {
        const char *name; fault_t f; step_t step; cause_t cause; uint32_t addr; const char *bits;
    } M[] = {
        { "clk_dead",      { .clk_dead = true },      STEP_CLK,   CAUSE_CLK,  CRU_CLK_STAT,               "bits=[3]" },
        { "clk_stat_lies", { .clk_stat_lies = true }, STEP_ALIVE, CAUSE_CLK,  UART0_BASE + UART_SCRATCH,  "bus error" },
        { "rst_stuck",     { .rst_stuck = true },     STEP_RST,   CAUSE_RST,  CRU_RST_STAT,               "bits=[3]" },
        { "rst_stat_lies", { .rst_stat_lies = true }, STEP_ALIVE, CAUSE_RST,  UART0_BASE + UART_SCRATCH,  "act=0x00000000" },
        { "rdata_stuck0",  { .rdata_stuck0 = 0x80u }, STEP_ALIVE, CAUSE_BUS,  UART0_BASE + UART_SCRATCH,  "bits=[7]" },
        { "id_xor",        { .id_xor = 0x100u },      STEP_ID,    CAUSE_ID,   UART0_BASE + UART_ID,       "bits=[8]" },
        { "ver_old",       { .ver = 0x00010105u },    STEP_VER,   CAUSE_VER,  UART0_BASE + UART_VER,      "bits=[9,8]" },
        { "rx_stuck0",     { .rx_stuck0 = 0x20u },    STEP_FUNC,  CAUSE_FUNC, UART0_BASE + UART_RXD,      "bits=[5]" },
        { "clk_settle_5",  { .clk_settle = 5 },       STEP_DONE,  CAUSE_OK,   0,                          "PASS" },
    };
    printf("fault matrix:\n");
    for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
        reset_soc(&M[i].f);
        bool ok = uart_bringup(&r);
        printf("  %-14s %s\n", M[i].name, r.msg);
        CHECK(ok == (M[i].cause == CAUSE_OK) && r.step == M[i].step && r.cause == M[i].cause);
        CHECK((M[i].cause == CAUSE_OK || r.addr == M[i].addr) && HAS(M[i].bits));
        CHECK(S.buserr == 0);                                          /* 어떤 경우든 로거는 깨끗하게 */
    }
    reset_soc(&(fault_t){ .clk_dead = true });
    CHECK(!uart_bringup(&r) && first_acc('w', CRU_RST_CTRL) == -1 && uart_accesses() == 0); /* 첫 실패에서 멈춤 */
    reset_soc(&(fault_t){ .rst_stuck = true });
    CHECK(!uart_bringup(&r) && r.done == B(STEP_CLK) && uart_accesses() == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
