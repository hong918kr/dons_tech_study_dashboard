/* BM-B13 SysTick: reload 계산, ms tick, wrap-safe delay
 *
 * Part A — reload = core_hz / tick_hz - 1 (24비트 한계, 반올림, 오차 ppm), SysTick 하드웨어 모델
 *          (CVR 다운카운트 → 0 에서 RVR 재장전, COUNTFLAG clear-on-read, TICKINT → SysTick_Handler),
 *          systick_init(), volatile g_ticks, millis().
 * Part B — wrap-safe 비교: time_after_eq(now, deadline) = (int32_t)(now - deadline) >= 0,
 *          timeout_expired(start, ms). g_ticks 를 0xFFFFFFF0 에서 시작해 wrap 을 지나게 하고,
 *          naive `now >= deadline` 이 틀리는 것과 2^31 한계를 테스트한다.
 * Part C — delay_ms(): tick 위상(phase) 때문에 delay_ms(1) 이 0.x ms 만에 끝나는 off-by-one 과 +1 규칙.
 *
 * 레지스터는 REG_READ/REG_WRITE 로만 만진다 (BM-B02 mock 축약판). SysTick 주소·비트는 ARMv7-M ARM B3.3
 * (Cortex-M 공통, 칩 벤더 무관). CLKSOURCE=0 의 "외부 기준 클럭" = HCLK/8 은 STM32F4 기준 모델 (RM0090 확인 권장).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   (-v: 위상 sweep 표 출력)
 */
#define UNIT_TEST 1
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= MMIO 접근 경계 (BM-B02 mock 축약판) ======================= */
#ifdef UNIT_TEST                                /* 호스트 빌드: 훅 */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
static void cpu_spin(void);                     /* 폴링 루프 한 바퀴 = SPIN_CYCLES 코어 클럭 */
#else                                           /* 타깃 빌드: 진짜 MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#define cpu_spin()            __asm volatile("nop")
#endif

/* SysTick (ARMv7-M B3.3, System Control Space) */
#define SYST_CSR            0xE000E010u         /* Control and Status */
#define SYST_RVR            0xE000E014u         /* Reload Value, [23:0] */
#define SYST_CVR            0xE000E018u         /* Current Value, 쓰기 = 0 으로 클리어 */
#define SYST_CSR_ENABLE     (1u << 0)
#define SYST_CSR_TICKINT    (1u << 1)
#define SYST_CSR_CLKSOURCE  (1u << 2)           /* 1 = 프로세서 클럭, 0 = 외부 기준 클럭 (구현 정의) */
#define SYST_CSR_COUNTFLAG  (1u << 16)          /* rc_r: 읽으면 0 이 된다 */
#define SYST_RVR_MAX        0x00FFFFFFu

/* ======================= mock core: 레지스터 표 + 훅 + 접근 로그 ======================= */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
#define MOCK_MAX_REGS 8
#define MOCK_LOG_CAP  32
typedef struct { uint32_t addr, val; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
static mock_reg_t g_regs[MOCK_MAX_REGS];
static int        g_nregs;
static mock_log_t g_log[MOCK_LOG_CAP];
static int        g_nlog;                       /* 앞 MOCK_LOG_CAP 개만 기록 */
static int        g_unexpected;                 /* 모르는 주소 접근 = 테스트 실패 */

static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nregs; i++) if (g_regs[i].addr == addr) return &g_regs[i];
    return NULL;
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nregs >= MOCK_MAX_REGS) return -1;
    g_regs[g_nregs] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return g_nregs++;
}
static void mock_log(char op, uint32_t addr, uint32_t val) {
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
}
static uint32_t mock_peek(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? r->val : 0u; }
static void mock_poke(uint32_t addr, uint32_t v) { mock_reg_t *r = mock_find(addr); if (r) r->val = v; }
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_unexpected++; mock_log('?', addr, 0u); return 0u; }
    uint32_t v = r->r ? r->r(addr, r->val, r->ctx) : r->val;
    mock_log('R', addr, v);
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (!r) { g_unexpected++; mock_log('?', addr, val); return; }
    r->val = r->w ? r->w(addr, r->val, val, r->ctx) : val;
    mock_log('W', addr, val);
}

