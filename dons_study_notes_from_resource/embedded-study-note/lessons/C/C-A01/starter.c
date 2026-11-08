/* C-A01 호스트 테스트 가능한 레지스터 접근 계층 설계
 *
 * 실제 프로젝트라면 파일 5개인 것을 한 파일에 순서대로 담았다:
 *   [1] stm32f4_spi_regs.h  레지스터 맵 (RM0090 발췌). 주소는 데이터시트 정수 그대로 둔다.
 *   [2] hal_reg.h           REG_READ / REG_WRITE / REG_MODIFY — 빌드 옵션으로 전환
 *                             -DREG_TARGET : volatile 직접 접근 (MCU 빌드, 오버헤드 0)
 *                             (기본)       : 호스트 mock 호출 → 접근 기록 + 하드웨어 규칙 모델
 *   [3] reg_mock.c          접근 로그, 스크립트 읽기, 기대 시퀀스 비교, 하드웨어 규칙 모델
 *   [4] spi_drv.c           SPI1 드라이버 (spi_init / spi_set_speed)
 *   [5] test_spi.c          단위 테스트 (main)
 * 호스트 빌드는 0x4001_3000 같은 주소를 절대 역참조하지 않는다. mock 은 주소를 "키" 로만 쓴다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= [1] 레지스터 맵 (STM32F4, RM0090) ================= */
#define RCC_BASE        0x40023800u
#define RCC_APB1ENR     (RCC_BASE + 0x40u)    /* SPI2/SPI3 클록은 여기 (SPI1 과 헷갈리는 단골) */
#define RCC_APB2RSTR    (RCC_BASE + 0x24u)
#define RCC_APB2ENR     (RCC_BASE + 0x44u)
#define RCC_APB2_SPI1   (1u << 12)            /* APB2RSTR.SPI1RST, APB2ENR.SPI1EN */

#define SPI1_BASE       0x40013000u
#define SPI1_CR1        (SPI1_BASE + 0x00u)
#define SPI1_CR2        (SPI1_BASE + 0x04u)
#define SPI1_SR         (SPI1_BASE + 0x08u)
#define SPI1_DR         (SPI1_BASE + 0x0Cu)

#define SPI_CR1_CPHA     (1u << 0)
#define SPI_CR1_CPOL     (1u << 1)
#define SPI_CR1_MSTR     (1u << 2)
#define SPI_CR1_BR_Pos   3u
#define SPI_CR1_BR_Msk   (7u << SPI_CR1_BR_Pos)   /* f_SCK = f_PCLK / 2^(BR+1) */
#define SPI_CR1_SPE      (1u << 6)
#define SPI_CR1_LSBFIRST (1u << 7)
#define SPI_CR1_SSI      (1u << 8)
#define SPI_CR1_SSM      (1u << 9)
#define SPI_CR1_DFF      (1u << 11)
#define SPI_CR2_SSOE     (1u << 2)
#define SPI_SR_RXNE      (1u << 0)
#define SPI_SR_TXE       (1u << 1)
#define SPI_SR_MODF      (1u << 5)
#define SPI_SR_OVR       (1u << 6)
#define SPI_SR_BSY       (1u << 7)

/* ================= [2] 레지스터 접근 계층 (hal_reg.h) ================= */
#if defined(REG_TARGET)
#  if !defined(__arm__)
#    error "REG_TARGET 은 32비트 ARM(MCU) 빌드 전용: 호스트에서 0x4000_0000 대를 역참조하면 SIGSEGV"
#  endif
static inline uint32_t reg_read32(uintptr_t a, int line) { (void)line; return *(const volatile uint32_t *)a; }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { (void)line; *(volatile uint32_t *)a = v; }
#else
static uint32_t mock_reg_read(uintptr_t a, int line);
static void     mock_reg_write(uintptr_t a, uint32_t v, int line);
static inline uint32_t reg_read32(uintptr_t a, int line) { return mock_reg_read(a, line); }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { mock_reg_write(a, v, line); }
#endif
#define REG_READ(a)             reg_read32((uintptr_t)(a), __LINE__)
#define REG_WRITE(a, v)         reg_write32((uintptr_t)(a), (uint32_t)(v), __LINE__)
#define REG_MODIFY(a, clr, set) reg_modify32((uintptr_t)(a), (uint32_t)(clr), (uint32_t)(set), __LINE__)

