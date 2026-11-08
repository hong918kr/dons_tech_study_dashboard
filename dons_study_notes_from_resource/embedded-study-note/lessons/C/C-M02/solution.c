/* C-M02 Read-Modify-Write 위험과 W1C 레지스터
 *
 * 레지스터의 비트마다 "쓰기 의미(write semantics)"가 다르다. RW 비트는 쓴 값이 저장되지만,
 * W1C 비트는 1을 써야 지워지고, STM32 TIMx_SR 같은 rc_w0 비트는 0을 써야 지워진다.
 * 호스트(맥)에는 진짜 레지스터가 없으므로 모든 접근을 reg_read / reg_write 로 보내고,
 * 이 두 함수가 비트별 규칙(reg_spec)을 적용한다 — 실제 칩에서는 주변장치 하드웨어가 하는 일이다.
 * 실제 펌웨어에서 reg_read(r) 는 *(volatile uint32_t *)addr, reg_write 는 그 주소에 store 한 번이다.
 *
 * 테스트는 ① 최종 저장값 ② 버스 접근 기록(trace)을 검사한다. 읽기 "직후, 쓰기 전" 에 하드웨어 이벤트나
 * ISR 을 끼워 넣는 one-shot 훅(after_read)으로 RMW 경쟁을 결정적으로(deterministic) 재현한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 비트별 접근 규칙 ---------------- */
typedef struct {
    uint32_t rw;   /* RW : 쓴 값이 그대로 저장된다                                         */
    uint32_t w1c;  /* W1C: 1을 쓴 비트만 0이 된다. 0 쓰기는 무시 (STM32 EXTI_PR, PCIe Status) */
    uint32_t w0c;  /* W0C: 0을 쓴 비트만 0이 된다. 1 쓰기는 무시 (STM32 TIMx_SR 의 rc_w0)     */
    uint32_t w1s;  /* W1S: 1을 쓴 비트만 1이 된다. 0 쓰기는 무시 (NVIC_ISER, nRF INTENSET)    */
    uint32_t rc;   /* RC : 읽으면 0이 된다. 쓰기는 무시                                       */
    /* 어느 마스크에도 없는 비트 = RO: 쓰기는 무시, 하드웨어만 바꾼다                        */
} reg_spec;

/* ---------------- Part A: 규칙 적용 (하드웨어 쪽 모델) ---------------- */
/* 다섯 마스크가 서로 겹치지 않으면 true (한 비트는 한 가지 의미만 가진다) */
static bool spec_valid(const reg_spec *s) {
    uint32_t m[5] = { s->rw, s->w1c, s->w0c, s->w1s, s->rc };
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++)
            if (m[i] & m[j]) return false;
    return true;
}
/* 현재 저장값 cur 인 레지스터에 w 를 썼을 때 새 저장값 */
static uint32_t apply_write(const reg_spec *s, uint32_t cur, uint32_t w) {
    uint32_t v = cur;
    v = (v & ~s->rw) | (w & s->rw);   /* RW : 쓴 값으로 교체            */
    v &= ~(w & s->w1c);               /* W1C: 1 쓴 자리만 지움          */
    v &= ~(~w & s->w0c);              /* W0C: 0 쓴 자리만 지움          */
    v |= (w & s->w1s);                /* W1S: 1 쓴 자리만 켬            */
    return v;                         /* RO, RC: 그대로                 */
}
/* 읽기의 부작용: 읽고 난 뒤 저장소에 남는 값 */
static uint32_t apply_read(const reg_spec *s, uint32_t cur) {
    return cur & ~s->rc;
}

/* ---------------- 시뮬레이터 (주어진 코드 — 고치지 않는다) ---------------- */
typedef struct {
    const char *name;
    uint32_t   *store;            /* 실제 저장소. alias 레지스터(CRSET/CRCLR)는 같은 store 를 공유 */
    reg_spec    spec;
    void      (*after_read)(void);/* one-shot 훅: 읽기 직후, 다음 쓰기 전에 실행 (HW 이벤트 / ISR)   */
} sim_reg;

