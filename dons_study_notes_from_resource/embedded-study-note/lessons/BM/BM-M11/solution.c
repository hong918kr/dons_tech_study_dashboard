/* BM-M11 저전력 모드: WFI/WFE, Stop/Standby, 슬립 진입 레이스
 *
 * Part A — 전력 상태 머신: Run / Sleep(WFI) / Stop / Standby 의 전류·웨이크업 지연·보존 범위·복귀 지점,
 *          duty cycle 별 평균 전류와 배터리 수명 (정수 연산: 전류 nA, 시간 µs, 전하 nA·µs).
 * Part B — 슬립 진입 레이스: Cortex-M 코어를 "연산 단위" 로 흉내 내는 결정적 시뮬레이터.
 *          IRQ 를 시퀀스의 모든 지점에 하나씩 주입해, 어느 idle 패턴이 웨이크업을 잃는지 센다.
 * Part C — 배터리 센서 노드의 슬립 매니저: 제약(RAM 보존, 고속 클럭, 깨울 소스, 지연)과
 *          손익분기(break-even) 시간으로 가장 깊은 모드를 고른다.
 *
 * 모드 수치는 STM32L4 계열 데이터시트의 "자릿수" 를 참고한 이 레슨의 모델 값이다 (특정 칩 실측 아님).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 주입 표 출력)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: 전력 모드 표 (주어진 모델 값) ======================= */
typedef enum { PM_RUN, PM_SLEEP, PM_STOP, PM_STANDBY, PM_COUNT } pmode_t;
typedef enum { RES_NONE, RES_AFTER_WFI, RES_RESET_VECTOR } resume_t;
enum {                                  /* 웨이크업 소스 (비트마스크) */
    SRC_TIM2      = 1u << 0,            /* 일반 타이머: APB 클럭 필요 → Sleep 에서만 */
    SRC_USART1    = 1u << 1,            /* 일반 UART: 고속 클럭 필요 → Sleep 에서만 */
    SRC_EXTI_GPIO = 1u << 2,            /* GPIO → EXTI 라인 (클럭 없이 동작) */
    SRC_LPUART    = 1u << 3,            /* LSE 로 도는 저전력 UART */
    SRC_RTC       = 1u << 4,            /* RTC 알람 / 웨이크업 타이머 (LSE 32.768 kHz) */
    SRC_WKUP_PIN  = 1u << 5,            /* 전용 WKUPx 핀 */
};
#define SRC_ALL 0x3Fu

typedef struct {
    const char *name;
    uint32_t i_nA;        /* 이 모드에 머무는 동안 전류 [nA] */
    uint32_t wake_us;     /* 하드웨어 웨이크업 지연 (이 동안 Run 전류로 계산) */
    uint32_t boot_us;     /* 깬 뒤 소프트웨어 재초기화 (Standby: 리셋 → startup → 클럭/센서 재설정) */
    bool ram_kept;        /* SRAM 내용 유지 */
    bool hsclk_kept;      /* PLL/HSE 같은 고속 클럭 유지 */
    resume_t resume;      /* 깨면 어디서부터 실행하나 */
    uint32_t wake_mask;   /* 이 모드를 깨울 수 있는 소스 */
} mode_info_t;

static const mode_info_t MODE[PM_COUNT] = {
    /* name       i_nA      wake  boot  ram    hsclk  resume             wake_mask */
    { "Run",     1500000u,   0u,    0u, true,  true,  RES_NONE,         0u },
    { "Sleep",    400000u,   1u,    0u, true,  true,  RES_AFTER_WFI,    SRC_ALL },
    { "Stop",       1800u,   5u,    0u, true,  false, RES_AFTER_WFI,
      SRC_EXTI_GPIO | SRC_LPUART | SRC_RTC | SRC_WKUP_PIN },
    { "Standby",     400u,  50u, 2000u, false, false, RES_RESET_VECTOR, SRC_RTC | SRC_WKUP_PIN },
};

