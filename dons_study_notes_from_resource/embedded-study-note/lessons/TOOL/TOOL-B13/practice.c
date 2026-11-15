/* TOOL-B13 추가 연습 — LOG_ONCE / LOG_EVERY_N / 모듈별 컴파일 타임 레벨 / 토큰화(지연) 로그
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 * 로그 코어(log_emit, 게이트)는 solution.c 와 같다 — 줄 형식만 짧게 "[T] func: msg\n".
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { LOG_LVL_NONE = 0, LOG_LVL_ERR = 1, LOG_LVL_WRN = 2, LOG_LVL_INF = 3, LOG_LVL_DBG = 4 };
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INF
#endif
#define LOG_PRINTF_(a, b) __attribute__((format(printf, a, b)))

typedef void (*log_sink_fn)(int lvl, const char *line, size_t len);
static log_sink_fn g_log_sink;
static int g_log_runtime_level = LOG_LVL_DBG;

static void log_emit(int lvl, const char *func, const char *fmt, ...) LOG_PRINTF_(3, 4);
static void log_emit(int lvl, const char *func, const char *fmt, ...) {
    char buf[96];
    int n = snprintf(buf, sizeof buf, "[%c] %s: ", "?EWID"[lvl >= 1 && lvl <= 4 ? lvl : 0], func);
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
    va_end(ap);
    size_t len = (size_t)n + (m > 0 ? (size_t)m : 0);
    if (len > sizeof buf - 2)
        len = sizeof buf - 2;
    buf[len++] = '\n';
    buf[len] = '\0';
    if (g_log_sink)
        g_log_sink(lvl, buf, len);
}

#define LOG_ENABLED(lvl) ((lvl) <= LOG_LEVEL)
#define LOG_AT_(lvl, ...) do { \
        if (LOG_ENABLED(lvl) && (lvl) <= g_log_runtime_level) \
            log_emit((lvl), __func__, __VA_ARGS__); \
    } while (0)
#define LOG_E(...) LOG_AT_(LOG_LVL_ERR, __VA_ARGS__)
#define LOG_W(...) LOG_AT_(LOG_LVL_WRN, __VA_ARGS__)
#define LOG_I(...) LOG_AT_(LOG_LVL_INF, __VA_ARGS__)
#define LOG_D(...) LOG_AT_(LOG_LVL_DBG, __VA_ARGS__)

/* ---------- 연습 1: LOG_ONCE — 호출 지점(call site)마다 딱 한 번 ---------- */
/* 매크로 안의 static 은 "펼쳐진 자리마다" 하나씩 생긴다 → 호출 지점별 플래그 */
#define LOG_ONCE(lvl, ...) do { \
        static unsigned char once_; \
        if (LOG_ENABLED(lvl) && !once_) { once_ = 1; LOG_AT_(lvl, __VA_ARGS__); } \
    } while (0)

/* ---------- 연습 2: LOG_EVERY_N — 1번째, N+1번째, 2N+1번째 … 만 ---------- */
#define LOG_EVERY_N(n, lvl, ...) do { \
        static unsigned cnt_; \
        if (LOG_ENABLED(lvl) && (cnt_++ % (n)) == 0u) LOG_AT_(lvl, __VA_ARGS__); \
    } while (0)

/* ---------- 연습 3: 모듈별 컴파일 타임 레벨 ---------- */
/* 각 .c 가 #define FLASH_LOG_LEVEL ... 처럼 자기 레벨을 정하고 MLOG(FLASH, D, ...) 로 부른다 */
#define FLASH_LOG_LEVEL LOG_LVL_DBG    /* 디버깅 중인 모듈만 DBG */
#define UART_LOG_LEVEL  LOG_LVL_ERR    /* 시끄러운 모듈은 ERR 만 */
#define LVL_E_ LOG_LVL_ERR
#define LVL_W_ LOG_LVL_WRN
#define LVL_I_ LOG_LVL_INF
#define LVL_D_ LOG_LVL_DBG
#define MLOG(mod, L, ...) do { \
        if (LVL_##L##_ <= mod##_LOG_LEVEL) \
            log_emit(LVL_##L##_, #mod, __VA_ARGS__); \
    } while (0)

/* ---------- 연습 4: 토큰화(지연) 로그 — 타깃은 포맷하지 않고 (포맷 위치, 인자) 만 저장 ---------- */
typedef struct {
    const char *fmt;                   /* 실제 시스템: 문자열 테이블 안의 오프셋/ID (4 B 이하) */
    int32_t a, b;
} tlog_rec_t;
#define TLOG_CAP 8u
static tlog_rec_t g_tlog[TLOG_CAP];
static unsigned g_tlog_n, g_tlog_dropped;

static void tlog_put(const char *fmt, int32_t a, int32_t b) {
    if (g_tlog_n >= TLOG_CAP) { g_tlog_dropped++; return; }
    g_tlog[g_tlog_n++] = (tlog_rec_t){ fmt, a, b };
}
/* 포맷 검사만 하는 가짜 함수 — if (0) 안에서만 불린다 (정의 불필요한 선언이면 충분하지만
   -Werror 환경 안전을 위해 빈 정의를 둔다) */
static void tlog_fmt_check(const char *fmt, ...) LOG_PRINTF_(1, 2);
static void tlog_fmt_check(const char *fmt, ...) { (void)fmt; }

#define TLOG(fmt, a, b) do { \
        if (0) tlog_fmt_check(fmt, (int)(a), (int)(b)); \
        static const char fmt_[] = fmt; \
        tlog_put(fmt_, (int32_t)(a), (int32_t)(b)); \
    } while (0)

