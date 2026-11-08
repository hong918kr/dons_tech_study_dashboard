/* C-A01 추가 연습 — 한 번에 검증
 *
 * 본문과 같은 접근 계층(REG_READ/REG_WRITE/REG_MODIFY)과 기록 mock 을 줄여서 다시 담았다
 * (클록 게이트·MODF 규칙은 빼고, 연습 3 을 위한 "DR 읽고 SR 읽으면 OVR 클리어" 규칙을 넣었다).
 *   연습 1  쓰기만 비교하는 매처: mock_check_writes()
 *   연습 2  최종 상태 비교 vs 순서 규칙: spe_last_ok()
 *   연습 3  읽기 순서가 의미를 갖는 시퀀스: spi_clear_ovr()
 *   연습 4  CMSIS 식 구조체 오버레이를 RAM 가짜로 테스트하기 (와 그 한계)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SPI1_BASE   0x40013000u
#define SPI1_CR1    (SPI1_BASE + 0x00u)
#define SPI1_CR2    (SPI1_BASE + 0x04u)
#define SPI1_SR     (SPI1_BASE + 0x08u)
#define SPI1_DR     (SPI1_BASE + 0x0Cu)
#define SPI_CR1_SPE   (1u << 6)
#define SPI_CR2_ERRIE (1u << 5)
#define SPI_SR_RXNE   (1u << 0)
#define SPI_SR_TXE    (1u << 1)
#define SPI_SR_OVR    (1u << 6)
#define SPI_SR_BSY    (1u << 7)

/* ---------- 접근 계층 (본문과 같음) ---------- */
#if defined(REG_TARGET)
#  if !defined(__arm__)
#    error "REG_TARGET 은 32비트 ARM(MCU) 빌드 전용"
#  endif
static inline uint32_t reg_read32(uintptr_t a, int line) { (void)line; return *(const volatile uint32_t *)a; }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { (void)line; *(volatile uint32_t *)a = v; }
#else
static uint32_t mock_reg_read(uintptr_t a, int line);
static void     mock_reg_write(uintptr_t a, uint32_t v, int line);
static inline uint32_t reg_read32(uintptr_t a, int line) { return mock_reg_read(a, line); }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { mock_reg_write(a, v, line); }
#endif
#define REG_READ(a)      reg_read32((uintptr_t)(a), __LINE__)
#define REG_WRITE(a, v)  reg_write32((uintptr_t)(a), (uint32_t)(v), __LINE__)

/* ---------- 줄인 mock ---------- */
typedef struct { char op; uintptr_t addr; uint32_t val; int line; } reg_access_t;
typedef struct { char op; uintptr_t addr; uint32_t val; uint32_t mask; } reg_expect_t;
#define EXP_W(a, v) { 'W', (a), (v), 0xFFFFFFFFu }
#define EXP_R(a)    { 'R', (a), 0u, 0u }
static const uintptr_t k_addr[] = { SPI1_CR1, SPI1_CR2, SPI1_SR, SPI1_DR };
static const uint32_t  k_reset[] = { 0u, 0u, SPI_SR_TXE, 0u };
#define NREGS   4
#define LOG_MAX 32
static uint32_t     g_val[NREGS];
static reg_access_t g_log[LOG_MAX];
static int          g_nlog;
static bool         g_dr_read;          /* 직전 접근이 DR 읽기였나 (OVR 클리어 시퀀스 모델) */

static int reg_index(uintptr_t a) { for (int i = 0; i < NREGS; i++) if (k_addr[i] == a) return i; return -1; }
static void mock_reset(void) { for (int i = 0; i < NREGS; i++) g_val[i] = k_reset[i]; g_nlog = 0; g_dr_read = false; }
static void mock_set(uintptr_t a, uint32_t v) { int i = reg_index(a); if (i >= 0) g_val[i] = v; }
static uint32_t mock_peek(uintptr_t a) { int i = reg_index(a); return i < 0 ? 0u : g_val[i]; }
static void log_push(char op, uintptr_t a, uint32_t v, int line) {
    if (g_nlog < LOG_MAX) { g_log[g_nlog].op = op; g_log[g_nlog].addr = a; g_log[g_nlog].val = v; g_log[g_nlog].line = line; g_nlog++; }
}
static uint32_t mock_reg_read(uintptr_t a, int line) {
    const int i = reg_index(a);
    uint32_t v = i < 0 ? 0u : g_val[i];
    if (a == SPI1_SR && g_dr_read) g_val[i] &= ~SPI_SR_OVR;   /* RM0090: DR 읽기 → SR 읽기 = OVR 클리어 */
    g_dr_read = (a == SPI1_DR);
    log_push('R', a, v, line);
    return v;
}
static void mock_reg_write(uintptr_t a, uint32_t v, int line) {
    const int i = reg_index(a);
    g_dr_read = false;
    log_push('W', a, v, line);
    if (i >= 0 && a != SPI1_SR) g_val[i] = v;
}
static int mock_check_seq(const reg_expect_t *exp, int n) {          /* 본문의 간단판 (메시지 없음) */
    for (int k = 0; k < n || k < g_nlog; k++) {
        if (k >= n || k >= g_nlog) return k;
        if (g_log[k].op != exp[k].op || g_log[k].addr != exp[k].addr || ((g_log[k].val ^ exp[k].val) & exp[k].mask)) return k;
    }
    return -1;
}