static char g_trace[512];         /* 예: "rSR=7 HW:SR|=4 wSR=1" (값은 16진수) */
static void trace(const char *op, const char *name, uint32_t v) {   /* op: "r" "w" "HW:" */
    size_t n = strlen(g_trace);
    snprintf(g_trace + n, sizeof g_trace - n, "%s%s%s%s%X", n ? " " : "", op, name,
             op[0] == 'H' ? "|=" : "=", (unsigned)v);
}
static void trace_tag(const char *tag) {                            /* "IRQ" "DI" "EI" */
    size_t n = strlen(g_trace);
    snprintf(g_trace + n, sizeof g_trace - n, "%s%s", n ? " " : "", tag);
}
static uint32_t reg_read(sim_reg *r) {
    uint32_t v = *r->store;
    *r->store = apply_read(&r->spec, v);
    trace("r", r->name, v);
    if (r->after_read) { void (*h)(void) = r->after_read; r->after_read = NULL; h(); }
    return v;
}
static void reg_write(sim_reg *r, uint32_t w) {
    trace("w", r->name, w);
    *r->store = apply_write(&r->spec, *r->store, w);
}
static void hw_raise(sim_reg *r, uint32_t bits) {   /* 하드웨어가 상태 비트를 세운다 */
    trace("HW:", r->name, bits);
    *r->store |= bits;
}

/* ---------------- 가짜 장치: UART 비슷한 주변장치 + 타이머 상태 레지스터 ---------------- */
#define SR_RX      (1u << 0)      /* W1C: 수신 완료 이벤트 */
#define SR_TX      (1u << 1)      /* W1C: 송신 완료 이벤트 */
#define SR_ERR     (1u << 2)      /* W1C: 오류 이벤트      */
#define SR_BUSY    (1u << 8)      /* RO : 장치 동작 중     */
#define SR_EVENTS  (SR_RX | SR_TX | SR_ERR)

#define CR_EN      (1u << 0)      /* RW */
#define CR_RXIE    (1u << 1)      /* RW */
#define CR_TXIE    (1u << 2)      /* RW */
#define CR_MODE_Pos 4u
#define CR_MODE_Msk (3u << CR_MODE_Pos)   /* RW 2비트 필드 */
#define CR_RWBITS  (CR_EN | CR_RXIE | CR_TXIE | CR_MODE_Msk)
#define CR_OVF     (1u << 8)      /* W1C: 같은 레지스터에 섞인 sticky 오버플로 플래그 */

#define TIM_UIF    (1u << 0)      /* STM32 TIMx_SR 와 같은 배치, 모두 rc_w0 */
#define TIM_CC1IF  (1u << 1)
#define TIM_CC2IF  (1u << 2)
#define TIM_FLAGS  (TIM_UIF | TIM_CC1IF | TIM_CC2IF)

static uint32_t sr_store, cr_store, tsr_store, m_store;
static sim_reg SR    = { "SR",    &sr_store,  { .w1c = SR_EVENTS },               NULL };
static sim_reg CR    = { "CR",    &cr_store,  { .rw = CR_RWBITS, .w1c = CR_OVF }, NULL };
static sim_reg CRSET = { "CRSET", &cr_store,  { .w1s = CR_RWBITS },               NULL };  /* CR 의 set alias   */
static sim_reg CRCLR = { "CRCLR", &cr_store,  { .w1c = CR_RWBITS },               NULL };  /* CR 의 clear alias */
static sim_reg TSR   = { "TSR",   &tsr_store, { .w0c = TIM_FLAGS },               NULL };
/* 모든 의미가 섞인 시험용 레지스터 (Part A/B 테스트용) */
static const reg_spec MIX = { .rw = 0x000000FFu, .w1c = 0x0000FF00u, .w0c = 0x00FF0000u,
                              .w1s = 0x0F000000u, .rc = 0x30000000u };   /* 0xC0000000 = RO */
static sim_reg M = { "M", &m_store, { .rw = 0x000000FFu, .w1c = 0x0000FF00u, .w0c = 0x00FF0000u,
                                      .w1s = 0x0F000000u, .rc = 0 }, NULL };  /* MIX 에서 RC 만 뺌 */

