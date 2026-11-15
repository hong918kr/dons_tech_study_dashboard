/* TOOL-B13 컴파일 타임 제거 가능한 로그 매크로
 * 이 파일 하나에 log.h (매크로) + log.c (log_emit, UART 링 싱크) + 테스트가 다 들어 있다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 * 데모:  -DLOG_DEMO_FMT_BUG 를 붙이면 포맷이 틀린 로그 2줄이 들어간다 (경고가 나와야 정답).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= log.h ======================= */
enum { LOG_LVL_NONE = 0, LOG_LVL_ERR = 1, LOG_LVL_WRN = 2, LOG_LVL_INF = 3, LOG_LVL_DBG = 4 };

#ifndef LOG_LEVEL                      /* 컴파일 타임 상한: 빌드마다 -DLOG_LEVEL=1 처럼 바꾼다 */
#define LOG_LEVEL LOG_LVL_INF          /* 테스트는 INF 기준 (DBG 는 바이너리에서 사라져야 함) */
#endif
#define LOG_LINE_MAX 64                /* 한 줄 버퍼 (NUL 포함): 스택에 잡으므로 작게 */

/* 파일 이름: clang 과 GCC 12+ 는 __FILE_NAME__ (basename) 을 컴파일 타임에 준다 */
#ifdef __FILE_NAME__
#define LOG_FILE_ __FILE_NAME__
#else
#define LOG_FILE_ log_basename(__FILE__)
#endif

/* Part A-1: printf 포맷 검사 속성 (fmt 가 몇 번째 인자, 가변 인자가 몇 번째부터) */
#if defined(__GNUC__)
#define LOG_PRINTF_(fmt_idx, first_arg) /* TODO: printf 포맷 검사 속성 */
#else
#define LOG_PRINTF_(fmt_idx, first_arg)
#endif

typedef void (*log_sink_fn)(int lvl, const char *line, size_t len);
extern log_sink_fn g_log_sink;         /* 완성된 한 줄을 받는 출력 (UART, RTT, 테스트 버퍼 …) */
extern int g_log_runtime_level;        /* 런타임 레벨: 컴파일 타임 상한 아래에서만 의미가 있다 */
extern unsigned g_log_truncated;       /* 잘린 줄 수 */

void log_emit(int lvl, const char *file, int line, const char *func, const char *fmt, ...)
    LOG_PRINTF_(5, 6);
const char *log_basename(const char *path);

/* Part A-2: 컴파일 타임에 상수가 되는 "이 레벨이 켜져 있나" */
#define LOG_ENABLED(lvl) 1 /* TODO */

/* Part A-3: 게이트. 꺼진 레벨 = if (0 && ...) → 코드·문자열·인자 평가가 모두 사라진다.
 *           그래도 log_emit 호출은 "파싱"되므로 포맷 검사와 변수 사용은 남는다. */