/* ======================= SysTick 하드웨어 모델 ======================= */
static uint64_t g_cycles;                       /* mock_reset 이후 코어 클럭 수 (넘치지 않는 오라클 시계) */
static unsigned g_ext_div;                      /* CLKSOURCE=0: 코어 8 클럭마다 1 번 (STM32F4 HCLK/8 모델) */
static unsigned long g_spins;                   /* 폭주 방지: mock_reset 이후 폴링 루프 바퀴 수 */
static jmp_buf  g_spin_jmp;                     /* 하네스가 걸어 둔 탈출 지점 (tick 이 안 세면 여기로) */
static bool     g_spin_armed;
#define SPIN_CYCLES 4u                          /* 폴링 루프 한 바퀴 비용 (ldr + cmp + 분기 대략) */
#define SPIN_LIMIT  200000ul                   /* 4 클럭 × 20만 = 800 ms 모델 시간 */

static uint32_t csr_on_read(uint32_t addr, uint32_t stored, void *ctx) {
    (void)addr; (void)ctx;
    mock_poke(SYST_CSR, stored & ~SYST_CSR_COUNTFLAG);    /* COUNTFLAG: 읽으면 클리어 (rc_r) */
    return stored;
}
static uint32_t csr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)ctx;
    return (old & SYST_CSR_COUNTFLAG) | (val & 0x7u);      /* COUNTFLAG 는 읽기 전용, 나머지 비트 RAZ/WI */
}
static uint32_t rvr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    return val & SYST_RVR_MAX;                             /* [31:24] 는 버려진다 */
}
static uint32_t cvr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    (void)addr; (void)old; (void)val; (void)ctx;
    mock_poke(SYST_CSR, mock_peek(SYST_CSR) & ~SYST_CSR_COUNTFLAG);   /* 아무 값이나 쓰면: CVR=0, COUNTFLAG=0 */
    return 0u;                                             /* 예외는 일으키지 않는다 */
}

static void mock_reset(void) {
    memset(g_regs, 0, sizeof g_regs); g_nregs = 0;
    g_nlog = 0; g_unexpected = 0; g_cycles = 0; g_ext_div = 0; g_spins = 0;
    mock_add_reg(SYST_CSR, 0x00000000u, csr_on_read, csr_on_write, NULL);
    mock_add_reg(SYST_RVR, 0x00ABCDEFu, NULL, rvr_on_write, NULL);   /* 리셋값 UNKNOWN → 쓰레기로 모델 */
    mock_add_reg(SYST_CVR, 0x00123456u, NULL, cvr_on_write, NULL);   /* 리셋값 UNKNOWN */
}

volatile uint32_t g_ticks;                      /* ISR 이 쓰고 main 이 읽는다 → volatile */
void SysTick_Handler(void);

/* ======================= Part A: reload 계산 ======================= */
typedef enum { ST_OK = 0, ST_INEXACT = 1, ST_ERR_ARG = -1, ST_ERR_RANGE = -2 } st_status_t;

/* TODO A1: 주기 N = core_hz / tick_hz 클럭 (가장 가까운 정수로 반올림), reload = N - 1.
 * - reload == NULL, core_hz == 0, tick_hz == 0 → ST_ERR_ARG
 * - reload 가 1..0x00FFFFFF 밖 (N < 2 또는 N > 2^24) → ST_ERR_RANGE (*reload 는 건드리지 않음)
 * - 나누어떨어지면 ST_OK, 아니면 ST_INEXACT. err_ppm (NULL 가능) = 실제 tick 주파수의 상대 오차
 *   (core/N - tick) / tick × 1e6 을 정수로 반올림 (0.5 는 0 에서 먼 쪽). 정수 연산만. */
