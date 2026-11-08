/* SIV-B08 검증 테스트 프레임워크 골격
 *
 * 수백 개의 실리콘 검증 테스트를 "등록 → 이름 필터로 골라 실행 → 결과 집계 → JSON 로그" 하는 경량 C
 * 프레임워크. 등록은 TEST() 매크로가 만드는 constructor 함수가 main 전에 레지스트리에 자기를 넣는
 * 방식이다 (clang/gcc, Mach-O/ELF 공통). linker section 방식은 md 와 demo_section.c 참고.
 *
 *   [프레임워크]  Part A 등록·체크·한 테스트 실행 / Part B 필터·정렬·JSON 이스케이프 / Part C 로그·러너
 *   [축약 mock]   SIV-B02 mock MMIO 를 축약 (비트별 access 마스크) + SRAM 모델 + 결함 주입 fault_cfg
 *   [이식 테스트] B03 리셋값 · B04 R/W 패턴 · B05 walking 1/0 · B06 주소 alias · B07 March C-
 *   [채점 하네스] CHECK(expr) = 이 레슨 채점용. 프레임워크의 CHECK_EQ / CHECK_MASK 와는 다른 것이다.
 *
 *   ./solution                                  레슨 자체 테스트 (N/M passed)
 *   ./solution --run 'REG.*'                    실제 실행: JSON 로그 → stdout, 실패 한 줄 → stderr
 *   ./solution --run '*' --fault reset|stuck|w1c|alias|cf   결함을 주입하고 실행 (exit 0/1/2)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ======================= 프레임워크: 자료구조 ======================= */
#define MAX_TESTS    128
#define MAX_FAIL_REC 4                 /* 테스트당 상세 기록할 실패 수 (나머지는 개수만 센다) */

typedef struct test_ctx test_ctx;
typedef void (*test_fn)(test_ctx *t);

typedef struct {
    const char *name;                  /* "SUITE.case" — 필터·정렬·로그의 키 */
    test_fn     fn;
    const char *file;
    int         line;
} test_case;

typedef struct {
    const test_case *tests[MAX_TESTS];
    int n;
    int overflow;                      /* 자리가 없어 등록 못 한 수 — 조용히 버리면 안 된다 */
} test_registry;

static test_registry g_registry;       /* TEST() 의 constructor 가 main 전에 채운다 */

typedef enum { ST_PASS, ST_FAIL, ST_SKIP } test_status;
static const char *const k_status[] = { "PASS", "FAIL", "SKIP" };

typedef struct {
    const char *what;                  /* 레지스터/메모리 이름 */
    uint32_t addr, mask, exp, act;
    const char *file;
    int line;
} fail_rec;

struct test_ctx {
    const test_case *tc;
    test_status status;
    int nchecks, nfails;
    fail_rec fails[MAX_FAIL_REC];
    char reason[96];                   /* SKIP 이유 / 진단 메모 (첫 메모가 남는다) */
    bool verbose;                      /* 실패를 사람이 읽는 한 줄로 stderr 에도 출력 */
};

typedef struct {
    char   buf[8192];
    size_t len;
    bool   truncated;
    FILE  *echo;                       /* NULL 이 아니면 같은 내용을 여기에도 쓴다 */
} logbuf;

__attribute__((format(printf, 2, 3)))
static void log_printf(logbuf *lb, const char *fmt, ...) {
    va_list ap;
    if (lb->echo) { va_start(ap, fmt); vfprintf(lb->echo, fmt, ap); va_end(ap); }
    size_t room = sizeof lb->buf - lb->len;
    va_start(ap, fmt);
    int n = vsnprintf(lb->buf + lb->len, room, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= room) { lb->len = sizeof lb->buf - 1; lb->truncated = true; }
    else lb->len += (size_t)n;
}

/* diff 의 1 비트 번호를 "[0,5,31]" 처럼 (8개 넘으면 ",...") */
static void fmt_bits(uint32_t diff, char *out, size_t cap) {
    size_t n = (size_t)snprintf(out, cap, "[");
    int shown = 0;
    for (int b = 0; b < 32 && n < cap; b++) {
        if (!((diff >> b) & 1u)) continue;
        if (shown == 8) { n += (size_t)snprintf(out + n, cap - n, ",..."); break; }
        n += (size_t)snprintf(out + n, cap - n, "%s%d", shown ? "," : "", b);
        shown++;
    }
    if (n < cap) snprintf(out + n, cap - n, "]");
}

