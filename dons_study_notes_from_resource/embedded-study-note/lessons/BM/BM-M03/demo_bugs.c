/* BM-M03 demo: EXTI 디바운서의 흔한 버그를 같은 mock 위에서 재현하고 피해를 센다.
 * solution.c 의 mock·드라이버·시뮬레이터를 그대로 가져온다 (main 이름만 바꿔서). 레슨 폴더에서 빌드:
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o /tmp/BM-M03_bugs && /tmp/BM-M03_bugs
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 순진한 설계: 하강 엣지만, 타이머 끝에 "아직 0 이면 PRESS" (확정 상태 추적 없음) ---------- */
static int v_ack_first;      /* 1 = ISR 에서 PR 먼저 지우고 마스크 (창이 생김) */
static int v_clear_on_unmask;/* 1 = unmask 직전에 PR 지움 */
static int v_nvic_mask;      /* 1 = EXTI IMR 대신 NVIC enable 비트로 막음 */
static int v_clear_nvic;     /* NVIC 방식일 때 재허용 전에 EXTI PR + NVIC 펜딩(ICPR) 지움 */
static uint32_t n_isr;
static void nv_exti_isr(void) {
    uint32_t bit = 1u << 13;
    n_isr++;
    if ((REG_READ(EXTI_PR) & bit) == 0u) return;
    if (v_nvic_mask) { g_nv_en[IRQ_EXTI15_10] = false; exti_ack(13); }
    else if (v_ack_first) { exti_ack(13); exti_mask(13); }
    else { exti_mask(13); exti_ack(13); }
    tim7_oneshot(DB_TICKS);
}
static void nv_tim_isr(void) {
    if ((REG_READ(TIM7_SR) & TIM_SR_UIF) == 0u) return;
    REG_WRITE(TIM7_SR, ~TIM_SR_UIF);
    if (btn_raw_pressed(PORT_C, 13)) ev_push(EV_PRESS, 'N', sim_now());
    if (v_nvic_mask) {
        if (v_clear_nvic) { exti_ack(13); g_nv_pend[IRQ_EXTI15_10] = false; }   /* EXTI PR + NVIC ICPR */
        g_nv_en[IRQ_EXTI15_10] = true;
    } else if (v_clear_on_unmask) exti_unmask(13);
    else REG_WRITE(EXTI_IMR, REG_READ(EXTI_IMR) | (1u << 13));
}
static void inj_bounce(void) {                 /* 바운스 한 번 (↑ 그리고 ↓) — 지금 이 순간 */
    pin_set(BTN_PORT, BTN_PIN, 1); exti_sample();
    pin_set(BTN_PORT, BTN_PIN, 0); exti_sample();
}
static void press_fixture(void) { sig_reset(12000, 1); sig_edge(1000, 0, 80); sig_edge(7000, 1, 80); }
static void run_naive(const char *name, bool latch, int ack_first, int clr, int nvic, int clr_nvic, int inject) {
    press_fixture();
    sim_start(0, latch);
    exti_config(PORT_C, 13, EXTI_FALLING);
    g_vec[IRQ_EXTI15_10] = nv_exti_isr; g_vec[IRQ_TIM7] = nv_tim_isr;
    nvic_lite_enable(IRQ_EXTI15_10); nvic_lite_enable(IRQ_TIM7);
    v_ack_first = ack_first; v_clear_on_unmask = clr; v_nvic_mask = nvic; v_clear_nvic = clr_nvic; n_isr = 0;
    if (inject) { g_inj_addr = nvic ? EXTI_PR : EXTI_IMR; g_inj_left = 1; g_inj_fn = inj_bounce; }
    sim_run_until(11999);
    printf("  %-46s %-7s  PRESS %d  (1번 눌렀다)  EXTI ISR %u\n", name, latch ? "latch" : "default",
           ev_count('N', EV_PRESS), (unsigned)n_isr);
}
/* 같은 결함들을 정답 디바운서(확정 상태 추적)에 넣으면? */
static void run_ref(const char *name, bool latch, int inject) {
    press_fixture();
    sim_start(RUN_EXTI, latch);
    if (inject) { g_inj_addr = EXTI_IMR; g_inj_left = 1; g_inj_fn = inj_bounce; }
    sim_run_until(11999);
    printf("  %-46s %-7s  PRESS %d  RELEASE %d  EXTI ISR %u\n", name, latch ? "latch" : "default",
           ev_count('E', EV_PRESS), ev_count('E', EV_RELEASE), (unsigned)g_isr_calls[IRQ_EXTI15_10]);
}

/* ---------- 재확인 뒤 다시 읽기를 빼먹은 버전 ---------- */
static void noreread_tim_isr(void) {
    if ((REG_READ(TIM7_SR) & TIM_SR_UIF) == 0u) return;
    REG_WRITE(TIM7_SR, ~TIM_SR_UIF);
    uint8_t p = btn_raw_pressed(g_dbx.port, g_dbx.line);
    if (p != g_dbx.stable) { g_dbx.stable = p; ev_push(p ? EV_PRESS : EV_RELEASE, 'E', sim_now()); }
    exti_unmask(g_dbx.line);
    g_dbx.state = DBX_IDLE;
}

/* ---------- 설정 순서를 거꾸로 한 exti_config ---------- */
static void bad_order_config(void) {
    REG_WRITE(RCC_APB2ENR, REG_READ(RCC_APB2ENR) | RCC_APB2ENR_SYSCFGEN);
    REG_WRITE(EXTI_RTSR, REG_READ(EXTI_RTSR) | (1u << 13));
    REG_WRITE(EXTI_FTSR, REG_READ(EXTI_FTSR) | (1u << 13));
    REG_WRITE(EXTI_IMR, REG_READ(EXTI_IMR) | (1u << 13));      /* unmask 를 먼저 */
    REG_WRITE(SYSCFG_EXTICR(13), (REG_READ(SYSCFG_EXTICR(13)) & ~0xF0u) | 0x20u);   /* 그다음 mux: PA13(0) → PC13(1) */
}