static st_status_t systick_reload(uint32_t core_hz, uint32_t tick_hz, uint32_t *reload, int32_t *err_ppm) {
    if (!reload || core_hz == 0u || tick_hz == 0u) return ST_ERR_ARG;
    uint64_t n = ((uint64_t)core_hz + tick_hz / 2u) / tick_hz;          /* 가장 가까운 N */
    if (n < 2u || n - 1u > SYST_RVR_MAX) return ST_ERR_RANGE;
    *reload = (uint32_t)(n - 1u);
    int64_t num = (int64_t)core_hz - (int64_t)(n * tick_hz);           /* core - N·tick */
    int64_t den = (int64_t)(n * tick_hz);
    int64_t ppm = (num >= 0 ? num * 1000000 + den / 2 : num * 1000000 - den / 2) / den;
    if (err_ppm) *err_ppm = (int32_t)ppm;
    return num == 0 ? ST_OK : ST_INEXACT;
}

/* TODO A2: SysTick 카운터 클럭 한 번 (ARMv7-M B3.3.1).
 * ENABLE=0 이면 아무 일 없음. CVR == 0 이면 RVR 값을 다시 싣는다 (이벤트 없음).
 * 아니면 1 감소; 그 결과 1 → 0 이 되었으면 COUNTFLAG=1, TICKINT=1 이면 SysTick_Handler() 호출.
 * 레지스터 상태는 mock_peek/mock_poke 로 (CPU 접근이 아니므로 로그에 남기지 않는다). */
static void systick_hw_clock(void) {
    uint32_t csr = mock_peek(SYST_CSR);
    if (!(csr & SYST_CSR_ENABLE)) return;
    uint32_t cvr = mock_peek(SYST_CVR);
    if (cvr == 0u) { mock_poke(SYST_CVR, mock_peek(SYST_RVR)); return; }   /* wrap: 재장전 */
    mock_poke(SYST_CVR, --cvr);
    if (cvr == 0u) {                                                       /* 1 → 0 전이 */
        mock_poke(SYST_CSR, csr | SYST_CSR_COUNTFLAG);
        if (csr & SYST_CSR_TICKINT) SysTick_Handler();
    }
}

static void mock_cycles(uint64_t n) {           /* 주어진 코드: 코어 클럭 n 개를 흘린다 */
    while (n--) {
        g_cycles++;
        if (mock_peek(SYST_CSR) & SYST_CSR_CLKSOURCE) systick_hw_clock();
        else if (++g_ext_div == 8u) { g_ext_div = 0; systick_hw_clock(); }
    }
}
#ifdef UNIT_TEST
static void cpu_spin(void) {
    mock_cycles(SPIN_CYCLES);
    if (++g_spins <= SPIN_LIMIT) return;
    if (g_spin_armed) longjmp(g_spin_jmp, 1);   /* 폴링이 안 끝난다 (tick 이 안 세나?) → 하네스로 탈출 */
    printf("MOCK: 폴링 루프 폭주 — 중단\n"); exit(2);
}
#endif

/* TODO A3: SysTick 을 tick_hz 로 설정. 계산이 실패하면 레지스터를 하나도 건드리지 않고 그 상태를 반환.
 * 순서: 끄기(CSR=0) → RVR=reload → CVR 클리어(아무 값 쓰기) → CSR = CLKSOURCE|TICKINT|ENABLE.
 * 반환: systick_reload 의 결과 (ST_OK / ST_INEXACT). */
static st_status_t systick_init(uint32_t core_hz, uint32_t tick_hz) {
    uint32_t reload = 0u;
    st_status_t st = systick_reload(core_hz, tick_hz, &reload, NULL);
    if (st < 0) return st;
    REG_WRITE(SYST_CSR, 0u);
    REG_WRITE(SYST_RVR, reload);
    REG_WRITE(SYST_CVR, 0u);
    REG_WRITE(SYST_CSR, SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE);
    return st;
}

/* TODO A4: tick ISR 과 millis(). (1 kHz tick 이면 tick = ms) */
void SysTick_Handler(void) {
    g_ticks++;
}
static uint32_t millis(void) {
    return g_ticks;
}

