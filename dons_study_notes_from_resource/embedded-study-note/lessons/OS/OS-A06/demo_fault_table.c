/* OS-A06 demo — 이 맥에서 "어떤 접근이 어떤 신호·si_code·ESR 로 오나" 표를 직접 찍는다.
 *   모든 경우를 같은 프로세스 안에서 잡고 siglongjmp 로 빠져나온다 (자식도, 크래시 리포트도 없음).
 *   ESR/FAR 는 arm64 macOS 의 uc_mcontext->__es.__esr / __far 에서 읽는다.
 *   커널이 대신 만지는 경우(read/write 시스템 콜의 버퍼)는 신호 대신 EFAULT 가 된다는 것도 본다.
 *   마지막에 mincore 로 "PROT_NONE 예약 = 실제 메모리 0" 을 확인한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_table.c -o dft && ./dft
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t PG;
static unsigned char *g_base;
static sigjmp_buf g_jb;
static volatile int g_sig, g_code;
static volatile uint32_t g_esr;
static volatile long g_off, g_far_off;

static void on_fault(int sig, siginfo_t *si, void *ucv) {
    g_sig = sig; g_code = si->si_code;
    g_off = (long)((unsigned char *)si->si_addr - g_base);
#if defined(__APPLE__) && defined(__aarch64__)
    ucontext_t *uc = ucv;
    g_esr = uc->uc_mcontext->__es.__esr;
    g_far_off = (long)((unsigned char *)(uintptr_t)uc->uc_mcontext->__es.__far - g_base);
#else
    (void)ucv;
#endif
    siglongjmp(g_jb, 1);
}

typedef void (*acc_fn)(void);
static void rd(void)        { (void)((volatile unsigned char *)g_base)[100]; }
static void wr(void)        { ((volatile unsigned char *)g_base)[100] = 1; }
static void ldadd(void)     { __atomic_fetch_add((int *)(void *)(g_base + 64), 1, __ATOMIC_SEQ_CST); }
static void zero4k(void)    { memset(g_base, 0, 4096); }                   /* libc memset: 0 이면 DC ZVA 를 쓸 수 있다 */
static void straddle(void)  { uint64_t v; memcpy(&v, g_base + PG - 4, 8); __asm__ volatile("" :: "r"(v)); }
static void self_kill(void) { kill(getpid(), SIGSEGV); }

static void row(const char *name, int prot, int populate, acc_fn fn) {
    if (populate == 1) {                                          /* PTE 를 만들어 둔 뒤 보호를 바꾼다 */
        mprotect(g_base, 2 * PG, PROT_READ | PROT_WRITE);
        g_base[0] = 1; g_base[PG] = 1;
        mprotect(g_base, 2 * PG, prot);
    } else if (populate == 0) {                                   /* 새 매핑 (한 번도 안 만진 페이지) */
        mmap(g_base, 2 * PG, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    }                                                             /* -1: 매핑을 그대로 둔다 (munmap 뒤) */
    g_sig = 0; g_code = 0; g_esr = 0; g_off = g_far_off = -1;
    if (sigsetjmp(g_jb, 1) == 0) fn();
    uint32_t e = g_esr;
    printf("%-34s %-7s %4d %8ld %8ld  0x%08x  EC=0x%02x WnR=%u CM=%u DFSC=0x%02x\n", name,
           g_sig == SIGBUS ? "SIGBUS" : g_sig == SIGSEGV ? "SIGSEGV" : "-", g_code, g_off, g_far_off,
           e, e >> 26, (e >> 6) & 1, (e >> 8) & 1, e & 0x3f);
}

int main(void) {
    PG = (size_t)sysconf(_SC_PAGESIZE);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    g_base = mmap(NULL, 2 * PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    printf("page %zu  SIGBUS=%d SIGSEGV=%d  SEGV_MAPERR=%d SEGV_ACCERR=%d BUS_ADRALN=%d BUS_ADRERR=%d SI_USER=%d\n",
           PG, SIGBUS, SIGSEGV, SEGV_MAPERR, SEGV_ACCERR, BUS_ADRALN, BUS_ADRERR, SI_USER);
    printf("%-34s %-7s %4s %8s %8s  %-10s\n", "access", "signal", "code", "si_addr", "FAR", "ESR");
    row("read  PROT_NONE (fresh)",           PROT_NONE, 0, rd);
    row("write PROT_NONE (fresh)",           PROT_NONE, 0, wr);
    row("write PROT_READ (fresh, no PTE)",   PROT_READ, 0, wr);
    row("write PROT_READ (populated)",       PROT_READ, 1, wr);
    row("read  PROT_NONE (populated)",       PROT_NONE, 1, rd);
    row("LDADDAL PROT_NONE (fresh)",         PROT_NONE, 0, ldadd);
    row("LDADDAL PROT_READ (populated)",     PROT_READ, 1, ldadd);
    row("memset 0 4 KiB PROT_NONE",          PROT_NONE, 0, zero4k);
    row("memset 0 4 KiB PROT_READ (pop.)",   PROT_READ, 1, zero4k);
    mmap(g_base, 2 * PG, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    mprotect(g_base, PG, PROT_READ | PROT_WRITE);
    g_sig = 0;
    if (sigsetjmp(g_jb, 1) == 0) straddle();
    printf("%-34s %-7s %4d %8ld %8ld  0x%08x\n", "8 B load across page 0 RW | 1 NONE",
           g_sig == SIGBUS ? "SIGBUS" : g_sig == SIGSEGV ? "SIGSEGV" : "-", g_code, g_off, g_far_off, (uint32_t)g_esr);
    row("kill(getpid(), SIGSEGV)",           PROT_NONE, 0, self_kill);
    munmap(g_base, 2 * PG);
    row("read after munmap",                 PROT_NONE, -1, rd);

    /* 시스템 콜이 보호된 버퍼를 만지면: 신호가 아니라 EFAULT */
    unsigned char *k = mmap(NULL, PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    int p[2];
    if (pipe(p) == 0) {
        g_sig = 0;
        (void)!write(p[1], "hello", 5);
        ssize_t n1 = read(p[0], k, 5);                                 /* 커널이 PROT_NONE 페이지에 쓰기 */
        int e1 = errno;
        mprotect(k, PG, PROT_READ);
        ssize_t n2 = read(p[0], k, 5);                                 /* 커널이 READ 페이지에 쓰기 */
        int e2 = errno;
        printf("syscall: read() into PROT_NONE -> %zd (%s), into PROT_READ -> %zd (%s), signals caught %d\n",
               n1, n1 < 0 ? strerror(e1) : "ok", n2, n2 < 0 ? strerror(e2) : "ok", g_sig);
        close(p[0]); close(p[1]);
    }
    munmap(k, PG);

    /* PROT_NONE 예약은 메모리를 안 쓴다: mincore 로 상주 페이지 세기 */
    enum { N = 256 };
    unsigned char *r = mmap(NULL, N * PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    char vec[N];
    int before = 0, after = 0;
    mincore(r, N * PG, vec);
    for (int i = 0; i < N; i++) before += vec[i] & 1;
    mprotect(r, N * PG, PROT_READ | PROT_WRITE);
    for (int i = 0; i < N; i += 16) r[(size_t)i * PG] = 1;          /* 16 페이지만 쓴다 */
    mincore(r, N * PG, vec);
    for (int i = 0; i < N; i++) after += vec[i] & 1;
    printf("mincore: %d-page reservation resident %d -> after writing 16 pages resident %d (%zu MiB reserved)\n",
           N, before, after, N * PG >> 20);
    munmap(r, N * PG);
    return 0;
}