typedef struct {
    uint64_t t_us;          /* 현재 시각 */
    uint64_t q;             /* 누적 전하 [nA·µs] (1 nA·µs = 1e-15 C) */
    uint32_t sram_var;      /* SRAM 에 있는 변수 하나 (Standby 면 사라진다) */
    uint32_t bkp_dr0;       /* RTC 백업 레지스터 (Standby 에서도 유지) */
    bool hsclk_on;          /* PLL 등 고속 클럭이 켜져 있나 */
    resume_t resumed;       /* 마지막 웨이크업의 복귀 지점 */
    bool sbf;               /* PWR_SR.SBF: "Standby 에서 깨어났음" 플래그 */
    uint32_t woke_by;       /* 마지막으로 깨운 소스 */
} mcu_t;
typedef struct { uint64_t t_us; uint32_t src; } wake_ev_t;

static void mcu_reset(mcu_t *m) {
    memset(m, 0, sizeof *m);
    m->hsclk_on = true;
}
static void run_for(mcu_t *m, uint64_t us) {          /* Run 모드에서 us 동안 일한다 */
    m->q += (uint64_t)MODE[PM_RUN].i_nA * us;
    m->t_us += us;
}

/* TODO A1: mode 로 들어가 잔다. ev[0..n) (시각 오름차순) 중 m->t_us 이후에 오고, 이 모드를 깨울 수 있는 첫 이벤트에서 깬다.
 * 전하: 잠든 구간은 그 모드 전류, wake_us + boot_us 는 Run 전류. 깬 시각 = 이벤트 + wake_us + boot_us.
 * 효과: RAM 미보존 → sram_var = 0, 고속 클럭 미보존 → hsclk_on = false, resumed/woke_by 기록,
 *       Standby 면 sbf = true. bkp_dr0 는 항상 유지.
 * 반환: 깨운 이벤트의 인덱스. mode 가 PM_RUN/범위 밖이거나 깨울 이벤트가 없으면 -1 (상태 변화 없음). */
static int pm_enter(mcu_t *m, pmode_t mode, const wake_ev_t *ev, int n) {
    if (mode <= PM_RUN || mode >= PM_COUNT) return -1;
    const mode_info_t *mi = &MODE[mode];
    for (int i = 0; i < n; i++) {
        if (ev[i].t_us < m->t_us || !(ev[i].src & mi->wake_mask)) continue;
        uint64_t over = (uint64_t)mi->wake_us + mi->boot_us;
        m->q += (uint64_t)mi->i_nA * (ev[i].t_us - m->t_us) + (uint64_t)MODE[PM_RUN].i_nA * over;
        m->t_us = ev[i].t_us + over;
        if (!mi->ram_kept) m->sram_var = 0u;
        if (!mi->hsclk_kept) m->hsclk_on = false;
        if (mode == PM_STANDBY) m->sbf = true;
        m->resumed = mi->resume;
        m->woke_by = ev[i].src;
        return i;
    }
    return -1;
}

/* TODO A2: 주기 T 마다: 깨어나서 t_act 동안 Run, 나머지는 mode 로 잔다 (깰 때 wake_us+boot_us 는 Run 전류).
 * 한 주기의 전하 [nA·µs]. mode == PM_RUN 이거나 t_act + 오버헤드 >= T 면 주기 내내 Run. */
