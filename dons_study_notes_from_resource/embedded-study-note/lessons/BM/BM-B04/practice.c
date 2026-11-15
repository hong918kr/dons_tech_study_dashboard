/* BM-B04 추가 연습 — 한 번에 검증
 *
 * 연습 1: 토글 — ODR ^= 대신 "ODR 읽기 + BSRR 쓰기" (BM-B11 의 toggle 과 같은 발상)
 * 연습 2: W1C "펜딩 전부 지우기" — PR = 0x7FFFFF 와 PR = PR(읽은 값) 의 차이
 * 연습 3: 접근 타입별 범용 flag_clear (rc_w1 / rc_w0 / rc_r / rw)
 * 연습 4: ISR 끝에서 플래그를 지우면 생기는 재진입 — 지연 쓰기(posted write) 모델과 read-back
 *
 * mock 은 solution.c 와 같은 BM-B02 축약판에 "지연 쓰기" 옵션만 더했다. 주소는 RM0090 대표값 (RM 확인 권장).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))

#define GPIOA_ODR   0x40020014u
#define GPIOA_BSRR  0x40020018u
#define EXTI_PR     0x40013C14u
#define TIM2_SR     0x40000010u
#define FAKE_RCR    0x40009000u          /* 연습 3: rc_r (읽으면 전부 지워지는) 가상 상태 레지스터 */
#define FAKE_RW     0x40009004u          /* 연습 3: 평범한 rw 상태 레지스터 (지우려면 RMW 필요) */
#define PIN(n)      (1u << (n))

/* ---- mock core (BM-B02 축약판 + 지연 쓰기) ---- */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val);
typedef void (*mock_fn)(void);
typedef struct { uint32_t addr, stored; mock_on_read r; mock_on_write w; } mock_reg_t;
#define MOCK_MAX_REGS 8
static mock_reg_t g_reg[MOCK_MAX_REGS];
static int g_nreg, g_unexpected;
static uint32_t g_nacc;
static struct { uint32_t at; mock_fn fn; bool maskable; } g_inj;
static bool g_in_isr;
static bool g_posted_mode;                       /* 연습 4: 쓰기가 "다음 버스 접근" 때 도착한다 */
static struct { bool valid; uint32_t addr, val; } g_wbuf;

static mock_reg_t *mock_find(uint32_t a) {
    for (int i = 0; i < g_nreg; i++) if (g_reg[i].addr == a) return &g_reg[i];
    return NULL;
}
static void mock_reset(void) {
    memset(g_reg, 0, sizeof g_reg); g_nreg = 0; g_unexpected = 0; g_nacc = 0;
    memset(&g_inj, 0, sizeof g_inj); g_in_isr = false; g_posted_mode = false; g_wbuf.valid = false;
}
static void mock_add_reg(uint32_t a, uint32_t rv, mock_on_read r, mock_on_write w) {
    if (g_nreg < MOCK_MAX_REGS) g_reg[g_nreg++] = (mock_reg_t){ a, rv, r, w };
}
static uint32_t mock_peek(uint32_t a) { mock_reg_t *r = mock_find(a); return r ? r->stored : 0u; }
static void mock_poke(uint32_t a, uint32_t v) { mock_reg_t *r = mock_find(a); if (r) r->stored = v; }
static void mock_commit(uint32_t a, uint32_t v) {
    mock_reg_t *r = mock_find(a);
    if (r == NULL) { g_unexpected++; return; }
    r->stored = r->w ? r->w(a, r->stored, v) : v;
}
static void mock_drain(void) { if (g_wbuf.valid) { g_wbuf.valid = false; mock_commit(g_wbuf.addr, g_wbuf.val); } }
static void mock_after(void) {
    g_nacc++;
    if (g_inj.fn == NULL || g_in_isr || g_nacc != g_inj.at) return;
    mock_fn fn = g_inj.fn; g_inj.fn = NULL;
    if (g_inj.maskable) { g_in_isr = true; fn(); g_in_isr = false; } else fn();
}
uint32_t mock_read32(uint32_t a) {
    mock_drain();                                 /* 읽기는 앞선 쓰기가 끝난 뒤에 (같은 버스, 순서 유지) */
    mock_reg_t *r = mock_find(a);
    uint32_t v = 0;
    if (r == NULL) g_unexpected++;
    else { v = r->stored; if (r->r) r->stored = r->r(a, r->stored); }
    mock_after();
    return v;
}
void mock_write32(uint32_t a, uint32_t v) {
    mock_drain();
    if (g_posted_mode) { g_wbuf.valid = true; g_wbuf.addr = a; g_wbuf.val = v; }
    else mock_commit(a, v);
    mock_after();
}
static void mock_inject_at(uint32_t n, mock_fn fn, bool maskable) { g_inj.at = n; g_inj.fn = fn; g_inj.maskable = maskable; }