/* TODO(A1) RMW: 읽고 → clr 비트 지우고 set 비트 세우고 → 쓴다. 반드시 reg_read32/reg_write32 를 거친다
 * (그래야 mock 이 R 과 W 를 둘 다 본다). 값이 안 바뀌어도 쓴다. ISR 과의 원자성은 보장하지 않는다 (C-M02). */
static inline void reg_modify32(uintptr_t a, uint32_t clr, uint32_t set, int line) {
    (void)a; (void)clr; (void)set; (void)line;   /* TODO */
}

/* ================= [3] 호스트 mock (reg_mock.c) ================= */
typedef struct { char op; uintptr_t addr; uint32_t val; int line; } reg_access_t;    /* 로그 한 줄 */
typedef struct { char op; uintptr_t addr; uint32_t val; uint32_t mask; } reg_expect_t; /* 기대 한 줄 */
#define EXP_W(a, v)     { 'W', (a), (v), 0xFFFFFFFFu }   /* 쓴 값 전체 비교 */
#define EXP_WM(a, v, m) { 'W', (a), (v), (m) }           /* m 비트만 비교 */
#define EXP_R(a)        { 'R', (a), 0u, 0u }             /* 읽기: 값은 상관없음 (mask 0) */

enum {                       /* 하드웨어 규칙 위반 (g_viol 에 OR 로 누적) */
    V_UNKNOWN      = 1u << 0,   /* 맵에 없는 주소 접근 (오프셋 오타, 다른 버스) */
    V_CLK_OFF      = 1u << 1,   /* 클록 꺼진 SPI1 접근: 쓰기 무시, 읽기 0 */
    V_RO_WRITE     = 1u << 2,   /* 읽기 전용 SR 에 쓰기 */
    V_CFG_WHILE_ON = 1u << 3,   /* SPE=1 인 채로 BR/CPOL/CPHA/MSTR/DFF/LSBFIRST 변경 */
    V_MODF         = 1u << 4,   /* 마스터 + SSM=1 + SSI=0 으로 켬 → mode fault */
    V_LOG_FULL     = 1u << 5    /* 로그 넘침 (테스트가 너무 길다) */
};
#define CR1_LOCKED (SPI_CR1_BR_Msk | SPI_CR1_CPOL | SPI_CR1_CPHA | SPI_CR1_MSTR | SPI_CR1_DFF | SPI_CR1_LSBFIRST)

typedef struct { uintptr_t addr; const char *name; uint32_t reset; bool spi; } reg_desc_t;
static const reg_desc_t k_regs[] = {
    { RCC_APB2RSTR, "RCC_APB2RSTR", 0x00000000u, false },
    { RCC_APB2ENR,  "RCC_APB2ENR",  0x00000000u, false },
    { SPI1_CR1,     "SPI1_CR1",     0x00000000u, true  },
    { SPI1_CR2,     "SPI1_CR2",     0x00000000u, true  },
    { SPI1_SR,      "SPI1_SR",      0x00000002u, true  },   /* 리셋 값: TXE=1 */
    { SPI1_DR,      "SPI1_DR",      0x00000000u, true  },
};
#define NREGS      ((int)(sizeof k_regs / sizeof k_regs[0]))
#define LOG_MAX    32
#define SCRIPT_MAX 16

static uint32_t     g_val[NREGS];      /* 모델 안의 "하드웨어" 레지스터 값 */
static reg_access_t g_log[LOG_MAX];
static int          g_nlog;
static unsigned     g_viol;
static struct { uintptr_t addr; uint32_t val; bool used; } g_script[SCRIPT_MAX];
static int          g_nscript;

