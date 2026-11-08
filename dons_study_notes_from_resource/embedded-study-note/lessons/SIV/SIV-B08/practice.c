/* SIV-B08 추가 연습 — 한 번에 검증
 *
 * solution.c 의 프레임워크·축약 mock·이식 테스트를 그대로 두고 다음을 더했다.
 *   연습 1  list_tests        : --list (실행하지 않고 필터에 맞는 테스트 이름만)
 *   연습 2  run_tests_shard   : CI 샤딩 — 이름순으로 정렬된 매칭 테스트의 k 번째를 k % n == shard 인 샤드가
 *   연습 3  REQUIRE_EQ        : 치명적(fatal) 체크 — 클럭이 꺼진 블록(모든 read = 0xDEADBEEF)에서 쓰레기 실패 폭주 막기
 *   연습 4  repeat_until_fail : 간헐(flaky) 결함 — 같은 테스트를 N 번 돌려 처음 실패한 반복 번호
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
    if (r->n >= MAX_TESTS) { r->overflow++; return -1; }
    r->tests[r->n++] = tc;
    return 0;
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
    t->nchecks++;
    if (((exp ^ act) & mask) == 0) return true;
    if (t->nfails < MAX_FAIL_REC)
        t->fails[t->nfails] = (fail_rec){ what, addr, mask, exp, act, file, line };
    t->nfails++;
    t->status = ST_FAIL;
    if (t->verbose && t->nfails <= MAX_FAIL_REC)
        print_fail(t->tc ? t->tc->name : "?", &t->fails[t->nfails - 1]);
    return false;
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
    memset(t, 0, sizeof *t);
    t->tc = tc;
    t->status = ST_PASS;
    t->verbose = cfg->verbose;
    if (cfg->before_each) cfg->before_each();
    uint64_t t0 = cfg->now_us();
    tc->fn(t);
    uint64_t dur = cfg->now_us() - t0;
    if (t->status == ST_PASS && t->nchecks == 0) {
        t->status = ST_FAIL;
        test_note(t, "no checks executed (vacuous pass)");
    }
    return dur;
}

/* ---------------- Part B: 이름 필터 · 정렬/중복 검사 · JSON 이스케이프 ---------------- */
/* glob: '*' = 임의 길이, '?' = 한 글자. 패턴은 p[0..plen) (NUL 로 안 끝날 수 있다). */
static bool glob_match(const char *p, size_t plen, const char *s) {
    size_t pi = 0, si = 0, star = (size_t)-1, mark = 0;
    while (s[si]) {
        if (pi < plen && (p[pi] == '?' || p[pi] == s[si])) { pi++; si++; }
        else if (pi < plen && p[pi] == '*') { star = pi++; mark = si; }
        else if (star != (size_t)-1) { pi = star + 1; si = ++mark; }
        else return false;
    }
    while (pi < plen && p[pi] == '*') pi++;
    return pi == plen;
}

/* "REG.*:MEM.*:-*.slow" — ':' 로 나눈 glob 들. '-' 로 시작하면 제외 패턴.
 * 포함 패턴이 하나도 없으면 전부 포함. NULL/"" = 전부. */
static bool filter_match(const char *filter, const char *name) {
    if (!filter || !*filter) return true;
    bool has_pos = false, pos_hit = false;
    for (const char *p = filter;;) {
        const char *e = strchr(p, ':');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > 0 && p[0] == '-') {
            if (glob_match(p + 1, len - 1, name)) return false;
        } else if (len > 0) {
            has_pos = true;
            if (glob_match(p, len, name)) pos_hit = true;
        }
        if (!e) break;
        p = e + 1;
    }
    return has_pos ? pos_hit : true;
}

static int cmp_test(const void *a, const void *b) {
    const test_case *x = *(const test_case *const *)a, *y = *(const test_case *const *)b;
    return strcmp(x->name, y->name);
}

/* 이름순 정렬(실행 순서를 링크 순서와 무관하게 고정). overflow 면 -2, 같은 이름이 있으면 *dup 에
 * 그 이름을 넣고 -1, 정상이면 0. */
static int registry_finalize(test_registry *r, const char **dup) {
    *dup = NULL;
    if (r->overflow) return -2;
    qsort(r->tests, (size_t)r->n, sizeof r->tests[0], cmp_test);
    for (int i = 1; i < r->n; i++)
        if (strcmp(r->tests[i - 1]->name, r->tests[i]->name) == 0) { *dup = r->tests[i]->name; return -1; }
    return 0;
}

/* src 를 JSON 문자열 내용으로 이스케이프해 dst(cap 바이트)에. " \ → \" \\, \n \t, 나머지 제어문자
 * (< 0x20) → \u00XX. 공간이 모자라면 이스케이프를 반으로 자르지 말고 거기서 멈춘다. 항상 NUL 종료.
 * 쓴 글자 수(NUL 제외)를 돌려준다. */