static uint32_t odr_w(uint32_t a, uint32_t o, uint32_t v)  { (void)a; (void)o; return v & 0xFFFFu; }
static uint32_t w1c_w(uint32_t a, uint32_t o, uint32_t v)  { (void)a; return o & ~v & 0x7FFFFFu; }
static uint32_t rcw0_w(uint32_t a, uint32_t o, uint32_t v) { (void)a; return o & v; }
static uint32_t bsrr_w(uint32_t a, uint32_t o, uint32_t v) {
    (void)a; (void)o;
    mock_poke(GPIOA_ODR, ((mock_peek(GPIOA_ODR) & ~(v >> 16)) | (v & 0xFFFFu)) & 0xFFFFu);
    return 0u;
}
static uint32_t rcr_r(uint32_t a, uint32_t stored) { (void)a; (void)stored; return 0u; }   /* 읽으면 전부 0 */
static uint32_t ro_w(uint32_t a, uint32_t o, uint32_t v) { (void)a; (void)v; return o; }   /* rc_r: 쓰기 무시 */

static void board_setup(void) {
    mock_reset();
    mock_add_reg(GPIOA_ODR, 0u, NULL, odr_w);
    mock_add_reg(GPIOA_BSRR, 0u, NULL, bsrr_w);
    mock_add_reg(EXTI_PR, 0u, NULL, w1c_w);
    mock_add_reg(TIM2_SR, 0u, NULL, rcw0_w);
    mock_add_reg(FAKE_RCR, 0u, rcr_r, ro_w);
    mock_add_reg(FAKE_RW, 0u, NULL, NULL);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* sweep: op 의 k 번째 접근 직후 inj (k=0: 전, k=n: 후). reg 가 expect 와 다른 k 의 비트마스크 */
static uint32_t sweep(uint32_t reg, uint32_t init, mock_fn op, mock_fn inj, bool maskable, uint32_t expect) {
    board_setup(); mock_poke(reg, init); op();
    uint32_t n = g_nacc, bad = 0;
    for (uint32_t k = 0; k <= n; k++) {
        board_setup(); mock_poke(reg, init);
        if (k == 0) inj(); else mock_inject_at(k, inj, maskable);
        op();
        if (mock_peek(reg) != expect) bad |= 1u << k;
    }
    return bad;
}

/* ===== 연습 1: 토글 ===== */
static void toggle_xor(uint32_t pins) { REG_WRITE(GPIOA_ODR, REG_READ(GPIOA_ODR) ^ pins); }   /* ODR ^= pins */
static void toggle_bsrr(uint32_t pins) {                     /* 읽기는 하지만 쓰기는 pins 안에서만 효과 */
    uint32_t odr = REG_READ(GPIOA_ODR);
    REG_WRITE(GPIOA_BSRR, ((odr & pins) << 16) | (~odr & pins & 0xFFFFu));
}
static void isr_set6(void)  { REG_WRITE(GPIOA_BSRR, PIN(6)); }
static void op_tx(void)     { toggle_xor(PIN(5)); }
static void op_tb(void)     { toggle_bsrr(PIN(5)); }

/* ===== 연습 2: W1C 전부 지우기 ===== */
static void clear_all_blind(void) { (void)REG_READ(EXTI_PR); REG_WRITE(EXTI_PR, 0x7FFFFFu); }   /* 읽고 처리했다 치고 전부 1 */
static void clear_all_read(void)  { uint32_t p = REG_READ(EXTI_PR); REG_WRITE(EXTI_PR, p); }    /* PR = PR */
static void hw_line11(void) { mock_poke(EXTI_PR, mock_peek(EXTI_PR) | PIN(11)); }

/* ===== 연습 3: 접근 타입별 범용 클리어 ===== */
typedef enum { ACC_RC_W1, ACC_RC_W0, ACC_RC_R, ACC_RW } acc_t;
static uint32_t g_rcr_shadow;          /* rc_r: 읽은 값을 버리면 다른 플래그를 잃는다 → 소프트웨어에 모아 둔다 */
/* mask 비트를 지운다. rc_r 은 하드웨어를 선택적으로 못 지우므로 shadow 에서 지운다. rw 는 RMW (단일 문맥 가정) */
static void flag_clear(acc_t t, uint32_t addr, uint32_t mask) {
    switch (t) {
    case ACC_RC_W1: REG_WRITE(addr, mask); break;
    case ACC_RC_W0: REG_WRITE(addr, ~mask); break;
    case ACC_RC_R:  g_rcr_shadow |= REG_READ(addr); g_rcr_shadow &= ~mask; break;
    case ACC_RW:    REG_WRITE(addr, REG_READ(addr) & ~mask); break;
    }
}

/* ===== 연습 4: ISR 끝에서 지우기 + 지연 쓰기 ===== */
static uint32_t g_handled;
static void isr_clear_at_end(void)     { g_handled++; (void)REG_READ(GPIOA_ODR); REG_WRITE(EXTI_PR, PIN(13)); }
static void isr_clear_at_end_rb(void)  { g_handled++; (void)REG_READ(GPIOA_ODR); REG_WRITE(EXTI_PR, PIN(13)); (void)REG_READ(EXTI_PR); }
static void isr_clear_at_start(void)   { REG_WRITE(EXTI_PR, PIN(13)); g_handled++; (void)REG_READ(GPIOA_ODR); }
/* 예외 복귀 순간 NVIC 가 보는 것 = 버스에 "도착한" 값. 아직 펜딩이면 같은 ISR 에 다시 들어간다 (최대 3회) */
static uint32_t run_irq(mock_fn isr) {
    board_setup(); g_posted_mode = true; g_handled = 0;
    mock_poke(EXTI_PR, PIN(13));
    for (int entry = 0; entry < 3 && (mock_peek(EXTI_PR) & PIN(13)); entry++) isr();
    mock_drain();
    return g_handled;
}

int main(void) {
    /* 연습 1 */
    CHECK(sweep(GPIOA_ODR, 0x0000u, op_tx, isr_set6, true, 0x0060u) == 0x2u);   /* ^= 는 k=1 에서 핀 6 유실 */
    CHECK(sweep(GPIOA_ODR, 0x0000u, op_tb, isr_set6, true, 0x0060u) == 0u);     /* BSRR 토글은 핀 6 안전 */
    CHECK(sweep(GPIOA_ODR, 0x0020u, op_tb, isr_set6, true, 0x0040u) == 0u);     /* 1 → 0 방향도 */

    /* 연습 2 */
    /* 규칙: 읽은(=처리한) 것만 지우고, 읽은 뒤 도착한 라인 11 은 펜딩으로 남아야 한다 */
    board_setup(); mock_poke(EXTI_PR, 0x2001u); mock_inject_at(1, hw_line11, false);
    clear_all_blind();
    CHECK(mock_peek(EXTI_PR) == 0u);                     /* 버그 재현: 처리 안 한 11 까지 지움 */
    board_setup(); mock_poke(EXTI_PR, 0x2001u); mock_inject_at(1, hw_line11, false);
    clear_all_read();
    CHECK(mock_peek(EXTI_PR) == 0x0800u);                /* PR = PR: 읽은 뒤 온 것은 남는다 */
    board_setup(); mock_poke(EXTI_PR, 0x2001u); mock_inject_at(2, hw_line11, false);
    clear_all_blind();
    CHECK(mock_peek(EXTI_PR) == 0x0800u);                /* 쓰기 뒤에 오면 둘 다 괜찮다 — 창은 읽기~쓰기 사이 */

    /* 연습 3: 각 타입에서 mask 비트만 지워지고 나머지는 남는다 (shadow 포함) */
    board_setup(); g_rcr_shadow = 0;
    mock_poke(EXTI_PR, 0x0007u); flag_clear(ACC_RC_W1, EXTI_PR, 0x2u);
    CHECK(mock_peek(EXTI_PR) == 0x0005u);
    mock_poke(TIM2_SR, 0x0007u); flag_clear(ACC_RC_W0, TIM2_SR, 0x2u);
    CHECK(mock_peek(TIM2_SR) == 0x0005u);
    mock_poke(FAKE_RCR, 0x0007u); flag_clear(ACC_RC_R, FAKE_RCR, 0x2u);
    CHECK(mock_peek(FAKE_RCR) == 0u && g_rcr_shadow == 0x0005u);                         /* 하드웨어는 전부 0, 나머지는 shadow 에 */
    mock_poke(FAKE_RCR, 0x0008u); flag_clear(ACC_RC_R, FAKE_RCR, 0x1u);
    CHECK(g_rcr_shadow == 0x000Cu);                                                      /* 누적 */
    mock_poke(FAKE_RW, 0x0007u); flag_clear(ACC_RW, FAKE_RW, 0x2u);
    CHECK(mock_peek(FAKE_RW) == 0x0005u);
    /* 같은 "rc_w1 식" 쓰기를 rc_w0 에 하면? → 지울 비트 외 전부 0 을 쓴 셈 = 다른 플래그 소실 */
    mock_poke(TIM2_SR, 0x0007u); flag_clear(ACC_RC_W1, TIM2_SR, 0x2u);
    CHECK(mock_peek(TIM2_SR) == 0x0002u);
    CHECK(g_unexpected == 0);

    /* 연습 4 */
    CHECK(run_irq(isr_clear_at_end) == 2u);       /* 쓰기가 아직 안 도착 → 복귀 때 펜딩 → 헛 재진입 1회 */
    CHECK(run_irq(isr_clear_at_end_rb) == 1u);    /* read-back 이 쓰기 도착을 보장 */
    CHECK(run_irq(isr_clear_at_start) == 1u);     /* 먼저 지우면 뒤따르는 접근이 밀어 준다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
