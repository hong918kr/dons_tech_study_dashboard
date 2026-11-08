/* SIV-B14 테스트 로깅과 Pass/Fail 리포트
 *
 * 입력: SIV-B08 프레임워크 로그를 축약한 JSON Lines — 한 줄 = 평평한(flat) JSON 객체 하나.
 *   {"type":"header","run_id":..,"start":..,"seed":"0x..","board":..,"chip":..,"fw":..}
 *   {"type":"test","t_us":..,"lvl":"ERR","test":"uart.reset.LCR","status":"FAIL","dur_us":..,
 *    "reg":"LCR","addr":"0x4000100C","exp":"0x00000003","act":"0x00000000","msg":".."}
 *   {"type":"log","t_us":..,"lvl":"WARN","msg":".."}
 *   {"type":"end","t_us":..,"records":10}
 * 출력: 텍스트(CI 한 줄 요약 + 실패 상세) / Markdown 리포트, 종료 코드 0(PASS) / 1(그 외)
 *
 *   ./solution                          자체 테스트 (CHECK)
 *   ./solution --emit good|faulty       mock UART 스위트 실행 → JSONL 로그를 stdout 으로
 *   ./solution --text|--md run.jsonl    로그 → 리포트 출력, 종료 코드 = 판정
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 공통: 문자열 빌더 (주어짐) ======================= */
typedef struct { char *buf; size_t cap, len; int overflow; } sb_t;   /* cap > 0 */

static sb_t sb_on(char *buf, size_t cap) { sb_t s = { buf, cap, 0, 0 }; buf[0] = '\0'; return s; }
static void sb_vprintf(sb_t *s, const char *fmt, va_list ap) {
    size_t room = s->cap - s->len;
    int n = vsnprintf(s->buf + s->len, room, fmt, ap);
    if (n < 0 || (size_t)n >= room) { s->overflow = 1; s->len = s->cap - 1; }
    else s->len += (size_t)n;
}
__attribute__((format(printf, 2, 3)))
static void sb_printf(sb_t *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); sb_vprintf(s, fmt, ap); va_end(ap);
}
static void copy_str(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

/* ======================= Part A: 한 줄 JSON 파서 ======================= */
#define JMAX_KV  16
#define JKEY_LEN 24
#define JVAL_LEN 128

typedef struct {
    char key[JKEY_LEN];
    char val[JVAL_LEN];  /* 문자열은 escape 해제한 값, 숫자/true/false/null 은 원문 그대로 */
    char kind;           /* 's' 문자열  'n' 숫자  'b' true/false  'z' null */
    int  truncated;      /* 값이 JVAL_LEN-1 자를 넘어 잘렸으면 1 */
} jkv_t;
typedef struct { int n; jkv_t kv[JMAX_KV]; } jobj_t;
typedef struct { int col; const char *why; } jerr_t;   /* col: 1부터 */

static int jfail(jerr_t *e, const char *base, const char *at, const char *why) {
    if (e) { e->col = (int)(at - base) + 1; e->why = why; }
    return -1;
}
static const char *jget(const jobj_t *o, const char *key) {
    for (int i = 0; i < o->n; i++)
        if (strcmp(o->kv[i].key, key) == 0) return o->kv[i].val;
    return NULL;
}
static int jis(const jobj_t *o, const char *key, const char *val) {
    const char *v = jget(o, key);
    return v && strcmp(v, val) == 0;
}
static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
}
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* *pp 는 여는 따옴표를 가리킨다. escape 를 풀어 dst(cap)에 복사하고 닫는 따옴표 다음으로 전진.
 * 넘치면 잘라내되 끝까지 읽고 *trunc = 1. \uXXXX 는 ASCII(1..0x7F)만 그대로, 나머지는 '?'. */
static int parse_string(const char **pp, char *dst, size_t cap, int *trunc,
                        jerr_t *e, const char *base) {
    (void)pp; (void)dst; (void)cap; (void)trunc; (void)e; (void)base;
    return -1; /* TODO: 따옴표 확인 → escape 해제 복사(넘치면 자르고 *trunc=1) → 닫는 따옴표 다음으로 */
}

