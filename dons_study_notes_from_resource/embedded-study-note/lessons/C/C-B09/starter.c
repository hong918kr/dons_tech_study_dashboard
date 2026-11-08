/* C-B09 volatile의 의미와 한계
 *
 * 호스트(맥)에는 진짜 레지스터가 없다. 그래서 모든 레지스터·공유 변수 접근을 bus_read / bus_write
 * 두 함수로 보내서 ① 접근 기록(trace)을 남기고 ② 가짜 장치를 움직이고 ③ 정해진 순간에 "인터럽트"를
 * 끼워 넣는다. 실제 펌웨어에서 bus_read(r) 는 그냥 *r, bus_write(r, v) 는 *r = v 한 줄이고,
 * volatile 이 그 접근의 "횟수와 순서"를 소스에 적힌 그대로 지켜 준다.
 * 테스트는 바로 그 횟수와 순서를 trace 문자열로 검사한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 가짜 장치 (메모리 맵 레지스터 블록) ---------------- */
typedef struct {
    volatile uint32_t SR;   /* 상태: bit0 BUSY, bit1 RXNE(수신 데이터 있음)              */
    volatile uint32_t DR;   /* 데이터: 읽으면 RX FIFO에서 1바이트 꺼냄(pop), 쓰면 TX로 보냄 */
    volatile uint32_t CR;   /* 명령: 쓸 때마다 장치가 명령 1개를 실행                       */
} DEV_TypeDef;

#define SR_BUSY  (1u << 0)
#define SR_RXNE  (1u << 1)
#define CMD_LOAD 0x1u
#define CMD_GO   0x2u

static DEV_TypeDef fake_dev;          /* 실제 칩이라면 (DEV_TypeDef *)0x40001000 */
#define DEV (&fake_dev)
static volatile uint32_t g_ticks;     /* 타이머 ISR 과 main 이 공유하는 RAM 변수 */

/* ---------------- 시뮬레이터 (주어진 코드 — 고치지 않는다) ---------------- */
static char g_trace[256];             /* 예: "rSR rSR wDR=41 IRQ"               */
static int  g_nacc;                   /* 지금까지의 버스 접근 수                 */
static int  g_irq_at = -1;            /* 이 번호의 접근 직후 인터럽트 (-1 = 없음) */
static bool g_irq_masked, g_irq_pending;
static int  sim_busy_left;            /* SR 을 이만큼 더 읽는 동안 BUSY = 1      */
static uint8_t sim_rx[8];
static int  sim_rx_n, sim_rx_i;

static void trace(const char *s) {
    size_t n = strlen(g_trace);
    snprintf(g_trace + n, sizeof g_trace - n, "%s%s", n ? " " : "", s);
}
static const char *name_of(const volatile uint32_t *r) {
    return r == &DEV->SR ? "SR" : r == &DEV->DR ? "DR" : r == &DEV->CR ? "CR" : "T";
}
static void sim_isr(void) {           /* 타이머 ISR: ticks += 100 */
    trace("IRQ");
    g_ticks = g_ticks + 100u;
}
static void after_access(void) {
    if (++g_nacc == g_irq_at) {
        if (g_irq_masked) g_irq_pending = true;   /* 마스크 중이면 펜딩만 */
        else              sim_isr();
    }
}
static uint32_t bus_read(const volatile uint32_t *r) {
    char buf[16];
    if (r == &DEV->SR) {                          /* 하드웨어가 상태를 갱신 */
        uint32_t sr = 0;
        if (sim_busy_left > 0) { sr |= SR_BUSY; sim_busy_left--; }
        if (sim_rx_i < sim_rx_n) sr |= SR_RXNE;
        fake_dev.SR = sr;
    } else if (r == &DEV->DR) {                   /* DR 읽기 = FIFO pop */
        fake_dev.DR = sim_rx_i < sim_rx_n ? sim_rx[sim_rx_i++] : 0u;
    }
    uint32_t v = *r;
    snprintf(buf, sizeof buf, "r%s", name_of(r));
    trace(buf);
    after_access();
    return v;
}
static void bus_write(volatile uint32_t *r, uint32_t v) {
    char buf[24];
    *r = v;
    snprintf(buf, sizeof buf, "w%s=%X", name_of(r), (unsigned)v);
    trace(buf);
    after_access();
}
/* Cortex-M 의 __disable_irq() / __enable_irq() (CPSID i / CPSIE i) 흉내 */
static void irq_disable(void) { trace("DI"); g_irq_masked = true; }
static void irq_enable(void) {
    trace("EI");
    g_irq_masked = false;
    if (g_irq_pending) { g_irq_pending = false; sim_isr(); }  /* 미뤄진 인터럽트가 곧바로 들어온다 */
}
static void sim_reset(int busy_reads, const uint8_t *rx, int rx_n, int irq_at) {
    memset(&fake_dev, 0, sizeof fake_dev);
    g_trace[0] = '\0';
    g_nacc = 0; g_irq_at = irq_at; g_irq_masked = g_irq_pending = false;
    sim_busy_left = busy_reads;
    sim_rx_n = rx_n; sim_rx_i = 0;
    if (rx_n > 0) memcpy(sim_rx, rx, (size_t)rx_n);
    g_ticks = 0;
}

