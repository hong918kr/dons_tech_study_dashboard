/* BM-B03 RCC: 주변장치 클럭 게이팅과 리셋 제어
 *
 * 호스트(맥) 빌드: UNIT_TEST 가 켜져 있어 REG_READ/REG_WRITE 가 mock 으로 간다.
 * 타깃 빌드: 드라이버 부분(periph_* / board_led_init)만 떼어 UNIT_TEST 없이 컴파일하면
 *   같은 코드가 volatile MMIO 접근이 된다 (demo_target.c 참고).
 * 주소·오프셋·비트·리셋값: RM0090 (STM32F405/407) 기준 대표값 — RM 확인 권장.
 * 테스트의 정답(ground truth)은 아래 mock 정의 자체다.
 * "enable 직후 N 사이클 동안 클럭이 아직 안 섰다" 는 STM32 errata
 * (ES0182 류 "Delay after an RCC peripheral clock enabling") 를 흉내 낸 **모델** 이다.
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- MMIO 접근 매크로 (BM-B02 와 같은 어휘) ---------------- */
#ifdef UNIT_TEST
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

/* ---------------- 주소 (RM0090 기준 대표값) ---------------- */
#define RCC_BASE      0x40023800u
#define RCC_AHB1RSTR  (RCC_BASE + 0x10u)
#define RCC_APB1RSTR  (RCC_BASE + 0x20u)
#define RCC_APB2RSTR  (RCC_BASE + 0x24u)
#define RCC_AHB1ENR   (RCC_BASE + 0x30u)
#define RCC_APB1ENR   (RCC_BASE + 0x40u)
#define RCC_APB2ENR   (RCC_BASE + 0x44u)
#define GPIOA_BASE    0x40020000u
#define GPIOB_BASE    0x40020400u
#define USART2_BASE   0x40004400u
#define GPIO_MODER    0x00u
#define GPIO_OTYPER   0x04u
#define GPIO_OSPEEDR  0x08u
#define GPIO_PUPDR    0x0Cu
#define GPIO_IDR      0x10u
#define GPIO_ODR      0x14u
#define GPIO_BSRR     0x18u
#define USART_SR      0x00u
#define USART_DR      0x04u
#define USART_BRR     0x08u
#define USART_CR1     0x0Cu

/* ---------------- 주변장치 표: (버스, 비트) ---------------- */
typedef enum { BUS_AHB1, BUS_APB1, BUS_APB2, BUS_COUNT } bus_t;
typedef enum { PERIPH_GPIOA, PERIPH_GPIOB, PERIPH_USART2, PERIPH_TIM2, PERIPH_USART1, PERIPH_COUNT } periph_id_t;
typedef struct { const char *name; bus_t bus; uint8_t bit; uint32_t base; } periph_info_t;
static const periph_info_t PERIPH[PERIPH_COUNT] = {
    [PERIPH_GPIOA]  = { "GPIOA",  BUS_AHB1,  0, GPIOA_BASE  },  /* AHB1ENR.GPIOAEN  bit0  */
    [PERIPH_GPIOB]  = { "GPIOB",  BUS_AHB1,  1, GPIOB_BASE  },  /* AHB1ENR.GPIOBEN  bit1  */
    [PERIPH_USART2] = { "USART2", BUS_APB1, 17, USART2_BASE },  /* APB1ENR.USART2EN bit17 */
    [PERIPH_TIM2]   = { "TIM2",   BUS_APB1,  0, 0x40000000u },  /* APB1ENR.TIM2EN   bit0  (레지스터 모델 없음) */
    [PERIPH_USART1] = { "USART1", BUS_APB2,  4, 0x40011000u },  /* APB2ENR.USART1EN bit4  (레지스터 모델 없음) */
};
static const uint32_t BUS_ENR[BUS_COUNT]  = { RCC_AHB1ENR,  RCC_APB1ENR,  RCC_APB2ENR  };
static const uint32_t BUS_RSTR[BUS_COUNT] = { RCC_AHB1RSTR, RCC_APB1RSTR, RCC_APB2RSTR };

