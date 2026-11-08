/* SIV-B10 추가 연습 — 한 번에 검증
 *
 * 본문 mock 을 "인스턴스 여러 개" 로 일반화한 축약판 (UART0 @0x1000 / UART1 @0x2000).
 *   연습 1  teardown(전원 내리기) 시퀀스와 순서 — 거꾸로 하면 버스 에러
 *   연습 2  리셋 펄스 재시도 — 복구됐어도 WARN 으로 남긴다
 *   연습 3  버스 에러 로거가 없는 칩: 읽은 값 시그니처로 1차 분류
 *   연습 4  기술자(descriptor) 테이블 기반 bring-up — 인스턴스별 클럭 배선 결함 진단
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CRU_CLK_EN      0x0000u
#define CRU_CLK_STAT    0x0004u
#define CRU_RST_CTRL    0x0008u
#define CRU_RST_STAT    0x000Cu
#define CRU_BUSERR      0x0010u
#define CRU_BUSERR_ADDR 0x0014u
#define UART_ID      0x00u
#define UART_CTRL    0x08u
#define UART_SCRATCH 0x1Cu
#define UART_SIZE    0x20u
#define UART_ID_VAL  0x55415254u
#define BUS_ERR_DATA 0xDEADBEEFu
#define MAX_POLLS 8
#define NIP 2

typedef struct { const char *name; uint32_t base; unsigned clk_bit, rst_bit; uint32_t id; } ip_desc_t;
static const ip_desc_t IPS[NIP] = {
    { "UART0", 0x1000u, 3, 3, UART_ID_VAL },
    { "UART1", 0x2000u, 4, 4, UART_ID_VAL },
};

/* ---------------- mock (주어진 코드) ---------------- */
typedef struct {
    unsigned clk_wire[NIP];   /* 0 = 정상. 아니면 인스턴스 i 의 클럭 게이트가 실제로 연결된 CLK_EN 비트 */
    int      rst_fail[NIP];   /* 리셋 해제가 이 횟수만큼 "걸려서" 안 끝남 (다시 펄스 주면 풀림)        */
    bool     rst_stuck[NIP];  /* 영원히 안 풀림                                                        */
} fault_t;
static fault_t F;
static struct {
    uint32_t clk_en, rst_ctrl, buserr, buserr_addr;
    int      fail_left[NIP];
    bool     hung[NIP];
    uint32_t ctrl[NIP], scratch[NIP];
} S;
typedef struct { char op; uint32_t addr; uint32_t val; } acc_t;
static acc_t g_log[1024];
static int   g_nlog;