/* 평평한 객체 한 줄: { "k": 문자열|숫자|true|false|null , ... } 뒤에는 공백만.
 * 중첩 객체/배열, 중복 키, 꼬리 쓰레기는 거부. 성공 0, 실패 -1 (+ e->col, e->why). */
static int json_parse_line(const char *line, jobj_t *o, jerr_t *e) {
    (void)parse_string; (void)skip_ws;
    o->n = 0;
    return jfail(e, line, line, "TODO"); /* TODO: '{' (key ':' value (',' ...)*)? '}' + 꼬리 공백만 */
}

/* "0x4000100C" 또는 "42" → *out. 빈 문자열/NULL/잘못된 글자/32비트 초과 → 0 (실패) */
static int parse_u32(const char *s, uint32_t *out) {
    (void)s; (void)out; (void)hexval;
    return 0; /* TODO: 10진/0x 16진, 잘못된 글자·빈 문자열·32비트 초과는 실패 */
}

/* ======================= Part B: 집계와 판정 ======================= */
typedef enum { ST_SKIP, ST_PASS, ST_FAIL, ST_ERROR, ST__N } st_t;      /* 뒤로 갈수록 심각 */
static const char *const ST_NAME[ST__N] = { "SKIP", "PASS", "FAIL", "ERROR" };
typedef enum { V_PASS, V_INVALID, V_INCOMPLETE, V_FAIL } verdict_t;    /* 뒤로 갈수록 우선 */
static const char *const V_NAME[] = { "PASS", "INVALID", "INCOMPLETE", "FAIL" };

#define MAX_TESTS 48
#define MAX_NOTES 8
typedef struct {
    char name[48];
    st_t st;                        /* 모든 attempt 중 가장 나쁜 결과 (sticky fail) */
    int attempts, bad_attempts;     /* bad = FAIL 또는 ERROR */
    uint32_t dur_us;                /* attempt 합 */
    uint32_t t_us;                  /* 가장 나쁜 결과가 기록된 시각 */
    int has_reg; char reg[16]; uint32_t addr, exp, act;
    char msg[JVAL_LEN];
} test_t;
typedef struct { uint32_t t_us; char lvl[8]; char msg[JVAL_LEN]; } note_t;
typedef struct {
    int has_header; char run_id[32], start[24], board[48], chip[24], fw[40]; uint32_t seed;
    int has_end; uint32_t end_records;
    int lines, records, bad_lines, first_bad_line, unknown_types, dropped;
    jerr_t first_bad;
    int lvl_info, lvl_warn, lvl_err, err_notes;
    int n_tests; test_t t[MAX_TESTS];
    int n_notes; note_t notes[MAX_NOTES];
} run_t;

static void mark_bad(run_t *r, int col, const char *why) {
    if (r->bad_lines++ == 0) { r->first_bad_line = r->lines; r->first_bad.col = col; r->first_bad.why = why; }
}
static test_t *find_or_add(run_t *r, const char *name) {
    for (int i = 0; i < r->n_tests; i++)
        if (strcmp(r->t[i].name, name) == 0) return &r->t[i];
    if (r->n_tests >= MAX_TESTS) return NULL;
    test_t *t = &r->t[r->n_tests++];
    memset(t, 0, sizeof *t);
    copy_str(t->name, sizeof t->name, name);
    return t;
}
static const test_t *find_test(const run_t *r, const char *name) {
    for (int i = 0; i < r->n_tests; i++)
        if (strcmp(r->t[i].name, name) == 0) return &r->t[i];
    return NULL;
}
static void tally(const run_t *r, int cnt[ST__N]) {
    for (int i = 0; i < ST__N; i++) cnt[i] = 0;
    for (int i = 0; i < r->n_tests; i++) cnt[r->t[i].st]++;
}

/* 모르는 status 는 절대 PASS 로 치지 않는다 → ERROR */
static st_t status_from(const char *s) {
    (void)s;
    return ST_PASS; /* TODO: PASS/FAIL/SKIP, 그 외는? */
}

