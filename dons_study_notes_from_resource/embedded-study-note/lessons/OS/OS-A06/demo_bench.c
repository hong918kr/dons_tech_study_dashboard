/* OS-A06 demo — 사용자 공간 폴트 한 번은 얼마나 비싼가, 증분 체크포인트는 언제 이득인가.
 *   1) 커널만의 첫 접근 폴트 (RW 매핑을 처음 쓰기)            — 기준선
 *   2) NONE → READ  (SIGBUS → 핸들러 → mprotect → 재실행)
 *   3) READ → RW    (쓰기 업그레이드, 페이지는 이미 상주)
 *   4) NONE → RW 한 번에 (ESR WnR 로 쓰기 판정)
 *   5) 에포크 하나의 비용: dirty 비율 f 에서 "증분 (쓰기 폴트 + dirty 복사 + 재보호)" vs "전체 memcpy 사본"
 *      재보호는 페이지마다 mprotect vs 연속 구간을 묶어 mprotect 한 번 (두 가지).
 *   각 항목 5 회 실행의 중앙값. 숫자는 이 기계·이 부하에서의 값 — 배수만 볼 것.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o db && ./db
 */
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static size_t PG;
enum { NP = 4096 };                              /* 16 KiB 페이지면 64 MiB */
static unsigned char *g_base;
static volatile unsigned char *g_state;          /* 0 NONE, 1 READ, 2 RW */
static int g_use_esr;
static volatile unsigned long g_faults;

