/* TOOL-M03 커스텀 섹션으로 자동 등록 테이블 만들기 — 셸 명령 테이블
 *
 * SHELL_CMD(name, fn, help) 한 줄이 const struct shell_cmd 하나를 커스텀 섹션에 넣는다.
 * 링커가 같은 이름의 섹션을 이어 붙여 "배열" 을 만들고, 경계 심볼로 그 배열을 순회한다.
 *   Mach-O : 섹션 "__DATA,__shellcmds",  경계 section$start$__DATA$__shellcmds / section$end$...
 *   ELF    : 섹션 "shellcmds"(C 식별자!), 경계 __start_shellcmds / __stop_shellcmds (링커가 자동 정의)
 *   ELF + 링커 스크립트 (-DSHELL_LDSCRIPT): KEEP(*(.shellcmds)) 와 __shellcmds_start/__shellcmds_end
 *
 * build: export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 *        cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct shell_cmd {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *help;
};

/* ---------------- 이식성 헤더 (Part A) ---------------- */
#define SHELL_CAT_(a, b) a##b
#define SHELL_CAT(a, b)  SHELL_CAT_(a, b)

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define SHELL_ASAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)                 /* GCC 는 이 매크로로 알려 준다 */
#  define SHELL_ASAN 1
#endif
#if defined(SHELL_ASAN) && defined(__APPLE__)
/* Mach-O 의 ASan 은 커스텀 섹션 전역에도 레드존을 붙인다 → 배열 간격이 깨진다 (5. 풀이 B-3) */
#  define SHELL_NO_ASAN __attribute__((no_sanitize("address")))
#else
#  define SHELL_NO_ASAN
#endif

#if defined(__APPLE__)
#  define SHELL_SECT_NAME "__shellcmds"                       /* TODO(B): 이름은 한 곳에서만 */
#  define SHELL_SECTION   "__DATA,__shellcmds"
extern const struct shell_cmd shell_cmds_begin[] __asm("section$start$__DATA$__shellcmd");
extern const struct shell_cmd shell_cmds_end[]   __asm("section$end$__DATA$__shellcmd");
#elif defined(__ELF__) && defined(SHELL_LDSCRIPT)
#  define SHELL_SECTION ".shellcmds"                          /* 스크립트가 KEEP + 경계 심볼 정의 */
extern const struct shell_cmd __shellcmds_start[], __shellcmds_end[];
#  define shell_cmds_begin __shellcmds_start
#  define shell_cmds_end   __shellcmds_end
#elif defined(__ELF__)
#  define SHELL_SECT_ID shellcmds                             /* C 식별자여야 __start_/__stop_ 이 생긴다 */
#  define SHELL_STR_(x) #x
#  define SHELL_STR(x)  SHELL_STR_(x)
#  define SHELL_SECTION SHELL_STR(SHELL_SECT_ID)
extern const struct shell_cmd SHELL_CAT(__start_, SHELL_SECT_ID)[];
extern const struct shell_cmd SHELL_CAT(__stop_, SHELL_SECT_ID)[];
#  define shell_cmds_begin SHELL_CAT(__start_, SHELL_SECT_ID)
#  define shell_cmds_end   SHELL_CAT(__stop_, SHELL_SECT_ID)
#else
#  error "need Mach-O or ELF"
#endif