/* 로그 한 줄을 run 에 반영. 깨진 줄은 mark_bad 후 다음 줄로 (전체를 버리지 않는다). */
static void ingest_line(run_t *r, const char *line) {
    r->lines++;
    (void)line; (void)find_or_add; (void)status_from;
    /* TODO: 빈 줄 무시 → 파싱(실패면 mark_bad) → lvl 집계 → type 별 처리
     *       header / test(sticky fail, dur 합, 상세) / log(WARN·ERR) / end / 그 외는 unknown_types++ */
}

__attribute__((format(printf, 4, 5)))
static void add_reason(sb_t *s, verdict_t *v, verdict_t lvl, const char *fmt, ...) {
    va_list ap;
    if (lvl > *v) *v = lvl;
    if (s->len) sb_printf(s, "; ");
    va_start(ap, fmt); sb_vprintf(s, fmt, ap); va_end(ap);
}

/* 판정: FAIL > INCOMPLETE > INVALID > PASS. why 에 모든 이유를 "; " 로 이어 쓴다. */
static verdict_t run_verdict(const run_t *r, char *why, size_t cap) {
    (void)r; (void)cap; (void)add_reason;
    why[0] = '\0';
    return V_PASS; /* TODO: 실패·ERR 로그 / end 누락·개수 불일치 / 깨진 줄·drop·header 없음·PASS 0개 */
}
static int exit_code(verdict_t v) { return v == V_PASS ? 0 : 1; }

/* 소요 시간 상위 n개의 인덱스를 idx 에 (내림차순, 같으면 먼저 나온 것 먼저). 개수 반환. */
static int top_slowest(const run_t *r, int idx[], int n) {
    (void)r; (void)idx; (void)n;
    return 0; /* TODO: dur_us 내림차순 상위 n개, 동률은 먼저 나온 것 먼저 */
}

/* ======================= Part C: 리포트 ======================= */
static const char *fmt_bits(uint32_t d, char *buf, size_t cap) {
    sb_t s = sb_on(buf, cap);
    int shown = 0;
    sb_printf(&s, "bits=[");
    for (int b = 31; b >= 0; b--) {
        if (!((d >> b) & 1u)) continue;
        if (shown == 8) { sb_printf(&s, ",..."); break; }
        sb_printf(&s, "%s%d", shown ? "," : "", b);
        shown++;
    }
    sb_printf(&s, "]");
    return buf;
}

/* Markdown 표 칸: '|' → "\|", 줄바꿈 → 공백, 빈 문자열 → "-" */
static void sb_md_cell(sb_t *s, const char *str) {
    (void)s; (void)str; /* TODO: '|' → "\\|", 줄바꿈 → 공백, 빈 문자열 → "-" */
}

static void render_md(const run_t *r, sb_t *s) {
    (void)r; (void)s; /* TODO: 제목(판정) · 이유 · 헤더 표 · 집계 · Failures 표 · Slowest 3 · Skipped · WARN/ERR */
}

/* 텍스트 리포트 (주어짐): 첫 줄 = CI 가 grep 하는 한 줄 요약 */
static void render_text(const run_t *r, sb_t *s) {
    char why[256], bits[64];
    int c[ST__N];
    verdict_t v = run_verdict(r, why, sizeof why);
    tally(r, c);
    sb_printf(s, "SIV RESULT: %s pass=%d fail=%d error=%d skip=%d | run=%s board=\"%s\" fw=%s seed=0x%08X\n",
              V_NAME[v], c[ST_PASS], c[ST_FAIL], c[ST_ERROR], c[ST_SKIP], r->run_id, r->board, r->fw,
              (unsigned)r->seed);
    sb_printf(s, "REASON: %s\n", why);
    for (int i = 0; i < r->n_tests; i++) {
        const test_t *t = &r->t[i];
        if (t->st < ST_FAIL) continue;
        sb_printf(s, "%-5s %s t=%uus", ST_NAME[t->st], t->name, (unsigned)t->t_us);
        if (t->has_reg)
            sb_printf(s, " reg=%s addr=0x%08X exp=0x%08X act=0x%08X diff=0x%08X %s", t->reg,
                      (unsigned)t->addr, (unsigned)t->exp, (unsigned)t->act,
                      (unsigned)(t->exp ^ t->act), fmt_bits(t->exp ^ t->act, bits, sizeof bits));
        if (t->attempts > 1) sb_printf(s, " attempts=%d/%d", t->bad_attempts, t->attempts);
        sb_printf(s, " msg=\"%s\"\n", t->msg);
    }
    for (int i = 0; i < r->n_notes; i++)
        sb_printf(s, "%-5s t=%uus %s\n", r->notes[i].lvl, (unsigned)r->notes[i].t_us, r->notes[i].msg);
}