static void print_fail(const char *test, const fail_rec *f) {
    char bits[48];
    uint32_t diff = (f->exp ^ f->act) & f->mask;
    fmt_bits(diff, bits, sizeof bits);
    fprintf(stderr, "FAIL %s reg=%s addr=0x%08X exp=0x%08X act=0x%08X diff=0x%08X bits=%s (%s:%d)\n",
            test, f->what, (unsigned)f->addr, (unsigned)f->exp, (unsigned)f->act, (unsigned)diff,
            bits, f->file, f->line);
}

__attribute__((format(printf, 2, 3)))
static void test_note(test_ctx *t, const char *fmt, ...) {
    if (t->reason[0]) return;          /* 첫 진단이 가장 쓸모 있다 */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->reason, sizeof t->reason, fmt, ap);
    va_end(ap);
}

/* ---------------- Part A: 등록 · 체크 · 한 테스트 실행 ---------------- */
/* 레지스트리 끝에 tc 를 붙인다. 꽉 찼으면 overflow 를 세고 -1. */
static int test_register(test_registry *r, const test_case *tc) {
    (void)r; (void)tc;
    return 0; /* TODO: 끝에 붙이기, 꽉 찼으면 overflow++ 후 -1 */
}

/* TEST(REG, reset_values) { ... }  →  본문 함수 + test_case + main 전에 도는 등록 함수 */
#define TEST(suite, name)                                                            \
    static void siv_##suite##__##name(test_ctx *t);                                  \
    static const test_case siv_##suite##__##name##_tc =                              \
        { #suite "." #name, siv_##suite##__##name, __FILE__, __LINE__ };             \
    __attribute__((constructor)) static void siv_##suite##__##name##_reg(void) {     \
        test_register(&g_registry, &siv_##suite##__##name##_tc);                     \
    }                                                                                \
    static void siv_##suite##__##name(__attribute__((unused)) test_ctx *t)

/* (act & mask) == (exp & mask) 이면 true. 아니면 실패를 기록(최대 MAX_FAIL_REC 개)하고 FAIL 로. */
static bool check_mask(test_ctx *t, const char *what, uint32_t addr, uint32_t mask,
                       uint32_t exp, uint32_t act, const char *file, int line) {
    (void)t; (void)what; (void)addr; (void)mask; (void)exp; (void)act; (void)file; (void)line;
    (void)print_fail;
    return true; /* TODO: nchecks++, 마스크 안 차이가 있으면 기록·FAIL·verbose 출력 후 false */
}

#define CHECK_MASK(what, addr, mask, exp, act) \
    check_mask(t, (what), (addr), (mask), (exp), (act), __FILE__, __LINE__)
#define CHECK_EQ(what, addr, exp, act) CHECK_MASK(what, addr, 0xFFFFFFFFu, exp, act)
#define TEST_NOTE(...) test_note(t, __VA_ARGS__)
#define TEST_SKIP(...)                                             \
    do { test_note(t, __VA_ARGS__);                                \
         if (t->status != ST_FAIL) t->status = ST_SKIP; return; } while (0)

typedef struct {
    const char *filter;                /* NULL 또는 "" = 전부 */
    uint64_t  (*now_us)(void);         /* 시계 주입: 실제 = CLOCK_MONOTONIC, 테스트 = 가짜 시계 */
    void      (*before_each)(void);    /* 테스트 격리: 매 테스트 전에 IP 리셋 */
    logbuf     *log;
    bool        verbose;
} run_cfg;

/* t 를 초기화하고 before_each → (시간 측정) tc->fn → 소요 시간을 돌려준다.
 * 체크를 하나도 안 했는데 PASS 면 FAIL 로 바꾸고 reason 에 "no checks" 를 남긴다 (vacuous pass 금지). */
static uint64_t run_one(const run_cfg *cfg, const test_case *tc, test_ctx *t) {
    (void)cfg; (void)tc; (void)t;
    return 0; /* TODO: t 초기화 → before_each → 시간 측정하며 fn 실행 → vacuous pass 처리 */
}

/* ---------------- Part B: 이름 필터 · 정렬/중복 검사 · JSON 이스케이프 ---------------- */
/* glob: '*' = 임의 길이, '?' = 한 글자. 패턴은 p[0..plen) (NUL 로 안 끝날 수 있다). */
static bool glob_match(const char *p, size_t plen, const char *s) {
    (void)p; (void)plen; (void)s;
    return false; /* TODO: * 와 ? (별 위치를 기억했다가 되돌아가기) */
}

/* "REG.*:MEM.*:-*.slow" — ':' 로 나눈 glob 들. '-' 로 시작하면 제외 패턴.
 * 포함 패턴이 하나도 없으면 전부 포함. NULL/"" = 전부. */
static bool filter_match(const char *filter, const char *name) {
    (void)filter; (void)name; (void)glob_match;
    return false; /* TODO: ':' 로 나누고, '-' 는 제외 패턴 */
}

static int cmp_test(const void *a, const void *b) {
    const test_case *x = *(const test_case *const *)a, *y = *(const test_case *const *)b;
    return strcmp(x->name, y->name);
}

/* 이름순 정렬(실행 순서를 링크 순서와 무관하게 고정). overflow 면 -2, 같은 이름이 있으면 *dup 에
 * 그 이름을 넣고 -1, 정상이면 0. */
static int registry_finalize(test_registry *r, const char **dup) {
    (void)r; (void)cmp_test;
    *dup = NULL;
    return 0; /* TODO: overflow → -2, qsort 이름순, 이웃끼리 같으면 -1 */
}

/* src 를 JSON 문자열 내용으로 이스케이프해 dst(cap 바이트)에. " \ → \" \\, \n \t, 나머지 제어문자
 * (< 0x20) → \u00XX. 공간이 모자라면 이스케이프를 반으로 자르지 말고 거기서 멈춘다. 항상 NUL 종료.
 * 쓴 글자 수(NUL 제외)를 돌려준다. */
static size_t json_escape(char *dst, size_t cap, const char *src) {
    (void)src;
    if (cap) dst[0] = '\0';
    return 0; /* TODO */
}

/* ---------------- Part C: JSON 결과 로그 · 러너 ---------------- */
/* 한 줄 = 한 JSON 객체 (JSON Lines). 예:
 * {"type":"test","test":"REG.reset_values","status":"FAIL","duration_us":7,"checks":6,"fails":1,
 *  "reg":"LOAD","addr":"0x40010008","expected":"0x0000FFFF","actual":"0x0000FEFF","mask":"0xFFFFFFFF",
 *  "file":"solution.c","line":321}
 * 실패가 있으면 첫 실패의 reg..line 을, reason 이 있으면 "reason" 을 덧붙인다. */
static void emit_result_json(logbuf *lb, const test_ctx *t, uint64_t dur_us) {
    (void)lb; (void)t; (void)dur_us; (void)k_status; (void)log_printf;
    /* TODO: 한 줄 JSON — test/status/duration_us/checks/fails (+첫 실패 reg..line, +reason) */
}

typedef struct { int registered, matched, pass, fail, skip; } run_summary;

/* finalize → run 헤더 줄 → 필터에 맞는 테스트를 이름순으로 실행·기록 → summary 줄.
 * 종료 코드: 0 = 실패 없음(SKIP 허용), 1 = FAIL 있음, 2 = 설정 오류(중복/overflow/필터에 맞는 테스트 0개). */
static int run_tests(const run_cfg *cfg, test_registry *r, run_summary *s) {
    (void)cfg; (void)r; (void)emit_result_json; (void)filter_match;
    memset(s, 0, sizeof *s);
    return 1; /* TODO: finalize → run 헤더 → 필터·실행·기록 → summary → 0/1/2 */
}

/* ======================= 축약 mock (SIV-B02 의 mock 을 줄여 넣었다) ======================= */
#define TMR0_BASE  0x40010000u
#define SRAM_BASE  0x20000000u
#define SRAM_WORDS 64

/* 비트별 access 마스크 (SIV-B01 기술서 축약). 어느 마스크에도 없는 비트 = 예약: 0으로 읽힘, 쓰기 무시.
 * vol = 하드웨어가 바꾸는 비트 → 값 비교에서 제외 */
typedef struct { const char *name; uint32_t off, reset, rw, w1c, ro, vol; } reg_desc;
static const reg_desc k_tmr0[] = {
    /* name       off     reset        rw           w1c   ro           vol         */
    { "ID",      0x000, 0x54310001u, 0,           0,    0xFFFFFFFFu, 0           },
    { "CTRL",    0x004, 0x00000000u, 0x000000FFu, 0,    0,           0           },
    { "LOAD",    0x008, 0x0000FFFFu, 0xFFFFFFFFu, 0,    0,           0           },
    { "COMPARE", 0x00C, 0x00000000u, 0xFFFFFFFFu, 0,    0,           0           },
    { "STATUS",  0x010, 0x00000000u, 0,           0xFu, 0x100u,      0x100u      },
    { "COUNT",   0x014, 0x00000000u, 0,           0,    0xFFFFFFFFu, 0xFFFFFFFFu },
    { "SCRATCH", 0x01C, 0x00000000u, 0xFFFFFFFFu, 0,    0,           0           },
};
#define NREGS (sizeof k_tmr0 / sizeof k_tmr0[0])

typedef struct {
    uint32_t reset_off, reset_xor;     /* B03: 리셋값 tie-off 오류 (그 레지스터의 리셋값 ^= xor)    */
    uint32_t stuck_off, stuck1, stuck0;/* B05: stuck-at-1 / stuck-at-0 비트                        */
    bool     w1c_as_rw;                /* B04: W1C 비트가 RW 로 구현됨                               */
    uint32_t ignore_addr_bit;          /* B06: 주소 디코더가 이 비트를 무시 → alias                  */
    int      cf_aggr, cf_victim;       /* B07: SRAM coupling — aggr 워드에 0→1 전이가 있으면 victim=1 */
} fault_cfg;

static fault_cfg g_fault = { .cf_aggr = -1, .cf_victim = -1 };
static uint32_t  g_cell[8];            /* 레지스터 저장소 (off >> 2) */
static uint32_t  g_sram[SRAM_WORDS];
static uint64_t  g_fake_us;            /* 가짜 시계: 버스 접근 1회 = 1us */
static bool      g_dma_present;        /* 이 SKU 에 DMA 블록이 있나 (없음 → SKIP) */

static const reg_desc *desc_at(uint32_t off) {
    for (size_t i = 0; i < NREGS; i++) if (k_tmr0[i].off == off) return &k_tmr0[i];
    return NULL;
}
static uint32_t apply_stuck(uint32_t off, uint32_t v) {
    return off == g_fault.stuck_off ? (v | g_fault.stuck1) & ~g_fault.stuck0 : v;
}
static void mock_reset(void) {
    memset(g_cell, 0, sizeof g_cell);
    for (size_t i = 0; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        uint32_t v = d->reset ^ (d->off == g_fault.reset_off ? g_fault.reset_xor : 0u);
        g_cell[d->off >> 2] = apply_stuck(d->off, v);
    }
    for (int i = 0; i < SRAM_WORDS; i++) g_sram[i] = 0xA5A5A5A5u;   /* 전원 인가 직후 쓰레기 */
}
static uint32_t reg_read32(uint32_t off) {
    g_fake_us++;
    off &= ~g_fault.ignore_addr_bit;
    const reg_desc *d = desc_at(off);
    if (!d) return 0;
    uint32_t v = g_cell[off >> 2];
    return (v & ~d->vol) | ((uint32_t)g_fake_us & d->vol);   /* volatile 비트는 시간 따라 변함 */
}
static void reg_write32(uint32_t off, uint32_t val) {
    g_fake_us++;
    off &= ~g_fault.ignore_addr_bit;
    const reg_desc *d = desc_at(off);
    if (!d) return;
    uint32_t cur = g_cell[off >> 2];
    uint32_t nv = (cur & ~d->rw) | (val & d->rw);
    if (g_fault.w1c_as_rw) nv = (nv & ~d->w1c) | (val & d->w1c);
    else                   nv &= ~(val & d->w1c);
    g_cell[off >> 2] = apply_stuck(off, nv & (d->rw | d->w1c | d->ro));
}
static uint32_t sram_read(int i) { g_fake_us++; return g_sram[i]; }
static void sram_write(int i, uint32_t v) {
    g_fake_us++;
    uint32_t old = g_sram[i];
    g_sram[i] = v;
    if (i == g_fault.cf_aggr && (~old & v)) g_sram[g_fault.cf_victim] = 0xFFFFFFFFu;
}
static uint64_t fake_now_us(void) { return g_fake_us; }

/* ======================= 이식한 테스트 (B03~B07) ======================= */
/* B04 축약: 현재값 cur 인 레지스터에 w 를 쓴 뒤의 기대값 */
static uint32_t expect_after_write(const reg_desc *d, uint32_t cur, uint32_t w) {
    uint32_t v = (cur & ~d->rw) | (w & d->rw);
    v &= ~(w & d->w1c);
    return v & (d->rw | d->w1c | d->ro);
}
/* B06: 레지스터마다 다른 패턴 (주소의 함수) */
static uint32_t addr_pattern(uint32_t off) { return (off * 0x01010101u) ^ 0xC3C3C3C3u; }

TEST(REG, reset_values) {                              /* B03: volatile 제외, 예약 비트는 0 */
    for (size_t i = 0; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        if (~d->vol == 0) continue;                    /* 전부 volatile → 비교할 게 없다 */
        CHECK_MASK(d->name, TMR0_BASE + d->off, ~d->vol, d->reset, reg_read32(d->off));
    }
}

TEST(REG, rw_patterns) {                               /* B04: access type 별 기대값 */
    static const uint32_t pats[] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };
    for (size_t i = 0; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        if (~d->vol == 0) continue;
        uint32_t model = d->reset;
        for (size_t k = 0; k < sizeof pats / sizeof pats[0]; k++) {
            reg_write32(d->off, pats[k]);
            model = expect_after_write(d, model, pats[k]);
            CHECK_MASK(d->name, TMR0_BASE + d->off, ~d->vol, model, reg_read32(d->off));
        }
    }
}

