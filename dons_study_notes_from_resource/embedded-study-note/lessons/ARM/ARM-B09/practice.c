/* ARM-B09 추가 연습 — practice.S 의 asm 함수 + C 로 짠 NZCV 디코더를 한 번에 검증
 *
 *   연습 1  span_printable / span_printable_bug   (ldrb vs ldrsb, b.lo vs b.lt)
 *   연습 2  my_strlen_post                        (post-index 로 고친 strlen)
 *   연습 3  my_strnlen                            (NUL 이 없어도 n 바이트 넘게 안 읽는다)
 *   연습 4  nzcv_decode                           (lldb 의 cpsr 값 → "NZCV" 문자열)
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

extern size_t span_printable(const char *s);
extern size_t span_printable_bug(const char *s);
extern size_t my_strlen_post(const char *s);
extern size_t my_strnlen(const char *s, size_t n);

/* 연습 4: cpsr/NZCV 비트(31..28) → "NZCV" 형태, 0 인 플래그는 '-' */
static void nzcv_decode(uint32_t cpsr, char out[5]) {
    static const char name[4] = {'N', 'Z', 'C', 'V'};
    for (int i = 0; i < 4; i++)
        out[i] = ((cpsr >> (31 - i)) & 1u) ? name[i] : '-';
    out[4] = '\0';
}

static int g_pass, g_total, g_fail_sec;
#define CHECK(expr, ...) do { g_total++; if (expr) { g_pass++; } \
    else if (g_fail_sec++ < 4) { printf("FAIL %s:%d  %s  [", __FILE__, __LINE__, #expr); \
        printf(__VA_ARGS__); printf("]\n"); } } while (0)

static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_sig;
static void on_fault(int sig) { g_sig = sig; siglongjmp(g_jmp, 1); }

/* fn(s) 또는 fn2(s, n) 을 fault 보호 아래 실행 (fault 면 (size_t)-1, *sig 에 신호 번호) */
static size_t guarded(size_t (*fn)(const char *), size_t (*fn2)(const char *, size_t),
                      const char *s, size_t n, int *sig) {
    struct sigaction sa, o1, o2;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &o1);
    sigaction(SIGBUS, &sa, &o2);
    volatile size_t r = (size_t)-1;
    g_sig = 0;
    if (sigsetjmp(g_jmp, 1) == 0)
        r = fn ? fn(s) : fn2(s, n);
    sigaction(SIGSEGV, &o1, NULL);
    sigaction(SIGBUS, &o2, NULL);
    *sig = g_sig;
    return r;
}

static size_t ref_strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i] != '\0') i++;
    return i;
}

int main(void) {
    /* ---------- 연습 1: 1바이트 문자열 255 개 전수 + 실제 문자열 ---------- */
    int bug_caught = 0;
    for (unsigned b = 1; b <= 255; b++) {
        char s[2] = {(char)b, '\0'};
        size_t want = (b >= 0x20) ? 1 : 0;              /* 부호 없는 바이트 기준 */
        CHECK(span_printable(s) == want, "byte=0x%02x got=%zu", b, span_printable(s));
        if (span_printable_bug(s) != want) bug_caught++;
    }
    CHECK(bug_caught == 128, "버그 판이 틀리는 바이트 수 = %d (0x80..0xFF 128개여야)", bug_caught);
    CHECK(span_printable("hello\nworld") == 5, "hello");
    CHECK(span_printable("caf\xc3\xa9 ok\tx") == 8, "utf8");
    CHECK(span_printable_bug("caf\xc3\xa9 ok\tx") == 3, "bug stops at 0xC3");
    CHECK(span_printable("") == 0, "empty");

    /* ---------- 연습 2, 3: 정렬 격자 ---------- */
    static _Alignas(16) char buf[16 + 64 + 1 + 32];
    for (unsigned al = 0; al < 16; al++) {
        for (size_t len = 0; len <= 64; len++) {
            memset(buf, '#', sizeof buf);
            buf[sizeof buf - 1] = '\0';
            for (size_t i = 0; i < len; i++) buf[al + i] = (char)('a' + (i % 26));
            buf[al + len] = '\0';
            CHECK(my_strlen_post(buf + al) == len, "post al=%u len=%zu", al, len);
            for (size_t n = 0; n <= 66; n += 3)
                CHECK(my_strnlen(buf + al, n) == ref_strnlen(buf + al, n),
                      "strnlen al=%u len=%zu n=%zu", al, len, n);
            CHECK(my_strnlen(buf + al, SIZE_MAX) == len, "strnlen n=SIZE_MAX len=%zu", len);
        }
    }

    /* ---------- 연습 2, 3: 가드 페이지 ---------- */
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    char *map = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 2; }
    mprotect(map, pg, PROT_NONE);
    mprotect(map + 2 * pg, pg, PROT_NONE);
    char *page = map + pg;
    for (size_t len = 0; len <= 64; len++) {
        int sig;
        memset(page, 'x', pg);
        char *s = page + pg - 1 - len;                  /* NUL = 페이지 마지막 바이트 */
        s[len] = '\0';
        size_t got = guarded(my_strlen_post, NULL, s, 0, &sig);
        CHECK(sig == 0 && got == len, "post page-end len=%zu sig=%d", len, sig);
        /* NUL 없는 k 바이트를 페이지 끝에 두고 n = k → 정확히 k, fault 없음 */
        memset(page, 'z', pg);
        char *t = page + pg - len;
        got = guarded(NULL, my_strnlen, t, len, &sig);
        CHECK(sig == 0 && got == len, "strnlen no-NUL page-end k=%zu sig=%d", len, sig);
        page[len] = '\0';
        got = guarded(NULL, my_strnlen, page, SIZE_MAX, &sig);
        CHECK(sig == 0 && got == len, "strnlen page-start len=%zu sig=%d", len, sig);
    }
    munmap(map, 3 * pg);

    /* ---------- 연습 4: lldb/gdb 에서 실제로 본 cpsr 값 ---------- */
    char f[5];
    nzcv_decode(0x60000000u, f); CHECK(strcmp(f, "-ZC-") == 0, "%s", f);  /* cmp 0,#0 뒤 */
    nzcv_decode(0x20000000u, f); CHECK(strcmp(f, "--C-") == 0, "%s", f);  /* cmp 0x5a,#0 뒤 */
    nzcv_decode(0x80000000u, f); CHECK(strcmp(f, "N---") == 0, "%s", f);
    nzcv_decode(0x90000000u, f); CHECK(strcmp(f, "N--V") == 0, "%s", f);
    nzcv_decode(0xF0000000u, f); CHECK(strcmp(f, "NZCV") == 0, "%s", f);
    nzcv_decode(0x0FFFFFFFu, f); CHECK(strcmp(f, "----") == 0, "%s", f);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
