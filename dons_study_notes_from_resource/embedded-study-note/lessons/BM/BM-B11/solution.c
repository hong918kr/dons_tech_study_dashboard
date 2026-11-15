/* BM-B11 GPIO 드라이버 (레지스터 직접): 모드·풀업·출력·입력
 *
 * Part A — gpio_init / gpio_write(BSRR) / gpio_read(IDR) / gpio_toggle(ODR 읽기 + BSRR 쓰기)
 * Part B — 다른 핀을 건드리지 않나 (전 핀 × 전 설정 exhaustive, 랜덤 프리필), AFRL/AFRH 경계,
 *          핀 0/15, 잘못된 인자 거부, RCC 클럭 enable 선행 (클럭 꺼지면 mock 이 쓰기를 무시)
 * Part C — 보드 초기화 (LED·버튼·USART2·SPI CS), 글리치 없는 출력 초기화, 버튼 → LED 루프
 *
 * 레지스터 주소·오프셋·리셋 값은 STM32F4 (RM0090) 기준 대표값이다 — 테스트의 정답은 아래 mock 정의.
 * 같은 드라이버 소스를 타깃용으로 빌드하려면 UNIT_TEST 를 빼면 된다 (REG_* 가 volatile MMIO 로 바뀜).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    (-v: 접근 로그 출력)
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== mock core (BM-B02 mock 축약판) ===================== */
#ifdef UNIT_TEST                      /* host build: hooks */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#else                                 /* target build: real MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#endif

typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);              /* CPU 가 보는 값 */
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx); /* 새 저장 값 */

#define MOCK_MAX_REGS 64
#define MOCK_LOG_CAP  256
typedef struct { uint32_t addr, val, reset; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;

static mock_reg_t g_regs[MOCK_MAX_REGS];
static int        g_nregs;
static mock_log_t g_log[MOCK_LOG_CAP];
static int        g_nlog, g_log_dropped;
static int        g_unexpected;                /* 정의 안 된 주소 접근 횟수 (테스트 실패 신호) */

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++)
        if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS || mock_find(addr)) return -1;
    g_regs[g_nregs++] = (mock_reg_t){ addr, reset_val, reset_val, r, w, ctx };
    return 0;
}
void mock_reset(void) { g_nregs = 0; g_nlog = 0; g_log_dropped = 0; g_unexpected = 0; }
static void mock_log_clear(void) { g_nlog = 0; g_log_dropped = 0; }
static void mock_log(char op, uint32_t addr, uint32_t val) {
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
    else g_log_dropped++;
}
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_unexpected++; mock_log('R', addr, 0); return 0; }
    uint32_t v = r->r ? r->r(addr, r->val, r->ctx) : r->val;
    mock_log('R', addr, v);
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_unexpected++; mock_log('W', addr, val); return; }
    mock_log('W', addr, val);
    r->val = r->w ? r->w(addr, r->val, val, r->ctx) : val;
}
/* 백도어: 훅도 로그도 없이 저장 값을 직접 보고/바꾼다 (테스트 준비·검사 전용) */
static uint32_t mock_peek(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? r->val : 0xDEADBEEFu; }
static void mock_poke(uint32_t addr, uint32_t v) { mock_reg_t *r = mock_find(addr); if (r) r->val = v; }

/* ===================== RCC + GPIO 모델 (RM0090 기준 대표값 — RM 확인 권장) ===================== */
#define RCC_BASE          0x40023800u
#define RCC_AHB1ENR       (RCC_BASE + 0x30u)   /* bit0 GPIOAEN, bit1 GPIOBEN, bit2 GPIOCEN, bit3 GPIODEN */
#define RCC_AHB1ENR_RESET 0x00100000u          /* bit20 CCMDATARAMEN 이 리셋 때 1 */

#define GPIOA 0x40020000u
#define GPIOB 0x40020400u
#define GPIOC 0x40020800u
#define GPIOD 0x40020C00u
#define GPIO_NPORTS 4
#define GPIO_MODER   0x00u   /* rw  핀당 2비트: 00 input, 01 output, 10 AF, 11 analog */
#define GPIO_OTYPER  0x04u   /* rw  핀당 1비트: 0 push-pull, 1 open-drain ([31:16] reserved) */
#define GPIO_OSPEEDR 0x08u   /* rw  핀당 2비트: 00 low .. 11 very high */
#define GPIO_PUPDR   0x0Cu   /* rw  핀당 2비트: 00 none, 01 pull-up, 10 pull-down, 11 reserved */
#define GPIO_IDR     0x10u   /* r   핀 레벨 (입력 버퍼 → 동기화기) */
#define GPIO_ODR     0x14u   /* rw  출력 래치 */
#define GPIO_BSRR    0x18u   /* w   [15:0] set, [31:16] reset — 읽으면 0 */
#define GPIO_LCKR    0x1Cu   /* rw  설정 잠금 (연습 2) */
#define GPIO_AFRL    0x20u   /* rw  핀 0..7  × 4비트 AF 번호 */
#define GPIO_AFRH    0x24u   /* rw  핀 8..15 × 4비트 AF 번호 */
static const uint32_t GPIO_REG_OFF[10] = { GPIO_MODER, GPIO_OTYPER, GPIO_OSPEEDR, GPIO_PUPDR, GPIO_IDR,
                                           GPIO_ODR, GPIO_BSRR, GPIO_LCKR, GPIO_AFRL, GPIO_AFRH };

