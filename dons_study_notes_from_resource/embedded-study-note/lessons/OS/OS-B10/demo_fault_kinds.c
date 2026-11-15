/* OS-B10 demo: 시뮬레이터의 폴트 종류가 진짜 OS 에서는 어떤 시그널이 되나 (macOS 실측용)
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_kinds.c -o /tmp/OS-B10_dfk && /tmp/OS-B10_dfk
 *
 * 각 경우를 fork 한 자식에서 실행한다. 자식은 SA_SIGINFO 핸들러에서 (signo, si_code, si_addr 가
 * 기대한 페이지 안인가) 를 exit status 에 담아 _exit 한다 (핸들러 안에서는 async-signal-safe 인
 * _exit 만 부른다). 부모는 waitpid 로 받아 표를 찍는다. 자식마다 alarm(2) 워치독.
 */
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static uintptr_t g_lo, g_hi;   /* 폴트가 나야 할 주소 범위 (자식 안에서만 씀) */

static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    uintptr_t a = (uintptr_t)si->si_addr;
    int in = (a >= g_lo && a < g_hi);
    int s = (sig == SIGSEGV) ? 1 : (sig == SIGBUS) ? 2 : 3;
    _exit(100 * in + 10 * s + (si->si_code & 7));
}

typedef void (*fn_t)(void);

static void run_case(const char *name, int kind, long pg) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        alarm(2);
        volatile char *p = NULL;
        char *m = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (m == MAP_FAILED) _exit(90);
        m[0] = 1; m[pg] = 1;
        switch (kind) {
        case 0: /* 미할당(unmapped): 두 번째 페이지를 munmap 하고 읽기 */
            munmap(m + pg, (size_t)pg);
            p = m + pg; break;
        case 1: /* 읽기 전용 페이지에 쓰기 */
            mprotect(m + pg, (size_t)pg, PROT_READ);
            p = m + pg; break;
        case 2: /* PROT_NONE 페이지 읽기 */
            mprotect(m + pg, (size_t)pg, PROT_NONE);
            p = m + pg; break;
        case 3: /* 실행 권한 없는 (RW) 페이지로 점프 */
            p = m + pg; break;
        case 4: /* NULL 근처 읽기 (macOS 는 __PAGEZERO 4 GB) */
            p = (volatile char *)(uintptr_t)0x10; break;
        }
        g_lo = (uintptr_t)p & ~(uintptr_t)(pg - 1);
        g_hi = g_lo + (uintptr_t)pg;
        if (kind == 4) { g_lo = 0; g_hi = (uintptr_t)pg; }
        if (kind == 1) { *p = 42; }
        else if (kind == 3) {
            fn_t f;
            void *vp = (void *)(uintptr_t)p;
            memcpy(&f, &vp, sizeof f);
            f();
        } else { char c = *p; (void)c; }
        _exit(0);   /* 폴트가 안 났다 */
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); exit(2); }
    if (WIFSIGNALED(st)) { printf("%-34s killed by signal %d (handler not reached)\n", name, WTERMSIG(st)); return; }
    int code = WEXITSTATUS(st);
    if (code == 0) { printf("%-34s no fault!\n", name); return; }
    if (code == 90) { printf("%-34s mmap failed\n", name); return; }
    int in = code / 100, s = (code / 10) % 10, c = code % 10;
    const char *sn = s == 1 ? "SIGSEGV" : s == 2 ? "SIGBUS" : "other";
    const char *cn = "?";
    if (s == 1) cn = c == SEGV_MAPERR ? "SEGV_MAPERR" : c == SEGV_ACCERR ? "SEGV_ACCERR" : "?";
    if (s == 2) cn = c == BUS_ADRALN ? "BUS_ADRALN" : c == BUS_ADRERR ? "BUS_ADRERR" : c == BUS_OBJERR ? "BUS_OBJERR" : "?";
    printf("%-34s %-7s si_code=%d (%-11s) si_addr in page: %s\n", name, sn, c, cn, in ? "yes" : "no");
}

int main(void) {
    long pg = sysconf(_SC_PAGESIZE);
    printf("page size = %ld\n", pg);
    run_case("A. read unmapped page   (INVALID)", 0, pg);
    run_case("B. write read-only page (PROT W)", 1, pg);
    run_case("C. read PROT_NONE page  (PROT R)", 2, pg);
    run_case("D. exec RW page         (PROT X)", 3, pg);
    run_case("E. read *(char*)0x10    (NULL)", 4, pg);
    return 0;
}
