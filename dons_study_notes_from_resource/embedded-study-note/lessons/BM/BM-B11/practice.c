/* BM-B11 추가 연습 — 한 번에 검증
 *
 * 연습 1  gpio_write_masked: 여러 핀을 BSRR 쓰기 한 번으로 (병렬 버스·7세그먼트)
 * 연습 2  gpio_get_config: 레지스터에서 핀 설정 역으로 읽기 (리셋 값 PA13/PA14 = SWD 해석 포함)
 * 연습 3  gpio_lock: LCKR 잠금 키 시퀀스 (mock 에 잠금 모델 추가), 잠긴 핀에 gpio_init → GPIO_ELOCKED
 * 연습 4  board_apply: 핀 설정 표를 "전부 검사한 뒤에만" 적용 (잘못된 항목·중복 핀 → 접근 0번)
 *
 * 본문 solution.c 의 mock·드라이버를 그대로 담고 (self-contained), LCKR 모델과 잠금 검사만 추가했다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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

typedef struct { int idx; uint32_t base; uint8_t ext[16]; uint8_t level[16];
                 bool locked; uint8_t lock_step; uint32_t lock_bits; } gpio_model_t;   /* + LCKR 모델 (연습 3) */
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
/* 연습 3 — LCKR 잠금 모델 (RM0090 기준 대표 동작 — RM 확인 권장):
 * [16]=LCKK 를 1 → 0 → 1 로 세 번 쓰고 그동안 [15:0] 이 같으면 잠김. 잠긴 핀의 설정 필드
 * (MODER/OTYPER/OSPEEDR/PUPDR/AFRL/AFRH) 는 다음 리셋까지 쓰기 무시. ODR/BSRR 은 잠기지 않는다. */
static uint32_t lckr_write(gpio_model_t *p, uint32_t old, uint32_t val) {
    if (p->locked) return old;                                      /* 잠긴 뒤 LCKR 도 고정 */
    uint32_t key = (val >> 16) & 1u, bits = val & 0xFFFFu;
    if (p->lock_step != 0 && bits != p->lock_bits) p->lock_step = 0; /* [15:0] 이 바뀌면 처음부터 */
    if (key == (p->lock_step == 1 ? 0u : 1u)) p->lock_step++;       /* 1 → 0 → 1 */
    else p->lock_step = key ? 1u : 0u;
    p->lock_bits = bits;
    if (p->lock_step == 3) { p->locked = true; return (1u << 16) | bits; }
    return bits;
}
static uint32_t lock_field_mask(const gpio_model_t *p, uint32_t off) {
    uint32_t m = 0;
    if (!p->locked) return 0;
    for (unsigned pin = 0; pin < 16u; pin++) {
        if (!((p->lock_bits >> pin) & 1u)) continue;
        if (off == GPIO_MODER || off == GPIO_OSPEEDR || off == GPIO_PUPDR) m |= 3u << (2u * pin);
        if (off == GPIO_OTYPER) m |= 1u << pin;
        if (off == GPIO_AFRL && pin < 8u) m |= 0xFu << (4u * pin);
        if (off == GPIO_AFRH && pin >= 8u) m |= 0xFu << (4u * (pin - 8u));
    }
    return m;
}
static uint32_t gpio_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    gpio_model_t *p = ctx;
    if (!model_clk_on(p->idx)) return old;          /* 클럭 꺼짐: 쓰기 무시 (BM-B03 규칙 축약) */
    uint32_t off = addr - p->base, nv = val;
    if (off == GPIO_IDR) return old;                /* 읽기 전용 */
    if (off == GPIO_LCKR) return lckr_write(p, old, val);
    if (off == GPIO_ODR || off == GPIO_OTYPER) nv = val & 0xFFFFu;
    if (off == GPIO_BSRR) {                         /* set 과 reset 이 같이 오면 set 이 이긴다 */
        uint32_t odr = mock_peek(p->base + GPIO_ODR);
        mock_poke(p->base + GPIO_ODR, ((odr & ~(val >> 16)) | (val & 0xFFFFu)) & 0xFFFFu);
        nv = 0;
    }
    uint32_t lm = lock_field_mask(p, off);
    nv = (nv & ~lm) | (old & lm);                   /* 잠긴 핀 필드는 옛 값 유지 */
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

/* ===================== GPIO 드라이버 (본문 정답 + 잠금 검사) ===================== */
#define GPIO_EINVAL (-1)
#define GPIO_ELOCKED (-2)
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
    uint32_t lck = REG_READ(port + GPIO_LCKR);     /* 연습 3: 잠긴 핀이면 거부 */
    if (((lck >> 16) & 1u) && ((lck >> pin) & 1u)) return GPIO_ELOCKED;
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