static uint64_t period_charge(uint64_t T, uint64_t t_act, pmode_t mode) {
    uint64_t irun = MODE[PM_RUN].i_nA;
    if (mode <= PM_RUN || mode >= PM_COUNT) return irun * T;
    uint64_t over = (uint64_t)MODE[mode].wake_us + MODE[mode].boot_us;
    if (t_act + over >= T) return irun * T;
    return irun * (t_act + over) + (uint64_t)MODE[mode].i_nA * (T - t_act - over);
}
/* TODO A3: 평균 전류 [nA] = 한 주기 전하 / T, 반올림. */
static uint32_t avg_nA(uint64_t T, uint64_t t_act, pmode_t mode) {
    return (uint32_t)((period_charge(T, t_act, mode) + T / 2u) / T);
}
/* TODO A3: 배터리 수명 [일] = 용량[mAh] / 평균전류[mA] / 24, 내림. (1 mAh = 1e6 nA·h) */
static uint32_t battery_days(uint32_t cap_mAh, uint32_t avg) {
    if (avg == 0u) return UINT32_MAX;
    return (uint32_t)((uint64_t)cap_mAh * 1000000u / avg / 24u);
}

/* ======================= Part B: 슬립 진입 레이스 시뮬레이터 ======================= */
/* 코어 상태. 한 "연산"(op) = CPSID / CPSIE / rF(플래그 읽고 분기) / WFI / WFE / SEV.
 * IRQ 는 inject_at 번째 op 가 끝난 직후 "도착" 한다 (0 = idle 함수 시작 전). 잠든 WFI/WFE 는
 * 잠든 직후가 그 op 의 끝이다 → "자는 동안 도착" 도 하나의 주입 지점이다. */
typedef struct {
    int primask;            /* 1 = __disable_irq() 상태 (CPSID i) */
    int pending;            /* IRQ 가 NVIC 에 펜딩 */
    int event;              /* 이벤트 레지스터 (B1.5.18) */
    int sevonpend;          /* SCB->SCR.SEVONPEND */
    volatile int flag;      /* ISR 이 세우는 "일 있음" 플래그 */
    int ops, inject_at;
    int lost;               /* 이미 도착한 이벤트를 두고 잠든 횟수 (= 웨이크업 유실) */
    int backstop;           /* 아무것도 못 깨워 다음 RTC 백스톱(1 s 뒤)까지 잔 횟수 */
    int isr_runs;
    char tr[200];
} cpu_t;
static cpu_t C;

static void tr(const char *s) {
    size_t n = strlen(C.tr);
    if (n && n + 1 < sizeof C.tr) C.tr[n++] = ' ';
    while (*s && n + 1 < sizeof C.tr) C.tr[n++] = *s++;
    C.tr[n] = '\0';
}
static void isr(void) {             /* EXTI ISR: 플래그를 세운다. 예외 진입/복귀는 이벤트 레지스터를 세운다 */
    tr("IRQ");
    C.pending = 0;
    C.flag = 1;
    C.isr_runs++;
    C.event = 1;                    /* B1.5.18: 선점 예외 = WFE wake-up event, exception return 도 set */
}
static void irq_arrive(void) {      /* 하드웨어가 IRQ 를 올린다 */
    if (C.sevonpend && !C.pending) C.event = 1;   /* Inactive→Pending 전이 = 이벤트 (SEVONPEND) */
    C.pending = 1;
    if (!C.primask) isr();          /* 막혀 있지 않으면 즉시 선점 */
    else tr("pend");                /* PRIMASK=1: 펜딩으로 남는다 */
}
static void step(void) {
    if (++C.ops == C.inject_at) irq_arrive();
}
static void sim_reset(int inject_at) {
    memset(&C, 0, sizeof C);
    C.inject_at = inject_at;
    if (inject_at == 0) irq_arrive();         /* idle 진입 전에 이미 도착 */
}

static void CPSID(void) { C.primask = 1; tr("CPSID"); step(); }
static void CPSIE(void) {
    C.primask = 0; tr("CPSIE");
    if (C.pending) isr();                     /* 펜딩이던 ISR 이 바로 실행된다 */
    step();
}
static int rd_flag(void) {                    /* ldr + cmp + 분기 한 묶음 */
    if (C.backstop >= 3) return 1;            /* 안전장치: 백스톱 3번이면 포기 (테스트가 FAIL 로 잡는다) */
    int v = C.flag; tr("rF"); step(); return v;
}
static void SEV(void) { C.event = 1; tr("SEV"); step(); }