/* ======================= Part B: wrap-safe 시간 비교 ======================= */
/* 주어진 코드: 흔한 버그 버전 (비교용) */
static bool time_after_eq_naive(uint32_t now, uint32_t deadline) { return now >= deadline; }

/* TODO B1: now 가 deadline 에 도달했거나 지났나. wrap 에 안전해야 한다: (int32_t)(now - deadline) >= 0.
 * 두 시각의 실제 거리가 2^31 tick 미만일 때만 맞다 (그 이상은 방향을 알 수 없다). */
static bool time_after_eq(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
/* TODO B2: start 이후 ms 가 지났나 (경과 시간 형식: millis() - start >= ms). */
static bool timeout_expired(uint32_t start, uint32_t ms) {
    return (uint32_t)(millis() - start) >= ms;
}

/* ======================= Part C: delay_ms 와 tick 위상 ======================= */
/* 주어진 코드: off-by-one 버전 (비교용) — "ms 개의 tick 경계를 본다" */
static void delay_ms_naive(uint32_t ms) {
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < ms) cpu_spin();
}
/* TODO C1: 최소 ms 를 보장하려면 몇 tick 경계를 기다려야 하나. 0 → 0 (즉시 반환),
 * 그 밖에는 ms + 1, 단 UINT32_MAX 는 넘치지 않게 그대로 (HAL_MAX_DELAY 관례). */
static uint32_t delay_wait_ticks(uint32_t ms) {
    if (ms == 0u) return 0u;
    return ms < UINT32_MAX ? ms + 1u : ms;
}
/* TODO C2: 최소 ms 밀리초, 최대 ms + 1 밀리초 (+ 루프 한 바퀴) 기다린다. 폴링 루프 몸통은 cpu_spin(). */
static void delay_ms(uint32_t ms) {
    uint32_t wait = delay_wait_ticks(ms);
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < wait) cpu_spin();
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs = 0x13579BDFu;               /* xorshift32, 고정 시드 */
static uint32_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static void start_systick(uint32_t core_hz, uint32_t tick_hz, uint32_t ticks0) {
    mock_reset();
    g_ticks = ticks0;
    (void)systick_init(core_hz, tick_hz);
}
/* 위상 p (0..N-1 클럭) 에서 delay 를 부르고, 걸린 코어 클럭 수를 잰다 */
static uint64_t measure_delay(void (*fn)(uint32_t), uint32_t ms, uint32_t phase, uint32_t ticks0) {
    start_systick(1000000u, 1000u, ticks0);     /* 모델: 1 MHz 코어, 1 kHz tick → N = 1000 클럭 */
    mock_cycles(1000u + phase);                 /* 첫 tick 이후 위상 p 로 */
    uint64_t t0 = g_cycles;
    if (setjmp(g_spin_jmp)) { g_spin_armed = false; return UINT64_MAX; }   /* 끝나지 않은 delay */
    g_spin_armed = true;
    fn(ms);
    g_spin_armed = false;
    return g_cycles - t0;
}
static void sweep(void (*fn)(uint32_t), uint32_t ms, uint32_t step, uint64_t *mn, uint64_t *mx) {
    *mn = UINT64_MAX; *mx = 0;
    for (uint32_t p = 0; p < 1000u; p += step) {
        uint64_t e = measure_delay(fn, ms, p, 0u);
        if (e < *mn) *mn = e;
        if (e > *mx) *mx = e;
    }
}