typedef enum { EXT_FLOAT = 0, EXT_LOW, EXT_HIGH } ext_t;   /* 보드 쪽에서 핀을 누가 구동하나 */
enum { LVL_0 = 0, LVL_1 = 1, LVL_Z = 2, LVL_X = 3 };       /* Z = 떠 있음, X = 충돌(contention) */

typedef struct { int idx; uint32_t base; uint8_t ext[16]; uint8_t level[16]; } gpio_model_t;
static gpio_model_t g_port[GPIO_NPORTS];
#define EDGE_CAP 512
typedef struct { uint8_t port, pin, level; } edge_t;
static edge_t   g_edge[EDGE_CAP];     /* 핀 레벨이 바뀔 때마다 기록 (글리치 검출용) */
static int      g_nedge;
static int      g_contention;         /* MCU 출력과 외부 구동이 반대로 싸운 횟수 */
static uint32_t g_noise;              /* 떠 있는 입력이 읽히는 값 = 잡음 */

static bool model_clk_on(int pi) { return (mock_peek(RCC_AHB1ENR) >> pi) & 1u; }
static uint32_t noise_bit(void) { g_noise ^= g_noise << 13; g_noise ^= g_noise >> 17; g_noise ^= g_noise << 5; return (g_noise >> 7) & 1u; }

static unsigned model_pin_level(const gpio_model_t *p, unsigned pin) {
    unsigned mode = (mock_peek(p->base + GPIO_MODER) >> (2u * pin)) & 3u;
    unsigned pull = (mock_peek(p->base + GPIO_PUPDR) >> (2u * pin)) & 3u;
    unsigned od   = (mock_peek(p->base + GPIO_OTYPER) >> pin) & 1u;
    unsigned odr  = (mock_peek(p->base + GPIO_ODR) >> pin) & 1u;
    unsigned ext  = p->ext[pin];
    unsigned drive = LVL_Z;                                   /* MCU 가 구동하는 값 */
    if (mode == 1u) drive = od ? (odr ? LVL_Z : LVL_0) : odr; /* open-drain 의 1 = 놓아 줌 */
    if (drive != LVL_Z) {
        if (ext != EXT_FLOAT && (unsigned)(ext == EXT_HIGH) != drive) return LVL_X;
        return drive;
    }
    if (ext != EXT_FLOAT) return ext == EXT_HIGH ? LVL_1 : LVL_0; /* 외부 구동 > 약한 풀 저항 */
    if (mode != 3u) {                                          /* analog 에서는 풀 저항 끊김 */
        if (pull == 1u) return LVL_1;
        if (pull == 2u) return LVL_0;
    }
    return LVL_Z;
}
static void model_update(gpio_model_t *p, bool log_edges) {
    for (unsigned pin = 0; pin < 16u; pin++) {
        uint8_t lv = (uint8_t)model_pin_level(p, pin);
        if (lv == p->level[pin]) continue;
        if (log_edges && g_nedge < EDGE_CAP) g_edge[g_nedge++] = (edge_t){ (uint8_t)p->idx, (uint8_t)pin, lv };
        if (lv == LVL_X) g_contention++;
        p->level[pin] = lv;
    }
}
static uint32_t model_idr(gpio_model_t *p) {
    uint32_t idr = 0;
    for (unsigned pin = 0; pin < 16u; pin++) {
        unsigned mode = (mock_peek(p->base + GPIO_MODER) >> (2u * pin)) & 3u;
        unsigned lv = p->level[pin];
        uint32_t bit = (mode == 3u) ? 0u                       /* analog: 슈미트 트리거 꺼짐 → 0 */
                     : (lv == LVL_0 || lv == LVL_1) ? lv : noise_bit();
        idr |= bit << pin;
    }
    return idr;
}
static uint32_t gpio_on_read(uint32_t addr, uint32_t stored, void *ctx) {
    gpio_model_t *p = ctx;
    if (!model_clk_on(p->idx)) return 0;            /* 클럭 꺼짐: 읽으면 0 */
    uint32_t off = addr - p->base;
    if (off == GPIO_BSRR) return 0;                 /* 쓰기 전용 */
    if (off == GPIO_IDR) return model_idr(p);
    return stored;
}
static uint32_t gpio_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    gpio_model_t *p = ctx;
    if (!model_clk_on(p->idx)) return old;          /* 클럭 꺼짐: 쓰기 무시 (BM-B03 규칙 축약) */
    uint32_t off = addr - p->base, nv = val;
    if (off == GPIO_IDR) return old;                /* 읽기 전용 */
    if (off == GPIO_ODR || off == GPIO_OTYPER) nv = val & 0xFFFFu;
    if (off == GPIO_BSRR) {                         /* set 과 reset 이 같이 오면 set 이 이긴다 */
        uint32_t odr = mock_peek(p->base + GPIO_ODR);
        mock_poke(p->base + GPIO_ODR, ((odr & ~(val >> 16)) | (val & 0xFFFFu)) & 0xFFFFu);
        nv = 0;
    }
    mock_poke(addr, nv);
    model_update(p, true);
    return nv;
}
static uint32_t gpio_reset_val(int pi, uint32_t off) {
    if (pi == 0 && off == GPIO_MODER)   return 0xA8000000u;   /* PA13/14/15 = AF (SWD/JTAG) */
    if (pi == 0 && off == GPIO_OSPEEDR) return 0x0C000000u;
    if (pi == 0 && off == GPIO_PUPDR)   return 0x64000000u;
    if (pi == 1 && off == GPIO_MODER)   return 0x00000280u;   /* PB3/PB4 = AF (JTAG) */
    if (pi == 1 && off == GPIO_OSPEEDR) return 0x000000C0u;
    if (pi == 1 && off == GPIO_PUPDR)   return 0x00000100u;
    return 0;
}
static void model_reset(void) {
    mock_reset();
    mock_add_reg(RCC_AHB1ENR, RCC_AHB1ENR_RESET, NULL, NULL, NULL);
    for (int i = 0; i < GPIO_NPORTS; i++) {
        gpio_model_t *p = &g_port[i];
        memset(p, 0, sizeof *p);
        p->idx = i;
        p->base = GPIOA + 0x400u * (uint32_t)i;
        for (int k = 0; k < 10; k++)
            mock_add_reg(p->base + GPIO_REG_OFF[k], gpio_reset_val(i, GPIO_REG_OFF[k]), gpio_on_read, gpio_on_write, p);
        for (unsigned pin = 0; pin < 16u; pin++) p->level[pin] = (uint8_t)model_pin_level(p, pin);
    }
    g_nedge = 0; g_contention = 0; g_noise = 0x2545F491u;
}
static void ext_set(uint32_t port, unsigned pin, ext_t e) {   /* 보드 쪽: 버튼·다른 칩이 핀을 구동 */
    gpio_model_t *p = &g_port[(port - GPIOA) / 0x400u];
    p->ext[pin] = (uint8_t)e;
    model_update(p, true);
}