TEST(REG, walking_bits) {                              /* B05: walking 1 + walking 0, RW 비트만 */
    for (size_t i = 0; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        for (int b = 0; b < 32; b++) {
            uint32_t m = 1u << b;
            if (!(d->rw & m)) continue;
            reg_write32(d->off, m);
            CHECK_MASK(d->name, TMR0_BASE + d->off, d->rw, m, reg_read32(d->off));
            reg_write32(d->off, ~m);
            CHECK_MASK(d->name, TMR0_BASE + d->off, d->rw, ~m, reg_read32(d->off));
        }
    }
}

/* B06: ① 모든 RW 레지스터에 addr_pattern(off) 를 쓰고 ② 전부 다시 읽어 비교한다 (중간에 읽지 않는다).
 * 틀린 값이 다른 레지스터의 패턴과 같으면 TEST_NOTE 로 "X@0x.. reads Y@0x.. pattern: addr bit N ignored?"
 * 를 남긴다 (두 오프셋의 XOR 이 1비트일 때 N = 그 비트 번호). */
TEST(REG, addr_alias) {
    (void)addr_pattern; /* TODO: 전부 쓰고 → 전부 읽기. 틀리면 누구 패턴인지 찾아 TEST_NOTE */
}

#define MARCH_R(i, e) CHECK_EQ("SRAM", SRAM_BASE + 4u * (uint32_t)(i), (e), sram_read(i))
TEST(MEM, march_c_minus) {                             /* B07: ⇑w0 ⇑r0w1 ⇑r1w0 ⇓r0w1 ⇓r1w0 ⇑r0 */
    const uint32_t z = 0, o = 0xFFFFFFFFu;
    for (int i = 0; i < SRAM_WORDS; i++) sram_write(i, z);
    for (int i = 0; i < SRAM_WORDS; i++) { MARCH_R(i, z); sram_write(i, o); }
    for (int i = 0; i < SRAM_WORDS; i++) { MARCH_R(i, o); sram_write(i, z); }
    for (int i = SRAM_WORDS - 1; i >= 0; i--) { MARCH_R(i, z); sram_write(i, o); }
    for (int i = SRAM_WORDS - 1; i >= 0; i--) { MARCH_R(i, o); sram_write(i, z); }
    for (int i = 0; i < SRAM_WORDS; i++) MARCH_R(i, z);
}

