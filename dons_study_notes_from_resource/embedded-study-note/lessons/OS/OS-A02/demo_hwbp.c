/* ENV: linux */
/* OS-A02 demo — 하드웨어 브레이크포인트/워치포인트 (NT_ARM_HW_BREAK / NT_ARM_HW_WATCH) + 소프트웨어 워치포인트 비용
 *
 *   1) 슬롯 수: PTRACE_GETREGSET 의 dbg_info (bits 7:0 = 슬롯 수, bits 15:8 = 디버그 아키텍처)
 *   2) HW BP: 코드를 고치지 않는다 (명령 워드 그대로). SIGTRAP si_code = TRAP_HWBKPT.
 *      ptrace 가 건 BP 는 커널이 자동으로 넘겨 주지 않는다 → 끄지 않고 CONT 하면 같은 자리에서 또 멈춘다.
 *   3) HW 워치포인트: 저장 명령이 "실행되기 전" 에 멈춘다 (값은 아직 옛 값). si_addr = 감시 주소.
 *   4) 같은 감시를 싱글스텝(소프트웨어 워치포인트)으로 하면 얼마나 느린가 — 실측 µs/step.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_hwbp.c -o hwbp && ./hwbp
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <asm/ptrace.h>

/* arch/arm64/include/asm/hw_breakpoint.h: ctrl = len<<5 | type<<3 | privilege<<1 | enabled */
#define CTRL(len, type) (((uint32_t)(len) << 5) | ((uint32_t)(type) << 3) | (2u << 1) | 1u) /* priv 2 = EL0 */
#define LEN4 0xfu   /* ARM_BREAKPOINT_LEN_4 */
#define LEN8 0xffu  /* ARM_BREAKPOINT_LEN_8 */
#define T_EXEC 0u   /* ARM_BREAKPOINT_EXECUTE */
#define T_STORE 2u  /* ARM_BREAKPOINT_STORE */