static void on_fault(int sig, siginfo_t *si, void *ucv) {
    (void)sig; (void)ucv;
    size_t i = (size_t)((unsigned char *)si->si_addr - g_base) / PG;
    unsigned char *pa = g_base + i * PG;
    int want_rw = g_state[i] == 1;
#if defined(__APPLE__) && defined(__aarch64__)
    if (g_use_esr && g_state[i] == 0) {
        uint32_t e = ((ucontext_t *)ucv)->uc_mcontext->__es.__esr;
        want_rw = (e >> 26) == 0x24 && ((e >> 6) & 1) && !((e >> 8) & 1);
    }
#endif
    mprotect(pa, PG, want_rw ? PROT_READ | PROT_WRITE : PROT_READ);
    g_state[i] = want_rw ? 2 : 1;
    g_faults++;
}
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median5(double *v) { qsort(v, 5, sizeof *v, cmpd); return v[2]; }
static void fresh(int prot) {                    /* 새 매핑 = 한 번도 안 만진 페이지 */
    mmap(g_base, NP * PG, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
    memset((void *)g_state, 0, NP);
}

int main(void) {
    PG = (size_t)sysconf(_SC_PAGESIZE);
    g_base = mmap(NULL, NP * PG, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    g_state = calloc(NP, 1);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    printf("page %zu, region %d pages = %zu MiB, median of 5 runs\n", PG, NP, (size_t)NP * PG >> 20);

    double v[5], k_rw, k_rd, k_up, k_esr;
    for (int r = 0; r < 5; r++) { fresh(PROT_READ | PROT_WRITE); double t = now_us();
        for (size_t i = 0; i < NP; i++) ((volatile unsigned char *)g_base)[i * PG] = 1;
        v[r] = (now_us() - t) / NP; }
    k_rw = median5(v);
    for (int r = 0; r < 5; r++) { fresh(PROT_NONE); g_use_esr = 0; double t = now_us();
        for (size_t i = 0; i < NP; i++) (void)((volatile unsigned char *)g_base)[i * PG];
        v[r] = (now_us() - t) / NP; }
    k_rd = median5(v);
    for (int r = 0; r < 5; r++) { double t = now_us();             /* 위에서 READ 가 된 페이지에 쓰기 */
        for (size_t i = 0; i < NP; i++) ((volatile unsigned char *)g_base)[i * PG] = 2;
        v[r] = (now_us() - t) / NP;
        mprotect(g_base, NP * PG, PROT_READ); memset((void *)g_state, 1, NP); }
    k_up = median5(v);
    for (int r = 0; r < 5; r++) { fresh(PROT_NONE); g_use_esr = 1; double t = now_us();
        for (size_t i = 0; i < NP; i++) ((volatile unsigned char *)g_base)[i * PG] = 3;
        v[r] = (now_us() - t) / NP; }
    k_esr = median5(v);
    g_use_esr = 0;
    printf("\n%-44s %10s %12s\n", "fault kind", "us/fault", "faults/s");
    printf("%-44s %10.3f %12.0f\n", "kernel only: first write to RW mapping", k_rw, 1e6 / k_rw);
    printf("%-44s %10.3f %12.0f\n", "NONE->READ via SIGBUS + mprotect (+kernel)", k_rd, 1e6 / k_rd);
    printf("%-44s %10.3f %12.0f\n", "READ->RW upgrade (page resident)", k_up, 1e6 / k_up);
    printf("%-44s %10.3f %12.0f\n", "NONE->RW in one fault (ESR WnR)", k_esr, 1e6 / k_esr);

    /* 5) 에포크 비용 */
    unsigned char *snap = malloc((size_t)NP * PG), *store = malloc((size_t)NP * PG);
    unsigned char *dirty = calloc(NP, 1);
    fresh(PROT_READ | PROT_WRITE);
    memset(g_base, 7, (size_t)NP * PG);                               /* 전부 상주 */
    mprotect(g_base, NP * PG, PROT_READ); memset((void *)g_state, 1, NP);
    for (int r = 0; r < 3; r++) memcpy(snap, g_base, (size_t)NP * PG);  /* 데우기 */
    for (int r = 0; r < 5; r++) { double t = now_us(); memcpy(snap, g_base, (size_t)NP * PG);
        __asm__ volatile("" :: "r"(snap) : "memory"); v[r] = now_us() - t; }   /* 사본을 버리지 못하게 */
    double full = median5(v);
    printf("\nepoch cost, %zu MiB region; full memcpy snapshot = %.0f us\n", (size_t)NP * PG >> 20, full);
    printf("%8s %8s %14s %16s %14s %10s\n", "dirty%", "pages", "incr per-page", "incr coalesced", "faults only", "full/incr");
    static const double fr[] = { 0.01, 0.05, 0.10, 0.25, 0.50, 1.00 };
    for (int k = 0; k < 6; k++) {
        size_t nd = (size_t)(fr[k] * NP);
        double vp[5], vc[5], vf[5];
        for (int mode = 0; mode < 2; mode++) {
            for (int r = 0; r < 5; r++) {
                memset(dirty, 0, NP);
                uint32_t x = 12345u + (uint32_t)k * 77u + (uint32_t)r;
                size_t marked = 0;
                while (marked < nd) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; size_t p = x % NP; if (!dirty[p]) { dirty[p] = 1; marked++; } }
                double t0 = now_us();
                for (size_t p = 0; p < NP; p++) if (dirty[p]) ((volatile unsigned char *)g_base)[p * PG + 8] = (unsigned char)r;
                double t1 = now_us();
                size_t out = 0;                                        /* 체크포인트: dirty 복사 + 재보호 */
                for (size_t p = 0; p < NP; p++) {
                    if (g_state[p] != 2) continue;
                    memcpy(store + out++ * PG, g_base + p * PG, PG);
                    __asm__ volatile("" :: "r"(store) : "memory");
                    if (mode == 0) mprotect(g_base + p * PG, PG, PROT_READ);
                }
                if (mode == 1) {
                    for (size_t p = 0; p < NP;) {
                        if (g_state[p] != 2) { p++; continue; }
                        size_t q = p;
                        while (q < NP && g_state[q] == 2) q++;
                        mprotect(g_base + p * PG, (q - p) * PG, PROT_READ);
                        p = q;
                    }
                }
                for (size_t p = 0; p < NP; p++) if (g_state[p] == 2) g_state[p] = 1;
                double t2 = now_us();
                (mode ? vc : vp)[r] = t2 - t0;
                vf[r] = t1 - t0;
                if (out != nd) printf("BUG: saved %zu != dirty %zu\n", out, nd);
            }
        }
        double p = median5(vp), c = median5(vc), f = median5(vf);
        printf("%7.0f%% %8zu %11.0f us %13.0f us %11.0f us %9.1fx\n", fr[k] * 100, nd, p, c, f, full / c);
    }
    printf("(full/incr: >1 means incremental is cheaper; full copy also needs a stop-the-world pause of that length)\n");
    return 0;
}