/* TODO B1: WFI 의 wake-up 조건 (ARMv7-M ARM B1.5.19).
 * isr_ran = "자는 동안 ISR 이 실제로 실행됐다". WFI 실행 순간과 자는 동안 모두 이 함수로 판단한다.
 * PRIMASK=1 이라 ISR 이 못 도는 경우를 생각하라. */
static bool wfi_wake_cond(bool isr_ran) {
    return isr_ran || C.pending;              /* PRIMASK 와 무관하게 "선점할 만한" IRQ 가 펜딩이면 깬다 */
}
/* TODO B2: WFE 의 wake-up 조건 (B1.5.18). 이벤트 레지스터는 isr()/irq_arrive()/SEV() 가 이미 관리한다. */
static bool wfe_wake_cond(void) {
    return C.event != 0;
}

static void sim_sleep(bool is_wfe) {          /* 주어진 코드: 잠들고, 자는 동안 도착을 처리하고, 깬다 */
    tr("zz");
    if (C.flag || C.pending) C.lost++;        /* 이미 와 있는 이벤트를 두고 잠든다 = 유실 */
    int before = C.isr_runs;
    step();                                   /* ← "자는 동안 IRQ 도착" 지점 */
    bool woke = is_wfe ? wfe_wake_cond() : wfi_wake_cond(C.isr_runs != before);
    if (!woke) { tr("RTC"); C.backstop++; }   /* 다음 RTC 백스톱까지 잔다 (1 s 지연) */
}
static void WFI(void) {
    tr("WFI");
    if (wfi_wake_cond(false)) { step(); return; }   /* 깨울 것이 이미 있으면 잠들지 않는다 */
    sim_sleep(false);
}
static void WFE(void) {
    tr("WFE");
    if (wfe_wake_cond()) { C.event = 0; step(); return; }   /* 이벤트가 있으면 지우고 바로 통과 */
    sim_sleep(true);
    C.event = 0;                              /* 깨면서 소비 (구현 정의 — 루프는 어느 쪽이든 견딘다) */
}

/* ---- idle 패턴들 ---- */
static void idle_naive(void) {                /* 주어진 코드: 레이스 있음 */
    if (!rd_flag()) WFI();
}
static void idle_enable_first(void) {         /* 주어진 코드: 틀린 고침 (WFI 전에 켠다) */
    CPSID();
    int f = rd_flag();
    CPSIE();
    if (!f) WFI();
}
static void idle_wfe_loop(void) {             /* 주어진 코드: 인터럽트를 켠 채 WFE 루프 */
    while (!rd_flag()) WFE();
}
/* TODO B3: 정석 — 인터럽트를 끈 채 확인하고 WFI, 그 다음 켠다. */
static void idle_correct(void) {
    CPSID();
    if (!rd_flag()) WFI();
    CPSIE();
}
/* TODO B4: PRIMASK 를 건 채 WFE 로 자는 원자적 idle (Zephyr k_cpu_atomic_idle 방식).
 * 마스크된 IRQ 가 WFE 를 깨우려면 무엇이 필요한가? (SCB->SCR 비트 = C.sevonpend) */
static void idle_atomic_wfe(void) {
    C.sevonpend = 1;                          /* SCB->SCR |= SCB_SCR_SEVONPEND_Msk (보통 부팅 때 한 번) */
    CPSID();
    if (!rd_flag()) WFE();
    CPSIE();
}

