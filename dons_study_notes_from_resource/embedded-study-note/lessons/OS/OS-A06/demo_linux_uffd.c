/* ENV: linux */
/* BUILD: -pthread */
/* OS-A06 demo — 같은 일을 리눅스(arm64)에서 커널 기능으로 하면?
 *   1) mprotect + SIGSEGV: si_code 와 ESR(esr_context 레코드)  — 맥과 비교
 *   2) userfaultfd MISSING (UFFD_USER_MODE_ONLY): 폴트를 시그널이 아니라 fd 메시지로 받아 UFFDIO_COPY 로 채움
 *   3) userfaultfd WP-async + PAGEMAP_SCAN: 커널이 dirty(written) 페이지 목록을 주고 다시 쓰기 보호까지 한 번에
 *   4) soft-dirty (/proc/self/clear_refs = 4, pagemap bit 55): arm64 에서 되나?
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_uffd.c -o d && ./d
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

static size_t PG;
static unsigned char *g_base;
static sigjmp_buf g_jb;
static volatile int g_sig, g_code; static volatile uint64_t g_esr; static volatile long g_off;

static uint64_t find_esr(ucontext_t *uc) {            /* __reserved[] 안의 _aarch64_ctx 레코드를 따라간다 */
#if defined(__aarch64__)
    struct _aarch64_ctx *h = (struct _aarch64_ctx *)(void *)uc->uc_mcontext.__reserved;
    while (h->magic != 0 && h->size != 0) {
        if (h->magic == ESR_MAGIC) return ((struct esr_context *)(void *)h)->esr;
        h = (struct _aarch64_ctx *)(void *)((char *)h + h->size);
    }
#else
    (void)uc;
#endif
    return 0;
}
static void on_fault(int sig, siginfo_t *si, void *ucv) {
    g_sig = sig; g_code = si->si_code; g_esr = find_esr(ucv);
    g_off = (long)((unsigned char *)si->si_addr - g_base);
    siglongjmp(g_jb, 1);
}
static void probe(const char *what, int prot, int write) {
    mprotect(g_base, PG, prot);
    g_sig = 0;
    if (sigsetjmp(g_jb, 1) == 0) {
        if (write) ((volatile unsigned char *)g_base)[5] = 1; else (void)((volatile unsigned char *)g_base)[5];
    }
    printf("  %-22s sig=%-7s si_code=%d (%s) off=%ld esr=0x%08llx WnR=%llu DFSC=0x%02llx\n", what,
           g_sig == SIGSEGV ? "SIGSEGV" : g_sig == SIGBUS ? "SIGBUS" : "none", g_code,
           g_code == SEGV_ACCERR ? "SEGV_ACCERR" : g_code == SEGV_MAPERR ? "SEGV_MAPERR" : "?", g_off,
           (unsigned long long)g_esr, (unsigned long long)(g_esr >> 6) & 1, (unsigned long long)g_esr & 0x3f);
}

/* ---------- 2) userfaultfd MISSING ---------- */
static int g_uffd; static volatile int g_stop; static long g_uffd_faults;
static void *uffd_thread(void *arg) {
    (void)arg;
    unsigned char *page = aligned_alloc(PG, PG);
    while (!g_stop) {
        struct pollfd p = { g_uffd, POLLIN, 0 };
        if (poll(&p, 1, 50) <= 0) continue;
        struct uffd_msg m;
        if (read(g_uffd, &m, sizeof m) != (ssize_t)sizeof m) continue;
        if (m.event != UFFD_EVENT_PAGEFAULT) continue;
        uint64_t a = m.arg.pagefault.address & ~(uint64_t)(PG - 1);
        size_t idx = (size_t)(a - (uint64_t)(uintptr_t)g_base) / PG;
        memset(page, (int)(0x40 + idx), PG);                 /* "백킹 스토어에서 읽어 온" 내용 */
        struct uffdio_copy c = { .dst = a, .src = (uint64_t)(uintptr_t)page, .len = PG, .mode = 0 };
        if (ioctl(g_uffd, UFFDIO_COPY, &c) == 0) g_uffd_faults++;
    }
    free(page);
    return NULL;
}