#define LOG_AT_(lvl, ...) do { \
        /* TODO: 컴파일 타임 상한 + 런타임 레벨로 거른다 (지금은 무조건 호출) */ \
            log_emit((lvl), LOG_FILE_, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

/* fmt 를 __VA_ARGS__ 안에 넣어 C11 그대로 (##__VA_ARGS__ / __VA_OPT__ 불필요) */
#define LOG_E(...) LOG_AT_(LOG_LVL_ERR, __VA_ARGS__)
#define LOG_W(...) LOG_AT_(LOG_LVL_WRN, __VA_ARGS__)
#define LOG_I(...) LOG_AT_(LOG_LVL_INF, __VA_ARGS__)
#define LOG_D(...) LOG_AT_(LOG_LVL_DBG, __VA_ARGS__)

/* ======================= log.c ======================= */
log_sink_fn g_log_sink;
int g_log_runtime_level = LOG_LVL_DBG;
unsigned g_log_truncated;

/* Part B-1: "a/b/c.c" → "c.c". '\\' 도 구분자 (Windows 호스트에서 빌드한 펌웨어) */
const char *log_basename(const char *path) {
    /* TODO */
    return path;



}

/* Part B-2: "[T] file:line func: message\n" 을 LOG_LINE_MAX 안에 만들고 싱크로 보낸다.
 * 넘치면 LOG_LINE_MAX-1 바이트(마지막은 '\n')로 자르고 g_log_truncated++. */
void log_emit(int lvl, const char *file, int line, const char *func, const char *fmt, ...) {
    /* TODO: 접두사 + 본문 + '\n', 잘림 처리, 싱크 호출 */
    (void)lvl; (void)file; (void)line; (void)func; (void)fmt;






















}

/* ---------- Part C: UART TX 링 버퍼 싱크 (ISR 이 비운다고 가정) ---------- */
#define RING_SIZE 256u                 /* 2의 거듭제곱 */
static struct {
    char buf[RING_SIZE];
    size_t head, tail;                 /* 계속 증가하는 인덱스: 사용량 = head - tail */
    unsigned dropped;                  /* 공간이 없어 버린 줄 수 */
} g_uart;

/* 한 줄이 통째로 들어갈 자리가 없으면 버리고 dropped++ (절대 기다리지 않는다, 반쪽 줄 금지) */
void uart_ring_sink(int lvl, const char *line, size_t len) {
    (void)lvl; (void)line; (void)len; /* TODO */







}

/* TX ISR 흉내: 링에서 최대 out_cap-1 바이트를 꺼내 NUL 로 끝낸다 (TODO 아님) */
static size_t uart_ring_drain(char *out, size_t out_cap) {
    size_t n = 0;
    while (g_uart.tail != g_uart.head && n + 1 < out_cap)
        out[n++] = g_uart.buf[g_uart.tail++ % RING_SIZE];
    out[n] = '\0';
    return n;
}

/* 펌웨어 드라이버 흉내 — 이 함수는 고치지 않는다 */
static unsigned g_flash_ops;
static int flash_erase(unsigned sector) {
    LOG_D("erase sector %u start", sector);
    if (sector >= 8u) {
        LOG_E("bad sector %u", sector);
        return -1;
    }
    if (sector == 0u)
        LOG_W("erasing boot sector");
    g_flash_ops++;
    LOG_I("sector %u erased", sector);
    return 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char cap[1024];                 /* 캡처 싱크: 받은 줄을 이어 붙인다 */
static size_t cap_len;
static int cap_lines, cap_last_lvl;
static void cap_sink(int lvl, const char *line, size_t len) {
    if (cap_len + len < sizeof cap) {
        memcpy(cap + cap_len, line, len);
        cap_len += len;
        cap[cap_len] = '\0';
    }
    cap_lines++;
    cap_last_lvl = lvl;
}
static void cap_reset(void) { cap_len = 0; cap[0] = '\0'; cap_lines = 0; cap_last_lvl = -1; }

/* ":123 " 처럼 줄 번호를 ":N " 으로 바꿔 비교 (레이아웃이 바뀌어도 테스트가 안 깨지게) */
static void strip_line_numbers(const char *in, char *out, size_t cap_out) {
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < cap_out; i++) {
        out[o++] = in[i];
        if (in[i] == ':' && in[i + 1] >= '0' && in[i + 1] <= '9') {
            while (in[i + 1] >= '0' && in[i + 1] <= '9')
                i++;
            if (o + 1 < cap_out)
                out[o++] = 'N';
        }
    }
    out[o] = '\0';
}

static int g_calls;
static int side_effect(void) { return ++g_calls; }
static const char g_body[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static void log_body(int body_len) { LOG_I("%.*s", body_len, g_body); }

static void test_part_a(void) {
    char want[160];
    int ln;
    const char *me = log_basename(__FILE__);

    cap_reset();
    LOG_I("x=%d", 5); ln = __LINE__;
    snprintf(want, sizeof want, "[I] %s:%d %s: x=5\n", me, ln, __func__);
    CHECK(strcmp(cap, want) == 0);
    CHECK(cap_lines == 1 && cap_last_lvl == LOG_LVL_INF);

    cap_reset();
    LOG_E("rc=%d", -5); ln = __LINE__;
    LOG_W("temp %u", 91u);
    snprintf(want, sizeof want, "[E] %s:%d %s: rc=-5\n[W] %s:%d %s: temp 91\n",
             me, ln, __func__, me, ln + 1, __func__);
    CHECK(strcmp(cap, want) == 0);

    /* 꺼진 레벨(DBG): 출력 없음, 인자도 평가하지 않는다 */
    cap_reset();
    int n = 10;
    LOG_D("n=%d", n++);
    CHECK(n == 10 && cap_lines == 0);
    /* 켜진 레벨: 인자는 정확히 한 번 평가 */
    LOG_I("n=%d", n++);
    CHECK(n == 11 && cap_lines == 1 && strstr(cap, ": n=10\n") != NULL);

    CHECK(LOG_ENABLED(LOG_LVL_INF) && !LOG_ENABLED(LOG_LVL_DBG));
    CHECK(strchr(cap, '/') == NULL);   /* 접두사에 디렉터리 경로가 없어야 한다 */
}

static void test_part_b(void) {
    char want[160];
    int ln;

    CHECK(strcmp(log_basename("src/drv/uart.c"), "uart.c") == 0);
    CHECK(strcmp(log_basename("main.c"), "main.c") == 0);
    CHECK(strcmp(log_basename("C:\\fw\\src\\spi.c"), "spi.c") == 0);
    CHECK(strcmp(log_basename("build/"), "") == 0);

    /* 가변 인자 없음 + %% */
    cap_reset();
    LOG_E("boom"); ln = __LINE__;
    LOG_W("duty 100%%");
    snprintf(want, sizeof want, "[E] %s:%d %s: boom\n[W] %s:%d %s: duty 100%%\n",
             log_basename(__FILE__), ln, __func__, log_basename(__FILE__), ln + 1, __func__);
    CHECK(strcmp(cap, want) == 0);

    /* do { } while (0): if/else 안에서도 한 문장 */
    cap_reset();
    int k = 0;
    for (int i = 0; i < 2; i++)
        if (i == 0)
            LOG_W("first");
        else
            k = 1;
    CHECK(k == 1 && cap_lines == 1);

    /* 꺼진 레벨은 함수 호출도 하지 않는다 */
    g_calls = 0;
    LOG_D("v=%d", side_effect());
    CHECK(g_calls == 0);
    LOG_I("v=%d", side_effect());
    CHECK(g_calls == 1);

    /* 잘림: 딱 맞으면 그대로, 1 바이트 넘치면 잘리고 '\n' 으로 끝난다 */
    cap_reset(); log_body(0);
    size_t plen = cap_len - 1;         /* 접두사 길이 ("[I] file:line log_body: ") */
    cap_reset(); g_log_truncated = 0; log_body((int)(LOG_LINE_MAX - 2 - plen));
    CHECK(cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 1] == '\n' && g_log_truncated == 0);
    cap_reset(); log_body((int)(LOG_LINE_MAX - 1 - plen));
    CHECK(cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 1] == '\n' && g_log_truncated == 1);
    cap_reset(); log_body(100);
    CHECK(cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 2] == 'x' && g_log_truncated == 2);

    /* 싱크가 없으면 조용히 아무것도 안 한다 */
    g_log_sink = NULL;
    LOG_E("nobody listens");
    g_log_sink = cap_sink;
    CHECK(cap_lines == 1);
}