/* 등록 매크로: 이름 하나로 정적 객체 하나 + 섹션 지정. TODO(A/B): 속성 */
#define SHELL_ENTRY_ATTR __attribute__((unused, section(SHELL_SECTION)))  /* "unused 경고" 만 껐다 */
#define SHELL_CMD(name_, fn_, help_) \
    static const struct shell_cmd SHELL_CAT(shell_cmd_, name_) SHELL_ENTRY_ATTR = { #name_, fn_, help_ }

/* ---------------- Part A: 경계 → 개수 / i 번째 ---------------- */
static size_t shell_count(const struct shell_cmd *b, const struct shell_cmd *e) {
    return (size_t)((const char *)e - (const char *)b);   /* TODO(A): "항목" 수 (바이트 수 아님) */
}
static const struct shell_cmd *shell_at(const struct shell_cmd *b, size_t i) {
    return (const struct shell_cmd *)((const char *)b + i); /* TODO(A): i 번째 항목의 주소 */
}

/* ---------------- Part B: 정렬 인덱스 · 중복 · 검색 ---------------- */
enum { SHELL_MAX = 32, SHELL_ARGV_MAX = 8 };
enum { SHELL_ENOENT = -1, SHELL_E2BIG = -2, SHELL_EDUP = -3, SHELL_EFULL = -4 };

static int cmp_cmd(const void *a, const void *b) {
    const struct shell_cmd *x = *(const struct shell_cmd *const *)a;
    const struct shell_cmd *y = *(const struct shell_cmd *const *)b;
    int c = strcmp(x->name, y->name);
    if (c != 0) return c;
    return (x > y) - (x < y);                       /* 같은 이름이면 주소 순: 결과가 결정적 */
}

/* [b,e) 의 항목 포인터를 idx 에 담고 이름순 정렬. 넘치면 SHELL_EFULL. */
static int shell_build_index(const struct shell_cmd *b, const struct shell_cmd *e,
                             const struct shell_cmd **idx, size_t max) {
    (void)b; (void)e; (void)idx; (void)max; (void)cmp_cmd;   /* TODO(B) */
    return 0;
}

/* 정렬된 idx 에서 "두 번 이상 나온 이름" 을 한 번씩 dups 에 담고 개수 반환 */
static size_t shell_find_dups(const struct shell_cmd *const *idx, size_t n,
                              const char **dups, size_t max) {
    (void)idx; (void)n; (void)dups; (void)max;     /* TODO(B) */
    return 0;
}

/* 이진 검색. 없으면 NULL + SHELL_ENOENT, 이름이 둘 이상이면 NULL + SHELL_EDUP */
static const struct shell_cmd *shell_lookup(const struct shell_cmd *const *idx, size_t n,
                                            const char *name, int *err) {
    (void)idx; (void)n; (void)name;                 /* TODO(B): 첫 번째 >= name 위치 */
    *err = SHELL_ENOENT;
    return NULL;
}

/* ---------------- Part C: 셸 (부팅 때 한 번 init, 이후 줄 단위 실행) ---------------- */
struct shell {
    const struct shell_cmd *idx[SHELL_MAX];
    size_t n;
    size_t ndup;
};

static int shell_init(struct shell *sh, const struct shell_cmd *b, const struct shell_cmd *e) {
    int n = shell_build_index(b, e, sh->idx, SHELL_MAX);
    if (n < 0) return n;
    sh->n = (size_t)n;
    const char *d[4];
    sh->ndup = shell_find_dups(sh->idx, sh->n, d, 4);
    for (size_t i = 0; i < sh->ndup && i < 4; i++) fprintf(stderr, "shell: duplicate command '%s'\n", d[i]);
    return sh->ndup ? SHELL_EDUP : 0;
}

/* line 을 제자리에서 공백(' ', '\t')으로 자른다. 빈 줄 → 0, 인자 > 8 → E2BIG, 모름 → ENOENT */
static int shell_exec(const struct shell *sh, char *line) {
    (void)sh; (void)line;                           /* TODO(C) */
    return SHELL_ENOENT;
}

/* "name     help\n" (이름 8칸 왼쪽 정렬) 을 이름순으로. 반환 = 쓴 길이, 넘치면 -1 */
static int shell_help(const struct shell *sh, char *buf, size_t cap) {
    (void)sh; (void)buf; (void)cap;                 /* TODO(C) */
    return -1;
}

/* ---------------- 등록되는 명령들 (실제로는 여러 .c 파일에 흩어져 있다) ---------------- */
static char g_out[256];
static int g_led = -1;
static struct shell g_sh;

static int cmd_led(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "on") == 0)  { g_led = 1; return 0; }
    if (strcmp(argv[1], "off") == 0) { g_led = 0; return 0; }
    return 2;                                       /* usage */
}
static int cmd_echo(int argc, char **argv) {
    size_t len = 0;
    g_out[0] = '\0';
    for (int i = 1; i < argc; i++) {
        int w = snprintf(g_out + len, sizeof g_out - len, i > 1 ? " %s" : "%s", argv[i]);
        if (w < 0 || (size_t)w >= sizeof g_out - len) return 1;   /* 잘림 */
        len += (size_t)w;
    }
    return 0;
}
static int cmd_sum(int argc, char **argv) {
    long s = 0;
    for (int i = 1; i < argc; i++) s += strtol(argv[i], NULL, 0);
    snprintf(g_out, sizeof g_out, "%ld", s);
    return 0;
}
static int cmd_version(int argc, char **argv) { (void)argc; (void)argv; snprintf(g_out, sizeof g_out, "fw 1.4.2"); return 0; }
static int cmd_uptime(int argc, char **argv)  { (void)argc; (void)argv; snprintf(g_out, sizeof g_out, "42 s"); return 0; }
static int cmd_reboot(int argc, char **argv)  { (void)argc; (void)argv; snprintf(g_out, sizeof g_out, "rebooting"); return 0; }
static int cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    return shell_help(&g_sh, g_out, sizeof g_out) < 0 ? 1 : 0;
}