/* ===== 연습 1 — 쓰기만 비교 =====
 * 로그의 'W' 항목만 순서대로 exp[0..n) (전부 EXP_W) 와 비교. 같으면 -1, 다르면 exp 쪽 인덱스.
 * 읽기(폴링 횟수, read-back)는 구현 세부라 바뀌어도 테스트가 깨지지 않는다. */
static int mock_check_writes(const reg_expect_t *exp, int n) {
    int e = 0;
    for (int k = 0; k < g_nlog; k++) {
        if (g_log[k].op != 'W') continue;
        if (e >= n) return n;                                    /* 기대보다 많이 썼다 */
        if (g_log[k].addr != exp[e].addr || ((g_log[k].val ^ exp[e].val) & exp[e].mask)) return e;
        e++;
    }
    return e == n ? -1 : e;                                      /* 덜 썼다 */
}
static void drv_poll_then_write(int busy_reads) {               /* 연습 1 용: 폴링 횟수만 다른 드라이버 */
    for (int k = 0; k < busy_reads; k++) (void)REG_READ(SPI1_SR);
    REG_WRITE(SPI1_CR1, 0x31Fu);
    REG_WRITE(SPI1_CR1, 0x35Fu);
}

/* ===== 연습 2 — 최종 상태로는 못 잡는 순서 버그 =====
 * 규칙: CR1 에 SPE=1 을 처음 쓴 뒤에는 CR2 쓰기가 없어야 한다 ("켜는 건 마지막"). SPE 쓰기가 없으면 false. */
static bool spe_last_ok(void) {
    int on = -1;
    for (int k = 0; k < g_nlog && on < 0; k++)
        if (g_log[k].op == 'W' && g_log[k].addr == SPI1_CR1 && (g_log[k].val & SPI_CR1_SPE)) on = k;
    if (on < 0) return false;
    for (int k = on + 1; k < g_nlog; k++)
        if (g_log[k].op == 'W' && g_log[k].addr == SPI1_CR2) return false;
    return true;
}
static void init_good(void)  { REG_WRITE(SPI1_CR1, 0x31Fu); REG_WRITE(SPI1_CR2, SPI_CR2_ERRIE); REG_WRITE(SPI1_CR1, 0x35Fu); }
static void init_early(void) { REG_WRITE(SPI1_CR1, 0x35Fu); REG_WRITE(SPI1_CR2, SPI_CR2_ERRIE); }   /* ERRIE 전에 켬 */

/* ===== 연습 3 — 읽기 순서가 곧 명령: OVR 클리어 ===== */
static void spi_clear_ovr(void) {
    (void)REG_READ(SPI1_DR);     /* 값은 버리지만 "읽기" 자체가 하드웨어 동작이다 → 반드시 일어나야 함 */
    (void)REG_READ(SPI1_SR);
}
static void spi_clear_ovr_wrong(void) { (void)REG_READ(SPI1_SR); (void)REG_READ(SPI1_DR); }

