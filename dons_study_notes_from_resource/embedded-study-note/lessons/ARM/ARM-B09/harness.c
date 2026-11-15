/* ARM-B09 harness — starter.S / solution.S 공용 테스트
 *
 *   size_t my_strlen(const char *s);   // starter.S 또는 solution.S 에 있는 AArch64 asm
 *
 * 1) 정렬 격자: 길이 0..64 × 시작 정렬 0..15 (NUL 뒤에는 0 이 아닌 쓰레기 바이트)
 * 2) 페이지 끝:  NUL 이 RW 페이지의 마지막 바이트 → 그 다음 바이트는 PROT_NONE 페이지
 *               → NUL 을 지나 한 바이트라도 더 읽으면 SIGBUS/SIGSEGV (잡아서 FAIL 로 바꾼다)
 * 3) 페이지 앞:  s 가 RW 페이지의 첫 바이트 → s[-1] 을 읽으면 앞쪽 가드 페이지에서 fault
 * 4) 0x80 이상 바이트(UTF-8 등)도 그냥 "0 이 아닌 바이트" 로 세야 한다
 */
#define _DEFAULT_SOURCE  /* glibc: MAP_ANONYMOUS, sigsetjmp (-std=c11 에서 필요) */
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

extern size_t my_strlen(const char *s);

static int g_pass, g_total, g_fail_sec;   /* g_fail_sec: 이 섹션의 FAIL 수 (4줄까지만 출력) */
#define CHECK(expr, ...) do { g_total++; if (expr) { g_pass++; } \
    else if (g_fail_sec++ < 4) { printf("FAIL %s:%d  %s  [", __FILE__, __LINE__, #expr); \
        printf(__VA_ARGS__); printf("]\n"); } } while (0)

/* ---- fault → FAIL 변환: SIGSEGV/SIGBUS 를 잡아 siglongjmp ---- */
static sigjmp_buf g_jmp;
static volatile sig_atomic_t g_sig;
static void on_fault(int sig) { g_sig = sig; siglongjmp(g_jmp, 1); }

/* my_strlen(s) 를 실행. fault 나면 (size_t)-1 반환하고 *sig 에 신호 번호 */
static void end_section(const char *name) {
    if (g_fail_sec > 4) printf("     ... %s: FAIL %d개 중 4개만 표시\n", name, g_fail_sec);
    g_fail_sec = 0;
}

static size_t guarded_strlen(const char *s, int *sig) {
    struct sigaction sa, old_segv, old_bus;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    volatile size_t r = (size_t)-1;
    g_sig = 0;
    if (sigsetjmp(g_jmp, 1) == 0)
        r = my_strlen(s);
    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS, &old_bus, NULL);
    *sig = g_sig;
    return r;
}

int main(void) {
    /* ---------- 1) 정렬 격자 ---------- */
    static _Alignas(16) char buf[16 + 64 + 1 + 32];
    for (unsigned al = 0; al < 16; al++) {
        for (size_t len = 0; len <= 64; len++) {
            memset(buf, '#', sizeof buf);            /* NUL 뒤 쓰레기 = '#' (0 아님) */
            buf[sizeof buf - 1] = '\0';              /* 버퍼 맨 끝에 진짜 끝 → 버그여도 버퍼 안에서 멈춤 */
            for (size_t i = 0; i < len; i++) buf[al + i] = (char)('a' + (i % 26));
            buf[al + len] = '\0';
            size_t got = my_strlen(buf + al);
            CHECK(got == len, "align=%u len=%zu got=%zu", al, len, got);
        }
    }
    end_section("정렬 격자");

    /* ---------- 2), 3) 가드 페이지: [NONE][RW][NONE] ---------- */
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);       /* Apple Silicon: 16384 */
    char *map = mmap(NULL, 3 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) { perror("mmap"); return 2; }
    mprotect(map, pg, PROT_NONE);
    mprotect(map + 2 * pg, pg, PROT_NONE);
    char *page = map + pg;                           /* RW 페이지 [page, page+pg) */
    for (size_t len = 0; len <= 64; len++) {
        int sig;
        /* 2) 페이지 끝: s[len] = NUL 이 page+pg-1 (마지막 바이트) */
        memset(page, 'x', pg);
        char *s = page + pg - 1 - len;
        s[len] = '\0';
        size_t got = guarded_strlen(s, &sig);
        CHECK(sig == 0 && got == len, "page-end len=%zu got=%zd sig=%d%s", len, (ssize_t)got, sig,
              sig ? " (NUL 다음 바이트를 읽음)" : "");
        /* 3) 페이지 앞: s = page (s[-1] 은 가드 페이지) */
        memset(page, 'y', pg);
        page[len] = '\0';
        got = guarded_strlen(page, &sig);
        CHECK(sig == 0 && got == len, "page-start len=%zu got=%zd sig=%d", len, (ssize_t)got, sig);
    }
    end_section("가드 페이지");
    munmap(map, 3 * pg);

    /* ---------- 4) 0x80 이상 바이트 ---------- */
    CHECK(my_strlen("\xff\x80\xc3\xa9") == 4, "high bytes");
    CHECK(my_strlen("caf\xc3\xa9 \xe2\x82\xac") == 9, "UTF-8 'cafe euro'");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