static int reg_index(uintptr_t a) {
    for (int i = 0; i < NREGS; i++)
        if (k_regs[i].addr == a) return i;
    return -1;
}
static const char *reg_name(uintptr_t a) { int i = reg_index(a); return i < 0 ? "???" : k_regs[i].name; }
static void mock_reset(void) {                   /* 전원 리셋: 모든 값 리셋 값, 로그·위반·스크립트 비움 */
    for (int i = 0; i < NREGS; i++) g_val[i] = k_regs[i].reset;
    g_nlog = 0; g_viol = 0; g_nscript = 0;
}
static void mock_clear_log(void) { g_nlog = 0; g_viol = 0; }    /* 상태는 두고 기록만 비움 */
static void mock_set(uintptr_t a, uint32_t v) { int i = reg_index(a); if (i >= 0) g_val[i] = v; }   /* 로그 없음 */
static uint32_t mock_peek(uintptr_t a) { int i = reg_index(a); return i < 0 ? 0u : g_val[i]; }      /* 로그 없음 */
static void mock_script_read(uintptr_t a, uint32_t v) {      /* "a 를 읽기 직전 하드웨어 값이 v 로 바뀐다" (주소별 FIFO) */
    if (g_nscript < SCRIPT_MAX) { g_script[g_nscript].addr = a; g_script[g_nscript].val = v; g_script[g_nscript].used = false; g_nscript++; }
}
static int mock_count(char op, uintptr_t a) {
    int n = 0;
    for (int k = 0; k < g_nlog; k++) n += (g_log[k].op == op && g_log[k].addr == a);
    return n;
}
static void mock_dump(const char *title) {
    printf("-- %s (%d accesses, viol=0x%02X)\n", title, g_nlog, g_viol);
    for (int k = 0; k < g_nlog; k++) {
        char nb[16];                             /* 맵에 없는 주소는 숫자로 */
        snprintf(nb, sizeof nb, "0x%08lX", (unsigned long)g_log[k].addr);
        printf("  #%-2d %c %-13s 0x%08X  line %d\n", k, g_log[k].op,
               reg_index(g_log[k].addr) < 0 ? nb : reg_name(g_log[k].addr), (unsigned)g_log[k].val, g_log[k].line);
    }
}
static bool spi_clk_on(void) { return (g_val[reg_index(RCC_APB2ENR)] & RCC_APB2_SPI1) != 0u; }

/* TODO(A2) 로그에 한 줄 추가. 꽉 차면 V_LOG_FULL 만 세우고 버린다 (배열 밖에 쓰면 안 된다). */
static void log_push(char op, uintptr_t a, uint32_t v, int line) {
    (void)op; (void)a; (void)v; (void)line;      /* TODO */
}

/* TODO(B1) 하드웨어 읽기 규칙: SPI1 레지스터인데 클록이 꺼져 있으면 V_CLK_OFF, 0 을 돌려준다. */
static uint32_t model_read(int i) {
    return g_val[i];                             /* TODO: 지금은 일반 메모리처럼 읽기만 */
}

/* TODO(B2) 하드웨어 쓰기 규칙. g_val[i] 를 하드웨어처럼 갱신한다.
 *  - SPI1 레지스터 + 클록 꺼짐 → V_CLK_OFF, 쓰기 무시
 *  - APB2RSTR 의 SPI1RST 를 1 로 쓰면 SPI1 레지스터 전부 리셋 값으로
 *  - SPI1_SR 은 읽기 전용 → V_RO_WRITE, 값 그대로
 *  - SPI1_CR1: 기존 SPE=1 인데 CR1_LOCKED 비트가 바뀌면 V_CFG_WHILE_ON (값은 저장)
 *              새 값이 SPE|MSTR|SSM 이고 SSI=0 이면 V_MODF: SR.MODF=1, 저장값에서 SPE·MSTR 지움 */
static void model_write(int i, uint32_t v) {
    g_val[i] = v;                                /* TODO: 지금은 일반 메모리처럼 저장만 */
}