/* ============ Part C 무대: mock UART + SIV-B08 축약 로거 (주어짐) ============ */
#define UART0_BASE 0x40001000u
enum { R_ID, R_CTRL, R_LCR, R_ISR, R_LSR, R_SCR, R__N };
typedef struct { const char *name; uint32_t off, reset; char acc; } rdesc_t;   /* R=RO W=RW C=W1C */
static const rdesc_t UREG[R__N] = {
    { "ID",   0x00, 0x55415254u, 'R' },   /* "UART" */
    { "CTRL", 0x04, 0x00000000u, 'W' },   /* [0]=EN, [10:8]=FORCE_IRQ (쓰면 ISR 비트 set, 저장 안 됨) */
    { "LCR",  0x0C, 0x00000003u, 'W' },   /* 8N1 */
    { "ISR",  0x10, 0x00000000u, 'C' },   /* [2:0] W1C */
    { "LSR",  0x14, 0x00000000u, 'R' },   /* [5]=THRE: EN 후 몇 번 읽으면 1 */
    { "SCR",  0x1C, 0x00000000u, 'W' },
};
typedef struct { int lcr_reset_bug, isr_w1c_as_rw, thre_stuck; } faults_t;
static struct { uint32_t r[R__N]; faults_t f; uint32_t now_us; int thre_in; } U;

static void uart_por(faults_t f) {
    memset(&U, 0, sizeof U);
    U.f = f;
    for (int i = 0; i < R__N; i++) U.r[i] = UREG[i].reset;
    if (f.lcr_reset_bug) U.r[R_LCR] = 0x00000000u;           /* 결함 1: 리셋 tie-off 오류 */
}
static uint32_t rd(int i) {
    U.now_us += 2;                                           /* fake clock: 접근당 2us */
    if (i == R_LSR && U.thre_in > 0 && --U.thre_in == 0 && !U.f.thre_stuck)
        U.r[R_LSR] |= 1u << 5;                               /* 결함 3: thre_stuck 이면 안 섬 */
    return U.r[i];
}
static void wr(int i, uint32_t v) {
    U.now_us += 2;
    switch (UREG[i].acc) {
    case 'R': break;
    case 'C': U.r[i] = U.f.isr_w1c_as_rw ? v : (U.r[i] & ~v); break;   /* 결함 2: W1C 가 RW */
    default:
        if (i == R_CTRL) {
            U.r[R_ISR] |= (v >> 8) & 7u;
            v &= 1u;
            if (v && !U.r[R_CTRL]) U.thre_in = 5;
        }
        U.r[i] = v;
    }
}