static size_t json_escape(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (cap == 0) return 0;
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        char tmp[8];
        size_t k;
        if (c == '"' || c == '\\') { tmp[0] = '\\'; tmp[1] = (char)c; k = 2; }
        else if (c == '\n')        { tmp[0] = '\\'; tmp[1] = 'n';     k = 2; }
        else if (c == '\t')        { tmp[0] = '\\'; tmp[1] = 't';     k = 2; }
        else if (c < 0x20)         { k = (size_t)snprintf(tmp, sizeof tmp, "\\u%04x", c); }
        else                       { tmp[0] = (char)c;                k = 1; }
        if (n + k >= cap) break;
        memcpy(dst + n, tmp, k);
        n += k;
    }
    dst[n] = '\0';
    return n;
}

/* ---------------- Part C: JSON 결과 로그 · 러너 ---------------- */
/* 한 줄 = 한 JSON 객체 (JSON Lines). 예:
 * {"type":"test","test":"REG.reset_values","status":"FAIL","duration_us":7,"checks":6,"fails":1,
 *  "reg":"LOAD","addr":"0x40010008","expected":"0x0000FFFF","actual":"0x0000FEFF","mask":"0xFFFFFFFF",
 *  "file":"solution.c","line":321}
 * 실패가 있으면 첫 실패의 reg..line 을, reason 이 있으면 "reason" 을 덧붙인다. */
static void emit_result_json(logbuf *lb, const test_ctx *t, uint64_t dur_us) {
    char name[160], s1[160], s2[200];
    json_escape(name, sizeof name, t->tc->name);
    log_printf(lb, "{\"type\":\"test\",\"test\":\"%s\",\"status\":\"%s\",\"duration_us\":%llu,"
               "\"checks\":%d,\"fails\":%d", name, k_status[t->status],
               (unsigned long long)dur_us, t->nchecks, t->nfails);
    if (t->nfails > 0) {
        const fail_rec *f = &t->fails[0];
        json_escape(s1, sizeof s1, f->what);
        json_escape(s2, sizeof s2, f->file);
        log_printf(lb, ",\"reg\":\"%s\",\"addr\":\"0x%08X\",\"expected\":\"0x%08X\",\"actual\":\"0x%08X\","
                   "\"mask\":\"0x%08X\",\"file\":\"%s\",\"line\":%d", s1, (unsigned)f->addr,
                   (unsigned)f->exp, (unsigned)f->act, (unsigned)f->mask, s2, f->line);
    }
    if (t->reason[0]) {
        json_escape(s2, sizeof s2, t->reason);
        log_printf(lb, ",\"reason\":\"%s\"", s2);
    }
    log_printf(lb, "}\n");
}

typedef struct { int registered, matched, pass, fail, skip; } run_summary;

/* finalize → run 헤더 줄 → 필터에 맞는 테스트를 이름순으로 실행·기록 → summary 줄.
 * 종료 코드: 0 = 실패 없음(SKIP 허용), 1 = FAIL 있음, 2 = 설정 오류(중복/overflow/필터에 맞는 테스트 0개). */
static int run_tests_shard(const run_cfg *cfg, test_registry *r, int shard, int nshards, run_summary *s) {
    memset(s, 0, sizeof *s);
    s->registered = r->n;
    const char *dup;
    int rc = registry_finalize(r, &dup);
    if (rc != 0) {
        char d[160];
        json_escape(d, sizeof d, dup ? dup : "");
        log_printf(cfg->log, "{\"type\":\"error\",\"msg\":\"%s\",\"test\":\"%s\",\"exit\":2}\n",
                   rc == -1 ? "duplicate test name" : "registry overflow", d);
        return 2;
    }
    for (int i = 0, k = 0; i < r->n; i++)
        if (filter_match(cfg->filter, r->tests[i]->name) && k++ % nshards == shard) s->matched++;
    char f[160];
    json_escape(f, sizeof f, cfg->filter ? cfg->filter : "");
    log_printf(cfg->log, "{\"type\":\"run\",\"filter\":\"%s\",\"shard\":\"%d/%d\",\"registered\":%d,\"matched\":%d}\n",
               f, shard, nshards, s->registered, s->matched);
    for (int i = 0, k = 0; i < r->n; i++) {
        if (!filter_match(cfg->filter, r->tests[i]->name)) continue;
        if (k++ % nshards != shard) continue;           /* 연습 2: 이 샤드 몫이 아니다 */
        test_ctx t;
        uint64_t d = run_one(cfg, r->tests[i], &t);
        emit_result_json(cfg->log, &t, d);
        if (t.status == ST_PASS) s->pass++;
        else if (t.status == ST_FAIL) s->fail++;
        else s->skip++;
    }
    int ec = s->matched == 0 ? 2 : s->fail ? 1 : 0;
    log_printf(cfg->log, "{\"type\":\"summary\",\"pass\":%d,\"fail\":%d,\"skip\":%d,\"exit\":%d}\n",
               s->pass, s->fail, s->skip, ec);
    return ec;
}
static int run_tests(const run_cfg *cfg, test_registry *r, run_summary *s) {
    return run_tests_shard(cfg, r, 0, 1, s);
}

