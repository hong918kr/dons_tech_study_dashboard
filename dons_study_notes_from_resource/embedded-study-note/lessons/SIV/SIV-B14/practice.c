/* SIV-B14 추가 연습 — 한 번에 검증
 *  1. 3단 종료 코드 (0 PASS / 1 제품 FAIL / 2 인프라·로그 문제)
 *  2. diff 비트를 범위로: 0x000000F3 → "[7:4,1:0]"
 *  3. 베이스라인 대비 회귀 비교 (new fail / fixed / still fail / missing)
 *  4. JUnit XML 한 줄 (XML escape)
 *  5. 소요 시간 회귀 (베이스라인의 2배 + 최소 증가량)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { ST_SKIP, ST_PASS, ST_FAIL, ST_ERROR } st_t;

/* ---- 연습 1: 3단 종료 코드 ---- */
typedef struct { int fails, errors, passes, bad_lines, has_header, has_end, end_mismatch; } summary_t;
static int exit_code3(const summary_t *s) {
    if (s->fails + s->errors > 0) return 1;                       /* 제품 결함이 먼저: 사람이 볼 것 */
    if (!s->has_end || s->end_mismatch || s->bad_lines || !s->has_header || s->passes == 0)
        return 2;                                                 /* 판정 불가: rig/로그 재확인, 재실행 */
    return 0;
}

/* ---- 연습 2: 비트 범위 표기 ---- */
static const char *bit_ranges(uint32_t d, char *buf, size_t cap) {
    size_t n = 0;
    int first = 1;
    n += (size_t)snprintf(buf + n, cap - n, "[");
    for (int b = 31; b >= 0 && n < cap; ) {
        if (!((d >> b) & 1u)) { b--; continue; }
        int hi = b;
        while (b >= 0 && ((d >> b) & 1u)) b--;
        int lo = b + 1;
        if (hi == lo) n += (size_t)snprintf(buf + n, cap - n, "%s%d", first ? "" : ",", hi);
        else          n += (size_t)snprintf(buf + n, cap - n, "%s%d:%d", first ? "" : ",", hi, lo);
        first = 0;
    }
    if (n < cap) snprintf(buf + n, cap - n, "]");
    return buf;
}

/* ---- 연습 3: 회귀 비교 ---- */
typedef struct { const char *name; st_t st; uint32_t dur_us; } res_t;
typedef struct { int new_fail, fixed, still_fail, missing, added; } regress_t;
static const res_t *find(const res_t *v, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(v[i].name, name) == 0) return &v[i];
    return NULL;
}
static regress_t compare(const res_t *base, int nb, const res_t *cur, int nc) {
    regress_t r = { 0, 0, 0, 0, 0 };
    for (int i = 0; i < nb; i++) {
        const res_t *c = find(cur, nc, base[i].name);
        int was_bad = base[i].st >= ST_FAIL;
        if (!c) { r.missing++; continue; }                        /* 커버리지 손실도 회귀다 */
        int is_bad = c->st >= ST_FAIL;
        if (!was_bad && is_bad) r.new_fail++;
        else if (was_bad && !is_bad && c->st == ST_PASS) r.fixed++;
        else if (was_bad && is_bad) r.still_fail++;
    }
    for (int i = 0; i < nc; i++) if (!find(base, nb, cur[i].name)) r.added++;
    return r;
}

/* ---- 연습 4: JUnit XML ---- */
static size_t xml_escape(char *dst, size_t cap, const char *s) {
    size_t n = 0;
    for (; *s && n + 7 < cap; s++) {
        const char *rep = NULL;
        switch (*s) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&apos;"; break;
        }
        if (rep) { size_t L = strlen(rep); memcpy(dst + n, rep, L); n += L; }
        else dst[n++] = *s;
    }
    dst[n] = '\0';
    return n;
}
static void junit_case(char *out, size_t cap, const char *name, st_t st, const char *msg) {
    char en[128], em[256];
    const char *dot = strrchr(name, '.');
    int cls = dot ? (int)(dot - name) : 0;
    xml_escape(en, sizeof en, name);
    xml_escape(em, sizeof em, msg);
    if (st == ST_PASS)
        snprintf(out, cap, "<testcase classname=\"%.*s\" name=\"%s\"/>", cls, name, en);
    else if (st == ST_SKIP)
        snprintf(out, cap, "<testcase classname=\"%.*s\" name=\"%s\"><skipped message=\"%s\"/></testcase>", cls, name, en, em);
    else
        snprintf(out, cap, "<testcase classname=\"%.*s\" name=\"%s\"><%s message=\"%s\"/></testcase>", cls, name, en,
                 st == ST_FAIL ? "failure" : "error", em);
}