/* ======================= mock core (BM-B02 mock 축약판) ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* CPU 가 보는 값 */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* 새 저장값 */
typedef struct { uint32_t addr, val, reset_val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { uint32_t cycle; char op; char note; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 32
#define MOCK_LOG_CAP  64
static struct {
    mock_reg_t reg[MOCK_MAX_REGS]; int nreg;
    mock_log_t log[MOCK_LOG_CAP];  int nlog; uint32_t log_lost;
    uint32_t cycle;       /* 버스 접근 1회 = 1 사이클 */
    uint32_t unknown;     /* 등록 안 된 주소 접근 → 테스트 실패로 센다 */
    char     note;        /* 훅이 남기는 표시: 'x' = 게이팅/리셋으로 무시됨 */
} g_mock;

void mock_reset(void) { memset(&g_mock, 0, sizeof g_mock); }
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_mock.nreg >= MOCK_MAX_REGS) return -1;
    g_mock.reg[g_mock.nreg++] = (mock_reg_t){ addr, reset_val, reset_val, r, w, ctx };
    return 0;
}
static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_mock.nreg; i++) if (g_mock.reg[i].addr == addr) return &g_mock.reg[i];
    return NULL;
}
static void mock_log(char op, uint32_t addr, uint32_t val) {
    if (g_mock.nlog < MOCK_LOG_CAP) g_mock.log[g_mock.nlog++] = (mock_log_t){ g_mock.cycle, op, g_mock.note, addr, val };
    else g_mock.log_lost++;
    g_mock.note = ' ';
}
uint32_t mock_read32(uint32_t addr) {
    g_mock.cycle++; g_mock.note = ' ';
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_mock.unknown++; mock_log('?', addr, 0); return 0; }
    uint32_t v = r->r ? r->r(addr, r->val, r->ctx) : r->val;
    mock_log('R', addr, v);
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    g_mock.cycle++; g_mock.note = ' ';
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_mock.unknown++; mock_log('?', addr, val); return; }
    r->val = r->w ? r->w(addr, r->val, val, r->ctx) : val;
    mock_log('W', addr, val);
}
/* 백도어(backdoor): 로그도 사이클도 없이 저장값을 보고/바꾼다 — 테스트와 모델 내부 전용 */
static uint32_t mock_peek(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? r->val : 0xDEADBEEFu; }
static void mock_poke(uint32_t addr, uint32_t v) { mock_reg_t *r = mock_find(addr); if (r) r->val = v; }
static void mock_restore_range(uint32_t lo, uint32_t hi) {      /* [lo, hi) 의 레지스터를 리셋값으로 */
    for (int i = 0; i < g_mock.nreg; i++)
        if (g_mock.reg[i].addr >= lo && g_mock.reg[i].addr < hi) g_mock.reg[i].val = g_mock.reg[i].reset_val;
}

/* ======================= RCC + 주변장치 모델 ======================= */
typedef struct {
    uint32_t delay[BUS_COUNT];          /* enable 쓰기 뒤 클럭이 서기까지 "죽은" 버스 사이클 수 (모델 값) */
    uint32_t ready_at[PERIPH_COUNT];    /* 이 사이클부터 클럭 유효 */
    bool     in_reset[PERIPH_COUNT];    /* xRSTR 비트 = 1 동안 리셋에 묶여 있음 */
    uint32_t dropped[PERIPH_COUNT];     /* 무시된 쓰기 수 (클럭 꺼짐 또는 리셋 중) */
    uint32_t zero_reads[PERIPH_COUNT];  /* 클럭 꺼짐이라 0 을 돌려준 읽기 수 */
} rcc_model_t;
static rcc_model_t g_rcc;
static bus_t       g_bus_ctx[BUS_COUNT]    = { BUS_AHB1, BUS_APB1, BUS_APB2 };
static periph_id_t g_per_ctx[PERIPH_COUNT] = { PERIPH_GPIOA, PERIPH_GPIOB, PERIPH_USART2, PERIPH_TIM2, PERIPH_USART1 };

/* Part A — TODO 1: 이 주변장치의 클럭이 "지금" 실제로 들어오고 있나?
 *   ENR 의 해당 비트가 1 이고, 현재 사이클 >= ready_at 일 때만 true. (ENR 은 백도어로 읽는다) */
