/* ARCH-A03 demo_fault.c — Part A (첫 접근 vs 재접근, 쓰기 먼저 vs 읽기 먼저) + Part C (prefault 방법) 실측.
 * solution.c 를 main 이름만 바꿔 포함한다 → 체크된 함수(touch_pages, resident_pages, prefault)로 잰다.
 *
 *   cc -std=c11 -O2 demo_fault.c -o demo_fault && ./demo_fault            (기본: 7 회, N = 256 1024 4096 16384 페이지)
 *   ./demo_fault 9 16384                                                  (9 회, N = 16384 만)
 *
 * 매 실행마다 새 mmap (fresh mapping) → 첫 패스는 반드시 페이지마다 폴트. 표의 값 = 실행들의 중앙값(median).
 * 각 패스: 페이지마다 1 바이트 (stride = 페이지) — ns/페이지, ru_minflt 증가/페이지, 커널(sys) 시간.
 * QoS USER_INTERACTIVE 를 요청한다 (힌트일 뿐 — macOS 는 코어 고정이 없다; P/E 코어 어디서든 돌 수 있다).
 */
#define main solution_main
#include "solution.c"
#undef main
#include <errno.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <time.h>
#include <mach/mach_vm.h>

#define MAXRUNS 15
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void ru_snap(long *flt, double *sys_us) {
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    *flt = r.ru_minflt;
    *sys_us = (double)r.ru_stime.tv_sec * 1e6 + (double)r.ru_stime.tv_usec;
}

typedef struct { double ns, flt, sys; } meas;   /* ns/페이지, 폴트/페이지, sys µs (패스 전체) */

static meas pass(uint8_t *m, size_t n, size_t pg, int mode) {
    long f0, f1; double s0, s1;
    ru_snap(&f0, &s0);
    uint64_t t0 = now_ns();
    volatile uint64_t sink = touch_pages(m, n, pg, 0, mode);
    uint64_t t1 = now_ns();
    ru_snap(&f1, &s1);
    (void)sink;
    meas r = {(double)(t1 - t0) / (double)n, (double)(f1 - f0) / (double)n, s1 - s0};
    return r;
}

static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof v[0], cmpd);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

/* 한 시나리오 = 새 매핑에 패스 3 개. modes[k] = T_READ/T_WRITE. */
static void scenario(const char *name, size_t n, size_t pg, const int modes[3], int runs) {
    double ns[3][MAXRUNS], fl[3][MAXRUNS], sy[3][MAXRUNS];
    for (int r = 0; r < runs; r++) {
        uint8_t *m = map_anon(n * pg);
        if (!m) { perror("mmap"); exit(1); }
        for (int k = 0; k < 3; k++) {
            meas x = pass(m, n, pg, modes[k]);
            ns[k][r] = x.ns; fl[k][r] = x.flt; sy[k][r] = x.sys;
        }
        munmap(m, n * pg);
    }
    printf("%-11s N=%-6zu", name, n);
    for (int k = 0; k < 3; k++) {
        double med = median(ns[k], runs);                 /* median() 이 정렬한다 → [0] = 최소, [runs-1] = 최대 */
        printf(" | %c%d %6.1f [%4.0f-%4.0f] ns %4.2f flt %5.0f us", modes[k] == T_WRITE ? 'W' : 'R', k + 1,
               med, ns[k][0], ns[k][runs - 1], median(fl[k], runs), median(sy[k], runs));
    }
    printf("\n");
}