static void reset_soc(const fault_t *f) {
    memset(&S, 0, sizeof S); memset(&F, 0, sizeof F);
    if (f) F = *f;
    for (int i = 0; i < NIP; i++) S.fail_left[i] = F.rst_fail[i];
    S.clk_en = 0x1u;                               /* TIMER0 */
    S.rst_ctrl = 0x6u | (1u << 3) | (1u << 4);     /* SPI0/I2C0/UART0/UART1 리셋 유지 */
    g_nlog = 0;
}
static unsigned gate_bit(int i) { return F.clk_wire[i] ? F.clk_wire[i] : IPS[i].clk_bit; }
static bool running(int i)  { return (S.clk_en >> gate_bit(i)) & 1u; }
static bool in_reset(int i) { return ((S.rst_ctrl >> IPS[i].rst_bit) & 1u) || F.rst_stuck[i] || S.hung[i]; }
static uint32_t bus_error(uint32_t a) { S.buserr |= 1u; S.buserr_addr = a; return BUS_ERR_DATA; }
static int ip_at(uint32_t a) {
    for (int i = 0; i < NIP; i++) if (a >= IPS[i].base && a < IPS[i].base + UART_SIZE) return i;
    return -1;
}
static uint32_t rd32(uint32_t a) {
    uint32_t v = 0;
    int i = ip_at(a);
    if (a == CRU_CLK_EN) v = S.clk_en;
    else if (a == CRU_CLK_STAT) {
        v = S.clk_en & 0x7u;
        for (int k = 0; k < NIP; k++) if (running(k)) v |= 1u << IPS[k].clk_bit;
    } else if (a == CRU_RST_CTRL) v = S.rst_ctrl;
    else if (a == CRU_RST_STAT) {
        v = ~S.rst_ctrl & 0x7u;
        for (int k = 0; k < NIP; k++) if (!in_reset(k)) v |= 1u << IPS[k].rst_bit;
    } else if (a == CRU_BUSERR) v = S.buserr;
    else if (a == CRU_BUSERR_ADDR) v = S.buserr_addr;
    else if (i >= 0 && running(i)) {
        uint32_t off = a - IPS[i].base;
        v = off == UART_ID ? IPS[i].id : off == UART_CTRL ? S.ctrl[i] : off == UART_SCRATCH ? S.scratch[i] : 0u;
    } else v = bus_error(a);
    if (g_nlog < 1024) g_log[g_nlog++] = (acc_t){ 'r', a, v };
    return v;
}
static void wr32(uint32_t a, uint32_t v) {
    if (g_nlog < 1024) g_log[g_nlog++] = (acc_t){ 'w', a, v };
    int i = ip_at(a);
    if (a == CRU_CLK_EN) S.clk_en = v & 0x3Fu;
    else if (a == CRU_RST_CTRL) {
        for (int k = 0; k < NIP; k++) {
            uint32_t m = 1u << IPS[k].rst_bit;
            if ((S.rst_ctrl & m) && !(v & m)) {                 /* 해제 에지 */
                S.hung[k] = S.fail_left[k] > 0;
                if (S.fail_left[k] > 0) S.fail_left[k]--;
            }
            if (v & m) { S.hung[k] = false; S.ctrl[k] = S.scratch[k] = 0; }   /* assert: 상태 리셋 */
        }
        S.rst_ctrl = v & 0x3Fu;
    } else if (a == CRU_BUSERR) S.buserr &= ~(v & 1u);
    else if (a < 0x1000u) { /* RO */ }
    else if (i >= 0 && running(i)) {
        if (in_reset(i)) return;
        uint32_t off = a - IPS[i].base;
        if (off == UART_CTRL) S.ctrl[i] = v & 3u;
        else if (off == UART_SCRATCH) S.scratch[i] = v;
    } else (void)bus_error(a);
}

/* ---------------- 결과 ---------------- */
typedef enum { ST_CLK = 1, ST_RST, ST_ALIVE, ST_ID } step_t;
typedef enum { C_OK, C_CLK, C_RST, C_BUS, C_ID } cause_t;
static const char *const STEP_NAME[]  = { "-", "CLK", "RST", "ALIVE", "ID" };
static const char *const CAUSE_NAME[] = { "OK", "CLOCK_NOT_APPLIED", "RESET_NOT_RELEASED", "BUS_DATA_PATH", "ID_MISMATCH" };
typedef struct { step_t step; cause_t cause; int retries; char msg[200]; } br_t;

static void appendf(char *buf, size_t cap, const char *fmt, ...) {
    size_t n = strlen(buf);
    if (n >= cap) return;
    va_list ap; va_start(ap, fmt); vsnprintf(buf + n, cap - n, fmt, ap); va_end(ap);
}
static bool fail(br_t *r, const ip_desc_t *d, step_t st, cause_t c, const char *reg,
                 uint32_t exp, uint32_t act, uint32_t mask) {
    r->step = st; r->cause = c; r->msg[0] = '\0';
    appendf(r->msg, sizeof r->msg, "FAIL ip=%s step=%s cause=%s reg=%s exp=0x%08X act=0x%08X bits=[",
            d->name, STEP_NAME[st], CAUSE_NAME[c], reg, (unsigned)exp, (unsigned)act);
    uint32_t df = (exp ^ act) & mask;
    bool first = true;
    for (int b = 31; b >= 0; b--) if (df & (1u << b)) { appendf(r->msg, sizeof r->msg, first ? "%d" : ",%d", b); first = false; }
    appendf(r->msg, sizeof r->msg, "]");
    return false;
}
static bool poll_bit(uint32_t addr, uint32_t m, bool want, uint32_t *last) {
    for (int i = 0; i < MAX_POLLS; i++) { *last = rd32(addr); if (((*last & m) != 0) == want) return true; }
    return false;
}

