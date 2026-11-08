/* SIV-M12 C 테스트 라이브러리 + mock DUT ("SoC A0")
 *
 * Python 시퀀서(solution.py)가 같은 소스를 두 가지로 빌드해서 부른다.
 *   ctypes : cc -std=c11 -Wall -Wextra -Werror -O2 -dynamiclib siv_tests.c -o siv_tests.dylib
 *   binary : cc -std=c11 -Wall -Wextra -Werror -O2 siv_tests.c -o siv_tests_bin
 *            ./siv_tests_bin --run sram_march --params "words=1024" --vdd 720 --temp 105 --defects 0xF
 *            → 결과 JSON 한 줄, exit 0 PASS / 1 FAIL / 2 ERROR / 64 사용법 오류
 *            ./siv_tests_bin            (인자 없음) → 골든 조건 자체 점검: 6개 테스트 기본값 실행, "6/6 passed"
 *   (dylib 에도 main 심볼이 들어가지만 ctypes 는 부르지 않으므로 무해하다)
 *
 * 실리콘이 없으므로 DUT(ID 레지스터, SRAM, I2C, PLL, DMA)는 이 파일 안의 mock 이다.
 * defects 비트는 "불량 샘플"을 흉내 낸다 (siv_set_defects / --defects):
 *   bit0 SRAM_LV_HOT : vdd < 740 mV 이고 temp >= 100 C 일 때 SRAM word 0x2A7 의 bit13 stuck-at-0
 *   bit1 I2C_FLAKY   : 시도마다 약 40% 확률로 byte 3 에서 NACK (seed, attempt 로 결정적)
 *   bit2 PLL_COLD    : temp <= -20 C 에서 PLL 이 lock 되지 않음
 *   bit3 DMA_RING    : desc_count > 16 이면 엔진이 링 끝을 넘어 descriptor 를 fetch → 버스 폴트
 *                      (호스트에선 raise(SIGSEGV) 로 모델링: 실제 보드라면 data abort / hard fault)
 */
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIV_API_VERSION 0x00010000u /* 상위 16비트 = major. 구조체가 바뀌면 major 를 올린다 */

enum { SIV_PASS = 0, SIV_FAIL = 1, SIV_ERROR = 2 };

typedef struct {
    int32_t  status;   /* SIV_PASS / SIV_FAIL / SIV_ERROR */
    uint32_t checks;   /* 실행한 비교 개수 (0 이면 PASS 로 인정하지 않는다) */
    uint32_t addr;     /* 첫 실패의 주소 */
    uint32_t expected;
    uint32_t actual;
    char     reg[16];  /* 첫 실패의 레지스터/메모리 이름 */
    char     msg[96];
} siv_result_t;

/* ---- 공개 API (ctypes 가 부르는 심볼) ---- */
uint32_t    siv_api_version(void);
size_t      siv_result_size(void);
int         siv_test_count(void);
const char *siv_test_name(int i);
void        siv_set_env(int vdd_mv, int temp_c);
void        siv_set_defects(uint32_t mask);
int         siv_run(const char *func, const char *params, uint32_t seed, int attempt, siv_result_t *out);

#define DEF_SRAM_LV_HOT 0x1u
#define DEF_I2C_FLAKY   0x2u
#define DEF_PLL_COLD    0x4u
#define DEF_DMA_RING    0x8u

static struct { int vdd_mv; int temp_c; uint32_t defects; } g_env = { 800, 25, 0 };

uint32_t siv_api_version(void) { return SIV_API_VERSION; }
size_t   siv_result_size(void) { return sizeof(siv_result_t); }
void     siv_set_env(int vdd_mv, int temp_c) { g_env.vdd_mv = vdd_mv; g_env.temp_c = temp_c; }
void     siv_set_defects(uint32_t mask) { g_env.defects = mask; }

/* ------------------------------------------------------------ 결과 기록 */
static void res_fail(siv_result_t *r, const char *reg, uint32_t addr, uint32_t exp, uint32_t act,
                     const char *msg)
{
    if (r->status != SIV_PASS) return; /* 첫 실패만 남긴다 (뒤 실패는 대개 연쇄) */
    r->status = SIV_FAIL;
    r->addr = addr;
    r->expected = exp;
    r->actual = act;
    snprintf(r->reg, sizeof r->reg, "%s", reg);
    snprintf(r->msg, sizeof r->msg, "%s", msg);
}

static int res_error(siv_result_t *r, const char *msg)
{
    r->status = SIV_ERROR;
    snprintf(r->msg, sizeof r->msg, "%s", msg);
    return SIV_ERROR;
}

static int check_eq(siv_result_t *r, const char *reg, uint32_t addr, uint32_t exp, uint32_t act,
                    const char *msg)
{
    r->checks++;
    if (exp == act) return 1;
    res_fail(r, reg, addr, exp, act, msg);
    return 0;
}

