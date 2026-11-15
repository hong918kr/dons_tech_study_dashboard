/* ARM-B08 demo_bugs.c — 틀린 프레임이 실제로 어떻게 터지는지 관찰 (테스트 아님)
 * build: cc -std=c11 -O2 demo_bugs.c demo_bugs.S shared_regcheck.S -o /tmp/ARM-B08_bugs && /tmp/ARM-B08_bugs
 * 각 버그를 fork 한 자식에서 실행. SIGSEGV/SIGBUS 핸들러(대체 스택)가 크래시 레지스터를 찍는다.
 */
#define _DARWIN_C_SOURCE 1
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ucontext.h>
#include <unistd.h>

uint64_t bug_fact_nolr(uint64_t n);
uint64_t bug_fact_noxsave(uint64_t n);
int64_t  bug_local_nofree(int32_t seed);
uint64_t bug_misaligned_sp(uint64_t x);
uint64_t bug_sum_clobber_x20(uint64_t n);
uint64_t call_preserve_check(uintptr_t fn, uint64_t a, uint64_t b, uint64_t *ret);

static void on_fault(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = (ucontext_t *)uc_;
    char buf[256];
    int n = snprintf(buf, sizeof buf, "  [crash] %s addr=%p pc=0x%llx sp=0x%llx lr=0x%llx fp=0x%llx esr=0x%x\n",
                     sig == SIGBUS ? "SIGBUS" : "SIGSEGV", si->si_addr,
                     (unsigned long long)uc->uc_mcontext->__ss.__pc, (unsigned long long)uc->uc_mcontext->__ss.__sp,
                     (unsigned long long)uc->uc_mcontext->__ss.__lr, (unsigned long long)uc->uc_mcontext->__ss.__fp,
                     uc->uc_mcontext->__es.__esr);
    write(1, buf, (size_t)n);
    _exit(128 + sig);
}

static void run(const char *name, int which) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        static char altstack[65536];
        stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
        sigaltstack(&ss, NULL);                       /* sp 가 망가져도 핸들러가 돌 자리 */
        struct sigaction sa; memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
        alarm(3);                                     /* 무한 루프 대비 */
        uint64_t r = 0, m;
        switch (which) {
        case 1: printf("  nolr(1) = %llu (base case: bl 없음 → 정상)\n", (unsigned long long)bug_fact_nolr(1));
                fflush(stdout);
                printf("  nolr(5) = %llu\n", (unsigned long long)bug_fact_nolr(5)); break;
        case 2: m = call_preserve_check((uintptr_t)bug_fact_noxsave, 5, 0, &r);
                printf("  noxsave(5) = %llu (기대 120), 보존 마스크 = 0x%llx\n", (unsigned long long)r, (unsigned long long)m); break;
        case 3: printf("  nofree(0) = %lld\n", (long long)bug_local_nofree(0)); break;
        case 4: printf("  misaligned(42) = %llu\n", (unsigned long long)bug_misaligned_sp(42)); break;
        case 5: m = call_preserve_check((uintptr_t)bug_sum_clobber_x20, 10, 0, &r);
                printf("  sum(10) = %llu (기대 45), 보존 마스크 = 0x%llx\n", (unsigned long long)r, (unsigned long long)m); break;
        }
        fflush(stdout);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("%-34s → 자식이 시그널로 죽음: %s\n", name, strsignal(WTERMSIG(st)));
    else printf("%-34s → exit %d%s\n", name, WEXITSTATUS(st),
                WEXITSTATUS(st) > 128 ? " (핸들러가 잡은 크래시)" : "");
}

int main(void) {
    run("bug1 x30 저장 안 함", 1);
    run("bug2 x19 저장 안 함", 2);
    run("bug3 지역 영역 해제 안 함", 3);
    run("bug4 sp 8 바이트 정렬", 4);
    run("bug5 x20 몰래 덮어씀", 5);
    return 0;
}
