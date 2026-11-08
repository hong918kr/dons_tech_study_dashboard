/* C-M15 드라이버 스타일 에러 처리: 반환 코드와 goto cleanup
 *
 * UART + RX DMA 드라이버의 init 은 4단계로 자원을 잡는다: 클럭 → GPIO → DMA 채널 → IRQ.
 * 중간 k 단계가 실패하면 1..k-1 단계에서 잡은 자원을 "역순으로" 정확히 한 번씩 놓아야 한다.
 *
 * 호스트(맥)에는 진짜 클럭·DMA·NVIC 가 없으므로 아래 "시뮬레이터" 가
 *   ① 자원 장부(ledger): 자원마다 지금 잡혀 있는 개수 (acquire +1, release -1)
 *   ② trace 문자열: "+CLK +GPIO !DMA -GPIO -CLK" 처럼 잡기(+)/실패(!)/놓기(-) 순서
 *   ③ 실패 주입(fault injection): k 번째 단계의 acquire 를 원하는 에러 코드로 실패시킴
 * 을 제공한다. 테스트는 "누수 0, 이중 해제 0, 올바른 에러 코드, 올바른 해제 순서" 를 검사한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 에러 코드: 음수 errno 스타일 (번호는 Linux errno 값) ---------------- */
/* 0 = 성공, 음수 = 에러, (핸들을 돌려주는 함수만) 양수/0 = 값.  if (ret < 0) 한 줄로 검사한다. */
typedef enum {
    DRV_OK        = 0,
    DRV_EIO       = -5,     /* 하드웨어 I/O 오류           */
    DRV_ENOMEM    = -12,    /* 채널·버퍼·슬롯 부족          */
    DRV_EBUSY     = -16,    /* 이미 사용 중 / 이미 init 됨  */
    DRV_ENODEV    = -19,    /* 장치 없음 (ID 불일치 등)     */
    DRV_EINVAL    = -22,    /* 잘못된 인자                  */
    DRV_ETIMEDOUT = -110,   /* ready 비트 대기 시간 초과    */
} drv_err_t;

/* ---------------- 시뮬레이터 (주어진 코드 — 고치지 않는다) ---------------- */
enum { ST_CLK = 1, ST_GPIO, ST_DMA, ST_IRQ };        /* 단계 번호 (fault_inject 용) */
#define UART_PINS       0x0003u                       /* PA9/PA10 같은 TX/RX 핀 두 개  */
#define UART_IRQn       37
#define DMA_CH          3                             /* 시뮬레이터가 내주는 채널 번호 */
#define CLK_RDY         (1u << 0)                     /* 클럭 상태 레지스터의 ready 비트 */
#define CLK_READY_POLLS 100u
#define DEVRES_MAX      8

static struct {
    int clk, gpio, dma, irq;   /* 지금 잡혀 있는 자원 수                       */
    int bad_release;           /* 잡지 않은 자원을 놓음 = 이중 해제 / 잘못된 핸들 */
} led;                          /* ledger: 자원 장부 */

static struct {
    int  fail_stage;           /* 이 단계의 acquire 가 fail_code 로 실패 (0 = 없음) */
    int  fail_code;
    int  clk_ready_after;      /* CLK_SR 을 이만큼 더 읽은 뒤 RDY=1 (-1 = 영원히 0) */
    int  polls;                /* reg_read 횟수                                      */
    int  dma_busy_frees;       /* dma_chan_free 가 EBUSY 로 거절할 남은 횟수 (abort → 0) */
    bool dma_free_broken;      /* dma_chan_free 가 항상 EIO (DMA 컨트롤러 먹통)      */
    int  devres_cap;           /* Part C: devres 슬롯 수                             */
    char trace[256];
} sim;

static volatile uint32_t CLK_SR;      /* 실제 칩이라면 RCC->CR 의 HSERDY/PLLRDY 같은 비트 */