int main(int argc, char **argv) {
    bool verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    uint32_t r = 0; int32_t ppm = 0;

    /* ---------- Part A1: reload 계산 ---------- */
    CHECK(systick_reload(168000000u, 1000u, &r, &ppm) == ST_OK && r == 167999u && ppm == 0);
    CHECK(systick_reload(72000000u, 1000u, &r, &ppm) == ST_OK && r == 71999u);
    CHECK(systick_reload(16000000u, 1000u, &r, &ppm) == ST_OK && r == 15999u);
    CHECK(systick_reload(16000000u, 1u, &r, &ppm) == ST_OK && r == 15999999u);       /* 0xF423FF: 1 s 도 들어간다 */
    CHECK(systick_reload(16777216u, 1u, &r, &ppm) == ST_OK && r == 0x00FFFFFFu);     /* 정확히 최대 */
    r = 7u;
    CHECK(systick_reload(16777217u, 1u, &r, &ppm) == ST_ERR_RANGE && r == 7u);       /* 2^24 + 1 클럭 */
    CHECK(systick_reload(168000000u, 10u, &r, &ppm) == ST_ERR_RANGE);                /* 16.8 M > 2^24 */
    CHECK(systick_reload(1000u, 1000u, &r, &ppm) == ST_ERR_RANGE);                   /* reload 0: 동작 안 함 */
    CHECK(systick_reload(1000u, 2000u, &r, &ppm) == ST_ERR_RANGE);
    CHECK(systick_reload(16000000u, 0u, &r, &ppm) == ST_ERR_ARG);
    CHECK(systick_reload(0u, 1000u, &r, &ppm) == ST_ERR_ARG);
    CHECK(systick_reload(16000000u, 1000u, NULL, &ppm) == ST_ERR_ARG);
    CHECK(systick_reload(32768u, 1000u, &r, &ppm) == ST_INEXACT && r == 32u && ppm == -7030);
    CHECK(systick_reload(10000000u, 3000u, &r, &ppm) == ST_INEXACT && r == 3332u && ppm == 100);
    CHECK(systick_reload(16000000u, 3u, &r, &ppm) == ST_INEXACT && r == 5333332u && ppm == 0);
    CHECK(systick_reload(32768u, 1024u, &r, NULL) == ST_OK && r == 31u);             /* err_ppm NULL 허용 */
    {   /* 무차별 대조: N ∈ {q-1..q+2} 중 |core - N·tick| 최소 (동점이면 큰 N) 를 직접 찾아 비교 */
        int bad = 0;
        for (int i = 0; i < 20000; i++) {
            uint32_t core = 1000000u + rnd() % 199000001u, tick = 1u + rnd() % 100000u;
            int64_t q = core / tick, best = 0, best_e = INT64_MAX;
            for (int64_t n = q - 1; n <= q + 2; n++) {
                int64_t e = llabs((int64_t)core - n * tick);
                if (n >= 1 && e <= best_e) { best = n; best_e = e; }
            }
            r = 0xA5A5A5A5u; ppm = 0;
            st_status_t st = systick_reload(core, tick, &r, &ppm);
            if (best < 2 || best > (int64_t)SYST_RVR_MAX + 1) { if (st != ST_ERR_RANGE) bad++; continue; }
            int64_t e = (int64_t)core - best * tick;
            if (r != (uint32_t)(best - 1)) bad++;
            if ((e == 0) != (st == ST_OK) || st < 0) bad++;
            if ((e > 0 && ppm < 0) || (e < 0 && ppm > 0)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------- Part A2: SysTick 하드웨어 모델 (RVR=3 → 주기 4 클럭) ---------- */
    mock_reset();
    REG_WRITE(SYST_RVR, 3u); REG_WRITE(SYST_CVR, 0xDEADu);
    REG_WRITE(SYST_CSR, SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE);       /* TICKINT 없이 */
    {
        uint32_t seq[9], flag_at = 0;
        for (int i = 0; i < 9; i++) {
            mock_cycles(1);
            seq[i] = mock_peek(SYST_CVR);
            if (mock_peek(SYST_CSR) & SYST_CSR_COUNTFLAG) { flag_at |= 1u << i; mock_poke(SYST_CSR, mock_peek(SYST_CSR) & ~SYST_CSR_COUNTFLAG); }
        }
        const uint32_t want[9] = { 3, 2, 1, 0, 3, 2, 1, 0, 3 };
        CHECK(memcmp(seq, want, sizeof want) == 0);                  /* 0 → RVR 재장전 → 3,2,1,0 */
        CHECK(flag_at == ((1u << 3) | (1u << 7)));                   /* COUNTFLAG 는 1→0 일 때만 (4, 8번째 클럭) */
    }
    mock_cycles(3);                                                  /* 다시 0 에 도달 */
    CHECK((REG_READ(SYST_CSR) & SYST_CSR_COUNTFLAG) != 0u);          /* 첫 읽기: 1 */
    CHECK((REG_READ(SYST_CSR) & SYST_CSR_COUNTFLAG) == 0u);          /* 두 번째 읽기: 읽어서 지워졌다 */
    REG_WRITE(SYST_CSR, SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE | SYST_CSR_TICKINT);
    g_ticks = 0; mock_cycles(400);
    CHECK(g_ticks == 100u);                                          /* TICKINT → 4 클럭마다 SysTick_Handler */
    REG_WRITE(SYST_RVR, 0u); g_ticks = 0; mock_cycles(100);          /* 지금 CVR == 0 → 0 을 재장전 */
    CHECK(g_ticks == 0u && mock_peek(SYST_CVR) == 0u);               /* reload 0: 1→0 전이가 없으니 tick 없음 */
    REG_WRITE(SYST_RVR, 3u); REG_WRITE(SYST_CSR, SYST_CSR_ENABLE | SYST_CSR_TICKINT);   /* CLKSOURCE=0 */
    REG_WRITE(SYST_CVR, 0u); g_ticks = 0; mock_cycles(320);
    CHECK(g_ticks == 10u);                                           /* 외부 기준 = /8 → 8배 느림 */
    CHECK(g_unexpected == 0);

    /* ---------- Part A3/A4: systick_init + 정확히 N 클럭마다 tick ---------- */
    mock_reset(); g_ticks = 0;
    CHECK(systick_init(1000000u, 1000u) == ST_OK);
    {
        int i_rvr = -1, i_cvr = -1, i_en = -1;
        for (int i = 0; i < g_nlog; i++) {
            if (g_log[i].op != 'W') continue;
            if (g_log[i].addr == SYST_RVR && g_log[i].val == 999u) i_rvr = i;
            if (g_log[i].addr == SYST_CVR) i_cvr = i;
            if (g_log[i].addr == SYST_CSR && g_log[i].val == (SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE)) i_en = i;
        }
        CHECK(i_rvr >= 0 && i_cvr >= 0 && i_en > i_rvr && i_en > i_cvr && i_en == g_nlog - 1);
        for (int i = 0; verbose && i < g_nlog; i++)
            printf("init log %d: %c 0x%08X = 0x%08X\n", i, g_log[i].op, (unsigned)g_log[i].addr, (unsigned)g_log[i].val);
    }
    mock_cycles(999);  CHECK(g_ticks == 0u);                         /* 999 클럭: 아직 */
    mock_cycles(1);    CHECK(g_ticks == 1u);                         /* 정확히 N = 1000 클럭에 첫 tick */
    mock_cycles(9000); CHECK(g_ticks == 10u && millis() == 10u);
    mock_reset(); g_ticks = 0;
    CHECK(systick_init(168000000u, 1000u) == ST_OK && mock_peek(SYST_RVR) == 167999u);
    mock_cycles(167999); CHECK(g_ticks == 0u);
    mock_cycles(1);      CHECK(g_ticks == 1u);
    mock_reset();
    CHECK(systick_init(168000000u, 10u) == ST_ERR_RANGE && g_nlog == 0);   /* 실패하면 레지스터 무손상 */
    CHECK(mock_peek(SYST_CSR) == 0u);
    mock_reset();
    CHECK(systick_init(32768u, 1000u) == ST_INEXACT && mock_peek(SYST_RVR) == 32u);
    CHECK(g_unexpected == 0);

    /* ---------- Part B1: wrap-safe 비교 ---------- */
    CHECK(time_after_eq(0x10u, 0x10u) == true);
    CHECK(time_after_eq(0x0Fu, 0x10u) == false);
    CHECK(time_after_eq(0x00000005u, 0xFFFFFFF0u) == true);          /* wrap 뒤의 now 가 wrap 전 deadline 을 지남 */
    CHECK(time_after_eq(0xFFFFFFF0u, 0x00000010u) == false);         /* wrap 전 now, wrap 뒤 deadline: 아직 */
    CHECK(time_after_eq_naive(0xFFFFFFF0u, 0x00000010u) == true);    /* naive: 즉시 만료 (버그 재현) */
    CHECK(time_after_eq_naive(0x00000005u, 0xFFFFFFF0u) == false);   /* naive: 영영 안 끝남 (버그 재현) */
    CHECK(time_after_eq(0x00000000u, 0x7FFFFFFFu) == false);         /* 거리 2^31 - 1: 맞다 */
    CHECK(time_after_eq(0x00000000u, 0x80000001u) == true);          /* 2^31 + 1 뒤의 deadline → "이미 지남" 으로 오판 */
    CHECK(time_after_eq(0x7FFFFFFFu, 0x00000000u) == true);
    CHECK(time_after_eq(0x80000000u, 0x00000000u) == false);         /* 정확히 2^31 지난 deadline → "아직" 으로 오판 */
    {   /* 64비트 오라클: |거리| < 2^31 이면 항상 일치. naive 는 wrap 걸친 경우에 틀린다 */
        int bad = 0, naive_bad = 0;
        for (int i = 0; i < 200000; i++) {
            uint64_t base = ((uint64_t)rnd() << 8) | (rnd() & 0xFFu);   /* 40비트 "진짜 시각" */
            int64_t d = (int64_t)(rnd() & 0x7FFFFFFFu) * ((rnd() & 1u) ? 1 : -1);
            if (i < 64) d = (int64_t)i - 32;                            /* 경계 근처 */
            uint64_t now64 = base + 0x100000000ull, dl64 = (uint64_t)((int64_t)now64 + d);
            bool truth = now64 >= dl64;
            if (time_after_eq((uint32_t)now64, (uint32_t)dl64) != truth) bad++;
            if (time_after_eq_naive((uint32_t)now64, (uint32_t)dl64) != truth) naive_bad++;
        }
        CHECK(bad == 0);
        CHECK(naive_bad > 0);
    }
    /* ---------- Part B2: timeout_expired, g_ticks 직접 설정 ---------- */
    g_ticks = 0xFFFFFFF0u; { uint32_t st0 = millis();
    g_ticks = 0x0000000Fu; CHECK(timeout_expired(st0, 32u) == false);        /* 31 ms 경과 */
    g_ticks = 0x00000010u; CHECK(timeout_expired(st0, 32u) == true);         /* 32 ms 경과 (wrap 지남) */
    g_ticks = 0xFFFFFFF0u; CHECK(timeout_expired(st0, 0u) == true);          /* 0 ms = 즉시 */
    g_ticks = 0xFFFFFFEFu; CHECK(timeout_expired(st0, 0xFFFFFFFFu) == true); /* 경과 형식은 2^32 - 1 까지 */
    g_ticks = 0xFFFFFFEEu; CHECK(timeout_expired(st0, 0xFFFFFFFFu) == false); }
    /* ---------- Part B3: 진짜 tick 으로 wrap 넘기기 + 폴링 타임아웃 ---------- */
    start_systick(1000000u, 1000u, 0xFFFFFFF0u);
    mock_cycles(32u * 1000u);
    CHECK(g_ticks == 0x00000010u);                                   /* 0xFFFFFFF0 + 32 = 0x10 */
    {
        start_systick(1000000u, 1000u, 0xFFFFFFF0u);
        uint32_t deadline = millis() + 32u; uint64_t t0 = g_cycles; g_spins = 0;
        while (!time_after_eq(millis(), deadline) && g_spins < 100000ul) cpu_spin();
        uint64_t safe_el = g_cycles - t0;
        start_systick(1000000u, 1000u, 0xFFFFFFF0u);
        deadline = millis() + 32u; t0 = g_cycles; g_spins = 0;
        while (!time_after_eq_naive(millis(), deadline) && g_spins < 100000ul) cpu_spin();
        uint64_t naive_el = g_cycles - t0;
        CHECK(safe_el >= 32000u && safe_el <= 32000u + SPIN_CYCLES); /* 32 ms 기다림 */
        CHECK(naive_el == 0u);                                       /* naive: 0xFFFFFFF0 >= 0x10 → 즉시 "만료" */
        if (verbose) printf("deadline 0x%08X: wrap-safe %llu cycles, naive %llu cycles\n",
                            (unsigned)deadline, (unsigned long long)safe_el, (unsigned long long)naive_el);
    }
    {   /* 6 ms 마다만 확인하는 루프: deadline 0xFFFFFFFE 의 "정확한 순간" 을 못 보고 wrap 을 넘는다 */
        int safe_at = -1, naive_at = -1;
        for (int k = 0; k < 10; k++) {
            g_ticks = 0xFFFFFFF0u + 6u * (uint32_t)k;                /* F0, F6, FC, 0x02, 0x08, ... */
            if (safe_at < 0 && time_after_eq(millis(), 0xFFFFFFFEu)) safe_at = k;
            if (naive_at < 0 && time_after_eq_naive(millis(), 0xFFFFFFFEu)) naive_at = k;
        }
        CHECK(safe_at == 3);                                         /* 0x00000002 에서 만료 */
        CHECK(naive_at == -1);                                       /* naive: 49.7일 뒤에나 만료 */
    }

    /* ---------- Part C: delay_ms 와 tick 위상 ---------- */
    CHECK(delay_wait_ticks(0u) == 0u);
    CHECK(delay_wait_ticks(1u) == 2u);
    CHECK(delay_wait_ticks(100u) == 101u);
    CHECK(delay_wait_ticks(UINT32_MAX - 1u) == UINT32_MAX);
    CHECK(delay_wait_ticks(UINT32_MAX) == UINT32_MAX);               /* 넘쳐서 0 이 되면 안 된다 */
    {
        uint64_t nmn, nmx, mn, mx;
        sweep(delay_ms_naive, 1u, 1u, &nmn, &nmx);
        CHECK(nmn < 1000u);                                          /* naive delay_ms(1): 1 ms 미만에 끝나는 위상이 있다 */
        sweep(delay_ms, 1u, 1u, &mn, &mx);
        CHECK(mn >= 1000u && mx <= 2000u + SPIN_CYCLES);             /* +1: 1 ms ≤ 실제 ≤ 2 ms (+ 루프 한 바퀴) */
        uint64_t mn5, mx5, nmn5, nmx5;
        sweep(delay_ms, 5u, 7u, &mn5, &mx5);
        sweep(delay_ms_naive, 5u, 7u, &nmn5, &nmx5);
        CHECK(mn5 >= 5000u && mx5 <= 6000u + SPIN_CYCLES);
        CHECK(nmn5 < 5000u);
        if (verbose) {
            printf("delay      | min cycles | max cycles  (N = 1000 cycles = 1 ms, spin = %u)\n", SPIN_CYCLES);
            printf("naive(1)   | %10llu | %10llu\n", (unsigned long long)nmn, (unsigned long long)nmx);
            printf("+1   (1)   | %10llu | %10llu\n", (unsigned long long)mn, (unsigned long long)mx);
            printf("naive(5)   | %10llu | %10llu\n", (unsigned long long)nmn5, (unsigned long long)nmx5);
            printf("+1   (5)   | %10llu | %10llu\n", (unsigned long long)mn5, (unsigned long long)mx5);
        }
    }
    CHECK(measure_delay(delay_ms, 0u, 500u, 0u) == 0u);              /* delay_ms(0): 즉시 */
    {
        uint64_t e = measure_delay(delay_ms, 40u, 999u, 0xFFFFFFF0u);
        CHECK(e >= 40000u && e <= 41000u + SPIN_CYCLES);             /* wrap 을 걸친 delay */
        CHECK(g_ticks > 0u && g_ticks < 0x100u);                     /* 실제로 wrap 을 지났다 */
        e = measure_delay(delay_ms, 3u, 0u, 0xFFFFFFFEu);
        CHECK(e >= 3000u && e <= 4000u + SPIN_CYCLES);               /* 시작 직후가 wrap 경계 */
    }
    CHECK(g_unexpected == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