/* ---------------- Part B: 상태 레지스터 다루기 ---------------- */
/* 주어진 버그 코드 1 — 습관적인 SR |= flag */
static void sr_clear_buggy(uint32_t flag) { reg_write(&SR, reg_read(&SR) | flag); }
/* 주어진 버그 코드 2 — TIMx->SR &= ~flag (rc_w0 레지스터) */
static void tim_clear_buggy(uint32_t flag) { reg_write(&TSR, reg_read(&TSR) & ~flag); }

/* B1: W1C 상태 레지스터 SR 에서 flag 비트만 지운다. 다른 플래그는 절대 건드리지 않는다. */
static void sr_clear(uint32_t flag) {
    reg_write(&SR, flag);                       /* 지울 비트만 1, 나머지 0 = 무시 */
}
/* B2: rc_w0 타이머 상태 레지스터 TSR 에서 flag 비트만 지운다. */
static void tim_clear(uint32_t flag) {
    reg_write(&TSR, ~flag);                     /* 지울 비트만 0, 나머지 1 = 무시 */
}
/* B3: SR 을 딱 한 번 읽고, 그때 보인 이벤트만 정확히 ack 한 뒤 그 집합을 돌려준다.
 *     읽은 뒤 새로 생긴 이벤트는 SR 에 남아야 한다. 이벤트가 없으면 쓰지 않는다. */
static uint32_t sr_ack_seen(void) {
    uint32_t seen = reg_read(&SR) & SR_EVENTS;  /* RO BUSY 비트는 버린다 */
    if (seen) reg_write(&SR, seen);             /* 본 것만 1 → 본 것만 지움 */
    return seen;
}
/* B4: 아무 레지스터에서나 안전한 RMW — RW 비트 중 clr 를 지우고 set 을 켠다.
 *     다른 의미의 비트는 "써도 아무 일 없는 값" 으로 쓴다. 다음이면 접근 없이 false:
 *     ① 읽기에 부작용이 있는 레지스터(rc != 0) ② clr/set 에 RW 가 아닌 비트가 있음 */
static bool reg_modify(sim_reg *r, uint32_t clr, uint32_t set) {
    if (r->spec.rc) return false;                       /* 읽는 순간 이벤트가 사라진다 */
    if ((clr | set) & ~r->spec.rw) return false;
    uint32_t v = reg_read(r);
    v = (v & r->spec.rw & ~clr) | set;                  /* W1C·W1S 자리는 0 (무해)   */
    v |= r->spec.w0c;                                   /* W0C 자리는 1 (무해)        */
    reg_write(r, v);
    return true;
}

/* ---------------- Part C: 드라이버와 ISR ---------------- */
static struct { int rx, tx, err, tx_left; } drv;
static bool g_masked, g_pending;
static void dev_isr(void);
static void sim_irq(void) {                     /* 인터럽트 발생 (마스크 중이면 펜딩) */
    if (g_masked) { g_pending = true; return; }
    trace_tag("IRQ");
    dev_isr();
}
/* Cortex-M __disable_irq() / __enable_irq() 흉내 — enable 직후 펜딩 인터럽트가 들어온다 */
static void irq_disable(void) { trace_tag("DI"); g_masked = true; }
static void irq_enable(void) {
    trace_tag("EI"); g_masked = false;
    if (g_pending) { g_pending = false; sim_irq(); }
}
/* 주어진 버그 ISR — 플래그마다 SR 을 다시 읽고 |= 로 지운다 */
static void dev_isr_buggy(void) {
    if (reg_read(&SR) & SR_RX)  { drv.rx++;  sr_clear_buggy(SR_RX); }
    if (reg_read(&SR) & SR_TX)  { drv.tx++;  sr_clear_buggy(SR_TX); }
    if (reg_read(&SR) & SR_ERR) { drv.err++; sr_clear_buggy(SR_ERR); }
}
/* 주어진 버그 코드 — main 문맥에서 CR 을 평범한 RMW 로 갱신 */
static void dev_rxie_buggy(bool on) {
    uint32_t v = reg_read(&CR);
    reg_write(&CR, on ? (v | CR_RXIE) : (v & ~CR_RXIE));
}