/* TODO(A3) 읽기: 맵에 없는 주소면 V_UNKNOWN, 0 을 로그에 남기고 0 반환.
 * 아직 안 쓴 스크립트 항목 중 같은 주소의 첫 항목이 있으면 소비하고 g_val[i] 를 그 값으로 바꾼 뒤,
 * model_read(i) 로 읽는다. 로그에는 "CPU 가 받은 값" 을 남긴다. */
static uint32_t mock_reg_read(uintptr_t a, int line) {
    const int i = reg_index(a);
    (void)line;                                  /* TODO: 로그, 미지 주소, 스크립트 읽기 */
    return i < 0 ? 0u : model_read(i);
}

/* 주어진 코드 — 쓰기: 로그에는 "CPU 가 쓴 값" v 를 남긴다 (모델이 무시하거나 바꿔 저장해도).
 * 맵에 없는 주소면 V_UNKNOWN 만. 아니면 model_write(i, v) 가 하드웨어처럼 반영한다. */
static void mock_reg_write(uintptr_t a, uint32_t v, int line) {
    const int i = reg_index(a);
    log_push('W', a, v, line);
    if (i < 0) { g_viol |= V_UNKNOWN; return; }
    model_write(i, v);
}

/* TODO(A4) 로그가 exp[0..n) 과 같으면 -1. 다르면 첫 차이 인덱스를 돌려주고 msg 에 이유를 쓴다.
 * 비교: op, addr 같고 (val & mask) == (exp.val & mask). 형식 (값은 %08X):
 *   "#7: expected W SPI1_CR1 0x0000035F, got W SPI1_CR1 0x0000031F (line 123)"
 *   "#9: expected W SPI1_CR1 0x0000035F, got <end of log>"
 *   "#10: unexpected extra W SPI1_CR2 0x00000000 (line 123)"
 * 기대가 읽기(mask 0)면 값 자리에 "any" 를 쓴다: "expected R SPI1_SR any". */
static int mock_check_seq(const reg_expect_t *exp, int n, char *msg, size_t cap) {
    (void)exp; (void)n;                          /* TODO */
    if (cap) msg[0] = '\0';
    return 0;
}

/* ================= [4] SPI1 드라이버 (spi_drv.c) — 레지스터는 REG_* 로만 만진다 ================= */
typedef struct {
    uint32_t pclk_hz;   /* APB2 클록 */
    uint32_t max_hz;    /* 슬레이브가 허용하는 최대 SCK */
    uint8_t  mode;      /* SPI mode 0..3 = (CPOL << 1) | CPHA  → CR1 의 비트 1:0 과 같은 배치 */
    bool     lsb_first;
    bool     frame16;   /* DFF: 16비트 프레임 */
    bool     hw_nss;    /* true: NSS 핀 하드웨어 출력(CR2.SSOE) / false: 소프트웨어 NSS(SSM=1, SSI=1) */
} spi_cfg_t;

/* 주어진 함수: f_PCLK / 2^(br+1) <= max_hz 인 가장 작은 br (0..7). 불가능하면 -1. 레지스터 접근 없음. */
static int spi_pick_br(uint32_t pclk_hz, uint32_t max_hz) {
    if (pclk_hz == 0u || max_hz == 0u) return -1;
    for (int br = 0; br < 8; br++)
        if (((uint64_t)max_hz << (br + 1)) >= pclk_hz) return br;
    return -1;
}

/* TODO(C1) 초기화. 설정이 틀리면(NULL, mode > 3, 분주 불가) 레지스터를 하나도 건드리지 않고 -1.
 * 순서: ① APB2ENR.SPI1EN 켜기(RMW) ② APB2ENR 한 번 더 읽기(클록 enable 직후 지연, errata)
 *       ③ APB2RSTR.SPI1RST 1 → 0 (각각 RMW): 부트로더가 남긴 상태 제거
 *       ④ CR1 = 설정(SPE=0) ⑤ CR2 = 설정 ⑥ CR1 = 설정 | SPE  (켜는 건 마지막) */
static int spi_init(const spi_cfg_t *c) {
    (void)c;                                     /* TODO */
    return -1;
}

