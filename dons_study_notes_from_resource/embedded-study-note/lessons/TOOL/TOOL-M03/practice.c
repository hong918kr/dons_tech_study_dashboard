/* TOOL-M03 추가 연습 — 한 번에 검증
 *   연습 1: initcall 레벨 (레벨마다 섹션 하나, 레벨 순서대로 실행, 같은 레벨 안의 순서는 믿지 않는다)
 *   연습 2: 실패 정책 (음수를 돌려준 initcall 은 기록하고 계속 — Linux do_one_initcall 처럼)
 *   연습 3: 경계 검사 table_ok (반쪽 오타로 begin > end, 레드존/패딩으로 간격 ≠ sizeof)
 *   연습 4: 탭 완성 shell_complete (정렬된 인덱스에서 접두사 범위 찾기)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o prac && ./prac   (VM: gcc/clang 도 같은 결과)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*initcall_t)(void);

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) && defined(__APPLE__)
#    define NO_ASAN __attribute__((no_sanitize("address")))
#  endif
#endif
#ifndef NO_ASAN
#  define NO_ASAN
#endif

/* ---------------- 연습 1·2: initcall 레벨 ---------------- */
#if defined(__APPLE__)
#  define INITCALL_SECTION(l) "__DATA,__initcall" #l
#  define DECLARE_LEVEL(l)                                                               \
    extern const initcall_t initcall##l##_begin[] __asm("section$start$__DATA$__initcall" #l); \
    extern const initcall_t initcall##l##_end[]   __asm("section$end$__DATA$__initcall" #l)
#elif defined(__ELF__)
#  define INITCALL_SECTION(l) "initcall" #l
/* 레벨이 비어 있으면 섹션이 없고 __start_ 가 정의되지 않는다 → weak 로 받아 NULL..NULL (0개) */
#  define DECLARE_LEVEL(l)                                                               \
    extern const initcall_t __start_initcall##l[] __attribute__((weak));                  \
    extern const initcall_t __stop_initcall##l[] __attribute__((weak));                   \
    static const initcall_t *const initcall##l##_begin = __start_initcall##l;             \
    static const initcall_t *const initcall##l##_end   = __stop_initcall##l
#endif
#define INITCALL(l, fn) \
    static const initcall_t initcall_##fn __attribute__((used, section(INITCALL_SECTION(l)))) NO_ASAN = fn

DECLARE_LEVEL(0);
DECLARE_LEVEL(1);
DECLARE_LEVEL(2);   /* 아무도 등록하지 않는 레벨 */
DECLARE_LEVEL(3);

struct level_range { const initcall_t *b, *e; };

static char g_log[128];
static void logf_(const char *s) { strncat(g_log, s, sizeof g_log - strlen(g_log) - 1); }

static int clock_init(void)  { logf_("0clock ");  return 0; }
static int pinmux_init(void) { logf_("1pinmux "); return 0; }
static int gpio_init(void)   { logf_("1gpio ");   return 0; }
static int sensor_init(void) { logf_("3sensor "); return -5; }     /* 센서가 없음 */
static int app_init(void)    { logf_("3app ");    return 0; }

INITCALL(3, app_init);      /* 정의 순서는 일부러 뒤섞음 */
INITCALL(1, gpio_init);
INITCALL(0, clock_init);
INITCALL(3, sensor_init);
INITCALL(1, pinmux_init);

struct initcall_report { int ran; int failed; int first_err; int per_level[4]; };

/* 레벨 0 → 3 순서로 모두 실행. 실패해도 계속하고 개수 · 첫 에러 코드를 남긴다 */
static struct initcall_report run_initcalls(void) {
    const struct level_range lv[4] = {
        { initcall0_begin, initcall0_end }, { initcall1_begin, initcall1_end },
        { initcall2_begin, initcall2_end }, { initcall3_begin, initcall3_end },
    };
    struct initcall_report r = { 0, 0, 0, { 0, 0, 0, 0 } };
    for (int l = 0; l < 4; l++) {
        for (const initcall_t *f = lv[l].b; f < lv[l].e; f++) {
            int rc = (*f)();
            r.ran++;
            r.per_level[l]++;
            if (rc != 0) {
                if (r.failed == 0) r.first_err = rc;
                r.failed++;
            }
        }
    }
    return r;
}

/* ---------------- 연습 3: 경계 검사 ---------------- */
/* [b,e) 가 elem 바이트짜리 항목의 배열로 볼 수 있으면 1 */
static int table_ok(const void *b, const void *e, size_t elem) {
    uintptr_t ub = (uintptr_t)b, ue = (uintptr_t)e;
    if (elem == 0 || ue < ub) return 0;              /* 반쪽 오타 → begin > end */
    if ((ue - ub) % elem != 0) return 0;             /* 레드존 · 패딩 → 간격 ≠ sizeof */
    return 1;
}