/* ===================== 연습 1: 여러 핀을 한 번에 ===================== */
/* mask 의 핀들을 value 의 해당 비트로. BSRR 쓰기 한 번 — 모든 핀이 같은 순간에 바뀐다 */
int gpio_write_masked(uint32_t port, uint16_t mask, uint16_t value) {
    if (port_index(port) < 0) return GPIO_EINVAL;
    if (mask == 0u) return 0;                                       /* 바꿀 것 없음 → 접근도 없음 */
    uint32_t set = (uint32_t)(value & mask), rst = (uint32_t)(~value & mask) & 0xFFFFu;
    REG_WRITE(port + GPIO_BSRR, (rst << 16) | set);
    return 0;
}

/* ===================== 연습 2: 설정 역으로 읽기 ===================== */
typedef struct { gpio_mode_t mode; gpio_pull_t pull; gpio_speed_t speed; gpio_otype_t otype; unsigned af; } gpio_cfg_t;
int gpio_get_config(uint32_t port, unsigned pin, gpio_cfg_t *out) {
    if (port_index(port) < 0 || pin > 15u || !out) return GPIO_EINVAL;
    unsigned s2 = 2u * pin;
    out->mode  = (gpio_mode_t)((REG_READ(port + GPIO_MODER) >> s2) & 3u);
    out->otype = (gpio_otype_t)((REG_READ(port + GPIO_OTYPER) >> pin) & 1u);
    out->speed = (gpio_speed_t)((REG_READ(port + GPIO_OSPEEDR) >> s2) & 3u);
    out->pull  = (gpio_pull_t)((REG_READ(port + GPIO_PUPDR) >> s2) & 3u);    /* 3 = reserved 도 그대로 보고 */
    out->af    = (REG_READ(port + (pin < 8u ? GPIO_AFRL : GPIO_AFRH)) >> (4u * (pin & 7u))) & 0xFu;
    return 0;
}

/* ===================== 연습 3: 설정 잠금 ===================== */
/* 키 시퀀스: W(LCKK|mask) → W(mask) → W(LCKK|mask) → R → R(LCKK 확인).
 * 성공(LCKK=1 이고 mask 가 모두 잠김) 0, 아니면 GPIO_ELOCKED (이미 다른 마스크로 잠겨 있음 등) */
int gpio_lock(uint32_t port, uint16_t mask) {
    if (port_index(port) < 0) return GPIO_EINVAL;
    uint32_t k = (1u << 16) | mask;
    REG_WRITE(port + GPIO_LCKR, k);
    REG_WRITE(port + GPIO_LCKR, mask);
    REG_WRITE(port + GPIO_LCKR, k);
    (void)REG_READ(port + GPIO_LCKR);
    uint32_t r = REG_READ(port + GPIO_LCKR);
    return (((r >> 16) & 1u) && (r & mask) == mask) ? 0 : GPIO_ELOCKED;
}

/* ===================== 연습 4: 표로 보드 초기화 ===================== */
typedef struct { uint32_t port; unsigned pin; gpio_mode_t mode; gpio_pull_t pull; gpio_speed_t speed;
                 gpio_otype_t otype; unsigned af; int initial; } pin_cfg_t;
static bool pin_cfg_valid(const pin_cfg_t *c) {
    return port_index(c->port) >= 0 && c->pin <= 15u && (unsigned)c->mode <= 3u && (unsigned)c->pull <= 2u &&
           (unsigned)c->speed <= 3u && (unsigned)c->otype <= 1u && c->af <= 15u &&
           !(c->mode == GPIO_MODE_ANALOG && c->pull != GPIO_PULL_NONE);
}
/* 1단계: 전부 검사 (범위 + 같은 핀 두 번) — 하나라도 틀리면 *bad = 그 인덱스, 레지스터 접근 0번.
 * 2단계: 적용. 출력 핀은 BSRR 로 초기값 먼저 (글리치 없음) */
int board_apply(const pin_cfg_t *t, int n, int *bad) {
    uint16_t used[GPIO_NPORTS] = { 0 };
    for (int i = 0; i < n; i++) {
        if (!pin_cfg_valid(&t[i])) { *bad = i; return GPIO_EINVAL; }
        int pi = port_index(t[i].port);
        if ((used[pi] >> t[i].pin) & 1u) { *bad = i; return GPIO_EINVAL; }
        used[pi] = (uint16_t)(used[pi] | (1u << t[i].pin));
    }
    for (int i = 0; i < n; i++) {
        const pin_cfg_t *c = &t[i];
        if (c->mode == GPIO_MODE_OUTPUT) { gpio_clock_enable(c->port); gpio_write(c->port, c->pin, c->initial); }
        int rc = gpio_init(c->port, c->pin, c->mode, c->pull, c->speed, c->otype, c->af);
        if (rc) { *bad = i; return rc; }
    }
    *bad = -1;
    return 0;
}