TEST(DMA, reset_values) {
    if (!g_dma_present) TEST_SKIP("DMA block fused off (fuse \"DMA_EN\"=0)");
    CHECK_EQ("DMA_ID", 0x40020000u, 0x444D4101u, 0u);
}

/* ======================= 채점 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static fault_cfg fault_preset(const char *name) {
    fault_cfg f = { .cf_aggr = -1, .cf_victim = -1 };
    if (!strcmp(name, "reset"))      { f.reset_off = 0x008; f.reset_xor = 0x00000100u; }
    else if (!strcmp(name, "stuck")) { f.stuck_off = 0x008; f.stuck1 = 1u << 5; }
    else if (!strcmp(name, "w1c"))   { f.w1c_as_rw = true; }
    else if (!strcmp(name, "alias")) { f.ignore_addr_bit = 0x10u; }
    else if (!strcmp(name, "cf"))    { f.cf_aggr = 10; f.cf_victim = 11; }
    return f;
}

static logbuf g_log;
static int run_with(const char *filter, const char *fault, run_summary *s) {
    memset(&g_log, 0, sizeof g_log);
    g_fault = fault_preset(fault);
    run_cfg cfg = { .filter = filter, .now_us = fake_now_us, .before_each = mock_reset, .log = &g_log };
    return run_tests(&cfg, &g_registry, s);
}
/* g_log 에서 "test":"<name>" 이 든 줄을 out 에 복사 (없으면 "") */
static const char *log_line(const char *name, char *out, size_t cap) {
    char key[96];
    snprintf(key, sizeof key, "\"test\":\"%s\"", name);
    out[0] = '\0';
    const char *p = strstr(g_log.buf, key);
    if (!p) return out;
    while (p > g_log.buf && p[-1] != '\n') p--;
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}
static bool line_has(const char *name, const char *needle) {
    char line[512];
    return strstr(log_line(name, line, sizeof line), needle) != NULL;
}
static int count_lines(const char *s) { int n = 0; for (; *s; s++) n += *s == '\n'; return n; }

