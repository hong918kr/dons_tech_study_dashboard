/* C-M15 추가 연습 — 한 번에 검증
 *
 * 본문과 같은 모양의 작은 시뮬레이터(자원 장부 + trace + 실패 주입)를 다시 둔다.
 * 연습 1·2 는 같은 4단계 init 을 "중첩 if" 와 "goto 없는 단계 카운터 + switch fall-through" 로
 * 다시 써서, 본문의 goto 버전과 trace 가 한 글자도 다르지 않음을 같은 실패 행렬로 확인한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    DRV_OK = 0, DRV_EIO = -5, DRV_ENOMEM = -12, DRV_EBUSY = -16,
    DRV_ENODEV = -19, DRV_EINVAL = -22, DRV_ETIMEDOUT = -110,
} drv_err_t;

/* ---------------- 미니 시뮬레이터 ---------------- */
enum { ST_CLK = 1, ST_GPIO, ST_DMA, ST_IRQ };
static struct { int clk, gpio, dma, irq, bad; } led;
static struct { int fail_stage, fail_code; char trace[256]; } sim;

static void trace(const char *s) {
    size_t n = strlen(sim.trace);
    snprintf(sim.trace + n, sizeof sim.trace - n, "%s%s", n ? " " : "", s);
}
static int acquire(int stage, int *cnt, const char *ok, const char *bad) {
    if (sim.fail_stage == stage) { trace(bad); return sim.fail_code; }
    (*cnt)++; trace(ok); return 0;
}
static void release(int *cnt, const char *tok) { if (*cnt > 0) (*cnt)--; else led.bad++; trace(tok); }
static int  clk_enable(void)     { return acquire(ST_CLK, &led.clk, "+CLK", "!CLK"); }
static int  gpio_request(void)   { return acquire(ST_GPIO, &led.gpio, "+GPIO", "!GPIO"); }
static int  dma_alloc(void)      { return acquire(ST_DMA, &led.dma, "+DMA", "!DMA"); }
static int  irq_request(void)    { return acquire(ST_IRQ, &led.irq, "+IRQ", "!IRQ"); }
static void clk_disable(void)    { release(&led.clk, "-CLK"); }
static void gpio_free(void)      { release(&led.gpio, "-GPIO"); }
static void dma_free(void)       { release(&led.dma, "-DMA"); }
static void irq_free(void)       { release(&led.irq, "-IRQ"); }
static void sim_reset(int stage, drv_err_t code) {
    memset(&led, 0, sizeof led); memset(&sim, 0, sizeof sim);
    sim.fail_stage = stage; sim.fail_code = code;
}

/* ---------------- 연습 1: 중첩 if (goto 없음, 단일 출구) ---------------- */
static drv_err_t init_nested(void) {
    int ret = clk_enable();
    if (ret == 0) {
        ret = gpio_request();
        if (ret == 0) {
            ret = dma_alloc();
            if (ret == 0) {
                ret = irq_request();
                if (ret != 0) {
                    dma_free();
                }
            }
            if (ret != 0) {
                gpio_free();
            }
        }
        if (ret != 0) {
            clk_disable();
        }
    }
    return (drv_err_t)ret;
}

/* ---------------- 연습 2: 단계 카운터 + switch fall-through (goto 없음) ---------------- */
/* done = 성공한 단계 수. teardown(done) 은 init 실패 경로와 deinit 이 "같은 코드" 를 쓴다. */
static void teardown(int done) {
    switch (done) {
    case 4: irq_free();    /* fall through */
    case 3: dma_free();    /* fall through */
    case 2: gpio_free();   /* fall through */
    case 1: clk_disable(); /* fall through */
    default: break;
    }
}
static drv_err_t init_staged(int *done_out) {
    int (*const step[])(void) = { clk_enable, gpio_request, dma_alloc, irq_request };
    int done = 0, ret = 0;
    while (done < 4 && (ret = step[done]()) == 0)
        done++;
    if (ret != 0) { teardown(done); done = 0; }
    *done_out = done;
    return (drv_err_t)ret;
}