typedef struct { st_t st; int reg; uint32_t exp, act; char msg[96]; } tres_t;
static tres_t res(st_t st, int reg, uint32_t exp, uint32_t act, const char *msg) {
    tres_t t = { st, reg, exp, act, "" };
    copy_str(t.msg, sizeof t.msg, msg);
    return t;
}
static uint32_t g_seed = 0x5EED1234u;
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static tres_t t_reset(int i) {
    uint32_t a = rd(i);
    return a == UREG[i].reset ? res(ST_PASS, -1, 0, 0, "") : res(ST_FAIL, i, UREG[i].reset, a, "reset value mismatch");
}
static tres_t t_rw(int i) {
    uint32_t s = g_seed, pat[5] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu, 0 };
    char m[96];
    pat[4] = xorshift32(&s);
    for (int k = 0; k < 5; k++) {
        wr(i, pat[k]);
        uint32_t a = rd(i);
        if (a != pat[k]) { snprintf(m, sizeof m, "write/read mismatch, pattern #%d", k); return res(ST_FAIL, i, pat[k], a, m); }
    }
    return res(ST_PASS, -1, 0, 0, "");
}
static tres_t t_w1c(int i) {
    wr(R_CTRL, 7u << 8);                                     /* FORCE_IRQ: ISR[2:0] = 1 */
    wr(i, 1u);                                               /* bit0 만 clear */
    uint32_t a = rd(i);
    wr(i, 7u);                                               /* 정리 */
    return a == 6u ? res(ST_PASS, -1, 0, 0, "")
                   : res(ST_FAIL, i, 6u, a, "FORCE=0x7 then write 0x1 | expect bit0 cleared, bits[2:1] kept");
}
static tres_t t_poll(int i) {
    uint32_t t0, a = 0;
    int polls = 0;
    char m[96];
    wr(R_CTRL, 1u);
    t0 = U.now_us;
    while (U.now_us - t0 < 200u) {
        a = rd(i); polls++;
        if (a & (1u << 5)) return res(ST_PASS, -1, 0, 0, "");
        U.now_us += 8;                                       /* 폴링 간격 */
    }
    snprintf(m, sizeof m, "timeout 200us waiting LSR.THRE=1, %d polls, last LSR=0x%08X", polls, (unsigned)a);
    return res(ST_FAIL, i, 1u << 5, a & (1u << 5), m);
}
static tres_t t_skip(int i) { (void)i; return res(ST_SKIP, -1, 0, 0, "needs loopback jumper J7 (TX-RX) on EVB"); }

static const struct { const char *name; tres_t (*fn)(int); int arg; } SUITE[] = {
    { "uart.id", t_reset, R_ID },          { "uart.reset.CTRL", t_reset, R_CTRL },
    { "uart.reset.LCR", t_reset, R_LCR },  { "uart.reset.ISR", t_reset, R_ISR },
    { "uart.reset.LSR", t_reset, R_LSR },  { "uart.reset.SCR", t_reset, R_SCR },
    { "uart.rw.SCR", t_rw, R_SCR },        { "uart.w1c.ISR", t_w1c, R_ISR },
    { "uart.poll.THRE", t_poll, R_LSR },   { "uart.loopback.ext", t_skip, 0 },
};

static void json_str(sb_t *s, const char *str) {
    sb_printf(s, "\"");
    for (; *str; str++) {
        unsigned char c = (unsigned char)*str;
        if (c == '"' || c == '\\') sb_printf(s, "\\%c", c);
        else if (c < 0x20) sb_printf(s, "\\u%04x", c);
        else sb_printf(s, "%c", c);
    }
    sb_printf(s, "\"");
}
static void run_suite(faults_t f, sb_t *log) {
    int n = 0, any = f.lcr_reset_bug || f.isr_w1c_as_rw || f.thre_stuck;
    uart_por(f);
    sb_printf(log, "{\"type\":\"header\",\"t_us\":0,\"lvl\":\"INFO\",\"run_id\":\"nightly-1108-%s\","
                   "\"start\":\"2026-11-08T09:00:00Z\",\"seed\":\"0x%08X\",\"board\":\"EVB-UART rev B sn0042\","
                   "\"chip\":\"UART-IP A0\",\"fw\":\"siv-0.3.1+g3f2a1c9\"}\n", any ? "faulty" : "good", (unsigned)g_seed);
    sb_printf(log, "{\"type\":\"log\",\"t_us\":%u,\"lvl\":\"WARN\",\"msg\":\"VDD_IO 1.71 V, near min 1.70 V\"}\n",
              (unsigned)U.now_us);
    for (size_t k = 0; k < sizeof SUITE / sizeof SUITE[0]; k++) {
        uint32_t t0 = U.now_us;
        tres_t r = SUITE[k].fn(SUITE[k].arg);
        sb_printf(log, "{\"type\":\"test\",\"t_us\":%u,\"lvl\":\"%s\",\"test\":\"%s\",\"status\":\"%s\",\"dur_us\":%u",
                  (unsigned)U.now_us, r.st >= ST_FAIL ? "ERR" : "INFO", SUITE[k].name, ST_NAME[r.st],
                  (unsigned)(U.now_us - t0));
        if (r.reg >= 0)
            sb_printf(log, ",\"reg\":\"%s\",\"addr\":\"0x%08X\",\"exp\":\"0x%08X\",\"act\":\"0x%08X\"",
                      UREG[r.reg].name, (unsigned)(UART0_BASE + UREG[r.reg].off), (unsigned)r.exp, (unsigned)r.act);
        if (r.msg[0]) { sb_printf(log, ",\"msg\":"); json_str(log, r.msg); }
        sb_printf(log, "}\n");
        n++;
    }
    sb_printf(log, "{\"type\":\"end\",\"t_us\":%u,\"lvl\":\"INFO\",\"records\":%d}\n", (unsigned)U.now_us, n);
}