static void trace(const char *s) {
    size_t n = strlen(sim.trace);
    snprintf(sim.trace + n, sizeof sim.trace - n, "%s%s", n ? " " : "", s);
}
static int fault_hit(int stage) { return sim.fail_stage == stage ? sim.fail_code : 0; }
static void fault_inject(int stage, drv_err_t code) { sim.fail_stage = stage; sim.fail_code = code; }
static void put_ref(int *cnt, const char *tok) {      /* release 공통: 0 에서 또 놓으면 버그 */
    if (*cnt > 0) (*cnt)--; else led.bad_release++;
    trace(tok);
}

static uint32_t reg_read(const volatile uint32_t *r) {
    sim.polls++;
    if (r == &CLK_SR && led.clk > 0) {                /* 클럭이 켜져 있어야 언젠가 ready */
        if (sim.clk_ready_after == 0) CLK_SR |= CLK_RDY;
        else if (sim.clk_ready_after > 0) sim.clk_ready_after--;
    }
    return *r;
}

/* HAL: acquire 는 0(또는 핸들) / 음수 에러, release 는 대부분 실패하지 않는다(void) */
static int clk_enable(void) {
    int e = fault_hit(ST_CLK);
    if (e) { trace("!CLK"); return e; }
    led.clk++; trace("+CLK"); return 0;
}
static void clk_disable(void) { put_ref(&led.clk, "-CLK"); CLK_SR = 0; }
static int gpio_request(uint32_t pins) {
    int e = fault_hit(ST_GPIO);
    (void)pins;
    if (e) { trace("!GPIO"); return e; }
    led.gpio++; trace("+GPIO"); return 0;
}
static void gpio_free(uint32_t pins) { (void)pins; put_ref(&led.gpio, "-GPIO"); }
static int dma_chan_alloc(void) {                     /* 성공하면 채널 번호(>= 0) */
    int e = fault_hit(ST_DMA);
    if (e) { trace("!DMA"); return e; }
    led.dma++; trace("+DMA"); return DMA_CH;
}
static int dma_chan_free(int ch) {                    /* 정리 함수인데 실패할 수 있다 (Part B) */
    if (ch != DMA_CH) { led.bad_release++; trace("-DMA:badch"); return DRV_EINVAL; }
    if (sim.dma_free_broken) { trace("-DMA:eio"); return DRV_EIO; }
    if (sim.dma_busy_frees > 0) { sim.dma_busy_frees--; trace("-DMA:busy"); return DRV_EBUSY; }
    put_ref(&led.dma, "-DMA"); return 0;
}
static void dma_chan_abort(int ch) { (void)ch; trace("ABORT"); sim.dma_busy_frees = 0; }
static int irq_request(int irqn, void (*isr)(void *), void *ctx) {
    int e = fault_hit(ST_IRQ);
    (void)irqn; (void)isr; (void)ctx;
    if (e) { trace("!IRQ"); return e; }
    led.irq++; trace("+IRQ"); return 0;
}
static void irq_free(int irqn) { (void)irqn; put_ref(&led.irq, "-IRQ"); }
static void uart_isr(void *ctx) { (void)ctx; }

static void sim_reset(void) {
    memset(&led, 0, sizeof led);
    memset(&sim, 0, sizeof sim);
    sim.devres_cap = DEVRES_MAX;
    CLK_SR = 0;
}

/* ---------------- 드라이버 ---------------- */
typedef struct uart_dev uart_dev_t;
typedef void (*release_fn)(uart_dev_t *d);
struct uart_dev {
    bool       ready;              /* init 이 끝까지 성공했나                     */
    int        dma_ch;             /* 할당받은 DMA 채널                           */
    drv_err_t  cleanup_err;        /* 정리 도중 생긴 첫 에러 (원래 에러를 덮지 않음) */
    release_fn res[DEVRES_MAX];    /* Part C: devres 스택 (LIFO)                  */
    int        nres;
};

/* ---------------- Part B: 경계 있는 폴링 + 실패할 수 있는 정리 ---------------- */
/* reg 의 mask 비트가 "전부" 1 이 될 때까지 최대 max_polls 번 읽는다.
 * 반환: DRV_OK / DRV_ETIMEDOUT (정확히 max_polls 번 읽은 뒤) / DRV_EINVAL (mask == 0) */
