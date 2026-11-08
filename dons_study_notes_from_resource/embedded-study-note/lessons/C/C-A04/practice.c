/* C-A04 추가 연습 — 한 번에 검증
 *   연습 1: my_strsep — 빈 필드를 살리는 토크나이저 (NMEA 0183 문장)
 *   연습 2: tok_span — 입력을 고치지 않는(const) 재진입 토크나이저 (플래시 문자열, DMA 버퍼)
 *   연습 3: UART 줄 조립기 — static 상태를 컨텍스트 구조체로 (UART 두 개)
 *   연습 4: 난수 발생기 — 숨은 상태 vs 상태 인자 (ISR 이 끼어들 때)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

static int is_delim(char c, const char *delim) {
    for (; *delim; delim++)
        if (*delim == c) return 1;
    return 0;
}
static char *my_strtok_r(char *s, const char *delim, char **save) {   /* 본문 solution 과 같은 것 */
    if (s == NULL) s = *save;
    if (s == NULL) return NULL;
    while (*s && is_delim(*s, delim)) s++;
    if (*s == '\0') { *save = s; return NULL; }
    char *tok = s;
    while (*s && !is_delim(*s, delim)) s++;
    if (*s) { *s = '\0'; *save = s + 1; } else *save = s;
    return tok;
}
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ---------- 연습 1: strsep (BSD) — 구분자 하나하나가 필드 경계, 빈 필드도 토큰 ---------- */
static char *my_strsep(char **sp, const char *delim) {
    char *tok = *sp, *s = tok;
    if (tok == NULL) return NULL;                  /* 이미 끝남 */
    while (*s && !is_delim(*s, delim)) s++;
    if (*s) { *s = '\0'; *sp = s + 1; }           /* 다음 필드 시작 (빈 필드일 수 있음) */
    else *sp = NULL;                               /* 마지막 필드였다 */
    return tok;
}
static int count_fields_strsep(char *line) {
    int n = 0;
    for (char *p = line; my_strsep(&p, ","); ) n++;
    return n;
}
static int count_fields_strtok(char *line) {
    int n = 0;
    char *sv;
    for (char *t = my_strtok_r(line, ",", &sv); t; t = my_strtok_r(NULL, ",", &sv)) n++;
    return n;
}

/* ---------- 연습 2: 비파괴 토크나이저 — 커서만 움직이고 (시작, 길이) 를 돌려준다 ---------- */
static int tok_span(const char **cur, const char *delim, const char **start, size_t *len) {
    const char *s = *cur;
    while (*s && is_delim(*s, delim)) s++;
    if (*s == '\0') { *cur = s; return 0; }
    *start = s;
    while (*s && !is_delim(*s, delim)) s++;
    *len = (size_t)(s - *start);
    *cur = s;                                      /* 구분자 위(또는 NUL)에 멈춘다 — 다음 호출이 건너뛴다 */
    return 1;
}

/* ---------- 연습 3: UART 줄 조립기 ---------- */
#define UART_LINE_MAX 16
static const char *line_feed_static(char c) {      /* 옛날식: 상태가 파일 static → UART 하나만 가능 */
    static char buf[UART_LINE_MAX];
    static size_t n;
    if (c == '\r') return NULL;
    if (c == '\n') { buf[n] = '\0'; n = 0; return buf; }
    if (n + 1 < UART_LINE_MAX) buf[n++] = c;
    return NULL;
}
typedef struct { char buf[UART_LINE_MAX]; char line[UART_LINE_MAX]; size_t n; unsigned overflow; } line_ctx_t;
static const char *line_feed(line_ctx_t *cx, char c) {   /* 재진입: 상태는 전부 cx 안 */
    if (c == '\r') return NULL;
    if (c == '\n') {
        memcpy(cx->line, cx->buf, cx->n);
        cx->line[cx->n] = '\0';                    /* 완성 줄은 따로 → 다음 바이트가 덮지 않는다 */
        cx->n = 0;
        return cx->line;
    }
    if (cx->n + 1 < UART_LINE_MAX) cx->buf[cx->n++] = c;
    else cx->overflow++;
    return NULL;
}

/* ---------- 연습 4: 난수 — 숨은 상태 vs 상태 인자 ---------- */
static uint32_t rng_static(void) { static uint32_t s = 12345u; return xorshift32(&s); }
static uint32_t rng_r(uint32_t *state) { return xorshift32(state); }
static uint32_t g_isr_draw;
static void isr_uses_rng_static(void) { g_isr_draw = rng_static(); }
static void isr_uses_rng_r(void) { static uint32_t isr_state = 999u; g_isr_draw = rng_r(&isr_state); }