/* Part C: prefault 방법별 — prefault 자체 시간 + 그 뒤 첫 사용(쓰기) 패스 + 두 번째 패스 */
static void prefault_table(size_t n, size_t pg, int runs) {
    printf("\nPart C prefault, N=%zu pages (%zu MiB), median of %d (ns/page; flt = minflt/page during use1)\n",
           n, n * pg >> 20, runs);
    printf("%-14s %10s %10s %10s %8s %9s\n", "method", "prefault", "use1", "use2", "flt", "resident");
    for (int how = -1; how < PF_N; how++) {
        double pf[MAXRUNS], u1[MAXRUNS], u2[MAXRUNS], fl[MAXRUNS], rs[MAXRUNS];
        for (int r = 0; r < runs; r++) {
            uint8_t *m = map_anon(n * pg);
            if (!m) { perror("mmap"); exit(1); }
            uint64_t t0 = now_ns();
            if (how >= 0 && prefault(m, n * pg, pg, how) != 0) { perror("prefault"); exit(1); }
            uint64_t t1 = now_ns();
            rs[r] = (double)resident_pages(m, n, pg);
            meas a = pass(m, n, pg, T_WRITE), b = pass(m, n, pg, T_WRITE);
            pf[r] = (double)(t1 - t0) / (double)n; u1[r] = a.ns; u2[r] = b.ns; fl[r] = a.flt;
            if (how == PF_MLOCK) munlock(m, n * pg);
            munmap(m, n * pg);
        }
        printf("%-14s %10.1f %10.1f %10.1f %8.2f %9.0f\n", how < 0 ? "none" : PFNAME[how],
               median(pf, runs), median(u1, runs), median(u2, runs), median(fl, runs), median(rs, runs));
    }
}

static void superpage_probe(void) {
    printf("\nsuperpage probe (2 MiB request, 4 MiB region):\n");
    void *a = mmap(NULL, 4u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0);
    printf("  mmap(fd=VM_FLAGS_SUPERPAGE_SIZE_2MB)  -> %s\n", a == MAP_FAILED ? strerror(errno) : "OK");
    if (a != MAP_FAILED) munmap(a, 4u << 20);
    a = mmap(NULL, 4u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, VM_FLAGS_SUPERPAGE_SIZE_ANY, 0);
    printf("  mmap(fd=VM_FLAGS_SUPERPAGE_SIZE_ANY)  -> %s\n", a == MAP_FAILED ? strerror(errno) : "OK");
    if (a != MAP_FAILED) munmap(a, 4u << 20);
    mach_vm_address_t va = 0;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &va, 4u << 20, VM_FLAGS_ANYWHERE | VM_FLAGS_SUPERPAGE_SIZE_2MB);
    printf("  mach_vm_allocate(SUPERPAGE_SIZE_2MB)  -> kr=%d (%s)\n", kr, mach_error_string(kr));
    if (kr == KERN_SUCCESS) mach_vm_deallocate(mach_task_self(), va, 4u << 20);
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 7;
    if (runs < 1 || runs > MAXRUNS) runs = 7;
    size_t Ns[8] = {256, 1024, 4096, 16384};
    int nN = 4;
    if (argc > 2) { nN = 0; for (int i = 2; i < argc && nN < 8; i++) Ns[nN++] = (size_t)strtoull(argv[i], NULL, 10); }

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    char brand[64] = "?"; size_t bl = sizeof brand;
    sysctlbyname("machdep.cpu.brand_string", brand, &bl, NULL, 0);
    double la[3] = {0, 0, 0};
    getloadavg(la, 3);
    size_t pg = sys_page_size();
    printf("%s, page %zu B, clang %s, runs %d, load avg %.2f %.2f %.2f (start)\n", brand, pg, __clang_version__, runs,
           la[0], la[1], la[2]);

    printf("\nPart A: 3 passes over a FRESH anonymous mapping, 1 byte per page\n"
           "        (median ns/page [min-max over runs], median minflt/page, median kernel time of the pass)\n");
    static const int WWW[3] = {T_WRITE, T_WRITE, T_WRITE}, RWW[3] = {T_READ, T_WRITE, T_WRITE},
                     WRR[3] = {T_WRITE, T_READ, T_READ};
    for (int i = 0; i < nN; i++) {
        scenario("write-first", Ns[i], pg, WWW, runs);
        scenario("read-first", Ns[i], pg, RWW, runs);
        scenario("write,read", Ns[i], pg, WRR, runs);
    }
    prefault_table(Ns[nN - 1], pg, runs);
    superpage_probe();
    getloadavg(la, 3);
    printf("\nload avg %.2f %.2f %.2f (end)\n", la[0], la[1], la[2]);
    return 0;
}