static void dummy_pass(test_ctx *t) { CHECK_EQ("X", 0, 1u, 1u); }
static void dummy_empty(test_ctx *t) { (void)t; }
static void dummy_skip(test_ctx *t) { TEST_SKIP("not on %s", "FPGA"); }
static void dummy_fail_then_skip(test_ctx *t) { CHECK_EQ("X", 0, 1u, 2u); TEST_SKIP("late skip"); }

static uint64_t mono_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--run")) {         /* 실제 실행 모드 */
        g_fault = fault_preset(argc >= 5 && !strcmp(argv[3], "--fault") ? argv[4] : "none");
        static logbuf lb;
        lb.echo = stdout;
        run_cfg cfg = { .filter = argv[2], .now_us = mono_now_us, .before_each = mock_reset,
                        .log = &lb, .verbose = true };
        run_summary s;
        return run_tests(&cfg, &g_registry, &s);
    }

    /* ---- Part A: 등록 ---- */
    CHECK(g_registry.n == 6);                              /* constructor 6개가 main 전에 돌았다 */
    static test_registry lr;
    static const test_case tcx = { "X.x", dummy_pass, "f.c", 1 };
    int rc = 0;
    for (int i = 0; i < MAX_TESTS + 2; i++) rc = test_register(&lr, &tcx);
    CHECK(lr.n == MAX_TESTS && lr.overflow == 2 && rc == -1);

    /* ---- Part A: check_mask ---- */
    test_ctx t = { .status = ST_PASS };
    CHECK(check_mask(&t, "R", 0x10, 0xF0u, 0x12u, 0x1Fu, "f.c", 7));     /* 마스크 밖 차이는 무시 */
    CHECK(t.nchecks == 1 && t.nfails == 0 && t.status == ST_PASS);
    CHECK(!check_mask(&t, "R", 0x10, 0xF0u, 0x12u, 0x32u, "f.c", 8));
    CHECK(t.nfails == 1 && t.status == ST_FAIL && t.fails[0].act == 0x32u && t.fails[0].line == 8);
    for (int i = 0; i < 5; i++) check_mask(&t, "R", (uint32_t)i, ~0u, 0u, 1u, "f.c", 100 + i);
    CHECK(t.nchecks == 7 && t.nfails == 6 && t.fails[0].line == 8 && t.fails[3].line == 102);

    /* ---- Part A: run_one ---- */
    run_cfg c0 = { .now_us = fake_now_us, .before_each = mock_reset };
    test_case tc_p = { "D.pass", dummy_pass, "f.c", 1 }, tc_e = { "D.empty", dummy_empty, "f.c", 2 };
    test_case tc_s = { "D.skip", dummy_skip, "f.c", 3 }, tc_fs = { "D.fs", dummy_fail_then_skip, "f.c", 4 };
    run_one(&c0, &tc_p, &t);
    CHECK(t.status == ST_PASS && t.nchecks == 1 && t.nfails == 0 && t.tc == &tc_p);
    run_one(&c0, &tc_e, &t);
    CHECK(t.status == ST_FAIL && strstr(t.reason, "no checks") != NULL);
    run_one(&c0, &tc_s, &t);
    CHECK(t.status == ST_SKIP && strcmp(t.reason, "not on FPGA") == 0);
    run_one(&c0, &tc_fs, &t);
    CHECK(t.status == ST_FAIL);                            /* 이미 실패한 뒤의 SKIP 은 FAIL 을 못 덮는다 */

    /* ---- Part B: glob / filter ---- */