/* ---------------- 연습 3: 에러 코드 → 이름 ---------------- */
static const char *const err_names[] = {
    [0]               = "OK",
    [-DRV_EIO]        = "EIO",
    [-DRV_ENOMEM]     = "ENOMEM",
    [-DRV_EBUSY]      = "EBUSY",
    [-DRV_ENODEV]     = "ENODEV",
    [-DRV_EINVAL]     = "EINVAL",
    [-DRV_ETIMEDOUT]  = "ETIMEDOUT",
};
#define ERR_N ((int)(sizeof err_names / sizeof err_names[0]))
_Static_assert(sizeof err_names / sizeof err_names[0] == 111, "table must cover -DRV_ETIMEDOUT");
static const char *drv_strerror(int code) {
    if (code > 0 || code <= -ERR_N) return "EUNKNOWN";   /* -code 를 먼저 하면 INT_MIN 에서 overflow */
    const char *s = err_names[-code];
    return s ? s : "EUNKNOWN";                  /* 구멍(지정 안 한 칸)은 NULL */
}

/* ---------------- 연습 4: 두 드라이버가 공유하는 클럭 (refcount) ---------------- */
static unsigned clk_ref;          /* 소비자 수                         */
static bool     clk_gate_on;      /* 실제 RCC enable 비트              */
static int      clk_gate_writes;  /* 게이트 레지스터를 쓴 횟수         */
static void clk_get(void) {
    if (clk_ref++ == 0) { clk_gate_on = true; clk_gate_writes++; }   /* 0 → 1 에서만 켠다 */
}
static drv_err_t clk_put(void) {
    if (clk_ref == 0) return DRV_EINVAL;          /* unsigned 를 0 에서 빼면 UINT_MAX 로 돈다 */
    if (--clk_ref == 0) { clk_gate_on = false; clk_gate_writes++; }   /* 1 → 0 에서만 끈다 */
    return DRV_OK;
}

/* ---------------- 연습 5: 시간 기반 타임아웃 (tick 랩어라운드 + 마지막 재확인) ---------------- */
static uint32_t fake_tick;        /* 32비트 자유 카운터 (SysTick ms 카운터 같은 것) */
static int      tick_calls, jump_at;      /* jump_at 번째 now() 호출에서 "선점" 발생 */
static uint32_t reg_val;
static int      ready_after;      /* reg 를 이만큼 읽은 뒤 ready (-1 = 안 됨) */
static uint32_t now(void) {
    if (++tick_calls == jump_at) { fake_tick += 1000u; ready_after = 0; }  /* 오래 선점된 사이 HW 는 ready */
    return fake_tick++;
}
static uint32_t rd(void) {
    if (ready_after == 0) reg_val = 1u; else if (ready_after > 0) ready_after--;
    return reg_val;
}
static drv_err_t wait_ready_until(uint32_t timeout) {
    uint32_t start = now();
    for (;;) {
        if (rd() & 1u) return DRV_OK;
        if ((uint32_t)(now() - start) >= timeout)            /* 빼기는 랩어라운드에 안전 */
            return (rd() & 1u) ? DRV_OK : DRV_ETIMEDOUT;      /* 포기하기 전에 한 번 더 */
    }
}
static void tick_reset(uint32_t t0, int ready, int jump) {
    fake_tick = t0; tick_calls = 0; jump_at = jump; reg_val = 0; ready_after = ready;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(sim.trace, (s)) == 0)
#define NO_LEAK()   (led.clk == 0 && led.gpio == 0 && led.dma == 0 && led.irq == 0 && led.bad == 0)