static bool clk_effective(periph_id_t id) {
    const periph_info_t *p = &PERIPH[id];
    if (!((mock_peek(BUS_ENR[p->bus]) >> p->bit) & 1u)) return false;
    return g_mock.cycle >= g_rcc.ready_at[id];
}

/* 주변장치 레지스터 훅: 클럭이 없으면 읽기 0, 쓰기 무시. 리셋 중이면 쓰기 무시. */
static uint32_t periph_on_read(uint32_t addr, uint32_t stored, void *ctx) {
    periph_id_t id = *(periph_id_t *)ctx;
    if (!clk_effective(id)) { g_rcc.zero_reads[id]++; g_mock.note = 'x'; return 0; }
    if (id <= PERIPH_GPIOB && (addr & 0x3FFu) == GPIO_BSRR) return 0;        /* BSRR: 쓰기 전용 */
    return stored;
}
static uint32_t periph_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    periph_id_t id = *(periph_id_t *)ctx;
    if (!clk_effective(id) || g_rcc.in_reset[id]) { g_rcc.dropped[id]++; g_mock.note = 'x'; return old; }
    if (id <= PERIPH_GPIOB) {
        uint32_t off = addr & 0x3FFu, base = addr - off;
        if (off == GPIO_IDR) return old;                                       /* IDR: 읽기 전용 */
        if (off == GPIO_BSRR) {                                                /* set 이 reset 보다 우선 */
            uint32_t odr = mock_peek(base + GPIO_ODR);
            mock_poke(base + GPIO_ODR, (odr & ~(val >> 16)) | (val & 0xFFFFu));
            return 0;
        }
    }
    return val;
}

/* Part B — TODO 2: ENR 쓰기 훅. 0→1 로 바뀐 비트의 주변장치는 delay[bus] 사이클 동안 아직 클럭이 없다.
 *   ready_at = (이 쓰기의 사이클) + delay + 1. 이미 1 이던 비트는 건드리지 않는다. 1→0 은 즉시 꺼짐. */
static uint32_t rcc_enr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr;
    bus_t bus = *(bus_t *)ctx;
    uint32_t rising = val & ~old;
    for (int id = 0; id < PERIPH_COUNT; id++)
        if (PERIPH[id].bus == bus && ((rising >> PERIPH[id].bit) & 1u))
            g_rcc.ready_at[id] = g_mock.cycle + g_rcc.delay[bus] + 1u;
    return val;
}
/* Part B — TODO 3: ENR 읽기 훅 = 되읽기(read-back) 동기화. 앞선 ENR 쓰기가 끝나야 읽기가 돌아오므로,
 *   이 버스에서 아직 대기 중인(ready_at > 지금) enable 은 "다음 접근부터" 유효해진다. */
static uint32_t rcc_enr_on_read(uint32_t addr, uint32_t stored, void *ctx) {
    (void)addr;
    bus_t bus = *(bus_t *)ctx;
    for (int id = 0; id < PERIPH_COUNT; id++)
        if (PERIPH[id].bus == bus && ((stored >> PERIPH[id].bit) & 1u) && g_rcc.ready_at[id] > g_mock.cycle + 1u)
            g_rcc.ready_at[id] = g_mock.cycle + 1u;
    return stored;
}
/* Part C — TODO 4: RSTR 쓰기 훅. 비트가 1 이 되는 순간 그 주변장치 레지스터 전부를 리셋값으로,
 *   1 인 동안 in_reset = true (쓰기 무시), 0 이 되면 해제. */
static uint32_t rcc_rstr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old;
    bus_t bus = *(bus_t *)ctx;
    for (int id = 0; id < PERIPH_COUNT; id++) {
        if (PERIPH[id].bus != bus) continue;
        bool r = (val >> PERIPH[id].bit) & 1u;
        if (r) mock_restore_range(PERIPH[id].base, PERIPH[id].base + 0x400u);
        g_rcc.in_reset[id] = r;
    }
    return val;
}

