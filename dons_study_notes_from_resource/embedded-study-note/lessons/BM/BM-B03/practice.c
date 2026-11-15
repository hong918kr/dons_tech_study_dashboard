/* BM-B03 추가 연습 (practice.c) — 본문 solution.c 의 mock·모델·드라이버를 그대로 담고 연습 1~4 를 더한다.
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
static void mock_idle(uint32_t n) { g_mock.cycle += n; }       /* NOP n 개: 버스 접근 없이 시간만 흐름 */
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

/* ======================= 추가 연습 ======================= */
/* 연습 1: 여러 주변장치를 한 번에 — 버스(ENR 레지스터)마다 RMW 1번 + 되읽기 1번 */
int periph_clock_enable_many(const periph_id_t *ids, int n) {
    uint32_t mask[BUS_COUNT] = { 0u, 0u, 0u };
    for (int i = 0; i < n; i++) {                        /* 먼저 전부 검사 — 잘못된 id 가 있으면 아무것도 안 건드림 */
        if ((unsigned)ids[i] >= PERIPH_COUNT) return -1;
        mask[PERIPH[ids[i]].bus] |= 1u << PERIPH[ids[i]].bit;
    }
    for (int b = 0; b < BUS_COUNT; b++) {
        if (!mask[b]) continue;
        REG_WRITE(BUS_ENR[b], REG_READ(BUS_ENR[b]) | mask[b]);
        (void)REG_READ(BUS_ENR[b]);
    }
    return 0;
}

/* 연습 2: 되읽기 대신 NOP n 개 (errata 의 다른 우회책 흉내) */
int periph_clock_enable_nops(periph_id_t id, uint32_t nops) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t enr = BUS_ENR[PERIPH[id].bus];
    REG_WRITE(enr, REG_READ(enr) | (1u << PERIPH[id].bit));
    mock_idle(nops);
    return 0;
}
/* 이 버스에서 "켜고 바로 쓴 첫 쓰기" 가 반영되는 최소 NOP 수 (탐색) */
static uint32_t min_nops(periph_id_t id, uint32_t reg, uint32_t d_ahb1, uint32_t d_apb1, uint32_t d_apb2) {
    for (uint32_t n = 0; n <= 32u; n++) {
        sim_init(d_ahb1, d_apb1, d_apb2);
        periph_clock_enable_nops(id, n);
        REG_WRITE(reg, 0x16Du);
        if (mock_peek(reg) == 0x16Du) return n;
    }
    return 999u;
}

/* 연습 3: deinit = 리셋 펄스(설정을 리셋값으로) + 클럭 끄기(전력) */
int periph_deinit(periph_id_t id) {
    if (periph_reset_pulse(id) != 0) return -1;
    return periph_clock_disable(id);
}