static const struct { int stage; drv_err_t code; const char *trace; } fm[] = {
    { 0,       DRV_OK,     "+CLK +GPIO +DMA +IRQ" },
    { ST_CLK,  DRV_EIO,    "!CLK" },
    { ST_GPIO, DRV_EBUSY,  "+CLK !GPIO -CLK" },
    { ST_DMA,  DRV_ENOMEM, "+CLK +GPIO !DMA -GPIO -CLK" },
    { ST_IRQ,  DRV_ENODEV, "+CLK +GPIO +DMA !IRQ -DMA -GPIO -CLK" },
};

int main(void) {
    /* 연습 1 */
    for (size_t i = 0; i < sizeof fm / sizeof fm[0]; i++) {
        sim_reset(fm[i].stage, fm[i].code);
        CHECK(init_nested() == fm[i].code && TRACE_IS(fm[i].trace));
        CHECK(i == 0 ? (led.clk == 1 && led.irq == 1) : NO_LEAK());
    }

    /* 연습 2 */
    for (size_t i = 0; i < sizeof fm / sizeof fm[0]; i++) {
        int done = -1;
        sim_reset(fm[i].stage, fm[i].code);
        CHECK(init_staged(&done) == fm[i].code && TRACE_IS(fm[i].trace));
        CHECK(i == 0 ? done == 4 : (done == 0 && NO_LEAK()));
    }
    {
        int done = 0;
        sim_reset(0, DRV_OK);
        CHECK(init_staged(&done) == DRV_OK);
        teardown(done);                                   /* deinit = teardown(4) */
        CHECK(TRACE_IS("+CLK +GPIO +DMA +IRQ -IRQ -DMA -GPIO -CLK") && NO_LEAK());
    }

    /* 연습 3 */
    CHECK(strcmp(drv_strerror(DRV_OK), "OK") == 0);
    CHECK(strcmp(drv_strerror(DRV_ETIMEDOUT), "ETIMEDOUT") == 0);
    CHECK(strcmp(drv_strerror(DRV_ENODEV), "ENODEV") == 0);
    CHECK(strcmp(drv_strerror(-7), "EUNKNOWN") == 0);         /* 표 안의 빈 칸 */
    CHECK(strcmp(drv_strerror(-111), "EUNKNOWN") == 0);       /* 표 끝 바로 다음 */
    CHECK(strcmp(drv_strerror(3), "EUNKNOWN") == 0);          /* 양수는 에러가 아님 */
    CHECK(strcmp(drv_strerror(-2147483647 - 1), "EUNKNOWN") == 0);  /* INT_MIN 도 안전 */

    /* 연습 4 */
    clk_get();                                   /* UART */
    clk_get();                                   /* SPI  */
    CHECK(clk_gate_on && clk_ref == 2 && clk_gate_writes == 1);
    CHECK(clk_put() == DRV_OK && clk_gate_on);   /* UART deinit: SPI 가 아직 쓰는 중 → 게이트 유지 */
    CHECK(clk_put() == DRV_OK && !clk_gate_on && clk_gate_writes == 2);
    CHECK(clk_put() == DRV_EINVAL && clk_ref == 0 && !clk_gate_on);  /* 불균형 put 감지 */

    /* 연습 5 */
    tick_reset(0xFFFFFFFCu, 5, -1);              /* 6번째 읽기에 ready, 도중에 tick 이 0 을 넘어감 */
    CHECK(wait_ready_until(0x20u) == DRV_OK);
    CHECK(fake_tick < 0x10u);                    /* 정말로 랩어라운드를 지났다 */
    tick_reset(0xFFFFFFF0u, -1, -1);             /* 끝내 안 됨 */
    CHECK(wait_ready_until(0x20u) == DRV_ETIMEDOUT);
    tick_reset(100u, -1, 2);                     /* 첫 검사 직후 1000틱 선점, 그사이 HW 는 ready */
    CHECK(wait_ready_until(50u) == DRV_OK);      /* 마지막 재확인이 없으면 ETIMEDOUT 오판 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