/* ===================== GPIO 드라이버 (여기를 구현) ===================== */
#define GPIO_EINVAL (-1)
typedef enum { GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1, GPIO_MODE_AF = 2, GPIO_MODE_ANALOG = 3 } gpio_mode_t;
typedef enum { GPIO_PULL_NONE = 0, GPIO_PULL_UP = 1, GPIO_PULL_DOWN = 2 } gpio_pull_t;
typedef enum { GPIO_SPEED_LOW = 0, GPIO_SPEED_MEDIUM = 1, GPIO_SPEED_FAST = 2, GPIO_SPEED_HIGH = 3 } gpio_speed_t;
typedef enum { GPIO_OTYPE_PP = 0, GPIO_OTYPE_OD = 1 } gpio_otype_t;

static const uint32_t GPIO_PORTS[GPIO_NPORTS] = { GPIOA, GPIOB, GPIOC, GPIOD };
/* 지원 포트면 0..3 (= RCC_AHB1ENR 의 GPIOxEN 비트 번호), 아니면 -1 */
static int port_index(uint32_t port) {
    for (int i = 0; i < GPIO_NPORTS; i++)
        if (GPIO_PORTS[i] == port) return i;
    return -1;
}
/* reg 의 [shift + width-1 : shift] 필드를 val 로 (mask = 필드 폭의 1들, 예: 2비트면 0x3) — C-B01 */
static inline uint32_t field_put(uint32_t reg, unsigned shift, uint32_t mask, uint32_t val) {
    return (reg & ~(mask << shift)) | ((val & mask) << shift);
}

/* RCC_AHB1ENR 의 GPIOxEN 을 켠다. 다른 비트 보존 (RMW) + read-back. 잘못된 포트 → GPIO_EINVAL */
int gpio_clock_enable(uint32_t port) {
    int pi = port_index(port);
    if (pi < 0) return GPIO_EINVAL;
    uint32_t en = REG_READ(RCC_AHB1ENR);
    REG_WRITE(RCC_AHB1ENR, en | (1u << (unsigned)pi));
    (void)REG_READ(RCC_AHB1ENR);                    /* read-back: 쓰기가 끝난 뒤 포트 접근 (BM-B03) */
    return 0;
}

/* 핀 하나를 설정한다. 그 핀의 필드만 바꾸고, 다른 핀 필드는 그대로.
 * 순서: 인자 검사 → 클럭 → OTYPER → OSPEEDR → PUPDR → (AF 면) AFRL/AFRH → MODER 마지막.
 * 잘못된 인자 (포트, pin > 15, enum 범위 밖, af > 15, analog + 풀) → 레지스터 접근 없이 GPIO_EINVAL */