/* ---- 연습 5: 소요 시간 회귀 ---- */
static int slow_regressions(const res_t *base, int nb, const res_t *cur, int nc,
                            uint32_t min_delta_us, const char *out[], int max_out) {
    int k = 0;
    for (int i = 0; i < nc && k < max_out; i++) {
        const res_t *b = find(base, nb, cur[i].name);
        if (!b || cur[i].st != ST_PASS) continue;
        uint64_t now = cur[i].dur_us, was = b->dur_us;
        if (now > 2u * was && now - was >= min_delta_us) out[k++] = cur[i].name;
    }
    return k;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 1 */
    summary_t ok = { 0, 0, 9, 0, 1, 1, 0 };
    summary_t f = ok;  f.fails = 3;
    summary_t tr = ok; tr.has_end = 0;
    summary_t ft = f;  ft.has_end = 0;
    summary_t sk = ok; sk.passes = 0;
    CHECK(exit_code3(&ok) == 0);
    CHECK(exit_code3(&f) == 1 && exit_code3(&ft) == 1);
    CHECK(exit_code3(&tr) == 2 && exit_code3(&sk) == 2);

    /* 2 */
    char b[64];
    CHECK(strcmp(bit_ranges(0x000000F3u, b, sizeof b), "[7:4,1:0]") == 0);
    CHECK(strcmp(bit_ranges(0x80000001u, b, sizeof b), "[31,0]") == 0);
    CHECK(strcmp(bit_ranges(0xFFFFFFFFu, b, sizeof b), "[31:0]") == 0);
    CHECK(strcmp(bit_ranges(0u, b, sizeof b), "[]") == 0);
    CHECK(strcmp(bit_ranges(0x00000020u, b, sizeof b), "[5]") == 0);

    /* 3 */
    const res_t base[] = { { "uart.id", ST_PASS, 2 }, { "uart.reset.LCR", ST_PASS, 2 },
                           { "uart.w1c.ISR", ST_FAIL, 8 }, { "uart.poll.THRE", ST_FAIL, 202 },
                           { "uart.rw.SCR", ST_PASS, 20 }, { "uart.dma.tx", ST_PASS, 400 } };
    const res_t cur[]  = { { "uart.id", ST_PASS, 2 }, { "uart.reset.LCR", ST_FAIL, 2 },
                           { "uart.w1c.ISR", ST_PASS, 8 }, { "uart.poll.THRE", ST_ERROR, 202 },
                           { "uart.rw.SCR", ST_PASS, 61 }, { "uart.fifo.depth", ST_PASS, 30 } };
    regress_t r = compare(base, 6, cur, 6);
    CHECK(r.new_fail == 1 && r.fixed == 1 && r.still_fail == 1);
    CHECK(r.missing == 1 && r.added == 1);

    /* 4 */
    char x[256];
    junit_case(x, sizeof x, "uart.w1c.ISR", ST_FAIL, "exp=0x6 act=0x1 & \"W1C as RW\" <bug>");
    CHECK(strcmp(x, "<testcase classname=\"uart.w1c\" name=\"uart.w1c.ISR\"><failure message=\"exp=0x6 act=0x1 &amp; "
                    "&quot;W1C as RW&quot; &lt;bug&gt;\"/></testcase>") == 0);
    junit_case(x, sizeof x, "uart.id", ST_PASS, "");
    CHECK(strcmp(x, "<testcase classname=\"uart\" name=\"uart.id\"/>") == 0);
    junit_case(x, sizeof x, "uart.loopback.ext", ST_SKIP, "needs J7");
    CHECK(strstr(x, "<skipped message=\"needs J7\"/>") != NULL);

    /* 5 */
    const char *slow[4];
    int k = slow_regressions(base, 6, cur, 6, 10, slow, 4);
    CHECK(k == 1 && strcmp(slow[0], "uart.rw.SCR") == 0);           /* 20 → 61 us */
    CHECK(slow_regressions(base, 6, cur, 6, 50, slow, 4) == 0);      /* 증가량 41us < 50us 면 잡음 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