/* 호스트 쪽 디코더: 레코드를 사람이 읽는 줄로 */
static size_t tlog_decode(char *out, size_t cap) {
    size_t o = 0;
    for (unsigned i = 0; i < g_tlog_n && o < cap; i++) {
        int k = snprintf(out + o, cap - o, g_tlog[i].fmt, (int)g_tlog[i].a, (int)g_tlog[i].b);
        if (k < 0) break;
        o += (size_t)k;
        if (o < cap - 1) { out[o++] = '\n'; out[o] = '\0'; }
    }
    return o;
}
/* 위 snprintf 의 fmt 는 리터럴이 아니다 → 디코더 안에서만 -Wformat-nonliteral 이 맞는 경고.
   -Wall -Wextra 에는 없지만 -Wformat=2 를 쓰는 프로젝트라면 이 함수만 pragma 로 끈다. */

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char cap[1024];
static size_t cap_len;
static int cap_lines;
static void cap_sink(int lvl, const char *line, size_t len) {
    (void)lvl;
    if (cap_len + len < sizeof cap) { memcpy(cap + cap_len, line, len); cap_len += len; cap[cap_len] = '\0'; }
    cap_lines++;
}
static void cap_reset(void) { cap_len = 0; cap[0] = '\0'; cap_lines = 0; }

static void poll_sensor(int i) { LOG_ONCE(LOG_LVL_WRN, "sensor not calibrated (i=%d)", i); }

int main(void) {
    g_log_sink = cap_sink;

    /* 연습 1 */
    cap_reset();
    for (int i = 0; i < 5; i++)
        poll_sensor(i);
    for (int i = 0; i < 3; i++)
        LOG_ONCE(LOG_LVL_ERR, "other site i=%d", i);
    CHECK(cap_lines == 2);
    CHECK(strcmp(cap, "[W] poll_sensor: sensor not calibrated (i=0)\n[E] main: other site i=0\n") == 0);

    /* 연습 2 */
    cap_reset();
    for (int i = 0; i < 10; i++)
        LOG_EVERY_N(4u, LOG_LVL_INF, "rx overrun #%d", i);
    CHECK(strcmp(cap, "[I] main: rx overrun #0\n[I] main: rx overrun #4\n[I] main: rx overrun #8\n") == 0);
    int evals = 0;
    for (int i = 0; i < 10; i++)
        LOG_EVERY_N(2u, LOG_LVL_DBG, "dbg %d", evals++);   /* DBG 는 컴파일에서 꺼짐 */
    CHECK(evals == 0 && cap_lines == 3);

    /* 연습 3 */
    cap_reset();
    int n = 0;
    MLOG(FLASH, D, "page %d programmed", n++);
    MLOG(UART, W, "framing error %d", n++);
    MLOG(UART, E, "dma stuck %d", n++);
    CHECK(n == 2);
    CHECK(strcmp(cap, "[D] FLASH: page 0 programmed\n[E] UART: dma stuck 1\n") == 0);

    /* 연습 4 */
    TLOG("boot reason %d, wdt %d", 3, 0);
    for (int i = 0; i < 9; i++)
        TLOG("adc ch%d = %d", i, 1000 + i);
    char out[512];
    tlog_decode(out, sizeof out);
    CHECK(g_tlog_n == TLOG_CAP && g_tlog_dropped == 2u);
    CHECK(strncmp(out, "boot reason 3, wdt 0\nadc ch0 = 1000\nadc ch1 = 1001\n", 51) == 0);
    CHECK(g_tlog[1].fmt == g_tlog[7].fmt);  /* 같은 호출 지점 = 같은 포맷 (ID 하나) */
    printf("  [practice 4] %u records x %zu B = %zu B stored vs %zu B of formatted text\n",
           g_tlog_n, sizeof(tlog_rec_t), g_tlog_n * sizeof(tlog_rec_t), strlen(out));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