/* 연습 4: 어떤 클럭이 켜져 있나 덤프 — "GPIOA AHB1[20] USART2" 형식, 용량 초과 시 -1 (잘린 문자열은 NUL 종료) */
static const char *BUS_NAME[BUS_COUNT] = { "AHB1", "APB1", "APB2" };
int rcc_dump(char *buf, size_t cap) {
    if (cap == 0u) return -1;
    size_t len = 0; bool trunc = false;
    buf[0] = '\0';
    for (int b = 0; b < BUS_COUNT; b++) {
        uint32_t v = REG_READ(BUS_ENR[b]);
        for (unsigned bit = 0; bit < 32u; bit++) {
            if (!((v >> bit) & 1u)) continue;
            char item[16];
            const char *name = NULL;
            for (int id = 0; id < PERIPH_COUNT; id++)
                if (PERIPH[id].bus == (bus_t)b && PERIPH[id].bit == bit) name = PERIPH[id].name;
            if (name) snprintf(item, sizeof item, "%s%s", len ? " " : "", name);
            else      snprintf(item, sizeof item, "%s%s[%u]", len ? " " : "", BUS_NAME[b], bit);
            for (const char *c = item; *c; c++) {
                if (len + 1u >= cap) { trunc = true; break; }
                buf[len++] = *c;
            }
            buf[len] = '\0';
        }
    }
    return trunc ? -1 : (int)len;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool log_is(int i, char op, uint32_t addr, uint32_t val) {
    return i < g_mock.nlog && g_mock.log[i].op == op && g_mock.log[i].addr == addr && g_mock.log[i].val == val;
}

int main(void) {
    const uint32_t MODER = GPIOA_BASE + GPIO_MODER, BRR = USART2_BASE + USART_BRR;

    /* 연습 1 */
    sim_init(2, 5, 3);
    const periph_id_t ids[4] = { PERIPH_GPIOA, PERIPH_USART2, PERIPH_GPIOB, PERIPH_TIM2 };
    CHECK(periph_clock_enable_many(ids, 4) == 0);
    CHECK(g_mock.nlog == 6 && log_is(1, 'W', RCC_AHB1ENR, 0x00100003u) && log_is(2, 'R', RCC_AHB1ENR, 0x00100003u)
          && log_is(4, 'W', RCC_APB1ENR, 0x00020001u) && log_is(5, 'R', RCC_APB1ENR, 0x00020001u));
    REG_WRITE(MODER, 0xA8000400u);
    REG_WRITE(BRR, 0x16Du);
    REG_WRITE(GPIOB_BASE + GPIO_MODER, 0x00000281u);
    CHECK(mock_peek(MODER) == 0xA8000400u && mock_peek(BRR) == 0x16Du && mock_peek(GPIOB_BASE + GPIO_MODER) == 0x281u);
    sim_init(2, 5, 3);
    const periph_id_t bad_ids[3] = { PERIPH_GPIOA, PERIPH_COUNT, PERIPH_USART2 };
    CHECK(periph_clock_enable_many(bad_ids, 3) == -1 && g_mock.nlog == 0);
    CHECK(periph_clock_enable_many(ids, 0) == 0 && g_mock.nlog == 0);

    /* 연습 2: 최소 NOP 수 = 버스 지연 D (모델: AHB1 2, APB1 5, APB2 3) */
    CHECK(min_nops(PERIPH_GPIOA, MODER, 2, 5, 3) == 2u);
    CHECK(min_nops(PERIPH_USART2, BRR, 2, 5, 3) == 5u);
    /* APB1 프리스케일러를 /4 → /8 로 바꾸면 (모델 D = 1 + 8 = 9): NOP 5 개는 깨지고 되읽기는 그대로 OK */
    CHECK(min_nops(PERIPH_USART2, BRR, 2, 9, 3) == 9u);
    sim_init(2, 9, 3);
    periph_clock_enable_nops(PERIPH_USART2, 5);
    REG_WRITE(BRR, 0x16Du);
    CHECK(mock_peek(BRR) == 0u && g_rcc.dropped[PERIPH_USART2] == 1u);
    sim_init(2, 9, 3);
    periph_clock_enable(PERIPH_USART2);
    REG_WRITE(BRR, 0x16Du);
    CHECK(mock_peek(BRR) == 0x16Du);

    /* 연습 3 */
    sim_init(2, 5, 3);
    periph_clock_enable(PERIPH_GPIOA);
    REG_WRITE(MODER, 0xA8000A80u);
    CHECK(periph_deinit(PERIPH_GPIOA) == 0);
    CHECK(mock_peek(MODER) == 0xA8000000u && mock_peek(RCC_AHB1ENR) == 0x00100000u && mock_peek(RCC_AHB1RSTR) == 0u);
    REG_WRITE(MODER, 0xA8000400u);
    CHECK(REG_READ(MODER) == 0u && g_rcc.dropped[PERIPH_GPIOA] == 1u);
    periph_clock_enable(PERIPH_GPIOA);
    CHECK(REG_READ(MODER) == 0xA8000000u);
    CHECK(periph_deinit(PERIPH_COUNT) == -1);

    /* 연습 4 */
    char buf[64];
    sim_init(0, 0, 0);
    CHECK(rcc_dump(buf, sizeof buf) == 8 && strcmp(buf, "AHB1[20]") == 0);
    periph_clock_enable(PERIPH_GPIOA); periph_clock_enable(PERIPH_USART2); periph_clock_enable(PERIPH_TIM2);
    CHECK(rcc_dump(buf, sizeof buf) == 26 && strcmp(buf, "GPIOA AHB1[20] TIM2 USART2") == 0);
    periph_clock_enable(PERIPH_USART1);
    CHECK(rcc_dump(buf, sizeof buf) == 33 && strcmp(buf, "GPIOA AHB1[20] TIM2 USART2 USART1") == 0);
    CHECK(rcc_dump(buf, 8) == -1 && strcmp(buf, "GPIOA A") == 0);
    buf[0] = 'Z';
    CHECK(rcc_dump(buf, 0) == -1 && buf[0] == 'Z');
    CHECK(g_mock.unknown == 0u && g_mock.log_lost == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