/* 메인 루프 한 번: idle 을 최대 3번 부르며 플래그를 기다렸다 소비한다. 반환: 처리했으면 1 */
static int main_loop_once(void (*idle)(void)) {
    for (int i = 0; i < 3; i++) {
        idle();
        if (C.flag) { C.flag = 0; return 1; }
    }
    return 0;
}
/* 모든 주입 지점(0..IRQ 없는 실행의 op 수)에 대해 돌려, 유실이 난 지점을 비트마스크로 돌려준다. */
static unsigned sweep(void (*idle)(void), int *npoints, int *all_handled) {
    sim_reset(-1);
    idle();                                   /* IRQ 없이 한 번: op 수 = 주입 지점 수 - 1 */
    int nops = C.ops;
    unsigned mask = 0u;
    *all_handled = 1;
    for (int k = 0; k <= nops; k++) {
        sim_reset(k);
        int h = main_loop_once(idle);
        if (C.lost) mask |= 1u << k;
        if (!h) *all_handled = 0;
    }
    *npoints = nops + 1;
    return mask;
}

/* ======================= Part C: 슬립 매니저 ======================= */
typedef struct {
    uint64_t idle_us;       /* 다음 일까지 남은 시간 */
    bool need_ram;          /* SRAM 상태를 유지해야 함 */
    bool need_hsclk;        /* 고속 클럭이 필요한 주변장치가 동작 중 (USART1 DMA 수신 등) */
    uint32_t wake_src;      /* 반드시 깨울 수 있어야 하는 소스들 */
    uint32_t max_wake_us;   /* 허용 웨이크업 지연 (wake_us + boot_us) */
} pm_req_t;

/* idle_us 동안 mode 로 지낼 때의 전하 (깨는 오버헤드 포함). 오버헤드가 idle 보다 길면 UINT64_MAX. */
static uint64_t idle_charge(pmode_t mode, uint64_t idle_us) {
    uint64_t over = (uint64_t)MODE[mode].wake_us + MODE[mode].boot_us;
    if (mode == PM_RUN) return (uint64_t)MODE[PM_RUN].i_nA * idle_us;
    if (over > idle_us) return UINT64_MAX;
    return (uint64_t)MODE[mode].i_nA * (idle_us - over) + (uint64_t)MODE[PM_RUN].i_nA * over;
}
/* TODO C1: 제약을 만족하는 모드(Sleep/Stop/Standby) 중 idle_charge 가 가장 작은 것. 같으면 얕은 쪽.
 * 제약: RAM/고속클럭 보존, wake_src 전부 지원, wake_us+boot_us <= max_wake_us, 오버헤드 <= idle_us.
 * 하나도 없으면 PM_RUN. */
static pmode_t pm_choose(const pm_req_t *r) {
    pmode_t best = PM_RUN;
    uint64_t best_q = idle_charge(PM_RUN, r->idle_us);
    for (int m = PM_SLEEP; m < PM_COUNT; m++) {
        const mode_info_t *mi = &MODE[m];
        if (r->need_ram && !mi->ram_kept) continue;
        if (r->need_hsclk && !mi->hsclk_kept) continue;
        if ((mi->wake_mask & r->wake_src) != r->wake_src) continue;
        if ((uint64_t)mi->wake_us + mi->boot_us > r->max_wake_us) continue;
        uint64_t q = idle_charge((pmode_t)m, r->idle_us);
        if (q < best_q) { best_q = q; best = (pmode_t)m; }
    }
    return best;
}
/* TODO C2: 손익분기 idle 시간 [µs]: deep 모드가 shallow 모드보다 전하가 "같거나 적어지는" 가장 짧은 idle.
 * I_s(t - o_s) + I_r o_s = I_d(t - o_d) + I_r o_d  →  t = ((I_r - I_d) o_d - (I_r - I_s) o_s) / (I_s - I_d), 올림.
 * (o = wake_us + boot_us, I_r = Run 전류.) 결과가 o_d 보다 작으면 o_d. */
static uint64_t breakeven_us(pmode_t shallow, pmode_t deep) {
    uint64_t ir = MODE[PM_RUN].i_nA, is = MODE[shallow].i_nA, id = MODE[deep].i_nA;
    uint64_t os = (uint64_t)MODE[shallow].wake_us + MODE[shallow].boot_us;
    uint64_t od = (uint64_t)MODE[deep].wake_us + MODE[deep].boot_us;
    uint64_t num = (ir - id) * od - (ir - is) * os;
    uint64_t t = (num + (is - id) - 1u) / (is - id);
    return t < od ? od : t;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(C.tr, (s)) == 0)

