/*
 * C23_lazy_vm_tricks.c
 * OSTEP Ch.23 (VAX/VMS) 에 나온 "게으른" VM 기법 3가지를 macOS에서 직접 관찰한다.
 *
 *  1) Demand zeroing : 큰 익명 mmap은 만들 때 비용 0, 처음 만질 때 페이지 폴트 + 0으로 채워짐
 *  2) Copy-on-write  : fork() 후 자식이 읽기만 하면 복사 없음, 쓰면 그때 페이지별 복사
 *  3) Reference bit 흉내 (Babaoglu & Joy, 1981): 보호 비트(PROT_NONE)로 trap을 받아
 *     "최근에 쓰인 페이지" 를 OS(여기선 시그널 핸들러)가 기록
 *
 * 측정: getrusage(RUSAGE_SELF).ru_minflt (디스크 I/O 없는 페이지 폴트 수)
 *       + mach task_info 의 resident_size (RSS)
 *
 * build: cc -Wall -Wextra -O0 code/C23_lazy_vm_tricks.c -o .work/bin/C23_lazy_vm_tricks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <mach/mach.h>

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static long minflt(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

static double rss_mib(void)
{
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &cnt) != KERN_SUCCESS)
        return -1.0;
    return (double)info.resident_size / (1024.0 * 1024.0);
}

/* ---------- 1) demand zeroing ---------- */
static void demo_demand_zero(size_t pg)
{
    const size_t len = 64u << 20;                 /* 64 MiB */
    size_t npages = len / pg;

    printf("== 1. Demand zeroing (64 MiB anonymous mmap, page=%zu B, %zu pages) ==\n",
           pg, npages);
    long f0 = minflt(); double r0 = rss_mib();
    unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    long f1 = minflt(); double r1 = rss_mib();
    printf("  after mmap        : +%5ld faults, RSS %+7.2f MiB  (주소 공간만 생김)\n",
           f1 - f0, r1 - r0);

    p[0] = 1;                                      /* 첫 페이지 한 번 터치 */
    long f2 = minflt(); double r2 = rss_mib();
    printf("  touch 1 page      : +%5ld faults, RSS %+7.2f MiB\n", f2 - f1, r2 - r1);

    unsigned long nonzero = 0;
    for (size_t i = pg; i < len; i += pg)          /* 나머지 페이지 첫 바이트 읽기 */
        nonzero += p[i] != 0;
    long f3 = minflt(); double r3 = rss_mib();
    printf("  read all pages    : +%5ld faults, RSS %+7.2f MiB, nonzero bytes seen = %lu\n",
           f3 - f2, r3 - r2, nonzero);

    for (size_t i = pg; i < len; i += pg)          /* 이제 쓰기 */
        p[i] = 2;
    long f4 = minflt(); double r4 = rss_mib();
    printf("  write all pages   : +%5ld faults, RSS %+7.2f MiB\n\n", f4 - f3, r4 - r3);
    munmap(p, len);
}

/* ---------- 2) copy-on-write fork ---------- */
static void demo_cow(size_t pg)
{
    const size_t len = 32u << 20;                  /* 32 MiB, 미리 다 채워 둠 */
    unsigned char *p = malloc(len);
    if (!p) { perror("malloc"); exit(1); }
    memset(p, 0xAB, len);

    printf("== 2. Copy-on-write fork (parent owns 32 MiB = %zu pages, all touched) ==\n",
           len / pg);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        long f0 = minflt();
        double t0 = now_us();
        unsigned long sum = 0;
        for (size_t i = 0; i < len; i += pg)       /* 자식: 읽기만 */
            sum += p[i];
        long f1 = minflt();
        double t1 = now_us();
        for (size_t i = 0; i < len; i += pg)       /* 자식: 이제 쓰기 */
            p[i] = 0xCD;
        long f2 = minflt();
        double t2 = now_us();
        printf("  child read  all pages : +%5ld faults, %7.0f us (sum=%lu)\n",
               f1 - f0, t1 - t0, sum);
        printf("  child write all pages : +%5ld faults, %7.0f us  <- 페이지마다 private copy\n",
               f2 - f1, t2 - t1);
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("  parent sees p[0] = 0x%02X (child's write did not leak)\n\n", p[0]);
    free(p);
}

/* ---------- 3) reference bit emulation via protection ---------- */
#define NPG 8
static unsigned char *region;
static size_t g_pg;
static volatile sig_atomic_t referenced[NPG];
static volatile sig_atomic_t traps;

static void on_fault(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)uc;
    unsigned char *a = (unsigned char *)si->si_addr;
    if (a < region || a >= region + NPG * g_pg)
        _exit(99);                                 /* 진짜 버그면 그냥 죽는다 */
    size_t idx = (size_t)(a - region) / g_pg;
    referenced[idx] = 1;                           /* "use bit = 1" 기록 */
    traps++;
    /* 원래 권한으로 되돌림 → 같은 페이지 다음 접근은 trap 없이 진행 */
    mprotect(region + idx * g_pg, g_pg, PROT_READ | PROT_WRITE);
}

static void clear_ref_bits(void)
{
    for (int i = 0; i < NPG; i++) referenced[i] = 0;
    mprotect(region, NPG * g_pg, PROT_NONE);       /* 전부 "접근 불가" 로 */
}

static void show_ref_bits(const char *tag)
{
    printf("  %-28s use bits:", tag);
    for (int i = 0; i < NPG; i++) printf(" %d", (int)referenced[i]);
    printf("   (traps so far=%d)\n", (int)traps);
}

static void demo_ref_bits(size_t pg)
{
    g_pg = pg;
    region = mmap(NULL, NPG * pg, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (region == MAP_FAILED) { perror("mmap"); exit(1); }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);                 /* macOS는 보호 위반이 SIGBUS로 */
    sigaction(SIGBUS, &sa, NULL);                  /* 오는 경우도 있어 둘 다 등록 */

    printf("== 3. Emulating reference bits with PROT_NONE (8 pages) ==\n");
    clear_ref_bits();
    show_ref_bits("epoch 1 start (all cleared)");
    region[1 * pg] = 1;  region[3 * pg] = 3;  region[3 * pg + 8] = 3;
    (void)*(volatile unsigned char *)&region[6 * pg];
    show_ref_bits("after touching 1,3,3,6");

    clear_ref_bits();                              /* 주기적 clear = clock의 바늘 */
    region[3 * pg] = 4; region[7 * pg] = 7;
    show_ref_bits("epoch 2: touched 3,7");

    printf("  => victim candidates in epoch 2 (use bit 0):");
    for (int i = 0; i < NPG; i++) if (!referenced[i]) printf(" %d", i);
    printf("\n");
    munmap(region, NPG * pg);
}

int main(void)
{
    size_t pg = (size_t)getpagesize();
    demo_demand_zero(pg);
    demo_cow(pg);
    demo_ref_bits(pg);
    return 0;
}