static drv_err_t wait_ready(const volatile uint32_t *reg, uint32_t mask, uint32_t max_polls) {
    /* TODO: 지금은 딱 한 번만 본다 — PLL 이 몇 번 뒤에 lock 되면 실패한다. 경계 있는 루프로. */
    (void)max_polls;
    return (reg_read(reg) & mask) ? DRV_OK : DRV_ETIMEDOUT;
}

/* DMA 채널 반납. dma_chan_free 가 실패할 수 있다:
 *   -EBUSY (전송 진행 중) → dma_chan_abort 로 강제 중단 후 한 번 더 시도
 *   그래도 실패          → d->cleanup_err 에 "첫 에러" 만 기록하고 그냥 진행 (정리는 멈추지 않는다) */
static void dma_release(uart_dev_t *d) {
    /* TODO: 지금은 반환값을 버린다 — EBUSY 면 채널이 그대로 새고, EIO 는 아무도 모른다. */
    (void)dma_chan_free(d->dma_ch);
    (void)dma_chan_abort;
    d->dma_ch = -1;
}

/* ---------------- Part A: goto 언와인딩 init + 멱등 deinit ---------------- */
/* 단계: clk_enable + wait_ready(CLK_SR, CLK_RDY) → gpio_request(UART_PINS)
 *       → dma_chan_alloc (채널을 d->dma_ch 에) → irq_request(UART_IRQn, uart_isr, d)
 * 실패: 앞 단계들을 역순으로 놓고 "실패한 단계의 에러 코드 그대로" 반환.
 * 이미 ready 면 아무것도 건드리지 않고 DRV_EBUSY (Part B: double init). */
static drv_err_t uart_init(uart_dev_t *d) {
    /* TODO: 4단계 acquire + 실패 시 goto 로 역순 해제 */
    (void)d; (void)clk_enable; (void)clk_disable; (void)gpio_request; (void)gpio_free;
    (void)dma_chan_alloc; (void)irq_request; (void)uart_isr;
    return DRV_ENODEV;
}

/* 완전히 init 된 장치를 역순으로 정리. 두 번 불러도, init 실패 후 불러도 안전해야 한다. */
static void uart_deinit(uart_dev_t *d) {
    (void)d; /* TODO */
}

/* ---------------- Part C: devm 스타일 — 해제 함수를 LIFO 로 등록 ---------------- */
/* (주어진 코드) 단계별 해제 함수 */
static void rel_clk(uart_dev_t *d)  { (void)d; clk_disable(); }
static void rel_gpio(uart_dev_t *d) { (void)d; gpio_free(UART_PINS); }
static void rel_dma(uart_dev_t *d)  { dma_release(d); }
static void rel_irq(uart_dev_t *d)  { (void)d; irq_free(UART_IRQn); }

/* fn 을 스택에 쌓는다. 슬롯이 없으면(n == sim.devres_cap) fn(d) 를 "즉시" 불러 방금 잡은
 * 자원을 놓고 DRV_ENOMEM (Linux devm_add_action_or_reset 와 같은 의미). */
static drv_err_t devres_add_or_reset(uart_dev_t *d, release_fn fn) {
    (void)d; (void)fn;
    return DRV_OK; /* TODO */
}
/* 쌓인 해제 함수를 최근 것부터 모두 부르고 스택을 비운다 (두 번 불러도 안전). */
static void devres_release_all(uart_dev_t *d) {
    (void)d; /* TODO */
}
/* uart_init 과 같은 4단계. 실패하면 그냥 return — 정리는 "버스(드라이버 코어)" 가 한다. */
static drv_err_t uart_probe_devm(uart_dev_t *d) {
    /* TODO: acquire 할 때마다 해제 함수를 devres_add_or_reset 로 등록. goto 도 정리 코드도 없다. */
    (void)d; (void)rel_clk; (void)rel_gpio; (void)rel_dma; (void)rel_irq; (void)devres_add_or_reset;
    return DRV_ENODEV;
}