/* ------------------------------------------------------------ 파라미터 "k=v;k=v" */
typedef struct { const char *key; uint32_t val; } param_t;

static int parse_params(const char *s, param_t *p, int n, siv_result_t *r)
{
    char buf[256];
    if (strlen(s) >= sizeof buf) return res_error(r, "params too long");
    strcpy(buf, s);
    char *tok = buf;
    while (*tok) {
        char *end = strchr(tok, ';');
        if (end) *end = '\0';
        char *eq = strchr(tok, '=');
        if (!eq) {
            snprintf(r->msg, sizeof r->msg, "bad param token '%.40s'", tok);
            r->status = SIV_ERROR;
            return SIV_ERROR;
        }
        *eq = '\0';
        int i;
        for (i = 0; i < n && strcmp(p[i].key, tok) != 0; i++) {}
        if (i == n) {
            snprintf(r->msg, sizeof r->msg, "unknown param '%.40s'", tok);
            r->status = SIV_ERROR;
            return SIV_ERROR;
        }
        char *num_end = NULL;
        unsigned long v = strtoul(eq + 1, &num_end, 0);
        if (eq[1] == '\0' || *num_end != '\0' || v > 0xFFFFFFFFul) {
            snprintf(r->msg, sizeof r->msg, "bad value for '%.40s'", tok);
            r->status = SIV_ERROR;
            return SIV_ERROR;
        }
        p[i].val = (uint32_t)v;
        if (!end) break;
        tok = end + 1;
    }
    return SIV_PASS;
}

static uint32_t xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* ------------------------------------------------------------ 테스트 1: ID */
#define ID_ADDR 0x40000000u
#define CHIP_ID 0x5A1000A0u /* part 0x5A10, stepping A0 */

static int t_id_check(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    param_t p[] = { { "expect_id", CHIP_ID } };
    (void)seed;
    (void)attempt;
    if (parse_params(params, p, 1, r)) return r->status;
    check_eq(r, "ID", ID_ADDR, p[0].val, CHIP_ID, "chip ID mismatch");
    return r->status;
}

/* ------------------------------------------------------------ 테스트 2: 리셋값 */
static const struct { const char *name; uint32_t addr, reset; } k_reset_tab[] = {
    { "CLK_CTRL", 0x40001000u, 0x00000001u },
    { "PLL_CFG",  0x40001008u, 0x00140A01u },
    { "I2C_CTRL", 0x40003000u, 0x00000000u },
    { "DMA_CTRL", 0x40004000u, 0x00000010u },
};

static uint32_t mock_reg_read(uint32_t addr)
{
    for (size_t i = 0; i < sizeof k_reset_tab / sizeof k_reset_tab[0]; i++)
        if (k_reset_tab[i].addr == addr) return k_reset_tab[i].reset; /* 리셋 직후 값 */
    return 0xDEADBEEFu;
}

static int t_reg_reset(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    (void)seed;
    (void)attempt;
    if (parse_params(params, NULL, 0, r)) return r->status;
    for (size_t i = 0; i < sizeof k_reset_tab / sizeof k_reset_tab[0]; i++)
        check_eq(r, k_reset_tab[i].name, k_reset_tab[i].addr, k_reset_tab[i].reset,
                 mock_reg_read(k_reset_tab[i].addr), "reset value mismatch");
    return r->status;
}

/* ------------------------------------------------------------ 테스트 3: SRAM March C- */
#define SRAM_BASE  0x20000000u
#define SRAM_WORDS 4096u
static uint32_t g_sram[SRAM_WORDS];

static int sram_defect_active(void)
{
    return (g_env.defects & DEF_SRAM_LV_HOT) && g_env.vdd_mv < 740 && g_env.temp_c >= 100;
}

static void sram_wr(uint32_t i, uint32_t v)
{
    if (i == 0x2A7u && sram_defect_active()) v &= ~(1u << 13); /* 약한 셀: 저전압·고온에서 1을 못 씀 */
    g_sram[i] = v;
}

static int sram_rd_check(siv_result_t *r, uint32_t i, uint32_t exp, const char *msg)
{
    return check_eq(r, "SRAM", SRAM_BASE + 4u * i, exp, g_sram[i], msg);
}