/* TODO(C2) 동작 중 속도 변경. 분주 불가면 접근 없이 -1.
 * CR1 을 한 번 읽는다. BR 이 이미 같으면 끝(0). SPE=0 이면 BR 만 바꿔 한 번 쓰고 끝.
 * SPE=1 이면: SR 을 최대 max_polls 번 읽으며 (TXE=1 && BSY=0) 을 기다린다 → 시간 초과면 CR1 을 안 쓰고 -1.
 * 준비되면 CR1 에 세 번 쓴다: SPE 끄기 → BR 바꾸기(SPE=0) → SPE 다시 켜기. */
static int spi_set_speed(uint32_t pclk_hz, uint32_t max_hz, int max_polls) {
    (void)pclk_hz; (void)max_hz; (void)max_polls; /* TODO */
    return -1;
}

/* ---- 코드 리뷰에서 나온 "버그 있는" 드라이버 4개 (주어진 코드). Part B 테스트가 이것들을 잡아야 한다 ---- */
static void bug_init_wrong_bus(void) {          /* SPI1 클록을 APB1 에서 켬 (SPI2EN 비트와 혼동) */
    REG_MODIFY(RCC_APB1ENR, 0u, 1u << 14);
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (3u << SPI_CR1_BR_Pos));
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (3u << SPI_CR1_BR_Pos) | SPI_CR1_SPE);
}
static void bug_init_no_ssi(void) {             /* 소프트웨어 NSS 를 쓰면서 SSI 를 안 세움 */
    REG_MODIFY(RCC_APB2ENR, 0u, RCC_APB2_SPI1);
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | (3u << SPI_CR1_BR_Pos));
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | (3u << SPI_CR1_BR_Pos) | SPI_CR1_SPE);
}
static void bug_set_speed_hot(void) {           /* 켜진 채로 BR 만 바꿔 씀 */
    const uint32_t cr1 = REG_READ(SPI1_CR1);
    REG_WRITE(SPI1_CR1, (cr1 & ~SPI_CR1_BR_Msk) | (1u << SPI_CR1_BR_Pos));
}
static void bug_clear_flags(void) {             /* "플래그 지우기" 라며 SR 에 0 을 씀 */
    REG_WRITE(SPI1_SR, 0u);
}

/* ================= [5] 테스트 (test_spi.c) ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define STARTS(s, prefix) (strncmp((s), (prefix), strlen(prefix)) == 0)

static void clock_on(void) { mock_set(RCC_APB2ENR, RCC_APB2_SPI1); }   /* 테스트 준비: 로그 없이 클록만 켬 */

