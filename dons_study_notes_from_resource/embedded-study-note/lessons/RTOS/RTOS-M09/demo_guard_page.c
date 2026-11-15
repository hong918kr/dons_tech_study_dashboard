/* RTOS-M09 데모 — 호스트판 "MPU 스택 가드": ucontext 스택 바로 아래에 PROT_NONE 페이지 (검사 없음, 관찰용)
 *
 *   arena = mmap 1 덩어리:  [ neighbor 128 KB (RW) ][ guard 1 page ][ task stack 64 KB ]
 *     (A) guard 페이지를 RW 로 둠  → 깊은 재귀가 neighbor 를 조용히 덮고, 태스크는 "정상" 으로 끝난다
 *     (B) guard = PROT_NONE         → 같은 재귀가 guard 를 건드리는 순간 SIGBUS/SIGSEGV (fork 로 자식에서 실행)
 *     (C) (B) + sigaltstack 핸들러   → 넘친 스택 위에선 핸들러도 못 도니 대체 스택에서, 폴트 주소가 guard 안인지 출력
 *     (D) 96 KB 지역 배열, 맨 아래 1 바이트만 기록 → 프레임 바닥이 guard 아래 neighbor 에 떨어진다.
 *         macOS arm64 clang 은 큰 프레임 앞에 ___chkstk_darwin 프로브를 넣으므로 그래도 guard 에서 멈춘다.
 *         -fno-stack-check 로 프로브를 빼면 guard 를 건너뛰어 neighbor 에 조용히 쓴다 (= Cortex-M 빌드의 현실).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_guard_page.c -o demo_guard_page && ./demo_guard_page
 *        cc -std=c11 -Wall -Wextra -Werror -O2 -fno-stack-check demo_guard_page.c -o demo_noprobe && ./demo_noprobe
 *   (_XOPEN_SOURCE 600 은 macOS <ucontext.h> 요구, _DARWIN_C_SOURCE 는 MAP_ANON·sigaltstack·SA_ONSTACK 노출용)
 */
#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* ucontext: macOS 10.6 deprecated (RTOS-B03) */
#endif

#define NEIGH (128 * 1024)
#define STACK (64 * 1024)

static size_t g_page;
static uint8_t *g_base, *g_guard, *g_stack;
static ucontext_t g_main, g_task;
static int g_levels;
static volatile int g_sink;

static int recurse(int n) {                       /* 프레임 ~1 KB 씩 */
    volatile uint8_t buf[1000];
    memset((void *)buf, 0xEE, sizeof buf);
    int r = n > 0 ? recurse(n - 1) : 0;
    return r + buf[n % 1000];
}
static void task_recurse(void) { g_sink = recurse(g_levels); }

static void touch_lowest(volatile uint8_t *p) { p[0] = 0x42; }   /* 가장 낮은 주소 1 바이트만 (snprintf 처럼) */
static void (*volatile g_touch)(volatile uint8_t *) = touch_lowest; /* 컴파일러가 배열을 줄이지 못하게 */
static __attribute__((noinline)) void big_frame(void) {
    volatile uint8_t big[96 * 1024];              /* 스택 64 KB + guard 16 KB 보다 큰 프레임 */
    g_touch(big);
    g_sink = big[0];
}
static void task_big(void) { big_frame(); }

static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    uintptr_t a = (uintptr_t)si->si_addr, g = (uintptr_t)g_guard;
    char msg[160];
    int n = snprintf(msg, sizeof msg, "    handler (on sigaltstack): sig=%s fault addr = guard%+ld B -> %s\n",
                     sig == SIGBUS ? "SIGBUS" : "SIGSEGV", (long)(a - g),
                     (a >= g && a < g + g_page) ? "inside guard page" : "outside guard page");
    write(1, msg, (size_t)n);                     /* async-signal-safe */
    _exit(42);
}

static void setup(int prot_guard) {
    size_t total = NEIGH + g_page + STACK;
    g_base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_base == MAP_FAILED) { perror("mmap"); exit(2); }
    g_guard = g_base + NEIGH; g_stack = g_guard + g_page;
    memset(g_base, 0x11, NEIGH);
    if (prot_guard && mprotect(g_guard, g_page, PROT_NONE) != 0) { perror("mprotect"); exit(2); }
}
static void run_task(void (*fn)(void)) {
    getcontext(&g_task);
    g_task.uc_stack.ss_sp = g_stack; g_task.uc_stack.ss_size = STACK; g_task.uc_link = &g_main;
    makecontext(&g_task, fn, 0);
    swapcontext(&g_main, &g_task);
}
static size_t neighbor_damage(void) {
    size_t n = 0;
    for (size_t i = 0; i < NEIGH; i++) n += g_base[i] != 0x11;
    return n;
}
static void child_report(const char *what, pid_t pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("    %s: child killed by signal %d (%s)\n", what, WTERMSIG(st), strsignal(WTERMSIG(st)));
    else printf("    %s: child exit code %d\n", what, WEXITSTATUS(st));
}

int main(void) {
    g_page = (size_t)sysconf(_SC_PAGESIZE);
    printf("page size = %zu B, stack = %d KB, neighbor below guard = %d KB\n", g_page, STACK / 1024, NEIGH / 1024);
    g_levels = 80;                                /* ~80 KB 필요 > 64 KB 스택 */

    /* (A) guard 없음 */
    setup(0);
    run_task(task_recurse);
    printf("(A) no guard   : task returned normally; neighbor bytes changed = %zu (guard page itself changed: %s)\n",
           neighbor_damage(), g_guard[0] != 0 || g_guard[g_page - 1] != 0 ? "yes" : "no");
    fflush(stdout);

    /* (B) guard PROT_NONE — 자식에서 */
    pid_t p = fork();
    if (p == 0) { setup(1); run_task(task_recurse); printf("    (B) child: returned?!\n"); _exit(0); }
    child_report("(B) guard page ", p);

    /* (C) guard + sigaltstack 핸들러 */
    fflush(stdout);
    p = fork();
    if (p == 0) {
        setup(1);
        static uint8_t alt[64 * 1024];
        stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt, .ss_flags = 0 };
        sigaltstack(&ss, NULL);
        struct sigaction sa; memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
        run_task(task_recurse);
        _exit(0);
    }
    child_report("(C) + handler  ", p);

    /* (D) 큰 프레임 1 바이트 쓰기 */
    fflush(stdout);
    p = fork();
    if (p == 0) {
        setup(1);
        static uint8_t alt[64 * 1024];
        stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt, .ss_flags = 0 };
        sigaltstack(&ss, NULL);
        struct sigaction sa; memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
        run_task(task_big);
        printf("    (D) child: big_frame returned, neighbor bytes changed = %zu\n", neighbor_damage());
        fflush(stdout);
        _exit(0);
    }
    child_report("(D) 96 KB frame", p);
    return 0;
}