volatile uint64_t g_watch;
__attribute__((noinline, used)) int hw_target(int x) { __asm__ volatile(""); return x * 2; }
__attribute__((noinline, used)) void work(long n) {
    uint64_t acc = 1;
    for (long i = 0; i < n; i++) { acc = acc * 31 + (uint64_t)i; __asm__ volatile("" : "+r"(acc)); }
    g_watch = acc; /* 감시 대상: 저장은 이 한 번뿐 */
}
static void (*volatile p_work)(long) = work;
static int (*volatile p_hw)(int) = hw_target;

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
static int slot(pid_t p, int note, uintptr_t addr, uint32_t ctrl) {
    struct user_hwdebug_state hw;
    memset(&hw, 0, sizeof hw);
    hw.dbg_regs[0].addr = addr;
    hw.dbg_regs[0].ctrl = ctrl;
    struct iovec iov = { &hw, offsetof(struct user_hwdebug_state, dbg_regs) + sizeof hw.dbg_regs[0] };
    return ptrace(PTRACE_SETREGSET, p, (void *)(long)note, &iov) == 0 ? 0 : -errno;
}
static uint64_t pc_of(pid_t p) {
    struct user_pt_regs r;
    struct iovec iov = { &r, sizeof r };
    return ptrace(PTRACE_GETREGSET, p, (void *)(long)NT_PRSTATUS, &iov) == 0 ? r.pc : 0;
}
static int stop(pid_t p, siginfo_t *si) { /* 2 s 감시 */
    int st;
    alarm(2);
    pid_t r = waitpid(p, &st, 0);
    alarm(0);
    if (r != p || !WIFSTOPPED(st)) return -1;
    memset(si, 0, sizeof *si);
    ptrace(PTRACE_GETSIGINFO, p, NULL, si);
    return WSTOPSIG(st);
}
static pid_t spawn(long n) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        raise(SIGSTOP);
        int r = p_hw(21);
        p_work(n);
        _exit(r & 0xff);
    }
    siginfo_t si;
    if (stop(p, &si) != SIGSTOP) return -1;
    ptrace(PTRACE_SETOPTIONS, p, NULL, (void *)(long)PTRACE_O_EXITKILL);
    return p;
}
static void on_alarm(int s) { (void)s; }

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    const long N = 4000;
    siginfo_t si;
    pid_t p = spawn(N);
    if (p < 0) return 1;

    struct user_hwdebug_state hw;
    struct iovec iov = { &hw, sizeof hw };
    ptrace(PTRACE_GETREGSET, p, (void *)(long)NT_ARM_HW_BREAK, &iov);
    printf("HW breakpoint slots %u (debug arch %#x)", hw.dbg_info & 0xff, (hw.dbg_info >> 8) & 0xff);
    iov.iov_len = sizeof hw;
    ptrace(PTRACE_GETREGSET, p, (void *)(long)NT_ARM_HW_WATCH, &iov);
    printf(", watchpoint slots %u\n", hw.dbg_info & 0xff);

    /* --- HW BP --- */
    errno = 0;
    long before = ptrace(PTRACE_PEEKTEXT, p, (void *)hw_target, NULL);
    int r = slot(p, NT_ARM_HW_BREAK, (uintptr_t)hw_target, CTRL(LEN4, T_EXEC));
    ptrace(PTRACE_CONT, p, NULL, NULL);
    int sig = stop(p, &si);
    long after = ptrace(PTRACE_PEEKTEXT, p, (void *)hw_target, NULL);
    printf("hw bp set=%d: sig=%d si_code=%d (TRAP_HWBKPT=%d) si_addr==hw_target %s pc==hw_target %s, text word unchanged %s\n",
           r, sig, si.si_code, TRAP_HWBKPT, (uintptr_t)si.si_addr == (uintptr_t)hw_target ? "yes" : "no",
           pc_of(p) == (uintptr_t)hw_target ? "yes" : "no", before == after ? "yes" : "no");
    ptrace(PTRACE_CONT, p, NULL, NULL); /* 끄지 않고 계속하면? */
    sig = stop(p, &si);
    printf("  CONT without disabling: stopped again sig=%d si_code=%d at same pc %s\n", sig, si.si_code,
           pc_of(p) == (uintptr_t)hw_target ? "yes" : "no");
    slot(p, NT_ARM_HW_BREAK, 0, 0); /* 끈다 */

    /* --- HW 워치포인트 --- */
    r = slot(p, NT_ARM_HW_WATCH, (uintptr_t)&g_watch, CTRL(LEN8, T_STORE));
    double t0 = now_us();
    ptrace(PTRACE_CONT, p, NULL, NULL);
    sig = stop(p, &si);
    double t_hw = now_us() - t0;
    uint64_t wpc = pc_of(p);
    long val = ptrace(PTRACE_PEEKDATA, p, (void *)&g_watch, NULL);
    printf("hw watch set=%d: sig=%d si_code=%d si_addr==&g_watch %s, pc=work+%#lx, g_watch still %ld (store not yet done)\n",
           r, sig, si.si_code, (uintptr_t)si.si_addr == (uintptr_t)&g_watch ? "yes" : "no",
           (unsigned long)(wpc - (uintptr_t)work), val);
    slot(p, NT_ARM_HW_WATCH, 0, 0);
    ptrace(PTRACE_SINGLESTEP, p, NULL, NULL);
    stop(p, &si);
    val = ptrace(PTRACE_PEEKDATA, p, (void *)&g_watch, NULL);
    printf("  disable + SINGLESTEP: si_code=%d (TRAP_TRACE=%d), g_watch now %#lx\n", si.si_code, TRAP_TRACE, (unsigned long)val);
    printf("  HW watch: CONT → hit took %.0f us for N=%ld loop iterations\n", t_hw, N);
    kill(p, SIGKILL);
    waitpid(p, NULL, 0);

    /* --- 같은 감시를 싱글스텝으로 --- */
    p = spawn(N);
    if (p < 0) return 1;
    slot(p, NT_ARM_HW_BREAK, (uintptr_t)work, CTRL(LEN4, T_EXEC)); /* work 입구까지는 HW BP 로 */
    ptrace(PTRACE_CONT, p, NULL, NULL);
    stop(p, &si);
    slot(p, NT_ARM_HW_BREAK, 0, 0);
    long steps = 0;
    t0 = now_us();
    for (;;) {
        ptrace(PTRACE_SINGLESTEP, p, NULL, NULL);
        if (stop(p, &si) != SIGTRAP) break;
        steps++;
        if (ptrace(PTRACE_PEEKDATA, p, (void *)&g_watch, NULL) != 0) break;
    }
    double t_sw = now_us() - t0;
    printf("SW watch (single-step + PEEK each step): %ld steps, %.0f us total, %.2f us/step, %.0fx slower than HW\n",
           steps, t_sw, t_sw / (double)steps, t_sw / t_hw);
    kill(p, SIGKILL);
    waitpid(p, NULL, 0);
    return 0;
}