/* ---- 연습 4: 기술자 기반 bring-up (연습 2 의 재시도 포함) ---- */
static bool ip_bringup(const ip_desc_t *d, int max_retry, br_t *r) {
    memset(r, 0, sizeof *r);
    uint32_t cm = 1u << d->clk_bit, rm = 1u << d->rst_bit, v;
    wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) | cm);
    if (!poll_bit(CRU_CLK_STAT, cm, true, &v)) return fail(r, d, ST_CLK, C_CLK, "CRU.CLK_STAT", cm, v, cm);
    wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) & ~rm);
    while (!poll_bit(CRU_RST_STAT, rm, true, &v)) {             /* 연습 2: 리셋 펄스 재시도 */
        if (r->retries == max_retry) return fail(r, d, ST_RST, C_RST, "CRU.RST_STAT", rm, v, rm);
        r->retries++;
        wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) | rm);            /* 다시 붙잡고 */
        wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) & ~rm);           /* 다시 놓는다 */
    }
    wr32(CRU_BUSERR, 1u);
    wr32(d->base + UART_SCRATCH, 0xA5A5A5A5u);
    v = rd32(d->base + UART_SCRATCH);
    if (rd32(CRU_BUSERR) & 1u) { wr32(CRU_BUSERR, 1u); return fail(r, d, ST_ALIVE, C_CLK, "SCRATCH", 0xA5A5A5A5u, v, 0); }
    if (v != 0xA5A5A5A5u) return fail(r, d, ST_ALIVE, v ? C_BUS : C_RST, "SCRATCH", 0xA5A5A5A5u, v, ~0u);
    v = rd32(d->base + UART_ID);
    if (v != d->id) return fail(r, d, ST_ID, C_ID, "ID", d->id, v, ~0u);
    r->cause = C_OK;
    snprintf(r->msg, sizeof r->msg, "%s ip=%s bring-up%s", r->retries ? "WARN" : "PASS", d->name,
             r->retries ? " recovered after reset retry (flaky reset: file a bug)" : " ok");
    return true;
}

/* ---- 연습 1: teardown ---- */
static bool ip_teardown(const ip_desc_t *d) {
    uint32_t rm = 1u << d->rst_bit, v;
    wr32(d->base + UART_CTRL, 0);                              /* ① IP 기능 정지 (클럭 살아 있을 때) */
    wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) | rm);               /* ② 리셋 assert                     */
    if (!poll_bit(CRU_RST_STAT, rm, false, &v)) return false;  /*    reset-done 이 내려갔는지         */
    wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) & ~(1u << d->clk_bit));  /* ③ 마지막에 클럭 게이트 닫기        */
    return true;
}
static void ip_teardown_buggy(const ip_desc_t *d) {           /* 클럭부터 끈다 */
    wr32(CRU_CLK_EN, rd32(CRU_CLK_EN) & ~(1u << d->clk_bit));
    wr32(CRU_RST_CTRL, rd32(CRU_RST_CTRL) | (1u << d->rst_bit));
    wr32(d->base + UART_CTRL, 0);
}