int main(void) {
    char msg[160];

    /* ===== Part A: 접근 계층 + 기록 mock ===== */
    mock_reset(); clock_on();
    REG_WRITE(SPI1_CR2, 0x44u);
    CHECK(g_nlog == 1 && g_log[0].op == 'W' && g_log[0].addr == SPI1_CR2 && g_log[0].val == 0x44u);
    CHECK(g_log[0].line == __LINE__ - 2);                       /* 로그가 호출한 소스 줄을 안다 */
    CHECK(mock_peek(SPI1_CR2) == 0x44u);
    CHECK(REG_READ(SPI1_SR) == SPI_SR_TXE);                     /* 리셋 값 0x2 */
    CHECK(g_nlog == 2 && g_log[1].op == 'R' && g_log[1].addr == SPI1_SR && g_log[1].val == SPI_SR_TXE);
    CHECK(g_viol == 0u);

    mock_reset(); clock_on();                                   /* 맵에 없는 주소 */
    CHECK(REG_READ(SPI1_BASE + 0x40u) == 0u && (g_viol & V_UNKNOWN));
    REG_WRITE(0x40013800u, 1u);
    CHECK(g_nlog == 2 && g_log[1].op == 'W' && g_log[1].addr == 0x40013800u);

    mock_reset(); clock_on();                                   /* 스크립트 읽기: BSY 가 두 번 보였다가 풀림 */
    mock_script_read(SPI1_SR, SPI_SR_BSY | SPI_SR_TXE);
    mock_script_read(SPI1_DR, 0xA5u);
    mock_script_read(SPI1_SR, SPI_SR_BSY | SPI_SR_TXE);
    CHECK(REG_READ(SPI1_SR) == 0x82u);
    CHECK(REG_READ(SPI1_SR) == 0x82u);                          /* DR 항목은 건너뛰고 SR 다음 항목 */
    CHECK(REG_READ(SPI1_SR) == 0x82u);                          /* 스크립트 소진 → 마지막 하드웨어 값 유지 */
    CHECK(REG_READ(SPI1_DR) == 0xA5u);
    CHECK(mock_count('R', SPI1_SR) == 3 && mock_count('R', SPI1_DR) == 1);

    mock_reset(); clock_on();                                   /* 로그 넘침: 배열 밖에 쓰지 않는다 (ASan 으로도 확인) */
    for (int k = 0; k < LOG_MAX + 8; k++) REG_WRITE(SPI1_DR, (uint32_t)k);
    CHECK(g_nlog == LOG_MAX && (g_viol & V_LOG_FULL));
    CHECK(g_log[LOG_MAX - 1].val == (uint32_t)(LOG_MAX - 1));

    mock_reset(); clock_on();                                   /* REG_MODIFY = R 다음 W, 다른 비트 보존 */
    mock_set(SPI1_CR2, 0x00F0u);
    REG_MODIFY(SPI1_CR2, 0x30u, 0x04u);
    CHECK(mock_peek(SPI1_CR2) == 0x00C4u);
    CHECK(g_nlog == 2 && g_log[0].op == 'R' && g_log[1].op == 'W' && g_log[1].val == 0x00C4u);
    mock_clear_log();
    REG_MODIFY(SPI1_CR2, 0u, 0x04u);                            /* 값이 같아도 쓴다 */
    CHECK(g_nlog == 2 && g_log[1].op == 'W');

    {   /* mock_check_seq */
        mock_reset(); clock_on();
        REG_WRITE(SPI1_CR1, 0x31Fu);
        (void)REG_READ(SPI1_SR);
        REG_WRITE(SPI1_CR1, 0x35Fu);
        const reg_expect_t ok[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_R(SPI1_SR), EXP_W(SPI1_CR1, 0x35Fu) };
        CHECK(mock_check_seq(ok, NELEM(ok), msg, sizeof msg) == -1);
        const reg_expect_t bad[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_R(SPI1_SR), EXP_W(SPI1_CR1, 0x31Fu) };
        CHECK(mock_check_seq(bad, NELEM(bad), msg, sizeof msg) == 2);
        CHECK(STARTS(msg, "#2: expected W SPI1_CR1 0x0000031F, got W SPI1_CR1 0x0000035F (line "));
        const reg_expect_t masked[] = { EXP_WM(SPI1_CR1, 0x040u, SPI_CR1_SPE), EXP_R(SPI1_SR), EXP_WM(SPI1_CR1, 0x040u, SPI_CR1_SPE) };
        CHECK(mock_check_seq(masked, NELEM(masked), msg, sizeof msg) == 0);   /* 첫 쓰기는 SPE=0 */
        const reg_expect_t longer[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_R(SPI1_SR), EXP_W(SPI1_CR1, 0x35Fu), EXP_R(SPI1_SR) };
        CHECK(mock_check_seq(longer, NELEM(longer), msg, sizeof msg) == 3
              && strcmp(msg, "#3: expected R SPI1_SR any, got <end of log>") == 0);
        CHECK(mock_check_seq(ok, 2, msg, sizeof msg) == 2 && STARTS(msg, "#2: unexpected extra W SPI1_CR1 0x0000035F (line "));
        const reg_expect_t wrong_op[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_W(SPI1_SR, 0u) };
        CHECK(mock_check_seq(wrong_op, NELEM(wrong_op), msg, sizeof msg) == 1);
    }

    /* ===== Part B: 하드웨어 규칙 모델이 버그 드라이버를 잡는가 ===== */
    mock_reset();                                               /* 클록 끈 채 SPI 접근 */
    CHECK(REG_READ(SPI1_SR) == 0u && (g_viol & V_CLK_OFF));
    mock_reset();
    REG_WRITE(SPI1_CR2, 0x4u);
    CHECK((g_viol & V_CLK_OFF) && mock_peek(SPI1_CR2) == 0u);   /* 쓰기는 무시된다 */

    mock_reset();
    bug_init_wrong_bus();
    CHECK((g_viol & V_UNKNOWN) && (g_viol & V_CLK_OFF) && mock_peek(SPI1_CR1) == 0u);

    mock_reset();
    bug_init_no_ssi();
    CHECK(g_viol == V_MODF);
    CHECK((mock_peek(SPI1_SR) & SPI_SR_MODF) && !(mock_peek(SPI1_CR1) & (SPI_CR1_SPE | SPI_CR1_MSTR)));

    mock_reset(); clock_on();
    mock_set(SPI1_CR1, SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (3u << SPI_CR1_BR_Pos));
    bug_set_speed_hot();
    CHECK(g_viol == V_CFG_WHILE_ON);

    mock_reset(); clock_on();
    mock_set(SPI1_SR, SPI_SR_TXE | SPI_SR_OVR);
    bug_clear_flags();
    CHECK(g_viol == V_RO_WRITE && mock_peek(SPI1_SR) == (SPI_SR_TXE | SPI_SR_OVR));

    mock_reset(); clock_on();                                   /* SPE 만 끄고 켜는 건 위반이 아니다 */
    mock_set(SPI1_CR1, SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI);
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI);
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (5u << SPI_CR1_BR_Pos));
    REG_WRITE(SPI1_CR1, SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (5u << SPI_CR1_BR_Pos) | SPI_CR1_SPE);
    CHECK(g_viol == 0u);

    mock_reset(); clock_on();                                   /* 리셋 펄스는 SPI1 레지스터를 리셋 값으로 */
    mock_set(SPI1_CR1, 0x35Fu); mock_set(SPI1_SR, 0x42u);
    REG_WRITE(RCC_APB2RSTR, RCC_APB2_SPI1);
    CHECK(mock_peek(SPI1_CR1) == 0u && mock_peek(SPI1_SR) == SPI_SR_TXE && g_viol == 0u);

    /* ===== Part C: SPI 드라이버를 기대 시퀀스로 검증 ===== */
    {
        const spi_cfg_t a = { 84000000u, 10000000u, 3u, false, false, false };   /* 84 MHz → 5.25 MHz, mode 3 */
        mock_reset();
        mock_set(RCC_APB2ENR, 1u << 4);                         /* USART1 클록은 이미 켜져 있음 → 보존돼야 */
        CHECK(spi_init(&a) == 0);
        const reg_expect_t exp[] = {
            EXP_R(RCC_APB2ENR),  EXP_W(RCC_APB2ENR, 0x00001010u), EXP_R(RCC_APB2ENR),
            EXP_R(RCC_APB2RSTR), EXP_W(RCC_APB2RSTR, 0x00001000u),
            EXP_R(RCC_APB2RSTR), EXP_W(RCC_APB2RSTR, 0x00000000u),
            EXP_W(SPI1_CR1, 0x0000031Fu), EXP_W(SPI1_CR2, 0x00000000u), EXP_W(SPI1_CR1, 0x0000035Fu),
        };
        const int at = mock_check_seq(exp, NELEM(exp), msg, sizeof msg);
        if (at != -1) printf("  spi_init(a): %s\n", msg);
        CHECK(at == -1);
        CHECK(g_viol == 0u && spi_clk_on());
        CHECK(mock_peek(SPI1_CR1) == 0x35Fu && mock_peek(RCC_APB2ENR) == 0x1010u);

        mock_reset();                                           /* 부트로더가 SPI1 을 다른 설정으로 켜 둔 채 넘겨줌 */
        mock_set(RCC_APB2ENR, RCC_APB2_SPI1);
        mock_set(SPI1_CR1, SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_Msk);
        CHECK(spi_init(&a) == 0 && g_viol == 0u && mock_peek(SPI1_CR1) == 0x35Fu);   /* 리셋 덕에 "켜진 채 변경" 없음 */

        const spi_cfg_t b = { 16000000u, 1000000u, 0u, true, true, true };      /* 16 MHz → 1 MHz, LSB, 16bit, HW NSS */
        mock_reset();
        CHECK(spi_init(&b) == 0 && g_viol == 0u);
        CHECK(mock_peek(SPI1_CR1) == 0x8DCu && mock_peek(SPI1_CR2) == SPI_CR2_SSOE);
        CHECK(g_nlog == 10 && g_log[9].op == 'W' && g_log[9].addr == SPI1_CR1 && (g_log[9].val & SPI_CR1_SPE));

        const spi_cfg_t bad_mode = { 84000000u, 10000000u, 4u, false, false, false };
        const spi_cfg_t too_slow = { 84000000u, 100000u, 0u, false, false, false };   /* 84 MHz / 256 = 328 kHz > 100 kHz */
        const spi_cfg_t zero_hz  = { 84000000u, 0u, 0u, false, false, false };
        mock_reset();
        CHECK(spi_init(&bad_mode) == -1 && spi_init(&too_slow) == -1 && spi_init(&zero_hz) == -1 && spi_init(NULL) == -1);
        CHECK(g_nlog == 0);                                     /* 실패하면 버스 접근 0회 */
        CHECK(spi_pick_br(84000000u, 42000000u) == 0 && spi_pick_br(84000000u, 10500000u) == 2
              && spi_pick_br(84000000u, 10499999u) == 3 && spi_pick_br(84000000u, 328125u) == 7);

        /* 속도 변경: BSY 가 두 번 보였다가 풀림 */
        mock_reset();
        CHECK(spi_init(&a) == 0);
        mock_clear_log();
        mock_script_read(SPI1_SR, SPI_SR_TXE | SPI_SR_BSY);
        mock_script_read(SPI1_SR, SPI_SR_TXE | SPI_SR_BSY);
        mock_script_read(SPI1_SR, SPI_SR_TXE);
        CHECK(spi_set_speed(84000000u, 21000000u, 5) == 0);
        const reg_expect_t sp[] = {
            EXP_R(SPI1_CR1), EXP_R(SPI1_SR), EXP_R(SPI1_SR), EXP_R(SPI1_SR),
            EXP_W(SPI1_CR1, 0x0000031Fu), EXP_W(SPI1_CR1, 0x0000030Fu), EXP_W(SPI1_CR1, 0x0000034Fu),
        };
        const int at2 = mock_check_seq(sp, NELEM(sp), msg, sizeof msg);
        if (at2 != -1) printf("  spi_set_speed: %s\n", msg);
        CHECK(at2 == -1 && g_viol == 0u);

        mock_clear_log();                                       /* 시간 초과: CR1 을 건드리지 않는다 */
        for (int k = 0; k < 4; k++) mock_script_read(SPI1_SR, SPI_SR_TXE | SPI_SR_BSY);
        CHECK(spi_set_speed(84000000u, 2000000u, 3) == -1);
        CHECK(mock_count('R', SPI1_SR) == 3 && mock_count('W', SPI1_CR1) == 0 && mock_peek(SPI1_CR1) == 0x34Fu);

        mock_clear_log();                                       /* 이미 같은 속도: 읽기 1번으로 끝 */
        CHECK(spi_set_speed(84000000u, 21000000u, 3) == 0 && g_nlog == 1);

        mock_reset(); clock_on();                               /* 꺼진 상태: 기다리지 않고 한 번 쓴다 */
        mock_set(SPI1_CR1, 0x31Fu);
        CHECK(spi_set_speed(84000000u, 42000000u, 3) == 0);
        CHECK(g_nlog == 2 && mock_count('R', SPI1_SR) == 0 && mock_peek(SPI1_CR1) == 0x307u);

        mock_reset();                                           /* 마지막으로 정상 init 의 trace 를 보여 준다 */
        mock_set(RCC_APB2ENR, 1u << 4);
        (void)spi_init(&a);
        mock_dump("spi_init(84 MHz, <=10 MHz, mode 3, SW NSS)");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