static int t_sram_march(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    param_t p[] = { { "words", 1024u } };
    (void)seed;
    (void)attempt;
    if (parse_params(params, p, 1, r)) return r->status;
    uint32_t n = p[0].val;
    if (n == 0 || n > SRAM_WORDS) return res_error(r, "words out of range 1..4096");
    const uint32_t D0 = 0x00000000u, D1 = 0xFFFFFFFFu; /* SIV-B07 March C- (word 단위 배경) */
    for (uint32_t i = 0; i < n; i++) sram_wr(i, D0);
    for (uint32_t i = 0; i < n; i++) { sram_rd_check(r, i, D0, "march M1 r0"); sram_wr(i, D1); }
    for (uint32_t i = 0; i < n; i++) { sram_rd_check(r, i, D1, "march M2 r1"); sram_wr(i, D0); }
    for (uint32_t i = n; i-- > 0;)   { sram_rd_check(r, i, D0, "march M3 r0"); sram_wr(i, D1); }
    for (uint32_t i = n; i-- > 0;)   { sram_rd_check(r, i, D1, "march M4 r1"); sram_wr(i, D0); }
    for (uint32_t i = 0; i < n; i++) sram_rd_check(r, i, D0, "march M5 r0");
    return r->status;
}

/* ------------------------------------------------------------ 테스트 4: I2C 루프백 */
#define I2C_STATUS 0x40003004u
#define I2C_RXDATA 0x40003008u
#define I2C_NACK   0x4u

static int t_i2c_loopback(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    param_t p[] = { { "bytes", 8u } };
    if (parse_params(params, p, 1, r)) return r->status;
    uint32_t n = p[0].val;
    if (n == 0 || n > 256) return res_error(r, "bytes out of range 1..256");
    int nack_at = -1;
    if (g_env.defects & DEF_I2C_FLAKY) {
        uint32_t x = xorshift32(seed ^ (0x9E3779B9u * (uint32_t)(attempt + 1)));
        if (x % 100u < 40u) nack_at = 3;
    }
    for (uint32_t b = 0; b < n; b++) {
        uint8_t tx = (uint8_t)(0xA5u ^ (b * 7u));
        if ((int)b == nack_at) {
            char m[48];
            snprintf(m, sizeof m, "NACK at byte %u", b);
            check_eq(r, "I2C_STATUS", I2C_STATUS, 0, I2C_NACK, m);
            return r->status;
        }
        check_eq(r, "I2C_RXDATA", I2C_RXDATA, tx, tx, "loopback data");
    }
    return r->status;
}

/* ------------------------------------------------------------ 테스트 5: PLL lock poll */
#define PLL_STATUS 0x40001004u
static volatile uint32_t g_pll_status;
static uint32_t g_pll_polls;

static uint32_t pll_read(void)
{
    g_pll_polls++;
    int cold_fail = (g_env.defects & DEF_PLL_COLD) && g_env.temp_c <= -20;
    if (!cold_fail && g_pll_polls >= 37u) g_pll_status = 1u; /* 정상: 37번째 읽기에서 LOCK */
    return g_pll_status;
}

static int t_pll_lock(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    param_t p[] = { { "max_polls", 1000u } }; /* 0 = 무제한 (디버그용 설정 — 행의 원인) */
    (void)seed;
    (void)attempt;
    if (parse_params(params, p, 1, r)) return r->status;
    g_pll_status = 0;
    g_pll_polls = 0;
    uint32_t n = 0;
    while (!(pll_read() & 1u)) {
        if (p[0].val != 0 && ++n >= p[0].val) {
            char m[48];
            snprintf(m, sizeof m, "no LOCK after %u polls", (unsigned)n);
            check_eq(r, "PLL_STATUS", PLL_STATUS, 1u, g_pll_status, m);
            return r->status;
        }
    }
    check_eq(r, "PLL_STATUS", PLL_STATUS, 1u, g_pll_status & 1u, "LOCK");
    return r->status;
}

/* ------------------------------------------------------------ 테스트 6: DMA 디스크립터 링 */
#define DMA_XFER_CNT 0x40004010u
#define DMA_RING     16u
typedef struct { uint32_t len, next; } desc_t;

static int t_dma_desc(const char *params, uint32_t seed, int attempt, siv_result_t *r)
{
    param_t p[] = { { "desc_count", 8u } };
    (void)seed;
    (void)attempt;
    if (parse_params(params, p, 1, r)) return r->status;
    uint32_t n = p[0].val;
    if (n == 0 || n > 1024) return res_error(r, "desc_count out of range 1..1024");
    desc_t ring[DMA_RING];
    for (uint32_t i = 0; i < DMA_RING; i++) ring[i] = (desc_t){ 64u + 4u * i, (i + 1u) % DMA_RING };
    uint32_t idx = 0, total = 0, expect = 0;
    for (uint32_t k = 0; k < n; k++) {
        if ((g_env.defects & DEF_DMA_RING) && k >= DMA_RING)
            raise(SIGSEGV); /* 링 끝 다음 주소(매핑 안 됨)를 fetch → 버스 폴트 모델 */
        total += ring[idx].len;
        expect += 64u + 4u * (k % DMA_RING);
        idx = ring[idx].next;
    }
    check_eq(r, "DMA_XFER_CNT", DMA_XFER_CNT, expect, total, "transferred byte count");
    return r->status;
}