/* 여러 줄 텍스트를 줄 단위로 ingest */
static void ingest_text(run_t *r, const char *text) {
    char line[512];
    while (*text) {
        size_t n = strcspn(text, "\n");
        size_t m = n < sizeof line - 1 ? n : sizeof line - 1;
        memcpy(line, text, m); line[m] = '\0';
        ingest_line(r, line);
        text += n + (text[n] == '\n');
    }
}

static int report_file(const char *path, int md) {
    static run_t r;
    static char line[1024], out[16384], why[256];
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    memset(&r, 0, sizeof r);
    while (fgets(line, sizeof line, fp)) {
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n' && !feof(fp)) {
            int ch;
            r.lines++; mark_bad(&r, (int)n, "line too long");
            while ((ch = fgetc(fp)) != EOF && ch != '\n') {}
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        ingest_line(&r, line);
    }
    fclose(fp);
    sb_t s = sb_on(out, sizeof out);
    if (md) render_md(&r, &s); else render_text(&r, &s);
    fputs(out, stdout);
    return exit_code(run_verdict(&r, why, sizeof why));
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const char LOG_B[] =
    "{\"type\":\"header\",\"run_id\":\"r1\",\"start\":\"2026-11-08T09:00:00Z\",\"seed\":\"0x1\",\"board\":\"b\",\"chip\":\"c\",\"fw\":\"f\"}\n"
    "{\"type\":\"test\",\"t_us\":10,\"lvl\":\"INFO\",\"test\":\"a\",\"status\":\"PASS\",\"dur_us\":5}\n"
    "{\"type\":\"test\",\"t_us\":20,\"lvl\":\"ERR\",\"test\":\"b\",\"status\":\"FAIL\",\"dur_us\":7,"
    "\"reg\":\"CTRL\",\"addr\":\"0x40001004\",\"exp\":\"0x1\",\"act\":\"0x0\"}\n"
    "{\"type\":\"test\",\"t_us\":30,\"lvl\":\"INFO\",\"test\":\"c\",\"status\":\"SKIP\",\"dur_us\":0,\"msg\":\"no jumper\"}\n"
    "{\"type\":\"test\",\"t_us\":40,\"lvl\":\"ERR\",\"test\":\"d\",\"status\":\"FAIL\",\"dur_us\":9}\n"
    "{\"type\":\"test\",\"t_us\":50,\"lvl\":\"INFO\",\"test\":\"d\",\"status\":\"PASS\",\"dur_us\":9}\n"
    "{\"type\":\"test\",\"t_us\":60,\"lvl\":\"ERR\",\"test\":\"e\",\"status\":\"TIMEOUT\",\"dur_us\":7}\n"
    "{\"type\":\"metric\",\"name\":\"vdd_core_mv\",\"value\":801}\n"
    "\n"
    "{\"type\":\"log\",\"t_us\":70,\"lvl\":\"WARN\",\"msg\":\"temp 85C\"}\n"
    "{\"type\":\"end\",\"t_us\":80,\"records\":6}\n";
#define HDR "{\"type\":\"header\",\"run_id\":\"r2\",\"seed\":\"7\",\"board\":\"b\",\"fw\":\"f\"}\n"
#define T_PASS "{\"type\":\"test\",\"test\":\"a\",\"status\":\"PASS\",\"dur_us\":5}\n"
#define T_SKIP "{\"type\":\"test\",\"test\":\"c\",\"status\":\"SKIP\"}\n"

static run_t R;
static char logbuf[8192], out[16384], why[256];

static verdict_t load(const char *text) {
    memset(&R, 0, sizeof R);
    ingest_text(&R, text);
    return run_verdict(&R, why, sizeof why);
}

static int self_test(void) {
    jobj_t o;
    jerr_t e = { 0, "" };
    uint32_t u = 0;
    int c[ST__N], idx[10];
    char longline[300], small[64];

    /* ---- Part A: 파서 ---- */
    CHECK(json_parse_line("{\"type\":\"test\", \"dur_us\": 42 ,\"ok\":true,\"x\":null}", &o, &e) == 0);
    CHECK(o.n == 4 && jis(&o, "type", "test") && o.kv[1].kind == 'n' && jis(&o, "dur_us", "42"));
    CHECK(o.n == 4 && o.kv[2].kind == 'b' && jis(&o, "ok", "true") && o.kv[3].kind == 'z');
    CHECK(json_parse_line("{\"msg\":\"a\\\"b\\\\c\\u0041\\n\"}", &o, &e) == 0 && jis(&o, "msg", "a\"b\\cA\n"));
    CHECK(json_parse_line("  {}  \r\n", &o, &e) == 0 && o.n == 0);
    CHECK(json_parse_line("{\"a\" 1}", &o, &e) == -1 && e.col == 6);
    CHECK(json_parse_line("{\"a\":1} x", &o, &e) == -1 && strcmp(e.why, "trailing garbage") == 0);
    CHECK(json_parse_line("{\"a\":{\"b\":1}}", &o, &e) == -1 && e.col == 6);
    CHECK(json_parse_line("{\"a\":\"open}", &o, &e) == -1 && strcmp(e.why, "unterminated string") == 0);
    CHECK(json_parse_line("{\"a\":1,\"a\":2}", &o, &e) == -1 && strcmp(e.why, "duplicate key") == 0);
    CHECK(json_parse_line("{\"a\":1,}", &o, &e) == -1);
    snprintf(longline, sizeof longline, "{\"msg\":\"%0200d\"}", 0);
    CHECK(json_parse_line(longline, &o, &e) == 0 && o.n == 1 && o.kv[0].truncated && strlen(o.kv[0].val) == JVAL_LEN - 1);
    CHECK(parse_u32("0x4000100C", &u) && u == 0x4000100Cu);
    CHECK(parse_u32("4294967295", &u) && u == 0xFFFFFFFFu);
    CHECK(!parse_u32("4294967296", &u) && !parse_u32("0x", &u) && !parse_u32("12z", &u) &&
          !parse_u32("-1", &u) && !parse_u32("", &u) && !parse_u32(NULL, &u));

    /* ---- Part B: 집계와 판정 ---- */
    CHECK(load(LOG_B) == V_FAIL && exit_code(V_FAIL) == 1);
    tally(&R, c);
    CHECK(R.n_tests == 5 && R.records == 6 && c[ST_PASS] == 1 && c[ST_FAIL] == 2 && c[ST_ERROR] == 1 && c[ST_SKIP] == 1);
    CHECK(find_test(&R, "b") && find_test(&R, "b")->addr == 0x40001004u && find_test(&R, "b")->exp == 1u);
    CHECK(find_test(&R, "d") && find_test(&R, "d")->st == ST_FAIL && find_test(&R, "d")->attempts == 2 &&
          find_test(&R, "d")->dur_us == 18u);                                 /* sticky fail */
    CHECK(find_test(&R, "e") && find_test(&R, "e")->st == ST_ERROR);          /* 모르는 status */
    CHECK(R.unknown_types == 1 && R.bad_lines == 0 && R.n_notes == 1 && R.lvl_warn == 1 && R.lvl_err == 3);
    CHECK(strstr(why, "3 test(s) failed") != NULL);
    CHECK(top_slowest(&R, idx, 3) == 3 && idx[0] == 3 && idx[1] == 1 && idx[2] == 4);   /* d, b, e (동률은 먼저) */
    CHECK(top_slowest(&R, idx, 10) == 5 && idx[4] == 2);
    CHECK(load(HDR T_PASS T_SKIP "{\"type\":\"end\",\"records\":2}\n") == V_PASS);
    CHECK(load(HDR T_PASS T_SKIP) == V_INCOMPLETE && strstr(why, "no end record"));
    CHECK(load(HDR T_PASS "{\"type\":\"end\",\"records\":3}\n") == V_INCOMPLETE);
    CHECK(load(HDR T_PASS "{\"type\":\"test\",\"test\":\"x\",\"sta\n" "{\"type\":\"end\",\"records\":1}\n") == V_INVALID &&
          R.first_bad_line == 3 && R.records == 1);
    CHECK(load(HDR T_SKIP "{\"type\":\"end\",\"records\":1}\n") == V_INVALID && strstr(why, "no test passed"));
    CHECK(load(T_PASS "{\"type\":\"end\",\"records\":1}\n") == V_INVALID && strstr(why, "no header"));
    CHECK(load(HDR HDR T_PASS "{\"type\":\"end\",\"records\":1}\n") == V_INVALID && R.first_bad_line == 2);
    CHECK(load(HDR T_PASS "{\"type\":\"log\",\"lvl\":\"ERR\",\"msg\":\"PMIC OV\"}\n{\"type\":\"end\",\"records\":1}\n") == V_FAIL);

    /* ---- Part C: mock UART → 로그 → 리포트 ---- */
    sb_t L = sb_on(logbuf, sizeof logbuf);
    run_suite((faults_t){ 0, 0, 0 }, &L);
    CHECK(!L.overflow && load(logbuf) == V_PASS);
    tally(&R, c);
    CHECK(c[ST_PASS] == 9 && c[ST_SKIP] == 1 && R.records == 10);
    L = sb_on(logbuf, sizeof logbuf);
    run_suite((faults_t){ 1, 1, 1 }, &L);
    CHECK(!L.overflow && load(logbuf) == V_FAIL && exit_code(V_FAIL) == 1);
    tally(&R, c);
    CHECK(c[ST_PASS] == 6 && c[ST_FAIL] == 3 && c[ST_SKIP] == 1 && R.bad_lines == 0);
    const test_t *t = find_test(&R, "uart.reset.LCR");
    CHECK(t && t->st == ST_FAIL && t->addr == 0x4000100Cu && t->exp == 3u && t->act == 0u);
    t = find_test(&R, "uart.w1c.ISR");
    CHECK(t && t->st == ST_FAIL && t->addr == 0x40001010u && t->exp == 6u && t->act == 1u);
    t = find_test(&R, "uart.poll.THRE");
    CHECK(t && t->st == ST_FAIL && t->exp == 0x20u && t->act == 0u && strstr(t->msg, "timeout"));
    CHECK(top_slowest(&R, idx, 1) == 1 && strcmp(R.t[idx[0]].name, "uart.poll.THRE") == 0);
    sb_t s = sb_on(small, sizeof small);
    sb_md_cell(&s, "a|b\nc");
    CHECK(strcmp(small, "a\\|b c") == 0);
    s = sb_on(out, sizeof out);
    render_md(&R, &s);
    CHECK(!s.overflow && strncmp(out, "# SIV Test Report: FAIL\n", 24) == 0);
    CHECK(strstr(out, "| `uart.reset.LCR` | FAIL |") && strstr(out, "LCR @ 0x4000100C | 0x00000003 | 0x00000000 | 0x00000003 bits=[1,0]"));
    CHECK(strstr(out, "0x7 then write 0x1 \\| expect") != NULL);            /* 파이프 escape */
    CHECK(strstr(out, "## Skipped") && strstr(out, "jumper J7"));
    s = sb_on(out, sizeof out);
    render_text(&R, &s);
    CHECK(strncmp(out, "SIV RESULT: FAIL pass=6 fail=3 error=0 skip=1", 45) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--emit") == 0) {
        int bad = strcmp(argv[2], "faulty") == 0;
        sb_t L = sb_on(logbuf, sizeof logbuf);
        run_suite((faults_t){ bad, bad, bad }, &L);
        fputs(logbuf, stdout);
        return 0;
    }
    if (argc == 3 && (strcmp(argv[1], "--text") == 0 || strcmp(argv[1], "--md") == 0))
        return report_file(argv[2], argv[1][2] == 'm');
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--emit good|faulty | --text FILE | --md FILE]\n", argv[0]);
        return 2;
    }
    return self_test();
}