/* 연습 1: 실행하지 않고 필터에 맞는 이름을 한 줄씩. 레지스트리 오류면 -1, 아니면 개수 */
static int list_tests(test_registry *r, const char *filter, logbuf *lb) {
    const char *dup;
    if (registry_finalize(r, &dup) != 0) return -1;
    int n = 0;
    for (int i = 0; i < r->n; i++)
        if (filter_match(filter, r->tests[i]->name)) { log_printf(lb, "%s\n", r->tests[i]->name); n++; }
    return n;
}
static const test_case *find_test(test_registry *r, const char *name) {
    for (int i = 0; i < r->n; i++) if (!strcmp(r->tests[i]->name, name)) return r->tests[i];
    return NULL;
}
/* 연습 4: tc 를 최대 max_iter 번 돌려 처음 FAIL 한 반복 번호(1부터). 끝까지 통과면 0. t 에 마지막 결과 */
static int repeat_until_fail(const run_cfg *cfg, const test_case *tc, int max_iter, test_ctx *t) {
    for (int it = 1; it <= max_iter; it++) {
        run_one(cfg, tc, t);
        if (t->status == ST_FAIL) return it;
    }
    return 0;
}
/* 연습 3: 실패하면 이 테스트를 즉시 끝내는 체크 (본문 함수에서 return) */
#define REQUIRE_EQ(what, addr, exp, act)                                              \
    do { if (!CHECK_EQ(what, addr, exp, act)) {                                       \
             TEST_NOTE("REQUIRE %s failed: test aborted", (what)); return; } } while (0)

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
static bool      g_clock_gated;        /* 연습 3: 블록 클럭 꺼짐 → 모든 read 가 0xDEADBEEF, write 무시 */
static int       g_flaky_period, g_resets; /* 연습 4: period 번째 리셋마다 LOAD 리셋값 bit0 이 틀림 */

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
        if (g_flaky_period && d->off == 0x008 && (g_resets + 1) % g_flaky_period == 0) v ^= 1u;
        g_cell[d->off >> 2] = apply_stuck(d->off, v);
    }
    for (int i = 0; i < SRAM_WORDS; i++) g_sram[i] = 0xA5A5A5A5u;   /* 전원 인가 직후 쓰레기 */
    g_resets++;
}
static uint32_t reg_read32(uint32_t off) {
    g_fake_us++;
    if (g_clock_gated) return 0xDEADBEEFu;
    off &= ~g_fault.ignore_addr_bit;
    const reg_desc *d = desc_at(off);
    if (!d) return 0;
    uint32_t v = g_cell[off >> 2];
    return (v & ~d->vol) | ((uint32_t)g_fake_us & d->vol);   /* volatile 비트는 시간 따라 변함 */
}
static void reg_write32(uint32_t off, uint32_t val) {
    g_fake_us++;
    if (g_clock_gated) return;
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
    for (size_t i = 0; i < NREGS; i++)
        if (k_tmr0[i].rw) reg_write32(k_tmr0[i].off, addr_pattern(k_tmr0[i].off));
    for (size_t i = 0; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        if (!d->rw) continue;
        uint32_t act = reg_read32(d->off);
        if (CHECK_MASK(d->name, TMR0_BASE + d->off, d->rw, addr_pattern(d->off), act)) continue;
        for (size_t j = 0; j < NREGS; j++) {
            const reg_desc *o = &k_tmr0[j];
            if (o == d || !o->rw || ((addr_pattern(o->off) ^ act) & d->rw)) continue;
            uint32_t x = d->off ^ o->off;
            int bit = -1;
            if (x && !(x & (x - 1u))) for (bit = 0; !((x >> bit) & 1u); bit++) {}
            TEST_NOTE("%s@0x%03X reads %s@0x%03X pattern: addr bit %d ignored?",
                      d->name, (unsigned)d->off, o->name, (unsigned)o->off, bit);
        }
    }
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

TEST(REG, id_first) {                                  /* 연습 3: ID 가 틀리면 나머지는 의미 없다 */
    REQUIRE_EQ("ID", TMR0_BASE + 0x000, 0x54310001u, reg_read32(0x000));
    for (size_t i = 1; i < NREGS; i++) {
        const reg_desc *d = &k_tmr0[i];
        if (~d->vol == 0) continue;
        CHECK_MASK(d->name, TMR0_BASE + d->off, ~d->vol, d->reset, reg_read32(d->off));
    }
}

/* ======================= 채점 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static logbuf g_log;
static run_cfg mkcfg(const char *filter) {
    memset(&g_log, 0, sizeof g_log);
    return (run_cfg){ .filter = filter, .now_us = fake_now_us, .before_each = mock_reset, .log = &g_log };
}

int main(void) {
    run_summary s, s0, s1, s2;
    CHECK(g_registry.n == 7);

    /* ---- 연습 1: --list ---- */
    run_cfg cfg = mkcfg("REG.*:-REG.walk*");
    CHECK(list_tests(&g_registry, cfg.filter, &g_log) == 4);
    CHECK(strcmp(g_log.buf, "REG.addr_alias\nREG.id_first\nREG.reset_values\nREG.rw_patterns\n") == 0);
    cfg = mkcfg("NOPE");
    CHECK(list_tests(&g_registry, cfg.filter, &g_log) == 0 && g_log.len == 0);

    /* ---- 연습 2: 샤딩 — 3 샤드의 합 = 전체, 서로 겹치지 않음 ---- */
    cfg = mkcfg(NULL);
    CHECK(run_tests(&cfg, &g_registry, &s) == 0 && s.matched == 7 && s.pass == 6 && s.skip == 1);
    cfg = mkcfg(NULL); run_tests_shard(&cfg, &g_registry, 0, 3, &s0);
    CHECK(strstr(g_log.buf, "\"shard\":\"0/3\"") && strstr(g_log.buf, "DMA.reset_values") &&
          strstr(g_log.buf, "REG.id_first") && strstr(g_log.buf, "REG.walking_bits") && !strstr(g_log.buf, "MEM."));
    cfg = mkcfg(NULL); run_tests_shard(&cfg, &g_registry, 1, 3, &s1);
    CHECK(strstr(g_log.buf, "MEM.march_c_minus") && strstr(g_log.buf, "REG.reset_values") && !strstr(g_log.buf, "DMA."));
    cfg = mkcfg(NULL); run_tests_shard(&cfg, &g_registry, 2, 3, &s2);
    CHECK(strstr(g_log.buf, "REG.addr_alias") && strstr(g_log.buf, "REG.rw_patterns") && !strstr(g_log.buf, "REG.id_first"));
    CHECK(s0.matched + s1.matched + s2.matched == 7 && s0.matched == 3 && s1.matched == 2 && s2.matched == 2);
    CHECK(s0.pass + s1.pass + s2.pass == 6 && s0.skip + s1.skip + s2.skip == 1);

    /* ---- 연습 3: REQUIRE — 클럭이 꺼진 블록 ---- */
    g_clock_gated = true;
    cfg = mkcfg("REG.reset_values:REG.id_first");
    CHECK(run_tests(&cfg, &g_registry, &s) == 1 && s.fail == 2);
    CHECK(strstr(g_log.buf, "\"test\":\"REG.reset_values\",\"status\":\"FAIL\",\"duration_us\":6,\"checks\":6,\"fails\":6") != NULL);
    CHECK(strstr(g_log.buf, "\"test\":\"REG.id_first\",\"status\":\"FAIL\",\"duration_us\":1,\"checks\":1,\"fails\":1,"
                            "\"reg\":\"ID\",\"addr\":\"0x40010000\",\"expected\":\"0x54310001\",\"actual\":\"0xDEADBEEF\"") != NULL);
    CHECK(strstr(g_log.buf, "\"reason\":\"REQUIRE ID failed: test aborted\"") != NULL);
    g_clock_gated = false;
    cfg = mkcfg("REG.id_first");
    CHECK(run_tests(&cfg, &g_registry, &s) == 0 && strstr(g_log.buf, "\"checks\":6,\"fails\":0}"));

    /* ---- 연습 4: 간헐 결함 — 3번째 리셋마다 LOAD 리셋값 bit0 틀림 ---- */
    test_ctx t;
    cfg = mkcfg(NULL);
    const test_case *rv = find_test(&g_registry, "REG.reset_values");
    CHECK(rv != NULL && find_test(&g_registry, "NOPE") == NULL);
    CHECK(repeat_until_fail(&cfg, rv, 10, &t) == 0);              /* 결함 없음: 10번 모두 통과 */
    g_flaky_period = 3; g_resets = 0;
    CHECK(repeat_until_fail(&cfg, rv, 10, &t) == 3);
    CHECK(t.status == ST_FAIL && t.nfails == 1 && !strcmp(t.fails[0].what, "LOAD") && t.fails[0].act == 0x0000FFFEu);
    g_resets = 0;
    CHECK(repeat_until_fail(&cfg, rv, 2, &t) == 0);               /* 2번만 돌리면 못 잡는다 */
    g_flaky_period = 0;

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