int main(void) {
    PG = (size_t)sysconf(_SC_PAGESIZE);
    printf("page size %zu\n1) mprotect + SIGSEGV on linux/arm64\n", PG);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    g_base = mmap(NULL, PG, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    probe("PROT_NONE read", PROT_NONE, 0);
    probe("PROT_NONE write", PROT_NONE, 1);
    probe("PROT_READ write (new)", PROT_READ, 1);
    mprotect(g_base, PG, PROT_READ | PROT_WRITE); g_base[0] = 1;              /* PTE 를 만든 뒤 */
    probe("PROT_READ write (pte)", PROT_READ, 1);
    munmap(g_base, PG);
    probe("unmapped read", PROT_NONE, 0);

    /* ---------- 2) ---------- */
    enum { N = 64 };
    printf("2) userfaultfd MISSING, UFFD_USER_MODE_ONLY (vm.unprivileged_userfaultfd 0 이어도 허용)\n");
    g_uffd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    if (g_uffd < 0) { printf("  userfaultfd: %s\n", strerror(errno)); return 0; }
    struct uffdio_api api = { .api = UFFD_API, .features = 0 };
    if (ioctl(g_uffd, UFFDIO_API, &api)) { printf("  UFFDIO_API: %s\n", strerror(errno)); return 0; }
    printf("  kernel features 0x%llx (WP_ASYNC %s)\n", (unsigned long long)api.features,
           (api.features & UFFD_FEATURE_WP_ASYNC) ? "yes" : "no");
    g_base = mmap(NULL, N * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    struct uffdio_register reg = { .range = { (uint64_t)(uintptr_t)g_base, N * PG }, .mode = UFFDIO_REGISTER_MODE_MISSING };
    if (ioctl(g_uffd, UFFDIO_REGISTER, &reg)) { printf("  REGISTER: %s\n", strerror(errno)); return 0; }
    pthread_t th; pthread_create(&th, NULL, uffd_thread, NULL);
    int ok = 1, touched = 0;
    for (int i = 0; i < N; i += 3) { ok &= g_base[(size_t)i * PG + 7] == (unsigned char)(0x40 + i); touched++; }
    for (int i = 0; i < N; i += 3) (void)((volatile unsigned char *)g_base)[(size_t)i * PG + 9];   /* 두 번째 접근: 폴트 없음 */
    g_stop = 1; pthread_join(th, NULL);
    printf("  touched %d pages -> %ld uffd faults, content from handler %s\n", touched, g_uffd_faults, ok ? "OK" : "BAD");
    close(g_uffd); munmap(g_base, N * PG);

    /* ---------- 3) WP async + PAGEMAP_SCAN ---------- */
    printf("3) uffd WP_ASYNC + PAGEMAP_SCAN (CRIU 류의 dirty 추적)\n");
    int u = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    struct uffdio_api api2 = { .api = UFFD_API, .features = UFFD_FEATURE_WP_ASYNC | UFFD_FEATURE_WP_UNPOPULATED };
    if (u < 0 || ioctl(u, UFFDIO_API, &api2)) { printf("  WP_ASYNC api: %s\n", strerror(errno)); return 0; }
    unsigned char *b = mmap(NULL, N * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    struct uffdio_register r2 = { .range = { (uint64_t)(uintptr_t)b, N * PG }, .mode = UFFDIO_REGISTER_MODE_WP };
    if (ioctl(u, UFFDIO_REGISTER, &r2)) { printf("  REGISTER WP: %s\n", strerror(errno)); return 0; }
    struct uffdio_writeprotect wp = { .range = { (uint64_t)(uintptr_t)b, N * PG }, .mode = UFFDIO_WRITEPROTECT_MODE_WP };
    if (ioctl(u, UFFDIO_WRITEPROTECT, &wp)) { printf("  WRITEPROTECT: %s\n", strerror(errno)); return 0; }
    int pm = open("/proc/self/pagemap", O_RDONLY);
    for (int epoch = 1; epoch <= 3; epoch++) {
        int written = 0;
        for (int i = epoch; i < N; i += 5 * epoch) { b[(size_t)i * PG + 1] = (unsigned char)epoch; written++; }
        for (int i = 0; i < N; i += 2) (void)((volatile unsigned char *)b)[(size_t)i * PG];   /* 읽기는 dirty 아님 */
        struct page_region vec[N];
        struct pm_scan_arg a = { .size = sizeof a, .flags = PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
                                 .start = (uint64_t)(uintptr_t)b, .end = (uint64_t)(uintptr_t)(b + N * PG),
                                 .vec = (uint64_t)(uintptr_t)vec, .vec_len = N,
                                 .category_mask = PAGE_IS_WRITTEN, .return_mask = PAGE_IS_WRITTEN };
        long n = ioctl(pm, PAGEMAP_SCAN, &a);
        long pages = 0;
        for (long k = 0; k < n; k++) pages += (long)((vec[k].end - vec[k].start) / PG);
        printf("  epoch %d: wrote %d pages -> PAGEMAP_SCAN %ld regions, %ld written pages (re-protected)\n",
               epoch, written, n, pages);
    }
    close(pm); close(u); munmap(b, N * PG);

    /* ---------- 4) soft-dirty ---------- */
    printf("4) soft-dirty\n");
    unsigned char *s = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    s[0] = 1;
    int cr = open("/proc/self/clear_refs", O_WRONLY);
    ssize_t w = cr >= 0 ? write(cr, "4", 1) : -1;
    s[0] = 2;
    uint64_t ent = 0; int pm2 = open("/proc/self/pagemap", O_RDONLY);
    ssize_t rd = pread(pm2, &ent, 8, (off_t)((uintptr_t)s / PG * 8));
    printf("  clear_refs<-4: %s, pagemap read %zd, bit55(soft-dirty)=%llu after write (arm64: no HAVE_ARCH_SOFT_DIRTY)\n",
           w == 1 ? "ok" : strerror(errno), rd, (unsigned long long)(ent >> 55) & 1);
    close(cr); close(pm2); munmap(s, PG);
    return 0;
}