/* ===== 연습 4 — 구조체 오버레이 + RAM 가짜 ===== */
typedef struct { volatile uint32_t CR1, CR2, SR, DR; } SPI_TypeDef;
_Static_assert(offsetof(SPI_TypeDef, SR) == 0x08u && offsetof(SPI_TypeDef, DR) == 0x0Cu, "RM0090 SPI map");
#if defined(REG_TARGET)
#  define SPI1 ((SPI_TypeDef *)SPI1_BASE)
#else
static SPI_TypeDef g_spi1_ram;
#  define SPI1 (&g_spi1_ram)
#endif
static void spi_enable_struct(SPI_TypeDef *s, uint32_t cr1, uint32_t cr2)       { s->CR1 = cr1; s->CR2 = cr2; s->CR1 = cr1 | SPI_CR1_SPE; }
static void spi_enable_struct_early(SPI_TypeDef *s, uint32_t cr1, uint32_t cr2) { s->CR1 = cr1 | SPI_CR1_SPE; s->CR2 = cr2; }
static void ram_reset(void) { g_spi1_ram.CR1 = 0u; g_spi1_ram.CR2 = 0u; g_spi1_ram.SR = SPI_SR_TXE; g_spi1_ram.DR = 0u; }
static int spi_wait_idle_struct(SPI_TypeDef *s, int max_polls) {
    for (int n = 0; n < max_polls; n++)
        if (!(s->SR & SPI_SR_BSY)) return 0;
    return -1;
}

/* ---------- 테스트 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

int main(void) {
    /* 연습 1 */
    const reg_expect_t w[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_W(SPI1_CR1, 0x35Fu) };
    mock_reset(); drv_poll_then_write(1);
    CHECK(mock_check_writes(w, NELEM(w)) == -1);
    mock_reset(); drv_poll_then_write(4);                        /* 폴링 횟수가 달라도 통과 */
    CHECK(mock_check_writes(w, NELEM(w)) == -1);
    const reg_expect_t full[] = { EXP_R(SPI1_SR), EXP_W(SPI1_CR1, 0x31Fu), EXP_W(SPI1_CR1, 0x35Fu) };
    CHECK(mock_check_seq(full, NELEM(full)) == 1);               /* 정확한 시퀀스 비교는 여기서 깨진다 */
    const reg_expect_t w_bad[] = { EXP_W(SPI1_CR1, 0x31Fu), EXP_W(SPI1_CR1, 0x34Fu) };
    CHECK(mock_check_writes(w_bad, NELEM(w_bad)) == 1);
    CHECK(mock_check_writes(w, 1) == 1);                         /* 더 썼다 */

    /* 연습 2 */
    mock_reset(); init_good();
    const uint32_t g_cr1 = mock_peek(SPI1_CR1), g_cr2 = mock_peek(SPI1_CR2);
    CHECK(spe_last_ok());
    mock_reset(); init_early();
    CHECK(mock_peek(SPI1_CR1) == g_cr1 && mock_peek(SPI1_CR2) == g_cr2);   /* 최종 상태는 똑같다 */
    CHECK(!spe_last_ok());                                                  /* 순서 규칙만 잡는다 */
    mock_reset(); REG_WRITE(SPI1_CR2, 0u);
    CHECK(!spe_last_ok());                                                  /* SPE 를 아예 안 켬 */

    /* 연습 3 */
    mock_reset(); mock_set(SPI1_SR, SPI_SR_TXE | SPI_SR_OVR);
    spi_clear_ovr();
    const reg_expect_t ovr[] = { EXP_R(SPI1_DR), EXP_R(SPI1_SR) };
    CHECK(mock_check_seq(ovr, NELEM(ovr)) == -1);
    CHECK(mock_peek(SPI1_SR) == SPI_SR_TXE);
    CHECK(g_log[1].val & SPI_SR_OVR);                            /* 그 SR 읽기는 OVR=1 을 본 뒤 지웠다 */
    mock_reset(); mock_set(SPI1_SR, SPI_SR_TXE | SPI_SR_OVR);
    spi_clear_ovr_wrong();
    CHECK(mock_peek(SPI1_SR) & SPI_SR_OVR);                      /* 순서가 틀리면 그대로 */

    /* 연습 4 */
    ram_reset();       /* volatile 객체라 memset 대신 필드별로 (C11 6.7.3p6) */
    spi_enable_struct(SPI1, 0x31Fu, SPI_CR2_ERRIE);
    const uint32_t r1 = SPI1->CR1, r2 = SPI1->CR2;
    ram_reset();
    spi_enable_struct_early(SPI1, 0x31Fu, SPI_CR2_ERRIE);
    CHECK(r1 == 0x35Fu && r2 == SPI_CR2_ERRIE);
    CHECK(SPI1->CR1 == r1 && SPI1->CR2 == r2);                   /* 한계: RAM 가짜는 순서 버그를 못 본다 */
    SPI1->SR = SPI_SR_BSY;
    CHECK(spi_wait_idle_struct(SPI1, 1000) == -1);               /* 한계: 시간이 흐르지 않아 BSY 가 안 풀린다 */
    SPI1->SR = SPI_SR_TXE;
    CHECK(spi_wait_idle_struct(SPI1, 1) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