/* 보드 모델 설치. 리셋값은 RM0090 기준 대표값 (AHB1ENR 의 bit20 CCMDATARAMEN = 1, GPIOA 의 PA13/14/15 = SWD/JTAG AF) */
static void sim_init(uint32_t d_ahb1, uint32_t d_apb1, uint32_t d_apb2) {
    mock_reset();
    memset(&g_rcc, 0, sizeof g_rcc);
    g_rcc.delay[BUS_AHB1] = d_ahb1; g_rcc.delay[BUS_APB1] = d_apb1; g_rcc.delay[BUS_APB2] = d_apb2;
    static const uint32_t ENR_RESET[BUS_COUNT] = { 0x00100000u, 0u, 0u };
    for (int b = 0; b < BUS_COUNT; b++) {
        mock_add_reg(BUS_ENR[b],  ENR_RESET[b], rcc_enr_on_read, rcc_enr_on_write,  &g_bus_ctx[b]);
        mock_add_reg(BUS_RSTR[b], 0u,           NULL,            rcc_rstr_on_write, &g_bus_ctx[b]);
    }
    static const uint32_t GPIO_RESET[2][7] = {      /* MODER OTYPER OSPEEDR PUPDR IDR ODR BSRR */
        { 0xA8000000u, 0u, 0x0C000000u, 0x64000000u, 0u, 0u, 0u },   /* GPIOA */
        { 0x00000280u, 0u, 0x000000C0u, 0x00000100u, 0u, 0u, 0u },   /* GPIOB */
    };
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < 7; k++)
            mock_add_reg(PERIPH[p].base + 4u * (uint32_t)k, GPIO_RESET[p][k], periph_on_read, periph_on_write, &g_per_ctx[p]);
    static const uint32_t USART_RESET[4] = { 0x000000C0u, 0u, 0u, 0u };  /* SR(TXE|TC) DR BRR CR1 */
    for (int k = 0; k < 4; k++)
        mock_add_reg(USART2_BASE + 4u * (uint32_t)k, USART_RESET[k], periph_on_read, periph_on_write, &g_per_ctx[PERIPH_USART2]);
}

/* ======================= 드라이버 API (타깃에서도 그대로 쓰는 부분) ======================= */
/* Part A/B — TODO 5: 클럭 켜기. 다른 비트는 보존(RMW)하고, 켠 직후 ENR 을 되읽어 동기화. 잘못된 id → -1 */
int periph_clock_enable(periph_id_t id) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t enr = BUS_ENR[PERIPH[id].bus];
    REG_WRITE(enr, REG_READ(enr) | (1u << PERIPH[id].bit));
    (void)REG_READ(enr);                       /* 되읽기: 위 쓰기가 끝난 뒤에야 돌아온다 */
    return 0;
}
/* Part A — TODO 6: 클럭 끄기 (RMW). 잘못된 id → -1 */
int periph_clock_disable(periph_id_t id) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t enr = BUS_ENR[PERIPH[id].bus];
    REG_WRITE(enr, REG_READ(enr) & ~(1u << PERIPH[id].bit));
    return 0;
}
/* Part C — TODO 7: 리셋 펄스. RSTR 비트를 1 로 (RMW) 했다가 다시 0 으로 (RMW). 잘못된 id → -1 */
int periph_reset_pulse(periph_id_t id) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t rstr = BUS_RSTR[PERIPH[id].bus], bit = 1u << PERIPH[id].bit;
    REG_WRITE(rstr, REG_READ(rstr) | bit);
    REG_WRITE(rstr, REG_READ(rstr) & ~bit);
    return 0;
}
/* Part C — TODO 8: LED(PA5) 초기화. 클럭 켜기 → 리셋 펄스 → MODER5 = 01 (다른 핀 보존) → BSRR 로 켜기 */
int board_led_init(void) {
    if (periph_clock_enable(PERIPH_GPIOA) != 0) return -1;
    if (periph_reset_pulse(PERIPH_GPIOA) != 0) return -1;
    uint32_t m = REG_READ(GPIOA_BASE + GPIO_MODER);
    REG_WRITE(GPIOA_BASE + GPIO_MODER, (m & ~(3u << 10)) | (1u << 10));
    REG_WRITE(GPIOA_BASE + GPIO_BSRR, 1u << 5);
    return 0;
}