/* ---------------- Part A: 폴링 루프 ---------------- */
/* BUSY 가 풀릴 때까지 SR 을 폴링한다.
 * 반환: 성공하면 SR 을 읽은 횟수(1 이상), max_polls 번 읽어도 BUSY 면 -1. */
static int dev_wait_idle(DEV_TypeDef *d, int max_polls) {
    /* TODO: 이 코드는 "volatile 이 없을 때 컴파일러가 만드는 코드"와 같은 모양이다. 고쳐라. */
    uint32_t sr = bus_read(&d->SR);              /* 한 번만 읽고 ...       */
    for (int i = 1; i <= max_polls; i++)
        if (!(sr & SR_BUSY)) return i;           /* ... 낡은 사본만 본다   */
    return -1;
}

/* ---------------- Part B: 읽기 부작용 · 연속 쓰기 ---------------- */
/* RXNE 가 서 있는 동안 DR 을 읽어 buf 에 담는다. 반환: 읽은 바이트 수 (최대 cap).
 * 규칙: 바이트마다 SR 1번 + DR 1번. (DR 읽기 = pop 이라 두 번 읽으면 바이트가 사라진다) */
static int dev_drain(DEV_TypeDef *d, uint8_t *buf, int cap) {
    (void)d; (void)buf; (void)cap;
    return 0; /* TODO */
}
/* 명령 프로토콜: DR = arg → CR = CMD_LOAD → CR = CMD_GO  (CR 에 연달아 두 번 쓴다) */
static void dev_send_cmd(DEV_TypeDef *d, uint8_t arg) {
    (void)d; (void)arg; /* TODO */
}

/* ---------------- Part C: ISR 과 공유하는 카운터 ---------------- */
/* (주어진 코드) volatile 이어도 ticks += n 은 "읽기 1번 + 쓰기 1번" 두 접근이다 */
static void ticks_add_unsafe(uint32_t n) {
    uint32_t t = bus_read(&g_ticks);
    bus_write(&g_ticks, t + n);
}
/* TODO: 읽기와 쓰기 사이에 인터럽트가 끼어들어도 ISR 의 갱신을 잃지 않게 */
static void ticks_add_safe(uint32_t n) {
    (void)n; (void)irq_disable; (void)irq_enable; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(g_trace, (s)) == 0)

int main(void) {
    /* Part A */
    sim_reset(0, NULL, 0, -1);                   /* 처음부터 idle */
    CHECK(dev_wait_idle(DEV, 10) == 1);
    CHECK(TRACE_IS("rSR"));
    sim_reset(3, NULL, 0, -1);                   /* SR 3번 읽는 동안 BUSY, 4번째에 풀림 */
    CHECK(dev_wait_idle(DEV, 10) == 4);
    CHECK(TRACE_IS("rSR rSR rSR rSR"));
    sim_reset(1000, NULL, 0, -1);                /* 끝내 안 풀림 → 타임아웃 */
    CHECK(dev_wait_idle(DEV, 5) == -1);
    CHECK(TRACE_IS("rSR rSR rSR rSR rSR"));      /* 정확히 max_polls 번 읽었나 */

    /* Part B */
    const uint8_t rx[] = { 0x41, 0x42, 0x43 };
    uint8_t buf[8] = { 0 };
    sim_reset(0, rx, 3, -1);
    CHECK(dev_drain(DEV, buf, 8) == 3);
    CHECK(buf[0] == 0x41 && buf[1] == 0x42 && buf[2] == 0x43);
    CHECK(TRACE_IS("rSR rDR rSR rDR rSR rDR rSR"));
    sim_reset(0, rx, 3, -1);
    CHECK(dev_drain(DEV, buf, 2) == 2);          /* cap 에서 멈추고 3번째 바이트는 남긴다 */
    CHECK(TRACE_IS("rSR rDR rSR rDR"));
    sim_reset(0, NULL, 0, -1);
    dev_send_cmd(DEV, 0x5A);
    CHECK(TRACE_IS("wDR=5A wCR=1 wCR=2"));       /* CR 쓰기 2번이 합쳐지면 안 된다 */

    /* Part C */
    sim_reset(0, NULL, 0, 1);                    /* 1번째 접근(읽기) 직후 인터럽트 */
    ticks_add_unsafe(5);
    CHECK(g_ticks == 5);                         /* ISR 의 +100 이 사라졌다 (lost update 재현) */
    CHECK(TRACE_IS("rT IRQ wT=5"));
    sim_reset(0, NULL, 0, 1);
    ticks_add_safe(5);
    CHECK(g_ticks == 105);                       /* 갱신을 잃지 않는다 */
    CHECK(TRACE_IS("DI rT wT=5 EI IRQ"));        /* 인터럽트는 EI 직후로 미뤄진다 */
    sim_reset(0, NULL, 0, -1);
    ticks_add_safe(7);
    CHECK(g_ticks == 7 && TRACE_IS("DI rT wT=7 EI"));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