/* ---- 연습 3: 로거 없는 칩에서 읽은 값으로 1차 분류 ---- */
static const char *classify_read(uint32_t v, uint32_t expect_id) {
    if (v == expect_id)    return "OK";
    if (v == BUS_ERR_DATA) return "BUS_ERROR_PATTERN: clock/power off or decode error (check logger/abort)";
    if (v == 0u)           return "ALL_ZERO: in reset, RAZ hole, or gated-returns-zero fabric";
    if (v == 0xFFFFFFFFu)  return "ALL_ONES: unmapped/floating bus or powered-off domain";
    return "OTHER: wrong IP at this base or ID tie-off error (report diff bits)";
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int last_acc(char op, uint32_t addr) {
    for (int i = g_nlog - 1; i >= 0; i--) if (g_log[i].op == op && g_log[i].addr == addr) return i;
    return -1;
}

int main(void) {
    br_t r;
    /* 연습 1 */
    reset_soc(NULL);
    CHECK(ip_bringup(&IPS[0], 0, &r) && S.scratch[0] == 0xA5A5A5A5u);
    wr32(IPS[0].base + UART_CTRL, 1u);
    CHECK(ip_teardown(&IPS[0]));
    CHECK(last_acc('w', IPS[0].base + UART_CTRL) < last_acc('w', CRU_RST_CTRL));
    CHECK(last_acc('w', CRU_RST_CTRL) < last_acc('w', CRU_CLK_EN));
    CHECK(S.clk_en == 0x1u && (S.rst_ctrl & (1u << 3)) && S.buserr == 0 && S.scratch[0] == 0 && S.ctrl[0] == 0);
    CHECK(ip_bringup(&IPS[0], 0, &r));                         /* 다시 올릴 수 있다 */
    ip_teardown_buggy(&IPS[0]);
    CHECK(S.buserr == 1u && S.buserr_addr == IPS[0].base + UART_CTRL);   /* 클럭 끈 뒤 접근 = 버스 에러 */

    /* 연습 2 */
    reset_soc(&(fault_t){ .rst_fail = { 1, 0 } });
    CHECK(ip_bringup(&IPS[0], 1, &r) && r.retries == 1 && strncmp(r.msg, "WARN", 4) == 0);
    printf("  %s\n", r.msg);
    reset_soc(&(fault_t){ .rst_fail = { 1, 0 } });
    CHECK(!ip_bringup(&IPS[0], 0, &r) && r.cause == C_RST);
    reset_soc(&(fault_t){ .rst_stuck = { true, false } });
    CHECK(!ip_bringup(&IPS[0], 1, &r) && r.cause == C_RST && r.retries == 1);
    reset_soc(NULL);
    CHECK(ip_bringup(&IPS[0], 1, &r) && r.retries == 0 && strncmp(r.msg, "PASS", 4) == 0);

    /* 연습 3 */
    CHECK(strncmp(classify_read(UART_ID_VAL, UART_ID_VAL), "OK", 2) == 0);
    CHECK(strncmp(classify_read(0xDEADBEEFu, UART_ID_VAL), "BUS_ERROR", 9) == 0);
    CHECK(strncmp(classify_read(0u, UART_ID_VAL), "ALL_ZERO", 8) == 0);
    CHECK(strncmp(classify_read(0xFFFFFFFFu, UART_ID_VAL), "ALL_ONES", 8) == 0);
    CHECK(strncmp(classify_read(0x55415354u, UART_ID_VAL), "OTHER", 5) == 0);

    /* 연습 4: UART1 클럭 게이트가 CLK_EN bit5 에 잘못 배선 */
    reset_soc(&(fault_t){ .clk_wire = { 0, 5 } });
    CHECK(ip_bringup(&IPS[0], 0, &r));
    CHECK(!ip_bringup(&IPS[1], 0, &r) && r.step == ST_CLK && r.cause == C_CLK);
    CHECK(strstr(r.msg, "ip=UART1") && strstr(r.msg, "bits=[4]"));
    printf("  %s\n", r.msg);
    CHECK(S.clk_en == (0x1u | (1u << 3) | (1u << 4)));        /* UART0 클럭은 그대로, bit5 는 안 건드림 */
    reset_soc(NULL);
    for (int i = 0; i < NIP; i++) CHECK(ip_bringup(&IPS[i], 0, &r));
    CHECK(S.rst_ctrl == 0x6u && S.buserr == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