/* ======================= 테스트가 쓰는 "버그 있는" 비교용 드라이버 ======================= */
static void naive_enable_assign(periph_id_t id) {          /* 버그 1: = 로 대입 → 다른 비트 다 지움 */
    REG_WRITE(BUS_ENR[PERIPH[id].bus], 1u << PERIPH[id].bit);
}
static void enable_no_sync(periph_id_t id) {               /* 버그 2: RMW 는 맞지만 되읽기 없음 */
    uint32_t enr = BUS_ENR[PERIPH[id].bus];
    REG_WRITE(enr, REG_READ(enr) | (1u << PERIPH[id].bit));
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_seed = 0x2463534Bu;
static uint32_t xorshift32(void) { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }

static const char *reg_name(uint32_t a) {
    static char buf[24];
    if (a == RCC_AHB1ENR)  return "RCC_AHB1ENR";
    if (a == RCC_APB1ENR)  return "RCC_APB1ENR";
    if (a == RCC_AHB1RSTR) return "RCC_AHB1RSTR";
    if (a == RCC_APB1RSTR) return "RCC_APB1RSTR";
    static const char *G[7] = { "MODER", "OTYPER", "OSPEEDR", "PUPDR", "IDR", "ODR", "BSRR" };
    static const char *U[4] = { "SR", "DR", "BRR", "CR1" };
    if (a >= GPIOA_BASE && a < GPIOA_BASE + 28u) { snprintf(buf, sizeof buf, "GPIOA_%s", G[(a - GPIOA_BASE) / 4u]); return buf; }
    if (a >= GPIOB_BASE && a < GPIOB_BASE + 28u) { snprintf(buf, sizeof buf, "GPIOB_%s", G[(a - GPIOB_BASE) / 4u]); return buf; }
    if (a >= USART2_BASE && a < USART2_BASE + 16u) { snprintf(buf, sizeof buf, "USART2_%s", U[(a - USART2_BASE) / 4u]); return buf; }
    snprintf(buf, sizeof buf, "0x%08X", (unsigned)a); return buf;
}
static void dump_log(const char *title) {
    printf("-- %s\n  cyc op %-14s value       note\n", title, "register");
    for (int i = 0; i < g_mock.nlog; i++) {
        const mock_log_t *e = &g_mock.log[i];
        printf("  %3u  %c %-14s 0x%08X  %s\n", (unsigned)e->cycle, e->op, reg_name(e->addr), (unsigned)e->val,
               e->note == 'x' ? (e->op == 'W' ? "IGNORED (no clock / in reset)" : "reads 0 (no clock)") : "");
    }
}
static bool log_is(int i, char op, uint32_t addr, uint32_t val) {
    return i < g_mock.nlog && g_mock.log[i].op == op && g_mock.log[i].addr == addr && g_mock.log[i].val == val;
}
/* 연속 쓰기를 시도해 처음으로 반영될 때까지 무시된 쓰기 수 (최대 limit) */
static uint32_t writes_until_effective(uint32_t addr, uint32_t val, uint32_t limit) {
    uint32_t lost = 0;
    while (lost < limit) { REG_WRITE(addr, val); if (mock_peek(addr) == val) break; lost++; }
    return lost;
}

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    const uint32_t MODER = GPIOA_BASE + GPIO_MODER;

    /* ===== Part A: 클럭 게이팅 (지연 0 = 이상적인 클럭) ===== */
    sim_init(0, 0, 0);
    REG_WRITE(MODER, 0xA8000400u);                                   /* 클럭 없이 쓰기 */
    CHECK(mock_peek(MODER) == 0xA8000000u && g_rcc.dropped[PERIPH_GPIOA] == 1u);
    CHECK(REG_READ(MODER) == 0u && g_rcc.zero_reads[PERIPH_GPIOA] == 1u);   /* 저장값은 있어도 0 으로 읽힘 */
    REG_WRITE(USART2_BASE + USART_BRR, 0x16Du);
    CHECK(mock_peek(USART2_BASE + USART_BRR) == 0u && g_rcc.dropped[PERIPH_USART2] == 1u);
    g_mock.nlog = 0;
    CHECK(periph_clock_enable(PERIPH_GPIOA) == 0);
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00100001u);                   /* CCMDATARAMEN(bit20) 보존 */
    CHECK(g_mock.nlog == 3 && log_is(0, 'R', RCC_AHB1ENR, 0x00100000u) && log_is(1, 'W', RCC_AHB1ENR, 0x00100001u)
          && log_is(2, 'R', RCC_AHB1ENR, 0x00100001u));                   /* 읽기 · 쓰기 · 되읽기 */
    REG_WRITE(MODER, 0xA8000400u);
    CHECK(mock_peek(MODER) == 0xA8000400u && REG_READ(MODER) == 0xA8000400u);
    CHECK(periph_clock_enable(PERIPH_USART2) == 0 && mock_peek(RCC_APB1ENR) == (1u << 17));
    CHECK(periph_clock_enable(PERIPH_USART1) == 0 && mock_peek(RCC_APB2ENR) == (1u << 4));
    CHECK(periph_clock_enable(PERIPH_TIM2) == 0 && mock_peek(RCC_APB1ENR) == ((1u << 17) | 1u));
    CHECK(periph_clock_enable(PERIPH_COUNT) == -1 && periph_clock_enable((periph_id_t)-1) == -1);
    CHECK(periph_clock_disable(PERIPH_COUNT) == -1 && periph_reset_pulse(PERIPH_COUNT) == -1);
    CHECK(g_mock.unknown == 0u);

    /* 버그 1 재현: = 대입은 CCM RAM 클럭(bit20)과 이미 켜 둔 GPIOB 를 꺼 버린다 */
    sim_init(0, 0, 0);
    mock_poke(RCC_AHB1ENR, 0x00100002u);                            /* GPIOB 는 이미 켜져 있음 */
    naive_enable_assign(PERIPH_GPIOA);
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00000001u);
    mock_poke(RCC_AHB1ENR, 0x00100002u);
    CHECK(periph_clock_enable(PERIPH_GPIOA) == 0 && mock_peek(RCC_AHB1ENR) == 0x00100003u);

    /* 무작위로 채운 ENR 3개에서 enable/disable 이 자기 비트 하나만 바꾸는가 (5 주변장치 × 200회) */
    int bad = 0;
    for (int t = 0; t < 200; t++)
        for (int id = 0; id < PERIPH_COUNT; id++) {
            sim_init(0, 0, 0);
            uint32_t before[BUS_COUNT];
            for (int b = 0; b < BUS_COUNT; b++) { before[b] = xorshift32(); mock_poke(BUS_ENR[b], before[b]); }
            uint32_t bit = 1u << PERIPH[id].bit; bus_t bus = PERIPH[id].bus;
            periph_clock_enable((periph_id_t)id);
            for (int b = 0; b < BUS_COUNT; b++)
                if (mock_peek(BUS_ENR[b]) != (b == (int)bus ? (before[b] | bit) : before[b])) bad++;
            periph_clock_disable((periph_id_t)id);
            for (int b = 0; b < BUS_COUNT; b++)
                if (mock_peek(BUS_ENR[b]) != (b == (int)bus ? (before[b] & ~bit) : before[b])) bad++;
        }
    CHECK(bad == 0);

    /* 끄고 접근: 쓰기 무시, 읽기 0. 다시 켜면 예전 값 그대로 (게이팅 ≠ 리셋) */
    sim_init(0, 0, 0);
    periph_clock_enable(PERIPH_GPIOA);
    REG_WRITE(MODER, 0xA8000400u);
    CHECK(periph_clock_disable(PERIPH_GPIOA) == 0 && mock_peek(RCC_AHB1ENR) == 0x00100000u);
    REG_WRITE(MODER, 0xA8000000u);
    CHECK(g_rcc.dropped[PERIPH_GPIOA] == 1u && REG_READ(MODER) == 0u);
    periph_clock_enable(PERIPH_GPIOA);
    CHECK(REG_READ(MODER) == 0xA8000400u);
    CHECK(g_mock.unknown == 0u);

    /* ===== Part B: "켜자마자 접근" 해저드 (모델 지연 AHB1 2, APB1 5, APB2 3 사이클) ===== */
    sim_init(2, 5, 3);
    g_mock.nlog = 0;
    enable_no_sync(PERIPH_GPIOA);
    REG_WRITE(MODER, 0xA8000400u);                                   /* 바로 다음 접근 → 무시 */
    CHECK(mock_peek(MODER) == 0xA8000000u && g_rcc.dropped[PERIPH_GPIOA] == 1u);
    CHECK(writes_until_effective(MODER, 0xA8000400u, 10) == 1u);     /* D=2: 쓰기 2번 잃고 3번째에 반영 */
    if (verbose) dump_log("enable_no_sync(GPIOA) then MODER writes, D_AHB1=2");
    sim_init(2, 5, 3);
    g_mock.nlog = 0;
    periph_clock_enable(PERIPH_GPIOA);
    REG_WRITE(MODER, 0xA8000400u);
    CHECK(mock_peek(MODER) == 0xA8000400u && g_rcc.dropped[PERIPH_GPIOA] == 0u);
    if (verbose) dump_log("periph_clock_enable(GPIOA) then MODER write, D_AHB1=2");

    /* 지연 D = 0..8 을 훑는다: 되읽기 없으면 정확히 D 번 잃고, 되읽기 있으면 0 번 */
    int sweep_bad = 0;
    for (uint32_t d = 0; d <= 8; d++) {
        sim_init(d, d, d);
        enable_no_sync(PERIPH_USART2);
        uint32_t lost_nosync = writes_until_effective(USART2_BASE + USART_BRR, 0x16Du, 20);
        sim_init(d, d, d);
        periph_clock_enable(PERIPH_USART2);
        uint32_t lost_sync = writes_until_effective(USART2_BASE + USART_BRR, 0x16Du, 20);
        if (lost_nosync != d || lost_sync != 0u) sweep_bad++;
        if (verbose) printf("  D=%u  lost writes: no read-back=%u  read-back=%u\n", (unsigned)d, (unsigned)lost_nosync, (unsigned)lost_sync);
    }
    CHECK(sweep_bad == 0);

    /* USART2 (APB1, D=5): 되읽기 없이 BRR·CR1 을 쓰면 둘 다 사라진다 → "UART 가 안 나와요" */
    sim_init(2, 5, 3);
    enable_no_sync(PERIPH_USART2);
    REG_WRITE(USART2_BASE + USART_BRR, 0x16Du);
    REG_WRITE(USART2_BASE + USART_CR1, (1u << 13) | (1u << 3));      /* UE | TE */
    CHECK(mock_peek(USART2_BASE + USART_BRR) == 0u && mock_peek(USART2_BASE + USART_CR1) == 0u);
    sim_init(2, 5, 3);
    periph_clock_enable(PERIPH_USART2);
    REG_WRITE(USART2_BASE + USART_BRR, 0x16Du);
    REG_WRITE(USART2_BASE + USART_CR1, (1u << 13) | (1u << 3));
    CHECK(mock_peek(USART2_BASE + USART_BRR) == 0x16Du && mock_peek(USART2_BASE + USART_CR1) == 0x2008u);

    /* 이미 켜진 주변장치는 같은 ENR 의 다른 비트를 켜도 다시 기다리지 않는다 */
    sim_init(2, 5, 3);
    periph_clock_enable(PERIPH_GPIOA);
    enable_no_sync(PERIPH_GPIOB);
    REG_WRITE(MODER, 0xA8000400u);
    REG_WRITE(GPIOB_BASE + GPIO_MODER, 0x00000281u);
    CHECK(mock_peek(MODER) == 0xA8000400u && mock_peek(GPIOB_BASE + GPIO_MODER) == 0x00000280u);

    /* 엉뚱한 레지스터(그 주변장치 자신)를 더미로 읽어도 동기화가 아니다 (모델: D=3) */
    sim_init(3, 5, 3);
    enable_no_sync(PERIPH_GPIOA);
    CHECK(REG_READ(GPIOA_BASE + GPIO_IDR) == 0u);                    /* 아직 클럭 없음 → 0 */
    REG_WRITE(MODER, 0xA8000400u);
    CHECK(mock_peek(MODER) == 0xA8000000u);
    CHECK(g_mock.unknown == 0u);

    /* ===== Part C: 리셋 펄스와 보드 초기화 ===== */
    sim_init(2, 5, 3);
    periph_clock_enable(PERIPH_GPIOA);
    periph_clock_enable(PERIPH_GPIOB);
    REG_WRITE(MODER, 0xA8000A80u);                                   /* "부트로더가 남긴" 설정 */
    REG_WRITE(GPIOA_BASE + GPIO_PUPDR, 0x64000015u);
    REG_WRITE(GPIOA_BASE + GPIO_ODR, 0x0000FFFFu);
    REG_WRITE(GPIOB_BASE + GPIO_ODR, 0x00000005u);
    g_mock.nlog = 0;
    CHECK(periph_reset_pulse(PERIPH_GPIOA) == 0);
    CHECK(g_mock.nlog == 4 && log_is(0, 'R', RCC_AHB1RSTR, 0u) && log_is(1, 'W', RCC_AHB1RSTR, 1u)
          && log_is(2, 'R', RCC_AHB1RSTR, 1u) && log_is(3, 'W', RCC_AHB1RSTR, 0u));
    CHECK(mock_peek(MODER) == 0xA8000000u && mock_peek(GPIOA_BASE + GPIO_PUPDR) == 0x64000000u
          && mock_peek(GPIOA_BASE + GPIO_ODR) == 0u && mock_peek(GPIOA_BASE + GPIO_OSPEEDR) == 0x0C000000u);
    CHECK(mock_peek(GPIOB_BASE + GPIO_ODR) == 5u);                   /* 이웃 주변장치는 그대로 */
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00100003u);                   /* 리셋 펄스는 클럭 enable 을 안 건드린다 */

    /* 리셋에 묶인 동안 쓰기 무시, 다른 RSTR 비트(GPIOB 를 일부러 잡아 둔 것)는 보존 */
    REG_WRITE(RCC_AHB1RSTR, 1u << 1);                                /* GPIOB 리셋 유지 */
    CHECK(periph_reset_pulse(PERIPH_GPIOA) == 0 && mock_peek(RCC_AHB1RSTR) == (1u << 1));
    REG_WRITE(GPIOB_BASE + GPIO_ODR, 0x00000005u);
    CHECK(mock_peek(GPIOB_BASE + GPIO_ODR) == 0u && g_rcc.in_reset[PERIPH_GPIOB] && !g_rcc.in_reset[PERIPH_GPIOA]);
    REG_WRITE(RCC_AHB1RSTR, 0u);
    REG_WRITE(GPIOB_BASE + GPIO_ODR, 0x00000005u);
    CHECK(mock_peek(GPIOB_BASE + GPIO_ODR) == 5u);

    /* 클럭이 꺼져 있어도 리셋은 걸린다 (모델). 켜고 나면 리셋값이 읽힌다 */
    sim_init(2, 5, 3);
    mock_poke(USART2_BASE + USART_BRR, 0x0683u); mock_poke(USART2_BASE + USART_SR, 0u);
    CHECK(periph_reset_pulse(PERIPH_USART2) == 0 && mock_peek(RCC_APB1RSTR) == 0u);
    periph_clock_enable(PERIPH_USART2);
    CHECK(REG_READ(USART2_BASE + USART_BRR) == 0u && REG_READ(USART2_BASE + USART_SR) == 0xC0u);

    /* board_led_init: 더러운 GPIOA 에서 시작해도 결과는 항상 같다. SWD 핀(PA13/14) AF 보존 */
    sim_init(2, 5, 3);
    mock_poke(MODER, 0xA8000A80u); mock_poke(GPIOA_BASE + GPIO_ODR, 0xFFFFu);
    g_mock.nlog = 0;
    CHECK(board_led_init() == 0);
    if (verbose) dump_log("board_led_init(), D_AHB1=2");
    CHECK(mock_peek(MODER) == 0xA8000400u && mock_peek(GPIOA_BASE + GPIO_ODR) == (1u << 5));
    CHECK(((mock_peek(MODER) >> 26) & 0x3Fu) == 0x2Au);              /* PA15/14/13 = 10 10 10 (AF) */
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00100001u && g_rcc.dropped[PERIPH_GPIOA] == 0u);
    CHECK(g_mock.nlog == 10 && log_is(7, 'R', MODER, 0xA8000000u) && log_is(8, 'W', MODER, 0xA8000400u)
          && log_is(9, 'W', GPIOA_BASE + GPIO_BSRR, 1u << 5));
    CHECK(board_led_init() == 0 && mock_peek(MODER) == 0xA8000400u && mock_peek(GPIOA_BASE + GPIO_ODR) == 0x20u);
    CHECK(g_mock.unknown == 0u && g_mock.log_lost == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