/* ===================== 테스트 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t t_rng = 0xBEEF1234u;
static uint32_t xs32(void) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 17; t_rng ^= t_rng << 5; return t_rng; }
static const char *pin_history(uint32_t port, unsigned pin) {
    static char buf[EDGE_CAP + 1];
    int n = 0, pi = (int)((port - GPIOA) / 0x400u);
    for (int i = 0; i < g_nedge; i++)
        if (g_edge[i].port == pi && g_edge[i].pin == pin) buf[n++] = "01ZX"[g_edge[i].level];
    buf[n] = '\0';
    return buf;
}

static void ex1_masked(void) {
    model_reset();
    gpio_clock_enable(GPIOA);
    mock_poke(GPIOA + GPIO_ODR, 0x00F0u);
    mock_log_clear();
    CHECK(gpio_write_masked(GPIOA, 0x00F0u, 0x0050u) == 0);
    CHECK(g_nlog == 1 && g_log[0].op == 'W' && g_log[0].addr == GPIOA + GPIO_BSRR && g_log[0].val == 0x00A00050u);
    CHECK(mock_peek(GPIOA + GPIO_ODR) == 0x0050u);
    long bad = 0;
    for (int i = 0; i < 2000; i++) {
        uint16_t odr = (uint16_t)xs32(), m = (uint16_t)xs32(), v = (uint16_t)xs32();
        mock_poke(GPIOA + GPIO_ODR, odr);
        gpio_write_masked(GPIOA, m, v);
        if (mock_peek(GPIOA + GPIO_ODR) != (uint32_t)((odr & ~m) | (v & m))) bad++;
    }
    CHECK(bad == 0);
    mock_log_clear();
    CHECK(gpio_write_masked(GPIOA, 0u, 0xFFFFu) == 0 && g_nlog == 0);
    CHECK(gpio_write_masked(0x1u, 1u, 1u) == GPIO_EINVAL && g_nlog == 0);
}

static void ex2_get_config(void) {
    long n = 0, bad = 0;
    for (unsigned pin = 0; pin < 16u; pin++)
    for (unsigned mode = 0; mode < 4u; mode++)
    for (unsigned pull = 0; pull < 3u; pull++)
    for (unsigned speed = 0; speed < 4u; speed++)
    for (unsigned ot = 0; ot < 2u; ot++)
    for (unsigned af = 0; af < (mode == GPIO_MODE_AF ? 16u : 1u); af++) {
        if (mode == GPIO_MODE_ANALOG && pull) continue;
        model_reset();
        gpio_init(GPIOC, pin, (gpio_mode_t)mode, (gpio_pull_t)pull, (gpio_speed_t)speed, (gpio_otype_t)ot, af);
        gpio_cfg_t c;
        n++;
        if (gpio_get_config(GPIOC, pin, &c) != 0 || c.mode != (gpio_mode_t)mode || c.pull != (gpio_pull_t)pull ||
            c.speed != (gpio_speed_t)speed || c.otype != (gpio_otype_t)ot || (mode == GPIO_MODE_AF && c.af != af)) bad++;
    }
    CHECK(n == 7040 && bad == 0);
    /* 리셋 값 해석: PA13 = AF0(SWDIO) 풀업 very-high, PA14 = AF0(SWCLK) 풀다운 low */
    model_reset();
    gpio_clock_enable(GPIOA);
    gpio_cfg_t a13, a14;
    CHECK(gpio_get_config(GPIOA, 13, &a13) == 0 && a13.mode == GPIO_MODE_AF && a13.pull == GPIO_PULL_UP &&
          a13.speed == GPIO_SPEED_HIGH && a13.af == 0u);
    CHECK(gpio_get_config(GPIOA, 14, &a14) == 0 && a14.mode == GPIO_MODE_AF && a14.pull == GPIO_PULL_DOWN &&
          a14.speed == GPIO_SPEED_LOW && a14.af == 0u);
    CHECK(gpio_get_config(GPIOA, 16, &a14) == GPIO_EINVAL && gpio_get_config(GPIOA, 0, NULL) == GPIO_EINVAL);
}

