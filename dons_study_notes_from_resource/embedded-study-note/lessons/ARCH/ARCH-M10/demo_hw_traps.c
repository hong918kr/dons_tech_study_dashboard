/* ARCH-M10 demo — 진짜 CPU(Apple M6, AArch64)의 예외를 사용자 공간에서 관찰한다.
 *
 *   1) 나눗셈 0 / INT_MIN / -1 : AArch64 UDIV/SDIV 는 트랩하지 않는다 (결과 0 / INT_MIN)
 *   2) UDF / BRK / 미정렬 LDXR(16 B 블록 안 vs 경계 넘기) / NULL 읽기 : 커널이 시그널로 바꿔 준다. 핸들러에서 ESR(원인) · FAR(주소) · PC 를 본다
 *      (PC 가 가리키는 명령 워드를 읽어서 "EPC = 폴트 난 명령" 임을 확인)
 *   3) 미정렬 LDR / LDP 는 일반 메모리에서 그냥 된다
 *   4) 건너뛰기 핸들러: SIGILL 에서 pc += 4 → 다음 명령부터 계속
 *   5) 재시도 핸들러: PROT_NONE 페이지에 쓰기 → 핸들러가 mprotect(RW) → 같은 STR 을 다시 실행해 성공
 *
 * arm64 전용. build: cc -std=c11 -O1 demo_hw_traps.c -o demo_hw_traps && ./demo_hw_traps
 * (시그널 핸들러 안의 snprintf 는 엄밀히는 async-signal-safe 가 아니다 — 관찰용 데모라서 허용)
 */
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "arm64 only"
#endif

static const char *ec_name(unsigned ec) {
    switch (ec) {
    case 0x00: return "Unknown reason (UDF 등)";
    case 0x20: return "Instruction Abort, lower EL";
    case 0x22: return "PC alignment";
    case 0x24: return "Data Abort, lower EL";
    case 0x3C: return "BRK (AArch64)";
    default:   return "?";
    }
}

static void say(const char *s) { (void)!write(1, s, strlen(s)); }

/* 자식 프로세스용: 원인을 찍고 종료 */
static void report(int sig, siginfo_t *si, void *uctx) {
    ucontext_t *uc = uctx;
    uint64_t esr = uc->uc_mcontext->__es.__esr, far = uc->uc_mcontext->__es.__far;
    uint64_t pc = __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    unsigned ec = (unsigned)(esr >> 26) & 0x3Fu, iss = (unsigned)esr & 0x1FFFFFFu;
    char buf[256];
    snprintf(buf, sizeof buf, "    sig=%-7s ESR=0x%08llx EC=0x%02x (%s) ISS=0x%06x FAR=0x%llx si_addr=%p *pc=0x%08x\n",
             sig == SIGILL ? "SIGILL" : sig == SIGTRAP ? "SIGTRAP" : sig == SIGBUS ? "SIGBUS" : sig == SIGSEGV ? "SIGSEGV" : "?",
             (unsigned long long)esr, ec, ec_name(ec), iss, (unsigned long long)far, si->si_addr, *(const uint32_t *)pc);
    say(buf);
    _exit(0);
}

static void run_child(const char *name, void (*fn)(void)) {
    printf("  %s\n", name);
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = report;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGILL, &sa, NULL); sigaction(SIGTRAP, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL); sigaction(SIGSEGV, &sa, NULL);
        fn();
        say("    (no trap)\n");
        _exit(0);
    }
    int st;
    waitpid(p, &st, 0);
}

static uint8_t buf_al[64] __attribute__((aligned(16)));

static void t_udf(void)  { __asm__ volatile("udf #0"); }
static void t_brk(void)  { __asm__ volatile("brk #0x42"); }
static void t_ldxr1(void) {                     /* 16 B 블록 안에서 미정렬 */
    uint64_t v;
    __asm__ volatile("ldxr %0, [%1]" : "=r"(v) : "r"(buf_al + 1) : "memory");
    (void)v;
}
static void t_ldxr9(void) {                     /* 9..16 → 16 B 경계를 넘는 미정렬 */
    uint64_t v;
    __asm__ volatile("ldxr %0, [%1]" : "=r"(v) : "r"(buf_al + 9) : "memory");
    (void)v;
}
static void t_null(void) {
    uint64_t v;
    __asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"((uint64_t *)0) : "memory");
    (void)v;
}