/* C1: ISR — SR 을 한 번 읽어 본 이벤트만 ack, RX/ERR 카운트, TX 완료면 tx_left 를 하나 줄이고
 *     보낼 게 없으면(tx_left == 0) TXIE 를 끈다 (CR 을 RMW 하지 말고 CRCLR alias 로). */
static void dev_isr(void) {
    uint32_t s = sr_ack_seen();
    if (s & SR_RX)  drv.rx++;
    if (s & SR_ERR) drv.err++;
    if (s & SR_TX) {
        drv.tx++;
        if (drv.tx_left > 0) drv.tx_left--;
        if (drv.tx_left == 0) reg_write(&CRCLR, CR_TXIE);
    }
}
/* C2: main 문맥에서 RXIE 를 켜고/끈다. ISR 이 언제 끼어들어도 ISR 의 CR 변경을 잃지 않게. */
static void dev_rxie(bool on) {
    reg_write(on ? &CRSET : &CRCLR, CR_RXIE);   /* 읽기 없음 → 경쟁 창(window) 없음 */
}
/* C3: CR.MODE(2비트 필드)를 mode 로 바꾼다. 필드는 alias 한 번으로 못 바꾸므로
 *     임계 구역 안에서 reg_modify. CR 의 OVF 플래그는 지우지 않는다. */
static void dev_set_mode(uint32_t mode) {
    irq_disable();
    (void)reg_modify(&CR, CR_MODE_Msk, (mode << CR_MODE_Pos) & CR_MODE_Msk);
    irq_enable();
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(g_trace, (s)) == 0)

static void reset(void) {
    g_trace[0] = '\0';
    sr_store = cr_store = tsr_store = m_store = 0;
    SR.after_read = CR.after_read = TSR.after_read = M.after_read = NULL;
    memset(&drv, 0, sizeof drv);
    g_masked = g_pending = false;
}
static void hw_tx_done(void)  { hw_raise(&SR, SR_TX); }
static void hw_err(void)      { hw_raise(&SR, SR_ERR); }
static void hw_cc1(void)      { hw_raise(&TSR, TIM_CC1IF); }