#define G(p, s) glob_match((p), strlen(p), (s))
    CHECK(G("REG.*", "REG.reset_values") && G("*.reset_*", "REG.reset_values"));
    CHECK(G("REG.?eset_values", "REG.reset_values") && !G("MEM.*", "REG.reset_values"));
    CHECK(G("*", "") && G("a*b*c", "aXbYc") && !G("a*b", "aXbYc") && G("*ab", "aab"));
    CHECK(!G("REG", "REG.reset_values") && G("REG.*", "REG.") && !G("", "x"));
    CHECK(filter_match(NULL, "A.b") && filter_match("", "A.b"));
    CHECK(filter_match("REG.*:MEM.*", "MEM.march_c_minus") && !filter_match("REG.*:MEM.*", "DMA.reset_values"));
    CHECK(!filter_match("-MEM.*", "MEM.march_c_minus") && filter_match("-MEM.*", "REG.rw_patterns"));
    CHECK(filter_match("REG.*:-REG.walk*", "REG.rw_patterns") && !filter_match("REG.*:-REG.walk*", "REG.walking_bits"));
    CHECK(!filter_match("DMA", "DMA.reset_values"));       /* 정확히 일치해야 한다 */

    /* ---- Part B: 정렬 / 중복 / overflow ---- */
    static test_registry r2;
    test_case a = { "REG.b", dummy_pass, "a.c", 1 }, b = { "MEM.a", dummy_pass, "b.c", 1 },
              c = { "REG.a", dummy_pass, "c.c", 1 }, d2 = { "REG.b", dummy_pass, "d.c", 9 };
    test_register(&r2, &a); test_register(&r2, &b); test_register(&r2, &c);
    const char *dup = "x";
    CHECK(registry_finalize(&r2, &dup) == 0 && dup == NULL);
    CHECK(r2.tests[0] == &b && r2.tests[1] == &c && r2.tests[2] == &a);
    test_register(&r2, &d2);
    CHECK(registry_finalize(&r2, &dup) == -1 && dup && strcmp(dup, "REG.b") == 0);
    CHECK(registry_finalize(&lr, &dup) == -2);

    /* ---- Part B: json_escape ---- */
    char jb[64];
    CHECK(json_escape(jb, sizeof jb, "a\"b\\c") == 7 && strcmp(jb, "a\\\"b\\\\c") == 0);
    CHECK(json_escape(jb, sizeof jb, "x\ny\tz\x01") == 13 && strcmp(jb, "x\\ny\\tz\\u0001") == 0);
    CHECK(json_escape(jb, 4, "ab\"c") == 2 && strcmp(jb, "ab") == 0);      /* \" 를 반으로 자르지 않음 */
    CHECK(json_escape(jb, 1, "abc") == 0 && jb[0] == '\0');

    /* ---- Part C: 깨끗한 mock — 전체 실행 ---- */
    run_summary s;
    int ec = run_with(NULL, "none", &s);
    CHECK(ec == 0 && s.registered == 6 && s.matched == 6 && s.pass == 5 && s.fail == 0 && s.skip == 1);
    CHECK(count_lines(g_log.buf) == 8 && !g_log.truncated);    /* run + test 6 + summary */
    const char *hdr = "{\"type\":\"run\",\"filter\":\"\",\"registered\":6,\"matched\":6}\n";
    CHECK(strncmp(g_log.buf, hdr, strlen(hdr)) == 0);
    CHECK(line_has("DMA.reset_values", "\"status\":\"SKIP\"") &&
          line_has("DMA.reset_values", "\"reason\":\"DMA block fused off (fuse \\\"DMA_EN\\\"=0)\""));
    CHECK(line_has("MEM.march_c_minus", "\"duration_us\":640,\"checks\":320,\"fails\":0}"));
    CHECK(strstr(g_log.buf, "{\"type\":\"summary\",\"pass\":5,\"fail\":0,\"skip\":1,\"exit\":0}") != NULL);
    /* 실행 순서 = 이름순 */
    CHECK(strstr(g_log.buf, "DMA.reset_values") < strstr(g_log.buf, "MEM.march_c_minus") &&
          strstr(g_log.buf, "REG.addr_alias") < strstr(g_log.buf, "REG.walking_bits"));

    /* ---- Part C: 필터 ---- */
    ec = run_with("REG.*:-REG.walk*", "none", &s);
    CHECK(ec == 0 && s.matched == 3 && s.pass == 3 && !strstr(g_log.buf, "walking"));
    ec = run_with("NOPE.*", "none", &s);
    CHECK(ec == 2 && s.matched == 0);                      /* 오타 난 필터가 초록불이 되면 안 된다 */

    /* ---- Part C: 결함 주입 → 정확한 리포트 ---- */
    ec = run_with(NULL, "reset", &s);
    CHECK(ec == 1 && s.fail == 1);
    CHECK(line_has("REG.reset_values", "\"reg\":\"LOAD\",\"addr\":\"0x40010008\",\"expected\":\"0x0000FFFF\",\"actual\":\"0x0000FEFF\""));
    ec = run_with("REG.rw_patterns", "w1c", &s);
    CHECK(ec == 1 && line_has("REG.rw_patterns", "\"reg\":\"STATUS\",\"addr\":\"0x40010010\",\"expected\":\"0x00000000\""));
    /* actual 은 읽은 원본 값(volatile bit 8 은 시계에 따라 다름) — mask 가 비교한 비트를 알려준다 */
    CHECK(line_has("REG.rw_patterns", "F\",\"mask\":\"0xFFFFFEFF\""));
    ec = run_with("REG.addr_alias", "alias", &s);
    CHECK(ec == 1 && line_has("REG.addr_alias", "\"reg\":\"COMPARE\"") &&
          line_has("REG.addr_alias", "COMPARE@0x00C reads SCRATCH@0x01C pattern: addr bit 4 ignored?"));
    ec = run_with("MEM.*", "cf", &s);
    CHECK(ec == 1 && line_has("MEM.march_c_minus", "\"fails\":1,\"reg\":\"SRAM\",\"addr\":\"0x2000002C\",\"expected\":\"0x00000000\",\"actual\":\"0xFFFFFFFF\""));
    ec = run_with("REG.walking_bits", "stuck", &s);
    CHECK(ec == 1 && line_has("REG.walking_bits", "\"fails\":32,\"reg\":\"LOAD\",\"addr\":\"0x40010008\",\"expected\":\"0x00000001\",\"actual\":\"0x00000021\""));

    /* ---- Part C: 결함 × 테스트 검출 매트릭스 ---- */
    static const char *const faults[] = { "reset", "stuck", "w1c", "alias", "cf" };
    static const char *const tests[] = { "REG.reset_values", "REG.rw_patterns", "REG.walking_bits",
                                         "REG.addr_alias", "MEM.march_c_minus" };
    static const char *const want[] = { "X....", ".XXX.", ".X...", "XX.X.", "....X" };
    char got[5][6];
    for (int f = 0; f < 5; f++) {
        run_with(NULL, faults[f], &s);
        for (int k = 0; k < 5; k++) got[f][k] = line_has(tests[k], "\"status\":\"FAIL\"") ? 'X' : '.';
        got[f][5] = '\0';
        CHECK(strcmp(got[f], want[f]) == 0);
    }
    printf("fault \\ test   reset  rw  walk  alias  march\n");
    for (int f = 0; f < 5; f++)
        printf("%-12s     %c     %c    %c     %c      %c\n", faults[f],
               got[f][0], got[f][1], got[f][2], got[f][3], got[f][4]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