/* (주어진 코드) Linux 드라이버 코어 흉내: probe 실패 / remove 시 devres 를 자동 해제 */
static drv_err_t bus_probe(uart_dev_t *d, drv_err_t (*probe)(uart_dev_t *)) {
    drv_err_t ret = probe(d);
    if (ret < 0) devres_release_all(d);
    return ret;
}
static void bus_remove(uart_dev_t *d) { devres_release_all(d); d->ready = false; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(sim.trace, (s)) == 0)
#define NO_LEAK()   (led.clk == 0 && led.gpio == 0 && led.dma == 0 && led.irq == 0 && led.bad_release == 0)
#define ALL_HELD()  (led.clk == 1 && led.gpio == 1 && led.dma == 1 && led.irq == 1 && led.bad_release == 0)

static const struct { int stage; drv_err_t code; const char *trace; } fault_matrix[] = {
    { ST_CLK,  DRV_EIO,    "!CLK" },
    { ST_GPIO, DRV_EBUSY,  "+CLK !GPIO -CLK" },
    { ST_DMA,  DRV_ENOMEM, "+CLK +GPIO !DMA -GPIO -CLK" },
    { ST_IRQ,  DRV_ENODEV, "+CLK +GPIO +DMA !IRQ -DMA -GPIO -CLK" },
};
#define N_FM (sizeof fault_matrix / sizeof fault_matrix[0])