/* ---- 4) 건너뛰기: pc += 4 ---- */
static volatile sig_atomic_t g_skipped;
static void skip_ill(int sig, siginfo_t *si, void *uctx) {
    (void)sig; (void)si;
    ucontext_t *uc = uctx;
    uint64_t pc = __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    __darwin_arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss, (void *)(pc + 4));   /* EPC + 1 명령 */
    g_skipped++;
}

/* ---- 5) 재시도: 권한을 고치고 같은 명령을 다시 ---- */
static void *g_page;
static size_t g_pagesz;
static volatile sig_atomic_t g_fixed;
static void fix_perm(int sig, siginfo_t *si, void *uctx) {
    (void)sig; (void)uctx;
    if ((uint8_t *)si->si_addr < (uint8_t *)g_page || (uint8_t *)si->si_addr >= (uint8_t *)g_page + g_pagesz) _exit(3);
    mprotect(g_page, g_pagesz, PROT_READ | PROT_WRITE);                                     /* 원인을 고친다 */
    g_fixed++;                                                                              /* pc 는 그대로 → 재실행 */
}

int main(void) {
    printf("1) divide: AArch64 UDIV/SDIV do not trap\n");
    uint32_t q, a = 1234, z = 0;
    int32_t sq, smin = INT32_MIN, m1 = -1;
    __asm__ volatile("udiv %w0, %w1, %w2" : "=r"(q) : "r"(a), "r"(z));
    __asm__ volatile("sdiv %w0, %w1, %w2" : "=r"(sq) : "r"(smin), "r"(m1));
    printf("  udiv 1234 / 0       = %u\n", q);
    printf("  sdiv INT32_MIN / -1 = %d\n", sq);

    printf("2) synchronous exceptions -> signals (child processes)\n");
    run_child("udf #0", t_udf);
    run_child("brk #0x42", t_brk);
    run_child("ldxr x, [buf+1]  (exclusive, misaligned inside 16 B)", t_ldxr1);
    run_child("ldxr x, [buf+9]  (exclusive, crosses 16 B boundary)", t_ldxr9);
    run_child("ldr  x, [0]      (NULL)", t_null);

    printf("3) misaligned plain LDR / LDP on normal memory\n");
    for (unsigned i = 0; i < sizeof buf_al; i++) buf_al[i] = (uint8_t)i;
    uint64_t v1, v2, v3;
    __asm__ volatile("ldr %0, [%1]" : "=r"(v1) : "r"(buf_al + 1) : "memory");
    __asm__ volatile("ldp %0, %1, [%2]" : "=r"(v2), "=r"(v3) : "r"(buf_al + 3) : "memory");
    printf("  ldr [buf+1] = 0x%016llx\n  ldp [buf+3] = 0x%016llx 0x%016llx\n",
           (unsigned long long)v1, (unsigned long long)v2, (unsigned long long)v3);

    printf("4) skip handler (SIGILL: pc += 4)\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = skip_ill;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &sa, NULL);
    volatile int after = 0;
    __asm__ volatile("udf #1\n\tudf #2\n\tudf #3" ::: "memory");
    after = 1;
    printf("  skipped %d UDFs, continued: after=%d\n", (int)g_skipped, after);

    printf("5) fix + retry handler (PROT_NONE page -> mprotect -> same STR again)\n");
    g_pagesz = (size_t)getpagesize();
    g_page = mmap(NULL, g_pagesz, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    sa.sa_sigaction = fix_perm;
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    volatile uint32_t *p = (volatile uint32_t *)g_page;
    p[5] = 0xC0FFEEu;
    printf("  page %zu B, faults fixed %d, p[5] = 0x%X\n", g_pagesz, (int)g_fixed, p[5]);
    return 0;
}
