/* ARM-B07 demo: 0 나누기 / INT64_MIN / -1 — AArch64 (트랩 없음) vs x86_64 (#DE → SIGFPE)
 *
 *   arm64 native : cc -O2 demo_div_trap.c -o /tmp/ARM-B07_div_arm && /tmp/ARM-B07_div_arm
 *   x86_64 (Rosetta 2): cc -arch x86_64 -O2 demo_div_trap.c -o /tmp/ARM-B07_div_x86 && /tmp/ARM-B07_div_x86
 *   UBSan (C 경로)  : cc -O1 -fsanitize=undefined demo_div_trap.c -o /tmp/ARM-B07_div_ub && /tmp/ARM-B07_div_ub
 *
 * hw_* 는 인라인 asm 으로 "그 CPU 의 나눗셈 명령" 을 직접 실행한다 (인라인 asm 문법 자체는 ARM-M02).
 * c_*  는 평범한 C — 이 입력들에선 UB 다. 컴파일러가 무엇을 내보냈는지에 따라 결과가 갈린다.
 */
#define _XOPEN_SOURCE 700
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static sigjmp_buf g_jb;
static volatile sig_atomic_t g_code;

static void on_fpe(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)uc;
    g_code = si->si_code;
    siglongjmp(g_jb, 1);                       /* 결과 레지스터는 버리고 탈출 */
}

static int64_t hw_sdiv(int64_t a, int64_t b) {
#if defined(__aarch64__)
    int64_t q;
    __asm__ volatile("sdiv %0, %1, %2" : "=r"(q) : "r"(a), "r"(b));
    return q;
#elif defined(__x86_64__)
    int64_t q, r;                              /* idiv: RDX:RAX / src → RAX=몫, RDX=나머지 */
    __asm__ volatile("cqto\n\tidivq %3" : "=a"(q), "=&d"(r) : "0"(a), "r"(b));
    (void)r;
    return q;
#endif
}
static uint64_t hw_udiv(uint64_t a, uint64_t b) {
#if defined(__aarch64__)
    uint64_t q;
    __asm__ volatile("udiv %0, %1, %2" : "=r"(q) : "r"(a), "r"(b));
    return q;
#elif defined(__x86_64__)
    uint64_t q, r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivq %3" : "=a"(q), "=&d"(r) : "0"(a), "r"(b));
    (void)r;
    return q;
#endif
}
__attribute__((noinline)) static int64_t c_sdiv(int64_t a, int64_t b) { return a / b; }
__attribute__((noinline)) static int64_t c_smod(int64_t a, int64_t b) { return a % b; }

static const char *code_name(int c) {
    return c == FPE_INTDIV ? "FPE_INTDIV" : c == FPE_INTOVF ? "FPE_INTOVF" : "other";
}

#define TRY(label, expr) do {                                                        \
        if (sigsetjmp(g_jb, 1) == 0) {                                              \
            int64_t v_ = (int64_t)(expr);                                           \
            printf("  %-28s = %" PRId64 " (0x%016" PRIx64 ")\n", label, v_, (uint64_t)v_); \
        } else {                                                                    \
            printf("  %-28s -> SIGFPE si_code=%d (%s)\n", label, (int)g_code, code_name(g_code)); \
        }                                                                           \
    } while (0)

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fpe;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGFPE, &sa, NULL);

    volatile int64_t a = 7, z = 0, m1 = -1, mn = INT64_MIN;   /* volatile: 상수 접기 방지 */
#if defined(__aarch64__)
    puts("arch = arm64");
#elif defined(__x86_64__)
    puts("arch = x86_64");
#endif
    TRY("hw sdiv 7 / 0", hw_sdiv(a, z));
    TRY("hw udiv 7 / 0", hw_udiv((uint64_t)a, (uint64_t)z));
    TRY("hw sdiv INT64_MIN / -1", hw_sdiv(mn, m1));
    TRY("C   7 / 0          (UB)", c_sdiv(a, z));
    TRY("C   INT64_MIN / -1 (UB)", c_sdiv(mn, m1));
    TRY("C   INT64_MIN % -1 (UB)", c_smod(mn, m1));
    puts("done (no crash)");
    return 0;
}