static void test_part_c(void) {
    char out[512], norm[512], want[256];

    /* 런타임 레벨: WRN 으로 낮추면 INF 는 출력도 인자 평가도 없다 */
    cap_reset();
    g_log_runtime_level = LOG_LVL_WRN;
    int n = 0;
    LOG_I("n=%d", n++);
    LOG_W("w=%d", n++);
    CHECK(n == 1 && cap_lines == 1 && cap_last_lvl == LOG_LVL_WRN);
    /* 런타임으로 DBG 를 켜도 컴파일 타임 상한(INF)을 넘지 못한다 */
    g_log_runtime_level = LOG_LVL_DBG;
    LOG_D("n=%d", n++);
    CHECK(n == 1 && cap_lines == 1);

    /* UART 링 싱크 + 드라이버 */
    g_log_sink = uart_ring_sink;
    CHECK(flash_erase(0) == 0);
    CHECK(flash_erase(9) == -1);
    size_t got = uart_ring_drain(out, sizeof out);
    strip_line_numbers(out, norm, sizeof norm);
    const char *me = log_basename(__FILE__);
    snprintf(want, sizeof want, "[W] %s:N flash_erase: erasing boot sector\n"
             "[I] %s:N flash_erase: sector 0 erased\n[E] %s:N flash_erase: bad sector 9\n", me, me, me);
    CHECK(got > 0 && strcmp(norm, want) == 0);
    CHECK(g_uart.dropped == 0 && g_flash_ops == 1);

    /* 링이 차면 줄 단위로 버린다 (반쪽 줄 없음, 기다리지 않음) */
    int ok = 0;
    for (int i = 0; i < 6; i++)
        ok += flash_erase(3) == 0;
    got = uart_ring_drain(out, sizeof out);
    const char *nl = strchr(out, '\n');
    size_t line_len = nl ? (size_t)(nl - out) + 1 : 0;
    unsigned lines = 0;
    for (size_t i = 0; i < got; i++)
        lines += out[i] == '\n';
    CHECK(ok == 6 && got > 0 && out[got - 1] == '\n' && got == lines * line_len);
    CHECK(line_len > 0 && lines == RING_SIZE / line_len && g_uart.dropped == 6u - lines);
    g_log_sink = cap_sink;
}

int main(void) {
    g_log_sink = cap_sink;
    test_part_a();
    test_part_b();
    test_part_c();
#ifdef LOG_DEMO_FMT_BUG
    LOG_D("%s", 42);                   /* 꺼진 레벨이어도 경고가 나와야 한다 */
    LOG_I("%s", 42);
#endif
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