SHELL_CMD(led,     cmd_led,     "led on|off");
SHELL_CMD(echo,    cmd_echo,    "echo args");
SHELL_CMD(sum,     cmd_sum,     "add integers");
SHELL_CMD(version, cmd_version, "firmware version");
SHELL_CMD(uptime,  cmd_uptime,  "seconds since boot");
SHELL_CMD(reboot,  cmd_reboot,  "soft reset");
SHELL_CMD(help,    cmd_help,    "list commands");
#define N_REGISTERED 7

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int dummy(int argc, char **argv) { (void)argc; (void)argv; return 0; }

static void test_part_a(void) {
    const struct shell_cmd arr[3] = { { "a", dummy, "" }, { "b", dummy, "" }, { "c", dummy, "" } };
    CHECK(shell_count(arr, arr + 3) == 3);                          /* 바이트 수(72)가 아니라 3 */
    CHECK(shell_at(arr, 2) == &arr[2] && shell_at(arr, 2)->name[0] == 'c');

    const struct shell_cmd *b = shell_cmds_begin, *e = shell_cmds_end;
    size_t bytes = (size_t)((const char *)e - (const char *)b);
    const struct shell_cmd *mine = &shell_cmd_help;                 /* 이름으로 참조 → 반드시 존재 */
    CHECK(mine >= b && mine < e);                                   /* 경계 심볼이 진짜 섹션을 가리키나 */
    CHECK(bytes % sizeof(struct shell_cmd) == 0);                   /* 레드존/패딩 없음 = 간격 = sizeof */
    CHECK(shell_count(b, e) == N_REGISTERED);                       /* 아무도 참조 안 한 6개도 살아 있나 */
    int ok = shell_count(b, e) > 0;
    for (size_t i = 0; i < shell_count(b, e); i++) {
        const struct shell_cmd *c = shell_at(b, i);
        ok = ok && c->name != NULL && c->fn != NULL && c->help != NULL;
    }
    CHECK(ok);
    printf("section: %zu bytes, sizeof(entry) = %zu, %zu entries\n", bytes, sizeof(struct shell_cmd),
           bytes / sizeof(struct shell_cmd));
}