static void ex3_lock(void) {
    model_reset();
    CHECK(gpio_init(GPIOA, 5, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    mock_log_clear();
    CHECK(gpio_lock(GPIOA, 1u << 5) == 0);
    CHECK(g_nlog == 5 && g_log[0].val == 0x00010020u && g_log[1].val == 0x00000020u && g_log[2].val == 0x00010020u);
    CHECK(mock_peek(GPIOA + GPIO_LCKR) == 0x00010020u);
    CHECK(gpio_init(GPIOA, 5, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == GPIO_ELOCKED);
    REG_WRITE(GPIOA + GPIO_MODER, 0u);                              /* 우회 시도: 잠긴 필드는 그대로 */
    CHECK(((mock_peek(GPIOA + GPIO_MODER) >> 10) & 3u) == 1u && mock_peek(GPIOA + GPIO_MODER) == 0x00000400u);
    CHECK(gpio_init(GPIOA, 6, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    CHECK(gpio_write(GPIOA, 5, 1) == 0 && gpio_read(GPIOA, 5) == 1); /* 데이터는 잠기지 않는다 */
    CHECK(gpio_lock(GPIOA, 1u << 6) == GPIO_ELOCKED);               /* 이미 잠김: 다음 리셋까지 못 바꿈 */
    /* 틀린 시퀀스 (1,1,1) / 중간에 마스크가 바뀜 → 안 잠긴다 */
    gpio_clock_enable(GPIOB);
    REG_WRITE(GPIOB + GPIO_LCKR, 0x10001u); REG_WRITE(GPIOB + GPIO_LCKR, 0x10001u); REG_WRITE(GPIOB + GPIO_LCKR, 0x10001u);
    CHECK(((REG_READ(GPIOB + GPIO_LCKR) >> 16) & 1u) == 0u);
    REG_WRITE(GPIOB + GPIO_LCKR, 0x10001u); REG_WRITE(GPIOB + GPIO_LCKR, 0x00003u); REG_WRITE(GPIOB + GPIO_LCKR, 0x10003u);
    CHECK(((REG_READ(GPIOB + GPIO_LCKR) >> 16) & 1u) == 0u);
    CHECK(gpio_init(GPIOB, 0, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
    model_reset();                                                  /* 리셋하면 풀린다 */
    CHECK(gpio_init(GPIOA, 5, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_SPEED_LOW, GPIO_OTYPE_PP, 0) == 0);
}

static void ex4_board_table(void) {
    static const pin_cfg_t board[] = {
        { GPIOA, 5,  GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_LOW,    GPIO_OTYPE_PP, 0, 0 },   /* LED */
        { GPIOC, 13, GPIO_MODE_INPUT,  GPIO_PULL_UP,   GPIO_SPEED_LOW,    GPIO_OTYPE_PP, 0, 0 },   /* 버튼 */
        { GPIOA, 2,  GPIO_MODE_AF,     GPIO_PULL_NONE, GPIO_SPEED_FAST,   GPIO_OTYPE_PP, 7, 0 },   /* USART2_TX */
        { GPIOA, 3,  GPIO_MODE_AF,     GPIO_PULL_UP,   GPIO_SPEED_FAST,   GPIO_OTYPE_PP, 7, 0 },   /* USART2_RX */
        { GPIOB, 6,  GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_SPEED_MEDIUM, GPIO_OTYPE_PP, 0, 1 },   /* 플래시 CS */
    };
    int bad = 99;
    model_reset();
    CHECK(board_apply(board, 5, &bad) == 0 && bad == -1);
    CHECK(mock_peek(GPIOA + GPIO_MODER) == 0xA80004A0u && mock_peek(GPIOA + GPIO_AFRL) == 0x00007700u);
    CHECK(mock_peek(GPIOA + GPIO_PUPDR) == 0x64000040u && mock_peek(GPIOB + GPIO_MODER) == 0x00001280u);
    CHECK(mock_peek(GPIOC + GPIO_PUPDR) == 0x04000000u && mock_peek(RCC_AHB1ENR) == 0x00100007u);
    CHECK(strcmp(pin_history(GPIOB, 6), "1") == 0);                 /* 글리치 없음 */
    ext_set(GPIOC, 13, EXT_LOW);                                     /* 버튼 누름 */
    CHECK(gpio_read(GPIOC, 13) == 0 && g_unexpected == 0);

    pin_cfg_t t2[5];
    memcpy(t2, board, sizeof t2);
    t2[3].pin = 16;                                                  /* 오타 */
    model_reset();
    mock_log_clear();
    CHECK(board_apply(t2, 5, &bad) == GPIO_EINVAL && bad == 3 && g_nlog == 0);
    memcpy(t2, board, sizeof t2);
    t2[4].port = GPIOA; t2[4].pin = 5;                               /* LED 핀에 CS 를 또 배정 */
    model_reset();
    mock_log_clear();
    CHECK(board_apply(t2, 5, &bad) == GPIO_EINVAL && bad == 4 && g_nlog == 0);
}

int main(void) {
    ex1_masked();
    ex2_get_config();
    ex3_lock();
    ex4_board_table();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