int gpio_init(uint32_t port, unsigned pin, gpio_mode_t mode, gpio_pull_t pull,
              gpio_speed_t speed, gpio_otype_t otype, unsigned af) {
    if (port_index(port) < 0 || pin > 15u || (unsigned)mode > 3u || (unsigned)pull > 2u ||
        (unsigned)speed > 3u || (unsigned)otype > 1u || af > 15u) return GPIO_EINVAL;
    if (mode == GPIO_MODE_ANALOG && pull != GPIO_PULL_NONE) return GPIO_EINVAL;
    gpio_clock_enable(port);
    unsigned s2 = 2u * pin;
    REG_WRITE(port + GPIO_OTYPER,  field_put(REG_READ(port + GPIO_OTYPER),  pin, 0x1u, (uint32_t)otype));
    REG_WRITE(port + GPIO_OSPEEDR, field_put(REG_READ(port + GPIO_OSPEEDR), s2,  0x3u, (uint32_t)speed));
    REG_WRITE(port + GPIO_PUPDR,   field_put(REG_READ(port + GPIO_PUPDR),   s2,  0x3u, (uint32_t)pull));
    if (mode == GPIO_MODE_AF) {
        uint32_t afr = port + (pin < 8u ? GPIO_AFRL : GPIO_AFRH);
        REG_WRITE(afr, field_put(REG_READ(afr), 4u * (pin & 7u), 0xFu, af));
    }
    REG_WRITE(port + GPIO_MODER,   field_put(REG_READ(port + GPIO_MODER),   s2,  0x3u, (uint32_t)mode));
    return 0;
}
/* 5인자 버전 (아이템 시그니처): push-pull, AF 0 */
int gpio_init5(uint32_t port, unsigned pin, gpio_mode_t mode, gpio_pull_t pull, gpio_speed_t speed) {
    return gpio_init(port, pin, mode, pull, speed, GPIO_OTYPE_PP, 0u);
}

/* BSRR 한 번 쓰기로 핀 출력 (읽기 없음). 0 / GPIO_EINVAL */
int gpio_write(uint32_t port, unsigned pin, int level) {
    if (port_index(port) < 0 || pin > 15u) return GPIO_EINVAL;
    REG_WRITE(port + GPIO_BSRR, level ? (1u << pin) : (1u << (pin + 16u)));
    return 0;
}
/* IDR 한 번 읽기로 핀 레벨. 0 / 1 / GPIO_EINVAL */
int gpio_read(uint32_t port, unsigned pin) {
    if (port_index(port) < 0 || pin > 15u) return GPIO_EINVAL;
    return (int)((REG_READ(port + GPIO_IDR) >> pin) & 1u);
}
/* ODR 을 읽어 (IDR 아님!) 반대 값을 BSRR 로 쓴다. ODR 에 직접 쓰지 않는다. 0 / GPIO_EINVAL */
int gpio_toggle(uint32_t port, unsigned pin) {
    if (port_index(port) < 0 || pin > 15u) return GPIO_EINVAL;
    uint32_t odr = REG_READ(port + GPIO_ODR);
    REG_WRITE(port + GPIO_BSRR, (odr & (1u << pin)) ? (1u << (pin + 16u)) : (1u << pin));
    return 0;
}

/* ===================== Part C: 보드 코드 ===================== */
/* 글리치 없는 출력 초기화: 출력 래치(ODR)에 초기값을 먼저 넣고 나서 MODER 를 output 으로.
 * (CS 처럼 "부팅 중 한 순간도 0 이 되면 안 되는" 핀) */
int gpio_init_output(uint32_t port, unsigned pin, int initial, gpio_speed_t speed, gpio_otype_t otype) {
    if (port_index(port) < 0 || pin > 15u) return GPIO_EINVAL;
    gpio_clock_enable(port);                        /* 클럭 없이 BSRR 을 쓰면 무시된다 */
    gpio_write(port, pin, initial);
    return gpio_init(port, pin, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, speed, otype, 0u);
}

/* Nucleo-F4 풍 보드: LED PA5 (출력, 초기 0), 버튼 PC13 (입력, 풀업, 누르면 0),
 * USART2 TX PA2 / RX PA3 (AF7, RX 는 풀업), SPI 플래시 CS PB6 (출력, 초기 1, medium) */
int board_init(void) {
    int rc = 0;
    rc |= gpio_init_output(GPIOA, 5, 0, GPIO_SPEED_LOW, GPIO_OTYPE_PP);
    rc |= gpio_init(GPIOC, 13, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0u);
    rc |= gpio_init(GPIOA, 2, GPIO_MODE_AF, GPIO_PULL_NONE, GPIO_SPEED_FAST, GPIO_OTYPE_PP, 7u);
    rc |= gpio_init(GPIOA, 3, GPIO_MODE_AF, GPIO_PULL_UP, GPIO_SPEED_FAST, GPIO_OTYPE_PP, 7u);
    rc |= gpio_init_output(GPIOB, 6, 1, GPIO_SPEED_MEDIUM, GPIO_OTYPE_PP);
    return rc ? GPIO_EINVAL : 0;
}