/* ---------- 재마스크 없이 엣지마다 타이머 재시작 (retrigger) ---------- */
static void retrig_exti_isr(void) { exti_ack(g_dbx.line); g_dbx.state = DBX_SETTLE; tim7_oneshot(DB_TICKS); }

int main(void) {
    printf("[1] 하강 엣지 + '타이머 끝에 아직 0 이면 PRESS' 설계, 튀는 누름 1번 (바운스 8 ms, 600 ms 누름)\n");
    run_naive("mask→ack, unmask 때 PR 안 지움", false, 0, 0, 0, 0, 0);
    run_naive("mask→ack, unmask 때 PR 안 지움", true, 0, 0, 0, 0, 0);
    run_naive("mask→ack, unmask 때 PR 지움", true, 0, 1, 0, 0, 0);
    run_naive("ack→mask (창에 바운스 주입), PR 안 지움", false, 1, 0, 0, 0, 1);
    run_naive("mask→ack (같은 주입), PR 안 지움", false, 0, 0, 0, 0, 1);
    run_naive("ack→mask (창에 바운스 주입), PR 지움", false, 1, 1, 0, 0, 1);
    run_naive("NVIC disable 로 막음, 재허용 전 정리 없음", false, 0, 0, 1, 0, 0);
    run_naive("NVIC disable 로 막음, EXTI PR + ICPR 정리", false, 0, 0, 1, 1, 0);
    printf("[2] 같은 상황에서 정답 디바운서 (확정 상태 추적 + unmask 전 PR 지움)\n");
    run_ref("solution", false, 0);
    run_ref("solution", true, 0);
    run_ref("solution (IMR RMW 사이에 바운스 주입)", false, 1);

    printf("[3] 마스크 창 안의 누름 (재확인 읽기 직후 버튼이 눌리고 그대로)\n");
    for (int v = 0; v < 2; v++) {
        sig_reset(9000, 1); sig_edge(1000, 0, 0); sig_edge(1003, 1, 0);
        sim_start(RUN_EXTI, false);
        if (v == 1) g_vec[IRQ_TIM7] = noreread_tim_isr;
        sim_run_until(1199);
        g_inj_addr = GPIO_IDR(BTN_PORT); g_inj_left = 1; g_inj_fn = inj_press_now;
        sim_run_until(8999);
        printf("  %-46s PRESS %d  (버튼은 780 ms 째 눌려 있음)\n", v ? "unmask 뒤 다시 읽기 없음" : "solution (다시 읽고 재예약)",
               ev_count('E', EV_PRESS));
    }

    printf("[4] 설정 순서: PA13=0, PC13=1(놓음) 에서 PC13 라인 설정 직후 ISR 진입 수\n");
    for (int v = 0; v < 2; v++) {
        sig_reset(3000, 1);
        sim_start(0, false);
        g_vec[IRQ_EXTI15_10] = dbx_exti_isr; g_vec[IRQ_TIM7] = dbx_tim_isr;
        g_dbx.port = PORT_C; g_dbx.line = 13; g_dbx.stable = 0; g_dbx.long_fired = 1;
        if (v == 0) exti_config(PORT_C, 13, EXTI_BOTH); else bad_order_config();
        nvic_lite_enable(IRQ_EXTI15_10); nvic_lite_enable(IRQ_TIM7);
        sim_run_until(2999);
        printf("  %-46s EXTI ISR %u, PR 남은 값 0x%04x\n", v ? "IMR/RTSR 먼저, EXTICR 나중" : "solution exti_config",
               (unsigned)g_isr_calls[IRQ_EXTI15_10], (unsigned)mock_peek(EXTI_PR));
    }

    printf("[5] 공유 벡터에서 PR |= bit 로 ack (라인 10, 13 이 같은 틱에 펜딩)\n");
    for (int v = 0; v < 2; v++) {
        world_reset(false);
        *R_PR = (1u << 10) | (1u << 13);
        if (v == 0) REG_WRITE(EXTI_PR, 1u << 13); else REG_WRITE(EXTI_PR, REG_READ(EXTI_PR) | (1u << 13));
        printf("  %-46s PR 0x%04x → 라인 10 펜딩 %s\n", v ? "REG_WRITE(PR, REG_READ(PR) | bit)" : "REG_WRITE(PR, bit)",
               (unsigned)mock_peek(EXTI_PR), (mock_peek(EXTI_PR) & (1u << 10)) ? "살아 있음" : "사라짐 (이벤트 유실)");
    }

    printf("[6] 마스크 없이 엣지마다 타이머 재시작 (retrigger) vs solution — 10 s 세션\n");
    for (int v = 0; v < 2; v++) {
        uint32_t len = session_build();
        sim_start(RUN_EXTI, false);
        if (v == 1) g_vec[IRQ_EXTI15_10] = retrig_exti_isr;
        sim_run_until(len - 1);
        printf("  %-46s pin edges %u, EXTI ISR %u, TIM7 ISR %u, events %d\n", v ? "retrigger (마스크 없음)" : "solution (마스크)",
               (unsigned)g_pin_edges, (unsigned)g_isr_calls[IRQ_EXTI15_10], (unsigned)g_isr_calls[IRQ_TIM7], ev_count('E', 0));
    }
    return 0;
}