static void test_part_b(void) {
    /* 두 모듈(sys.c, pwr.c)이 모두 "reboot" 를 등록한 상황을 평범한 배열로 흉내 */
    const struct shell_cmd t[6] = {
        { "reboot", dummy, "sys" }, { "led", dummy, "" }, { "reboot", dummy, "pwr" },
        { "echo", dummy, "" },      { "sum", dummy, "" }, { "reboot", dummy, "bl" },
    };
    const struct shell_cmd *idx[8];
    int n = shell_build_index(t, t + 6, idx, 8);
    CHECK(n == 6);
    int sorted = n == 6;
    for (int i = 1; i < n && sorted; i++) sorted = strcmp(idx[i - 1]->name, idx[i]->name) <= 0;
    CHECK(sorted && n == 6 && strcmp(idx[0]->name, "echo") == 0 && strcmp(idx[5]->name, "sum") == 0);
    CHECK(n == 6 && idx[2] == &t[0] && idx[3] == &t[2] && idx[4] == &t[5]);  /* 동률은 주소 순 */
    CHECK(shell_build_index(t, t + 6, idx, 4) == SHELL_EFULL);

    const char *d[4] = { 0 };
    size_t nd = n == 6 ? shell_find_dups(idx, 6, d, 4) : 0;
    CHECK(nd == 1 && d[0] != NULL && strcmp(d[0], "reboot") == 0); /* 세 번 나와도 한 번만 */

    int err = 99;
    const struct shell_cmd *c = n == 6 ? shell_lookup(idx, 6, "reboot", &err) : NULL;
    CHECK(c == NULL && err == SHELL_EDUP);
    err = 99;
    c = n == 6 ? shell_lookup(idx, 6, "led", &err) : NULL;
    CHECK(c == &t[1] && err == 0);
    err = 99;
    c = n == 6 ? shell_lookup(idx, 6, "zzz", &err) : NULL;
    CHECK(c == NULL && err == SHELL_ENOENT);
    err = 99;
    c = n == 6 ? shell_lookup(idx, 6, "ec", &err) : NULL;          /* 접두사는 일치가 아니다 */
    CHECK(c == NULL && err == SHELL_ENOENT);
}

static void test_part_c(void) {
    CHECK(shell_init(&g_sh, shell_cmds_begin, shell_cmds_end) == 0 && g_sh.n == N_REGISTERED && g_sh.ndup == 0);

    char l1[] = "led on";
    CHECK(shell_exec(&g_sh, l1) == 0 && g_led == 1);
    char l2[] = "led blink";
    CHECK(shell_exec(&g_sh, l2) == 2 && g_led == 1);
    char l3[] = "  sum 1\t2   0x27 ";
    CHECK(shell_exec(&g_sh, l3) == 0 && strcmp(g_out, "42") == 0);
    char l4[] = "echo  hello   shell";
    CHECK(shell_exec(&g_sh, l4) == 0 && strcmp(g_out, "hello shell") == 0);
    char l5[] = "selftest";
    CHECK(shell_exec(&g_sh, l5) == SHELL_ENOENT);
    char l6[] = "   ";
    CHECK(shell_exec(&g_sh, l6) == 0);
    char l7[] = "sum 1 2 3 4 5 6 7 8";                              /* argv 9개 */
    CHECK(shell_exec(&g_sh, l7) == SHELL_E2BIG);
    char l8[] = "sum 1 2 3 4 5 6 7";                                /* argv 8개 = 한계 */
    CHECK(shell_exec(&g_sh, l8) == 0 && strcmp(g_out, "28") == 0);

    char l9[] = "help";
    const char *want =
        "echo     echo args\n"
        "help     list commands\n"
        "led      led on|off\n"
        "reboot   soft reset\n"
        "sum      add integers\n"
        "uptime   seconds since boot\n"
        "version  firmware version\n";
    CHECK(shell_exec(&g_sh, l9) == 0 && strcmp(g_out, want) == 0);
    char small[16];
    CHECK(shell_help(&g_sh, small, sizeof small) == -1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);               /* 크래시해도 앞의 FAIL 줄은 남게 */
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