static void print_sweep(const char *name, void (*idle)(void)) {
    int np, h;
    sim_reset(-1);
    idle();
    printf("%-18s IRQ 없음: %s\n", name, C.tr);
    unsigned mask = sweep(idle, &np, &h);
    for (int k = 0; k < np; k++) {
        sim_reset(k);
        main_loop_once(idle);
        printf("  k=%d %-5s %s\n", k, (mask >> k) & 1u ? "LOST" : "ok", C.tr);
    }
}

int main(int argc, char **argv) {
    /* ---------- Part A ---------- */
    mcu_t m;
    mcu_reset(&m);
    m.sram_var = 0xC0FFEEu; m.bkp_dr0 = 42u;
    wake_ev_t e1[] = { { 100u, SRC_TIM2 } };
    CHECK(pm_enter(&m, PM_SLEEP, e1, 1) == 0);
    CHECK(m.t_us == 101u && m.q == 400000ull * 100u + 1500000ull * 1u);
    CHECK(m.resumed == RES_AFTER_WFI && m.sram_var == 0xC0FFEEu && m.hsclk_on && !m.sbf);

    mcu_reset(&m);
    m.sram_var = 0xC0FFEEu;
    wake_ev_t e2[] = { { 200u, SRC_TIM2 }, { 300u, SRC_USART1 }, { 500u, SRC_EXTI_GPIO } };
    CHECK(pm_enter(&m, PM_STOP, e2, 3) == 2);           /* TIM2/USART1 은 Stop 을 못 깨운다 */
    CHECK(m.t_us == 505u && m.q == 1800ull * 500u + 1500000ull * 5u);
    CHECK(m.woke_by == SRC_EXTI_GPIO && m.sram_var == 0xC0FFEEu && !m.hsclk_on);   /* PLL 다시 켜야 함 */

    mcu_reset(&m);
    m.sram_var = 0xC0FFEEu; m.bkp_dr0 = 42u;
    wake_ev_t e3[] = { { 1000u, SRC_EXTI_GPIO }, { 5000u, SRC_RTC } };
    CHECK(pm_enter(&m, PM_STANDBY, e3, 2) == 1);        /* 일반 GPIO 는 Standby 를 못 깨운다 */
    CHECK(m.t_us == 5000u + 50u + 2000u && m.q == 400ull * 5000u + 1500000ull * 2050u);
    CHECK(m.resumed == RES_RESET_VECTOR && m.sbf && m.sram_var == 0u && m.bkp_dr0 == 42u);

    mcu_reset(&m);                                      /* 깨울 소스가 없는 모드로 들어가면 영영 못 깬다 */
    wake_ev_t e4[] = { { 10u, SRC_TIM2 } };
    CHECK(pm_enter(&m, PM_STOP, e4, 1) == -1 && m.t_us == 0u && m.q == 0u);
    CHECK(pm_enter(&m, PM_RUN, e1, 1) == -1);
    mcu_reset(&m);
    m.t_us = 150u;                                      /* 지난 이벤트는 무시 */
    wake_ev_t e5[] = { { 100u, SRC_RTC }, { 400u, SRC_RTC } };
    CHECK(pm_enter(&m, PM_STOP, e5, 2) == 1 && m.t_us == 405u);

    /* duty cycle → 평균 전류 (T = 10 s) */
    const uint64_t T = 10000000u;
    CHECK(period_charge(T, 1000u, PM_RUN) == 1500000ull * T);
    CHECK(period_charge(T, 1000u, PM_SLEEP) == 1500000ull * 1001u + 400000ull * (T - 1001u));
    CHECK(period_charge(T, 1000u, PM_STOP) == 1500000ull * 1005u + 1800ull * (T - 1005u));
    CHECK(period_charge(T, 1000u, PM_STANDBY) == 1500000ull * 3050u + 400ull * (T - 3050u));
    CHECK(period_charge(T, T, PM_STOP) == 1500000ull * T);          /* 쉴 틈이 없다 */
    CHECK(period_charge(3000u, 1000u, PM_STANDBY) == 1500000ull * 3000u);  /* 오버헤드가 주기를 넘음 */
    CHECK(avg_nA(T, 1000u, PM_RUN) == 1500000u);
    CHECK(avg_nA(T, 1000u, PM_SLEEP) == 400110u);
    CHECK(avg_nA(T, 1000u, PM_STOP) == 1951u);
    CHECK(avg_nA(T, 1000u, PM_STANDBY) == 857u);
    CHECK(avg_nA(T, 100000u, PM_STOP) == 16783u);
    CHECK(avg_nA(T, 100000u, PM_STANDBY) == 15703u);           /* D=1% 에서도 Standby 가 이긴다 */
    CHECK(battery_days(225u, 1951u) == 4805u);          /* CR2032 225 mAh 공칭 */
    CHECK(battery_days(225u, 1500000u) == 6u);
    CHECK(battery_days(225u, 0u) == UINT32_MAX);

    /* 시뮬레이터 3주기 == 닫힌 식 3배. RTC 알람은 웨이크업 지연만큼 일찍 (kT - 5 µs) 맞춰 일이 kT 에 시작 */
    mcu_reset(&m);
    wake_ev_t rtc[] = { { T - 5u, SRC_RTC }, { 2 * T - 5u, SRC_RTC }, { 3 * T - 5u, SRC_RTC } };
    for (int p = 0; p < 3; p++) {
        run_for(&m, 1000u);                             /* 센서 읽기 1 ms */
        CHECK(pm_enter(&m, PM_STOP, rtc, 3) == p);
        CHECK(m.t_us == (uint64_t)(p + 1) * T);
    }
    CHECK(m.q == 3u * period_charge(T, 1000u, PM_STOP));

    /* ---------- Part B ---------- */
    int np, h;
    unsigned lm;
    lm = sweep(idle_naive, &np, &h);
    CHECK(np == 3 && lm == (1u << 1));                  /* 확인 직후 도착하면 유실 */
    sim_reset(1); main_loop_once(idle_naive);
    CHECK(TRACE_IS("rF IRQ WFI zz RTC") && C.backstop == 1);

    lm = sweep(idle_enable_first, &np, &h);
    CHECK(np == 5 && lm == ((1u << 1) | (1u << 2) | (1u << 3)));   /* 더 나빠졌다 */

    lm = sweep(idle_correct, &np, &h);
    CHECK(np == 5 && lm == 0u && h == 1);               /* 모든 지점에서 유실 0 */
    sim_reset(1); main_loop_once(idle_correct);
    CHECK(TRACE_IS("CPSID pend rF WFI CPSIE IRQ") && C.backstop == 0);   /* 펜딩이 WFI 를 바로 끝냄 */
    sim_reset(3); main_loop_once(idle_correct);
    CHECK(TRACE_IS("CPSID rF WFI zz pend CPSIE IRQ") && C.backstop == 0);  /* PRIMASK=1 인데도 깬다 */
    sim_reset(0); main_loop_once(idle_correct);
    CHECK(TRACE_IS("IRQ CPSID rF CPSIE"));
    CHECK(C.isr_runs == 1);

    lm = sweep(idle_wfe_loop, &np, &h);
    CHECK(lm == 0u && h == 1);                          /* 예외 진입/복귀가 이벤트 레지스터를 세운다 */
    sim_reset(1); main_loop_once(idle_wfe_loop);
    CHECK(TRACE_IS("rF IRQ WFE rF"));                   /* 이벤트가 서 있어 WFE 가 그냥 통과 */
    sim_reset(-1); C.event = 1; C.inject_at = 4;        /* 리셋 직후 이벤트 레지스터는 UNKNOWN(=1 일 수도) */
    main_loop_once(idle_wfe_loop);
    CHECK(TRACE_IS("rF WFE rF WFE zz IRQ rF"));         /* 첫 WFE 는 헛통과 → 루프라서 안전 */
    sim_reset(-1); C.event = 1;                          /* "SEV; WFE" = 이벤트 레지스터를 확실히 비우는 관용구 */
    SEV(); WFE();
    CHECK(C.event == 0 && TRACE_IS("SEV WFE"));
    WFE();
    CHECK(TRACE_IS("SEV WFE WFE zz RTC") && C.backstop == 1);   /* 이제 진짜로 잔다 */

    lm = sweep(idle_atomic_wfe, &np, &h);
    CHECK(np == 5 && lm == 0u && h == 1);
    sim_reset(2); main_loop_once(idle_atomic_wfe);
    CHECK(TRACE_IS("CPSID rF pend WFE CPSIE IRQ"));
    sim_reset(3); main_loop_once(idle_atomic_wfe);
    CHECK(TRACE_IS("CPSID rF WFE zz pend CPSIE IRQ") && C.backstop == 0);

    /* ---------- Part C ---------- */
    pm_req_t r = { 10000000u, true, false, SRC_RTC, 100000u };
    CHECK(pm_choose(&r) == PM_STOP);                    /* RAM 필요 → Standby 불가 */
    r.need_ram = false;
    CHECK(pm_choose(&r) == PM_STANDBY);                 /* 10 s 면 Standby 가 이득 */
    r.idle_us = 1000000u;
    CHECK(pm_choose(&r) == PM_STOP);                    /* 1 s 는 손익분기 미만 */
    r.idle_us = 10000000u; r.max_wake_us = 100u;
    CHECK(pm_choose(&r) == PM_STOP);                    /* 2 ms 부팅 지연 허용 안 됨 */
    r.max_wake_us = 100000u; r.wake_src = SRC_RTC | SRC_EXTI_GPIO;
    CHECK(pm_choose(&r) == PM_STOP);                    /* 일반 GPIO 버튼은 Standby 를 못 깨움 */
    r.wake_src = SRC_RTC | SRC_WKUP_PIN;
    CHECK(pm_choose(&r) == PM_STANDBY);                 /* 버튼을 WKUP 핀에 달면 가능 */
    pm_req_t u = { 50000u, true, true, SRC_USART1, 100000u };
    CHECK(pm_choose(&u) == PM_SLEEP);                   /* USART1 수신 중 → 고속 클럭 유지 */
    pm_req_t s = { 3u, true, false, SRC_RTC, 100000u };
    CHECK(pm_choose(&s) == PM_SLEEP);                   /* 3 µs: Stop 웨이크업(5 µs)보다 짧다 */
    s.idle_us = 0u;
    CHECK(pm_choose(&s) == PM_RUN);                     /* 잘 틈이 없다 */
    CHECK(breakeven_us(PM_STOP, PM_STANDBY) == 2190493u);
    CHECK(breakeven_us(PM_SLEEP, PM_STOP) == 17u);
    CHECK(idle_charge(PM_STANDBY, 2190493u) <= idle_charge(PM_STOP, 2190493u));
    CHECK(idle_charge(PM_STANDBY, 2190492u) > idle_charge(PM_STOP, 2190492u));
    CHECK(battery_days(225u, avg_nA(T, 3000u, PM_STANDBY)) > battery_days(225u, avg_nA(T, 3000u, PM_STOP)));

    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        print_sweep("idle_naive", idle_naive);
        print_sweep("idle_enable_first", idle_enable_first);
        print_sweep("idle_correct", idle_correct);
        print_sweep("idle_wfe_loop", idle_wfe_loop);
        print_sweep("idle_atomic_wfe", idle_atomic_wfe);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