/* ---------------- 연습 4: 탭 완성 ---------------- */
/* names = 이름순 정렬. prefix 로 시작하는 이름 수를 반환하고, out 에
 *   0개 → "" , 1개 → 이름 + " " , 여러 개 → 가장 긴 공통 접두사 */
static int shell_complete(const char *const *names, size_t n, const char *prefix, char *out, size_t cap) {
    size_t plen = strlen(prefix), lo = 0, hi = n;
    while (lo < hi) {                                /* 첫 번째 >= prefix */
        size_t mid = lo + (hi - lo) / 2;
        if (strcmp(names[mid], prefix) < 0) lo = mid + 1; else hi = mid;
    }
    size_t first = lo, last = lo;
    while (last < n && strncmp(names[last], prefix, plen) == 0) last++;
    int cnt = (int)(last - first);
    if (cap == 0) return cnt;
    out[0] = '\0';
    if (cnt == 0) return 0;
    size_t common = strlen(names[first]);
    for (size_t i = first + 1; i < last; i++) {
        size_t k = 0;
        while (k < common && names[i][k] == names[first][k]) k++;
        common = k;
    }
    if (common + 2 > cap) return -1;
    memcpy(out, names[first], common);
    out[common] = '\0';
    if (cnt == 1) { out[common] = ' '; out[common + 1] = '\0'; }
    return cnt;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int has_word(const char *log, const char *w) { return strstr(log, w) != NULL; }

static void test_initcalls(void) {
    g_log[0] = '\0';
    struct initcall_report r = run_initcalls();
    printf("initcall log: %s\n", g_log);
    CHECK(r.ran == 5 && r.per_level[0] == 1 && r.per_level[1] == 2 && r.per_level[2] == 0 && r.per_level[3] == 2);
    /* 레벨 숫자(각 단어의 첫 글자)가 줄지 않는다 = 레벨 순서 보장 */
    int mono = 1; char prev = '0';
    for (const char *p = g_log; *p; ) {
        if (*p < prev) mono = 0;
        prev = *p;
        p = strchr(p, ' ');
        if (p == NULL) break;
        p++;
    }
    CHECK(mono);
    CHECK(has_word(g_log, "1gpio ") && has_word(g_log, "1pinmux ") && has_word(g_log, "0clock ") &&
          has_word(g_log, "3app ") && has_word(g_log, "3sensor "));
    CHECK(r.failed == 1 && r.first_err == -5);             /* 연습 2: 실패해도 3app 까지 돌았다 */
    CHECK(initcall2_begin == initcall2_end);                /* 빈 레벨 */
    CHECK(table_ok(initcall1_begin, initcall1_end, sizeof(initcall_t)));
}

static void test_table_ok(void) {
    static const int arr[6] = { 1, 2, 3, 4, 5, 6 };
    CHECK(table_ok(arr, arr + 6, sizeof arr[0]));
    CHECK(!table_ok(arr + 6, arr, sizeof arr[0]));          /* begin > end */
    CHECK(!table_ok(arr, (const char *)(arr + 5) + 2, sizeof arr[0]));  /* 22 바이트 */
    CHECK(table_ok(arr, arr, sizeof arr[0]));               /* 빈 표는 정상 */
    CHECK(!table_ok(arr, arr + 1, 0));
}

static void test_complete(void) {
    const char *names[] = { "echo", "flash_erase", "flash_read", "flash_write", "help", "led", "reboot" };
    size_t n = sizeof names / sizeof names[0];
    char out[32];
    CHECK(shell_complete(names, n, "he", out, sizeof out) == 1 && strcmp(out, "help ") == 0);
    CHECK(shell_complete(names, n, "fl", out, sizeof out) == 3 && strcmp(out, "flash_") == 0);
    CHECK(shell_complete(names, n, "flash_r", out, sizeof out) == 1 && strcmp(out, "flash_read ") == 0);
    CHECK(shell_complete(names, n, "x", out, sizeof out) == 0 && out[0] == '\0');
    CHECK(shell_complete(names, n, "", out, sizeof out) == 7 && out[0] == '\0');
    CHECK(shell_complete(names, n, "reboot", out, sizeof out) == 1 && strcmp(out, "reboot ") == 0);
    char tiny[4];
    CHECK(shell_complete(names, n, "fl", tiny, sizeof tiny) == -1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_initcalls();
    test_table_ok();
    test_complete();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