int main(void) {
    uart_dev_t d;

    /* ===== Part A: 정상 경로 · 멱등 deinit ===== */
    sim_reset(); d = (uart_dev_t){ 0 };
    CHECK(uart_init(&d) == DRV_OK && d.ready);
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ") && ALL_HELD() && d.dma_ch == DMA_CH);
    uart_deinit(&d);
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA -GPIO -CLK") && NO_LEAK() && !d.ready);
    uart_deinit(&d);                                            /* 두 번째 deinit: no-op */
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA -GPIO -CLK") && NO_LEAK());
    sim_reset(); d = (uart_dev_t){ 0 };
    uart_deinit(&d);                                            /* init 한 적 없는 장치 */
    CHECK(TRACE_IS("") && NO_LEAK());

    /* ===== Part A: 단계 k(1..4) 실패 주입 → 정확한 코드 + 역순 해제 + 누수 0 + 재시도 가능 ===== */
    for (size_t i = 0; i < N_FM; i++) {
        sim_reset(); d = (uart_dev_t){ 0 };
        fault_inject(fault_matrix[i].stage, fault_matrix[i].code);
        CHECK(uart_init(&d) == fault_matrix[i].code);          /* 코드를 덮어쓰지 않고 전파 */
        CHECK(TRACE_IS(fault_matrix[i].trace));
        CHECK(NO_LEAK() && !d.ready);
        uart_deinit(&d);                                        /* 실패 후 deinit 도 안전 */
        CHECK(NO_LEAK());
        fault_inject(0, DRV_OK);                                /* 원인이 사라지면 재시도 성공 */
        CHECK(uart_init(&d) == DRV_OK && ALL_HELD());
        uart_deinit(&d);
        CHECK(NO_LEAK());
    }

    /* ===== Part B-1: 경계 있는 폴링 ===== */
    static volatile uint32_t reg;
    sim_reset(); reg = 0x1u;
    CHECK(wait_ready(&reg, 0x1u, 10) == DRV_OK && sim.polls == 1);        /* 이미 ready */
    sim_reset(); reg = 0x1u;
    CHECK(wait_ready(&reg, 0x3u, 10) == DRV_ETIMEDOUT && sim.polls == 10);/* 비트 "전부" 필요 */
    sim_reset(); reg = 0x1u;
    CHECK(wait_ready(&reg, 0x1u, 0) == DRV_ETIMEDOUT && sim.polls == 0);  /* 0번 = 즉시 타임아웃 */
    sim_reset();
    CHECK(wait_ready(&reg, 0u, 10) == DRV_EINVAL && sim.polls == 0);
    sim_reset(); d = (uart_dev_t){ 0 }; sim.clk_ready_after = 3;          /* PLL 이 4번째 읽기에 lock */
    CHECK(uart_init(&d) == DRV_OK && sim.polls == 4);
    uart_deinit(&d);

    /* ===== Part B-2: 클럭은 켜졌지만 ready 가 안 옴 → 그 단계가 켠 클럭도 끈다 ===== */
    sim_reset(); d = (uart_dev_t){ 0 }; sim.clk_ready_after = -1;
    CHECK(uart_init(&d) == DRV_ETIMEDOUT);
    CHECK(TRACE_IS("+CLK -CLK") && NO_LEAK() && sim.polls == (int)CLK_READY_POLLS);

    /* ===== Part B-3: double init ===== */
    sim_reset(); d = (uart_dev_t){ 0 };
    CHECK(uart_init(&d) == DRV_OK);
    CHECK(uart_init(&d) == DRV_EBUSY);
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ") && ALL_HELD());                /* 두 번 잡지 않음 */
    uart_deinit(&d);
    CHECK(NO_LEAK());

    /* ===== Part B-4: 정리 함수 자체의 실패 ===== */
    sim_reset(); d = (uart_dev_t){ 0 };
    fault_inject(ST_IRQ, DRV_ENODEV); sim.dma_busy_frees = 1;             /* 일시적: abort 후 성공 */
    CHECK(uart_init(&d) == DRV_ENODEV);                                   /* 원래 에러 유지 */
    CHECK(TRACE_IS("+CLK +GPIO +DMA !IRQ -DMA:busy ABORT -DMA -GPIO -CLK"));
    CHECK(NO_LEAK() && d.cleanup_err == DRV_OK);
    sim_reset(); d = (uart_dev_t){ 0 };
    fault_inject(ST_IRQ, DRV_ENODEV); sim.dma_free_broken = true;         /* 영구: 기록하고 계속 */
    CHECK(uart_init(&d) == DRV_ENODEV);                                   /* EIO 로 덮지 않음 */
    CHECK(TRACE_IS("+CLK +GPIO +DMA !IRQ -DMA:eio -GPIO -CLK"));          /* 나머지 정리는 계속 */
    CHECK(led.dma == 1 && led.gpio == 0 && led.clk == 0 && d.cleanup_err == DRV_EIO);
    sim_reset(); d = (uart_dev_t){ 0 };
    CHECK(uart_init(&d) == DRV_OK);
    sim.dma_busy_frees = 1;                                               /* deinit 경로에서도 */
    uart_deinit(&d);
    CHECK(NO_LEAK() && TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA:busy ABORT -DMA -GPIO -CLK"));

    /* ===== Part C: devm 스타일 — 같은 실패 행렬에서 같은 trace ===== */
    sim_reset(); d = (uart_dev_t){ 0 };
    CHECK(bus_probe(&d, uart_probe_devm) == DRV_OK && ALL_HELD() && d.nres == 4);
    bus_remove(&d);
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA -GPIO -CLK") && NO_LEAK() && d.nres == 0);
    bus_remove(&d);                                                       /* 멱등 */
    CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA -GPIO -CLK") && NO_LEAK());
    for (size_t i = 0; i < N_FM; i++) {
        sim_reset(); d = (uart_dev_t){ 0 };
        fault_inject(fault_matrix[i].stage, fault_matrix[i].code);
        CHECK(bus_probe(&d, uart_probe_devm) == fault_matrix[i].code);
        CHECK(TRACE_IS(fault_matrix[i].trace) && NO_LEAK() && d.nres == 0);
    }
    sim_reset(); d = (uart_dev_t){ 0 }; sim.clk_ready_after = -1;
    CHECK(bus_probe(&d, uart_probe_devm) == DRV_ETIMEDOUT && TRACE_IS("+CLK -CLK") && NO_LEAK());
    sim_reset(); d = (uart_dev_t){ 0 }; sim.devres_cap = 2;               /* 3번째 등록이 실패 */
    CHECK(bus_probe(&d, uart_probe_devm) == DRV_ENOMEM);
    CHECK(TRACE_IS("+CLK +GPIO +DMA -DMA -GPIO -CLK") && NO_LEAK() && d.nres == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