/* 메인 루프 한 바퀴: 버튼이 "안 눌림 → 눌림" 으로 바뀔 때마다 LED 토글 (디바운스는 BM-M03) */
typedef struct { int prev_pressed; unsigned presses; } app_t;
void app_step(app_t *a) {
    int pressed = (gpio_read(GPIOC, 13) == 0);
    if (pressed && !a->prev_pressed) { gpio_toggle(GPIOA, 5); a->presses++; }
    a->prev_pressed = pressed;
}

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t t_rng = 0x1234567u;
static uint32_t xs32(void) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 17; t_rng ^= t_rng << 5; return t_rng; }
static uint32_t t_field(uint32_t reg, unsigned shift, uint32_t mask, uint32_t v) {   /* 하네스 자체 기대값 계산 */
    return (reg & ~(mask << shift)) | ((v & mask) << shift);
}
static bool log_is(int i, char op, uint32_t addr, uint32_t val) {
    return i < g_nlog && g_log[i].op == op && g_log[i].addr == addr && g_log[i].val == val;
}
static const char *pin_history(uint32_t port, unsigned pin) {   /* 이 핀의 레벨 변화: "01", "1", "Z" ... */
    static char buf[EDGE_CAP + 1];
    int n = 0, pi = (int)((port - GPIOA) / 0x400u);
    for (int i = 0; i < g_nedge; i++)
        if (g_edge[i].port == pi && g_edge[i].pin == pin) buf[n++] = "01ZX"[g_edge[i].level];
    buf[n] = '\0';
    return buf;
}
static const char *reg_name(uint32_t a) {
    static char buf[24];
    if (a == RCC_AHB1ENR) return "RCC_AHB1ENR";
    static const char *nm[10] = { "MODER", "OTYPER", "OSPEEDR", "PUPDR", "IDR", "ODR", "BSRR", "LCKR", "AFRL", "AFRH" };
    for (int i = 0; i < GPIO_NPORTS; i++)
        for (int k = 0; k < 10; k++)
            if (a == GPIO_PORTS[i] + GPIO_REG_OFF[k]) { snprintf(buf, sizeof buf, "GPIO%c_%s", 'A' + i, nm[k]); return buf; }
    snprintf(buf, sizeof buf, "?%08X", (unsigned)a);
    return buf;
}
static void dump_log(const char *title) {
    printf("  -- %s (%d accesses)\n", title, g_nlog);
    for (int i = 0; i < g_nlog; i++)
        printf("  %2d  %c  %-14s 0x%08X\n", i, g_log[i].op, reg_name(g_log[i].addr), (unsigned)g_log[i].val);
}
static bool is_gpio_addr(uint32_t a) { return a >= GPIOA && a < GPIOA + 0x400u * GPIO_NPORTS; }