/* ------------------------------------------------------------ 디스패처 */
typedef int (*test_fn)(const char *params, uint32_t seed, int attempt, siv_result_t *r);
static const struct { const char *name; test_fn fn; } k_tests[] = {
    { "id_check", t_id_check },     { "reg_reset", t_reg_reset }, { "sram_march", t_sram_march },
    { "i2c_loopback", t_i2c_loopback }, { "pll_lock", t_pll_lock }, { "dma_desc", t_dma_desc },
};
#define N_TESTS ((int)(sizeof k_tests / sizeof k_tests[0]))

int siv_test_count(void) { return N_TESTS; }
const char *siv_test_name(int i) { return (i >= 0 && i < N_TESTS) ? k_tests[i].name : NULL; }

int siv_run(const char *func, const char *params, uint32_t seed, int attempt, siv_result_t *out)
{
    if (!out) return SIV_ERROR;
    memset(out, 0, sizeof *out);
    if (!func) return res_error(out, "func is NULL");
    if (g_env.vdd_mv < 500) {
        snprintf(out->msg, sizeof out->msg, "DUT not powered (vdd=%d mV)", g_env.vdd_mv);
        out->status = SIV_ERROR;
        return SIV_ERROR;
    }
    for (int i = 0; i < N_TESTS; i++) {
        if (strcmp(k_tests[i].name, func) != 0) continue;
        int st = k_tests[i].fn(params ? params : "", seed, attempt, out);
        if (st == SIV_PASS && out->checks == 0) return res_error(out, "no checks executed");
        return st;
    }
    snprintf(out->msg, sizeof out->msg, "unknown test '%.40s'", func);
    out->status = SIV_ERROR;
    return SIV_ERROR;
}

/* ------------------------------------------------------------ 바이너리 러너용 main */
static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static int num_arg(const char *s, long *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (*s == '\0' || *end != '\0') return 0;
    *out = v;
    return 1;
}

int main(int argc, char **argv)
{
    const char *run = NULL, *params = "";
    long seed = 0, attempt = 0, vdd = 800, temp = 25, defects = 0;
    int list = 0;
    for (int i = 1; i < argc; i++) {
        int has = i + 1 < argc, ok = 1;
        if (!strcmp(argv[i], "--list")) list = 1;
        else if (has && !strcmp(argv[i], "--run")) run = argv[++i];
        else if (has && !strcmp(argv[i], "--params")) params = argv[++i];
        else if (has && !strcmp(argv[i], "--seed")) ok = num_arg(argv[++i], &seed);
        else if (has && !strcmp(argv[i], "--attempt")) ok = num_arg(argv[++i], &attempt);
        else if (has && !strcmp(argv[i], "--vdd")) ok = num_arg(argv[++i], &vdd);
        else if (has && !strcmp(argv[i], "--temp")) ok = num_arg(argv[++i], &temp);
        else if (has && !strcmp(argv[i], "--defects")) ok = num_arg(argv[++i], &defects);
        else ok = 0;
        if (!ok) {
            fprintf(stderr, "usage: %s --list | --run NAME [--params k=v;..] [--seed N] [--attempt N]"
                            " [--vdd MV] [--temp C] [--defects MASK]\n", argv[0]);
            return 64;
        }
    }
    if (argc == 1) { /* 자체 점검: 골든 샘플, 공칭 조건, 파라미터 기본값 */
        int ok = 0;
        for (int i = 0; i < N_TESTS; i++) {
            siv_result_t r;
            int st = siv_run(k_tests[i].name, "", 1u, 0, &r);
            printf("%s  %-13s checks=%u %s\n", st == SIV_PASS ? "PASS" : "FAIL", k_tests[i].name,
                   (unsigned)r.checks, r.msg);
            ok += st == SIV_PASS;
        }
        printf("%d/%d passed\n", ok, N_TESTS);
        return ok == N_TESTS ? 0 : 1;
    }
    if (list) {
        for (int i = 0; i < N_TESTS; i++) puts(k_tests[i].name);
        return 0;
    }
    if (!run) {
        fprintf(stderr, "%s: --run NAME required\n", argv[0]);
        return 64;
    }
    siv_set_env((int)vdd, (int)temp);
    siv_set_defects((uint32_t)defects);
    siv_result_t r;
    int st = siv_run(run, params, (uint32_t)seed, (int)attempt, &r);
    static const char *const names[] = { "PASS", "FAIL", "ERROR" };
    printf("{\"status\":\"%s\",\"checks\":%u,\"reg\":", names[st], (unsigned)r.checks);
    json_str(r.reg);
    printf(",\"addr\":%u,\"expected\":%u,\"actual\":%u,\"msg\":", (unsigned)r.addr, (unsigned)r.expected,
           (unsigned)r.actual);
    json_str(r.msg);
    printf("}\n");
    fflush(stdout);
    return st;
}