int main(void) {
    /* 연습 1 */
    char g1[] = "$GPGGA,123519,4807.038,N,,,1,08", g2[sizeof g1];
    memcpy(g2, g1, sizeof g1);
    CHECK(count_fields_strsep(g1) == 8);           /* 빈 필드 2개 포함 */
    CHECK(count_fields_strtok(g2) == 6);           /* strtok 는 ",,," 를 구분자 하나처럼 → 필드 위치가 밀린다 */
    char g3[] = "a,,b,", *p = g3;
    const char *want3[] = { "a", "", "b", "" };
    int ok = 1;
    for (int i = 0; i < 4; i++) { char *t = my_strsep(&p, ","); if (!t || !STREQ(t, want3[i])) ok = 0; }
    CHECK(ok && p == NULL && my_strsep(&p, ",") == NULL);
    char g4[] = "", *q = g4;
    CHECK(my_strsep(&q, ",") == g4 && q == NULL);  /* 빈 문자열 = 빈 필드 1개 (strtok 는 0개) */

    /* 연습 2 — my_strtok_r 과 같은 토큰열인지 결정적 퍼즈로 대조, 입력은 그대로 */
    uint32_t seed = 0xA04u;
    int bad = 0;
    for (int c = 0; c < 3000; c++) {
        static const char alpha[] = "ab,; ";
        char src[24], copy[24];
        int len = (int)(xorshift32(&seed) % 21u);
        for (int i = 0; i < len; i++) src[i] = alpha[xorshift32(&seed) % (sizeof alpha - 1)];
        src[len] = '\0';
        memcpy(copy, src, sizeof src);
        const char *cur = src, *st;
        size_t sl;
        char *sv, *t = my_strtok_r(copy, ",; ", &sv);
        int more = tok_span(&cur, ",; ", &st, &sl);
        while (t && more) {
            if (st - src != t - copy || sl != strlen(t)) { bad++; break; }
            t = my_strtok_r(NULL, ",; ", &sv);
            more = tok_span(&cur, ",; ", &st, &sl);
        }
        if ((t == NULL) != (more == 0)) bad++;
        if ((int)strlen(src) != len) bad++;        /* 입력이 바뀌지 않았다 */
    }
    CHECK(bad == 0);
    static const char flash_cmd[] = "led 1 on";    /* const (MCU 에선 플래시) — strtok 로는 못 자른다 */
    const char *cur = flash_cmd, *st;
    size_t sl;
    char words[32] = "";
    while (tok_span(&cur, " ", &st, &sl)) { strncat(words, st, sl); strcat(words, "|"); }
    CHECK(STREQ(words, "led|1|on|"));

    /* 연습 3 — UART1 "AT+X\n", UART2 "OK\n" 의 바이트가 번갈아 도착 */
    const char *u1 = "AT+X\r\n", *u2 = "OK\r\n";
    char got_static[2][UART_LINE_MAX] = { "", "" };
    line_ctx_t cx[2];
    memset(cx, 0, sizeof cx);
    char got_ctx[2][UART_LINE_MAX] = { "", "" };
    for (size_t i = 0; u1[i] || (i < strlen(u2)); i++) {
        for (int port = 0; port < 2; port++) {
            const char *src = port == 0 ? u1 : u2;
            if (i >= strlen(src)) continue;
            const char *l1 = line_feed_static(src[i]);
            if (l1) memcpy(got_static[port], l1, strlen(l1) + 1);
            const char *l2 = line_feed(&cx[port], src[i]);
            if (l2) memcpy(got_ctx[port], l2, strlen(l2) + 1);
        }
    }
    CHECK(STREQ(got_static[1], "AOTK+X") && STREQ(got_static[0], ""));   /* 두 UART 가 한 버퍼에 섞임 */
    CHECK(STREQ(got_ctx[0], "AT+X") && STREQ(got_ctx[1], "OK"));
    line_ctx_t big;
    memset(&big, 0, sizeof big);
    const char *res = NULL;
    for (const char *s = "0123456789ABCDEFGHIJ\n"; *s; s++) { const char *r = line_feed(&big, *s); if (r) res = r; }
    CHECK(res && STREQ(res, "0123456789ABCDE") && big.overflow == 5u);     /* 15자 + NUL, 5자 버림 */

    /* 연습 4 — main 이 4개를 뽑는 도중(2번째 뒤) ISR 이 1개 뽑는다 */
    uint32_t ref_s = 12345u, ref[4];
    for (int i = 0; i < 4; i++) ref[i] = xorshift32(&ref_s);              /* 끼어들기 없는 기준 수열 */
    uint32_t got[4];
    for (int i = 0; i < 4; i++) { got[i] = rng_static(); if (i == 1) isr_uses_rng_static(); }
    CHECK(got[0] == ref[0] && got[1] == ref[1] && g_isr_draw == ref[2] && got[2] == ref[3]);  /* ISR 이 main 의 수를 훔침 */
    uint32_t st_main = 12345u;
    for (int i = 0; i < 4; i++) { got[i] = rng_r(&st_main); if (i == 1) isr_uses_rng_r(); }
    CHECK(memcmp(got, ref, sizeof ref) == 0);      /* 상태 인자: main 의 수열은 그대로 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