/* ---- Part A: 기본 동작 ---- */
static void test_part_a(void) {
    model_reset();
    mock_log_clear();
    CHECK(gpio_init(GPIOA, 5, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    if (g_verbose) dump_log("gpio_init(GPIOA, 5, OUTPUT, NONE, LOW, PP, 0) from reset");
    CHECK(mock_peek(GPIOA + GPIO_MODER) == 0xA8000400u);          /* [11:10] = 01, PA13-15 그대로 */
    CHECK(mock_peek(GPIOA + GPIO_OSPEEDR) == 0x0C000000u && mock_peek(GPIOA + GPIO_PUPDR) == 0x64000000u);
    CHECK(mock_peek(GPIOA + GPIO_OTYPER) == 0u && mock_peek(GPIOA + GPIO_AFRL) == 0u);
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00100001u);                 /* GPIOAEN 켜고 bit20 보존 */
    CHECK(g_nlog > 0 && g_log[g_nlog - 1].op == 'W' && g_log[g_nlog - 1].addr == GPIOA + GPIO_MODER);

    mock_log_clear();
    CHECK(gpio_write(GPIOA, 5, 1) == 0);
    CHECK(g_nlog == 1 && log_is(0, 'W', GPIOA + GPIO_BSRR, 0x00000020u));   /* 읽기 없이 쓰기 1번 */
    CHECK(mock_peek(GPIOA + GPIO_ODR) == 0x20u && g_port[0].level[5] == LVL_1);
    mock_log_clear();
    CHECK(gpio_write(GPIOA, 5, 0) == 0);
    CHECK(g_nlog == 1 && log_is(0, 'W', GPIOA + GPIO_BSRR, 0x00200000u));
    CHECK(mock_peek(GPIOA + GPIO_ODR) == 0u && strcmp(pin_history(GPIOA, 5), "010") == 0);

    gpio_write(GPIOA, 5, 1);
    mock_log_clear();
    CHECK(gpio_read(GPIOA, 5) == 1);                              /* 출력 모드에서도 IDR 은 핀을 읽는다 */
    CHECK(g_nlog == 1 && g_log[0].op == 'R' && g_log[0].addr == GPIOA + GPIO_IDR);

    mock_log_clear();
    CHECK(gpio_toggle(GPIOA, 5) == 0);
    CHECK(g_nlog == 2 && log_is(0, 'R', GPIOA + GPIO_ODR, 0x20u) && log_is(1, 'W', GPIOA + GPIO_BSRR, 0x00200000u));
    CHECK(gpio_toggle(GPIOA, 5) == 0 && mock_peek(GPIOA + GPIO_ODR) == 0x20u);

    /* 입력: 외부 구동 > 풀 저항 > 떠 있음(잡음) */
    CHECK(gpio_init(GPIOA, 1, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    CHECK(gpio_read(GPIOA, 1) == 1);
    ext_set(GPIOA, 1, EXT_LOW);
    CHECK(gpio_read(GPIOA, 1) == 0);
    ext_set(GPIOA, 1, EXT_FLOAT);
    CHECK(gpio_init(GPIOA, 1, GPIO_MODE_INPUT, GPIO_PULL_DOWN, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    CHECK(gpio_read(GPIOA, 1) == 0);
    ext_set(GPIOA, 1, EXT_HIGH);
    CHECK(gpio_read(GPIOA, 1) == 1);
}

/* ---- Part B: 다른 핀 보존 (exhaustive), 경계, 잘못된 인자, 클럭 선행 ---- */
static void test_exhaustive(void) {
    long combos = 0, bad_rc = 0, bad_field = 0, bad_other = 0, bad_enr = 0, bad_order = 0;
    for (int pi = 0; pi < GPIO_NPORTS; pi++)
    for (unsigned pin = 0; pin < 16u; pin++)
    for (unsigned mode = 0; mode < 4u; mode++)
    for (unsigned pull = 0; pull < 3u; pull++)
    for (unsigned speed = 0; speed < 4u; speed++)
    for (unsigned ot = 0; ot < 2u; ot++)
    for (unsigned af = 0; af < (mode == GPIO_MODE_AF ? 16u : 1u); af++) {
        if (mode == GPIO_MODE_ANALOG && pull != GPIO_PULL_NONE) continue;
        uint32_t port = GPIO_PORTS[pi];
        model_reset();
        mock_poke(RCC_AHB1ENR, xs32());                   /* 대상 포트 클럭이 꺼져 있을 수도 있다 */
        uint32_t snap[GPIO_NPORTS][10], enr0 = mock_peek(RCC_AHB1ENR);
        for (int q = 0; q < GPIO_NPORTS; q++)
            for (int k = 0; k < 10; k++) {
                uint32_t v = xs32();
                if (GPIO_REG_OFF[k] == GPIO_OTYPER || GPIO_REG_OFF[k] == GPIO_ODR) v &= 0xFFFFu;
                if (GPIO_REG_OFF[k] == GPIO_BSRR || GPIO_REG_OFF[k] == GPIO_IDR) v = 0;
                mock_poke(GPIO_PORTS[q] + GPIO_REG_OFF[k], v);
                snap[q][k] = v;
            }
        mock_log_clear();
        int rc = gpio_init(port, pin, (gpio_mode_t)mode, (gpio_pull_t)pull, (gpio_speed_t)speed, (gpio_otype_t)ot, af);
        combos++;
        if (rc != 0) { bad_rc++; continue; }
        uint32_t exp[10];
        memcpy(exp, snap[pi], sizeof exp);
        exp[0] = t_field(exp[0], 2u * pin, 3u, mode);
        exp[1] = t_field(exp[1], pin, 1u, ot);
        exp[2] = t_field(exp[2], 2u * pin, 3u, speed);
        exp[3] = t_field(exp[3], 2u * pin, 3u, pull);
        if (mode == GPIO_MODE_AF) exp[pin < 8u ? 8 : 9] = t_field(exp[pin < 8u ? 8 : 9], 4u * (pin % 8u), 0xFu, af);
        for (int k = 0; k < 10; k++)
            if (mock_peek(port + GPIO_REG_OFF[k]) != exp[k]) bad_field++;
        for (int q = 0; q < GPIO_NPORTS; q++)
            for (int k = 0; k < 10; k++)
                if (q != pi && mock_peek(GPIO_PORTS[q] + GPIO_REG_OFF[k]) != snap[q][k]) bad_other++;
        if (mock_peek(RCC_AHB1ENR) != (enr0 | (1u << pi))) bad_enr++;
        /* 접근 순서: 포트 접근은 모두 ENR 쓰기 뒤, 마지막 쓰기는 MODER, IDR/ODR/BSRR/LCKR 쓰기 없음 */
        int first_enr_w = -1, last_w = -1;
        for (int i = 0; i < g_nlog; i++) {
            if (g_log[i].op == 'W' && g_log[i].addr == RCC_AHB1ENR && first_enr_w < 0) first_enr_w = i;
            if (is_gpio_addr(g_log[i].addr) && first_enr_w < 0) bad_order++;
            if (g_log[i].op == 'W') last_w = i;
            uint32_t off = g_log[i].addr - port;
            if (g_log[i].op == 'W' && (off == GPIO_IDR || off == GPIO_ODR || off == GPIO_BSRR || off == GPIO_LCKR)) bad_order++;
        }
        if (last_w < 0 || g_log[last_w].addr != port + GPIO_MODER) bad_order++;
    }
    printf("exhaustive gpio_init: %ld configs (4 ports x 16 pins x mode/pull/speed/otype/af)\n", combos);
    CHECK(combos == 28160);
    CHECK(bad_rc == 0);
    CHECK(bad_field == 0);              /* 대상 핀 필드만 바뀌고, 다른 핀·다른 레지스터는 그대로 */
    CHECK(bad_other == 0);              /* 다른 포트는 손대지 않음 */
    CHECK(bad_enr == 0);                /* 클럭 켬 + ENR 다른 비트 보존 */
    CHECK(bad_order == 0 && g_unexpected == 0);
}
static void test_part_b(void) {
    /* AFRL / AFRH 경계 (핀 7 | 8), 핀 0 / 15 */
    model_reset();
    CHECK(gpio_init(GPIOB, 7, GPIO_MODE_AF, GPIO_PULL_UP, GPIO_SPEED_FAST, GPIO_OTYPE_OD, 4) == 0);
    CHECK(mock_peek(GPIOB + GPIO_AFRL) == 0x40000000u && mock_peek(GPIOB + GPIO_AFRH) == 0u);
    CHECK(gpio_init(GPIOB, 8, GPIO_MODE_AF, GPIO_PULL_UP, GPIO_SPEED_FAST, GPIO_OTYPE_OD, 4) == 0);
    CHECK(mock_peek(GPIOB + GPIO_AFRL) == 0x40000000u && mock_peek(GPIOB + GPIO_AFRH) == 0x00000004u);
    CHECK(mock_peek(GPIOB + GPIO_OTYPER) == 0x0180u);
    CHECK(gpio_init(GPIOD, 15, GPIO_MODE_AF, GPIO_PULL_DOWN, GPIO_SPEED_HIGH, GPIO_OTYPE_OD, 15) == 0);
    CHECK(mock_peek(GPIOD + GPIO_MODER) == 0x80000000u && mock_peek(GPIOD + GPIO_AFRH) == 0xF0000000u);
    CHECK(mock_peek(GPIOD + GPIO_OTYPER) == 0x8000u && mock_peek(GPIOD + GPIO_PUPDR) == 0x80000000u);
    CHECK(gpio_init(GPIOD, 0, GPIO_MODE_ANALOG, GPIO_PULL_NONE, GPIO_SPEED_HIGH, GPIO_OTYPE_PP, 0) == 0);
    CHECK(mock_peek(GPIOD + GPIO_MODER) == 0x80000003u && mock_peek(GPIOD + GPIO_OSPEEDR) == 0xC0000003u);
    mock_log_clear();
    CHECK(gpio_write(GPIOD, 15, 0) == 0 && log_is(0, 'W', GPIOD + GPIO_BSRR, 0x80000000u));
    CHECK(gpio_write(GPIOD, 0, 1) == 0 && log_is(1, 'W', GPIOD + GPIO_BSRR, 0x00000001u));

    /* 잘못된 인자: GPIO_EINVAL, 레지스터 접근 0번 */
    model_reset();
    mock_log_clear();
    int bad = 0;
    bad += gpio_init(0x12345678u, 0, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 16, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 1, (gpio_mode_t)4, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 1, GPIO_MODE_INPUT, (gpio_pull_t)3, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 1, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, (gpio_speed_t)4, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 1, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, (gpio_otype_t)2, 0) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 1, GPIO_MODE_AF, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 16) == GPIO_EINVAL;
    bad += gpio_init(GPIOA, 0, GPIO_MODE_ANALOG, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_EINVAL;
    bad += gpio_write(GPIOA, 16, 1) == GPIO_EINVAL;
    bad += gpio_read(GPIOA, 99) == GPIO_EINVAL;
    bad += gpio_toggle(0x40021000u, 0) == GPIO_EINVAL;             /* GPIOE: 이 mock 에는 없는 포트 */
    bad += gpio_clock_enable(0u) == GPIO_EINVAL;
    CHECK(bad == 12);
    CHECK(g_nlog == 0 && g_unexpected == 0);

    /* 클럭 선행: 모델 자체 확인 → 드라이버가 켜는지 → 순서 */
    model_reset();
    REG_WRITE(GPIOC + GPIO_MODER, 0x04000000u);                    /* 클럭 없이 직접 쓰기 */
    CHECK(mock_peek(GPIOC + GPIO_MODER) == 0u);                    /* 무시됨 */
    ext_set(GPIOC, 13, EXT_HIGH);
    CHECK(gpio_read(GPIOC, 13) == 0);                              /* 클럭 없으면 IDR 도 0 */
    mock_poke(RCC_AHB1ENR, 0x00100002u);                           /* GPIOB 만 켜진 상태 */
    mock_log_clear();
    CHECK(gpio_init(GPIOC, 13, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    CHECK(log_is(0, 'R', RCC_AHB1ENR, 0x00100002u) && log_is(1, 'W', RCC_AHB1ENR, 0x00100006u));
    CHECK(log_is(2, 'R', RCC_AHB1ENR, 0x00100006u));               /* read-back */
    CHECK(mock_peek(GPIOC + GPIO_PUPDR) == 0x04000000u && gpio_read(GPIOC, 13) == 1);
    mock_poke(RCC_AHB1ENR, 0x00100002u);                           /* 누군가 GPIOC 클럭을 끔 */
    gpio_write(GPIOC, 13, 1);
    CHECK(mock_peek(GPIOC + GPIO_ODR) == 0u);                      /* 쓰기 사라짐 */

    /* 떠 있는 입력은 값이 흔들린다, analog 는 항상 0 */
    model_reset();
    CHECK(gpio_init(GPIOA, 1, GPIO_MODE_INPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    int ones = 0;
    for (int i = 0; i < 64; i++) ones += gpio_read(GPIOA, 1);
    printf("floating PA1, 64 reads -> %d ones\n", ones);
    CHECK(ones > 0 && ones < 64);
    CHECK(gpio_init(GPIOA, 0, GPIO_MODE_ANALOG, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    ext_set(GPIOA, 0, EXT_HIGH);
    CHECK(gpio_read(GPIOA, 0) == 0);

    /* open-drain: 핀 레벨(IDR) ≠ 출력 래치(ODR). toggle 은 ODR 기준이어야 한다 */
    model_reset();
    CHECK(gpio_init_output(GPIOB, 9, 1, GPIO_SPEED_LOW, GPIO_OTYPE_OD) == 0);
    CHECK(gpio_init(GPIOB, 9, GPIO_MODE_OUTPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_OD, 0) == 0);
    CHECK(gpio_read(GPIOB, 9) == 1);                               /* 놓아 줌 + 풀업 */
    ext_set(GPIOB, 9, EXT_LOW);                                    /* 다른 장치가 라인을 누름 (I2C 처럼) */
    CHECK(gpio_read(GPIOB, 9) == 0 && (mock_peek(GPIOB + GPIO_ODR) >> 9 & 1u) == 1u);
    CHECK(gpio_toggle(GPIOB, 9) == 0 && (mock_peek(GPIOB + GPIO_ODR) >> 9 & 1u) == 0u);
    CHECK(g_contention == 0);                                      /* OD 의 0 과 외부 0 은 싸우지 않는다 */
}

/* ---- Part C: 보드 초기화, 글리치, 버튼 → LED ---- */
static void naive_cs_init(void) {                                  /* 흔한 순서: 모드 먼저, 값은 나중 */
    gpio_init(GPIOB, 6, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_MEDIUM, GPIO_OTYPE_PP, 0);
    gpio_write(GPIOB, 6, 1);
}
static void test_part_c(void) {
    model_reset();
    CHECK(board_init() == 0);
    if (g_verbose) {
        printf("  board_init result:\n");
        for (int q = 0; q < 3; q++)
            printf("  GPIO%c MODER=%08X OTYPER=%04X OSPEEDR=%08X PUPDR=%08X ODR=%04X AFRL=%08X AFRH=%08X\n", 'A' + q,
                   (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_MODER), (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_OTYPER),
                   (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_OSPEEDR), (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_PUPDR),
                   (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_ODR), (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_AFRL),
                   (unsigned)mock_peek(GPIO_PORTS[q] + GPIO_AFRH));
        printf("  RCC_AHB1ENR=%08X\n", (unsigned)mock_peek(RCC_AHB1ENR));
    }
    CHECK(mock_peek(GPIOA + GPIO_MODER) == 0xA80004A0u && mock_peek(GPIOA + GPIO_AFRL) == 0x00007700u);
    CHECK(mock_peek(GPIOA + GPIO_OSPEEDR) == 0x0C0000A0u && mock_peek(GPIOA + GPIO_PUPDR) == 0x64000040u);
    CHECK((mock_peek(GPIOA + GPIO_MODER) >> 26) == 0x2Au);           /* PA13/14/15 = AF: SWD 살아 있음 */
    CHECK(mock_peek(GPIOB + GPIO_MODER) == 0x00001280u && mock_peek(GPIOB + GPIO_ODR) == 0x0040u);
    CHECK(mock_peek(GPIOC + GPIO_MODER) == 0u && mock_peek(GPIOC + GPIO_PUPDR) == 0x04000000u);
    CHECK(mock_peek(RCC_AHB1ENR) == 0x00100007u && g_unexpected == 0);
    CHECK(strcmp(pin_history(GPIOB, 6), "1") == 0);                /* CS: Z → 1, 0 을 한 번도 안 거침 */
    CHECK(strcmp(pin_history(GPIOA, 5), "0") == 0);

    model_reset();
    naive_cs_init();
    printf("naive CS init, PB6 history: \"%s\"\n", pin_history(GPIOB, 6));
    CHECK(strcmp(pin_history(GPIOB, 6), "01") == 0);              /* 0 글리치 → 플래시가 잠깐 선택됨 */

    /* 버튼 → LED */
    model_reset();
    board_init();
    g_nedge = 0;
    app_t app = { 0, 0 };
    static const ext_t seq[] = { EXT_FLOAT, EXT_FLOAT, EXT_LOW, EXT_LOW, EXT_FLOAT, EXT_LOW, EXT_LOW, EXT_LOW, EXT_FLOAT, EXT_FLOAT };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) { ext_set(GPIOC, 13, seq[i]); app_step(&app); }
    CHECK(app.presses == 2u);
    CHECK(strcmp(pin_history(GPIOA, 5), "10") == 0);

    /* 잘못된 핀을 출력으로: 눌린 버튼(외부 0)과 push-pull 1 이 싸운다 → 모델이 contention 으로 잡는다 */
    model_reset();
    ext_set(GPIOC, 13, EXT_LOW);
    gpio_init_output(GPIOC, 13, 1, GPIO_SPEED_LOW, GPIO_OTYPE_PP);
    CHECK(g_contention == 1 && strcmp(pin_history(GPIOC, 13), "0X") == 0);
}

int main(int argc, char **argv) {
    g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    test_part_a();
    test_exhaustive();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