int main(void) {
    /* ---- Part A: spec_valid ---- */
    CHECK(spec_valid(&MIX));
    CHECK(spec_valid(&(reg_spec){ 0 }));                       /* 전부 RO 도 유효 */
    CHECK(!spec_valid(&(reg_spec){ .rw = 0x1u, .w1c = 0x1u }));
    CHECK(!spec_valid(&(reg_spec){ .w0c = 0x10u, .rc = 0x30u }));
    CHECK(!spec_valid(&(reg_spec){ .w1s = 0x80000000u, .rw = 0xFFFFFFFFu }));

    /* ---- Part A: apply_write 표 (MIX: RW 0xFF | W1C 0xFF00 | W0C 0xFF0000 | W1S 0x0F000000 | RC 0x30000000 | RO 0xC0000000) ---- */
    static const struct { uint32_t cur, w, want; } tw[] = {
        { 0x00000000u, 0x00000000u, 0x00000000u },
        { 0xFFFFFFFFu, 0x00000000u, 0xFF00FF00u },  /* RW→0, W0C 전부 지워짐, 나머지 유지 */
        { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFF00FFu },  /* W1C 전부 지워짐, W0C 유지          */
        { 0x00000000u, 0xFFFFFFFFu, 0x0F0000FFu },  /* RW, W1S 만 켜짐                     */
        { 0x00F0F000u, 0x00003000u, 0x0000C000u },  /* W1C 2비트 지움, W0C(0 씀) 모두 지움  */
        { 0x00FF0000u, 0xFFFEFFFFu, 0x0FFE00FFu },  /* W0C bit16 만 지움 (TIM 방식)          */
        { 0x0A00A5A5u, 0x0500005Au, 0x0F00A55Au },  /* W1S 누적, W1C 0 씀 → 유지            */
        { 0xC0000000u, 0x3F000000u, 0xCF000000u },  /* RO, RC 는 쓰기로 안 바뀜              */
    };
    for (size_t i = 0; i < sizeof tw / sizeof tw[0]; i++)
        CHECK(apply_write(&MIX, tw[i].cur, tw[i].w) == tw[i].want);
    CHECK(apply_read(&MIX, 0xFFFFFFFFu) == 0xCFFFFFFFu);      /* RC 두 비트만 사라짐 */
    CHECK(apply_read(&MIX, 0x0000F00Fu) == 0x0000F00Fu);

    /* ---- Part B1: W1C 에 |= 하면 모든 pending 이 지워진다 (재현) → 고침 ---- */
    reset(); sr_store = SR_RX | SR_TX | SR_ERR | SR_BUSY;
    sr_clear_buggy(SR_RX);
    CHECK(sr_store == SR_BUSY);                                /* TX, ERR 까지 사라짐! */
    CHECK(TRACE_IS("rSR=107 wSR=107"));
    reset(); sr_store = SR_RX | SR_TX | SR_ERR | SR_BUSY;
    sr_clear(SR_RX);
    CHECK(sr_store == (SR_TX | SR_ERR | SR_BUSY));
    CHECK(TRACE_IS("wSR=1"));                                  /* 읽기 없이 쓰기 한 번 */

    /* ---- Part B2: rc_w0 (STM32 TIMx_SR) — 관습이 반대 ---- */
    reset(); tsr_store = TIM_UIF | TIM_CC2IF;
    reg_write(&TSR, TIM_UIF);                                  /* W1C 습관 그대로 쓰면? */
    CHECK(tsr_store == TIM_UIF);                               /* UIF 는 남고 CC2IF 가 지워짐 */
    reset(); tsr_store = TIM_UIF | TIM_CC2IF; TSR.after_read = hw_cc1;
    tim_clear_buggy(TIM_UIF);                                  /* SR &= ~UIF, 사이에 CC1IF 발생 */
    CHECK(tsr_store == TIM_CC2IF);                             /* CC1IF 유실 */
    CHECK(TRACE_IS("rTSR=5 HW:TSR|=2 wTSR=4"));
    reset(); tsr_store = TIM_UIF | TIM_CC2IF; TSR.after_read = hw_cc1;
    tim_clear(TIM_UIF);
    if (TSR.after_read) { TSR.after_read = NULL; hw_cc1(); }   /* 읽기가 없으면 이벤트는 그 뒤에 온다 */
    CHECK(tsr_store == (TIM_CC1IF | TIM_CC2IF));
    CHECK(TRACE_IS("wTSR=FFFFFFFE HW:TSR|=2"));

    /* ---- Part B3: 본 것만 ack ---- */
    reset(); sr_store = SR_RX | SR_BUSY; SR.after_read = hw_tx_done;
    CHECK(sr_ack_seen() == SR_RX);
    CHECK(sr_store == (SR_TX | SR_BUSY));                      /* 읽은 뒤 온 TX 는 살아 있다 */
    CHECK(TRACE_IS("rSR=101 HW:SR|=2 wSR=1"));
    reset(); sr_store = SR_BUSY;
    CHECK(sr_ack_seen() == 0);
    CHECK(TRACE_IS("rSR=100"));                                /* 이벤트 없으면 쓰지 않는다 */

    /* ---- Part B4: 혼합 레지스터의 안전 RMW ---- */
    reset(); cr_store = CR_EN | CR_TXIE | CR_OVF;
    reg_write(&CR, (reg_read(&CR) & ~CR_TXIE) | CR_RXIE);      /* 평범한 RMW (재현) */
    CHECK(cr_store == (CR_EN | CR_RXIE));                      /* OVF 가 조용히 지워짐 */
    reset(); cr_store = CR_EN | CR_TXIE | CR_OVF;
    CHECK(reg_modify(&CR, CR_TXIE, CR_RXIE));
    CHECK(cr_store == (CR_EN | CR_RXIE | CR_OVF));
    CHECK(TRACE_IS("rCR=105 wCR=3"));
    reset(); m_store = 0xFFFFFFFFu;
    CHECK(reg_modify(&M, 0x0Fu, 0x00u));
    CHECK(m_store == 0xFFFFFFF0u);                             /* RW 외 비트 전부 보존 */
    CHECK(TRACE_IS("rM=FFFFFFFF wM=FF00F0"));
    reset(); m_store = 0x12u;
    CHECK(!reg_modify(&M, 0, 0x100u) && m_store == 0x12u && TRACE_IS(""));  /* W1C 비트 요청 거부 */
    reset();
    { sim_reg ev = { "EV", &m_store, { .rw = 0xFu, .rc = 0xF0u }, NULL };
      m_store = 0x35u;
      CHECK(!reg_modify(&ev, 0x1u, 0x2u) && m_store == 0x35u && TRACE_IS("")); }  /* RC 있으면 거부 */

    /* ---- Part C1: ISR — |= 버그 ISR (재현) vs 고친 ISR ---- */
    reset(); sr_store = SR_RX | SR_TX | SR_ERR;
    dev_isr_buggy();
    CHECK(drv.rx == 1 && drv.tx == 0 && drv.err == 0);         /* TX, ERR 를 못 봄 */
    reset(); sr_store = SR_RX | SR_TX | SR_ERR; cr_store = CR_EN | CR_TXIE; drv.tx_left = 2;
    dev_isr();
    CHECK(drv.rx == 1 && drv.tx == 1 && drv.err == 1 && drv.tx_left == 1);
    CHECK(sr_store == 0 && cr_store == (CR_EN | CR_TXIE));
    CHECK(TRACE_IS("rSR=7 wSR=7"));
    reset(); sr_store = SR_RX; SR.after_read = hw_err;          /* ISR 이 읽은 직후 ERR 발생 */
    dev_isr();
    CHECK(drv.rx == 1 && drv.err == 0 && sr_store == SR_ERR);   /* 펜딩으로 남아야 → 재진입 */
    dev_isr();
    CHECK(drv.err == 1 && sr_store == 0);
    reset(); sr_store = SR_TX; cr_store = CR_EN | CR_RXIE | CR_TXIE; drv.tx_left = 1;
    dev_isr();
    CHECK(drv.tx_left == 0 && cr_store == (CR_EN | CR_RXIE));
    CHECK(TRACE_IS("rSR=2 wSR=2 wCRCLR=4"));

    /* ---- Part C2: main 의 RMW 와 ISR 경쟁 ---- */
    reset(); cr_store = CR_EN | CR_TXIE; sr_store = SR_TX; CR.after_read = sim_irq;
    dev_rxie_buggy(true);                                       /* 읽기 직후 TX 완료 ISR */
    CHECK(cr_store == (CR_EN | CR_RXIE | CR_TXIE));             /* ISR 이 끈 TXIE 가 되살아남 */
    CHECK(TRACE_IS("rCR=5 IRQ rSR=2 wSR=2 wCRCLR=4 wCR=7"));
    reset(); cr_store = CR_EN | CR_TXIE; sr_store = SR_TX; CR.after_read = sim_irq;
    dev_rxie(true);
    if (CR.after_read) { CR.after_read = NULL; sim_irq(); }     /* 읽기가 없으면 IRQ 는 그 뒤에 */
    CHECK(cr_store == (CR_EN | CR_RXIE));
    CHECK(TRACE_IS("wCRSET=2 IRQ rSR=2 wSR=2 wCRCLR=4"));
    reset(); cr_store = CR_EN | CR_RXIE | CR_OVF;
    dev_rxie(false);
    CHECK(cr_store == (CR_EN | CR_OVF) && TRACE_IS("wCRCLR=2"));

    /* ---- Part C3: 필드 변경은 임계 구역 + reg_modify ---- */
    reset(); cr_store = CR_EN | CR_TXIE | (1u << CR_MODE_Pos) | CR_OVF;
    sr_store = SR_TX; CR.after_read = sim_irq;
    dev_set_mode(2);
    CHECK(cr_store == (CR_EN | (2u << CR_MODE_Pos) | CR_OVF));
    CHECK(TRACE_IS("DI rCR=115 wCR=25 EI IRQ rSR=2 wSR=2 wCRCLR=4"));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
