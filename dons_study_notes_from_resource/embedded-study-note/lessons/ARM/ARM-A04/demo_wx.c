/* ARM-A04 demo_wx.c — macOS 의 실행 메모리 정책과 EL0 에서 되는/안 되는 캐시 명령을 실측 (CHECK 없음)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_wx.c -o /tmp/ARM-A04_wx && /tmp/ARM-A04_wx
 *        codesign -dv /tmp/ARM-A04_wx      # adhoc,linker-signed, hardened runtime 아님 → MAP_JIT 에 entitlement 불필요
 * 위험한 시도는 전부 fork 한 자식에서 → 부모가 시그널 번호를 출력.
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

typedef uint64_t (*fn_t)(uint64_t);
static uint32_t *g_jit;
static uint8_t g_buf[256] __attribute__((aligned(128)));

static void report(const char *what, void (*fn)(void)) {
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) { fn(); fflush(stdout); _exit(0); }
    int st = 0; waitpid(p, &st, 0);
    if (WIFSIGNALED(st)) printf("  %-40s -> killed by signal %d (%s)\n", what, WTERMSIG(st), strsignal(WTERMSIG(st)));
    else printf("  %-40s -> ok (exit %d)\n", what, WEXITSTATUS(st));
}
static void t_ctr(void)    { uint64_t v; __asm__ volatile("mrs %0, ctr_el0" : "=r"(v)); printf("  CTR_EL0 = 0x%llx\n", (unsigned long long)v); }
static void t_midr(void)   { uint64_t v; __asm__ volatile("mrs %0, midr_el1" : "=r"(v)); printf("  MIDR_EL1 = 0x%llx\n", (unsigned long long)v); }
static void t_dczid(void)  { uint64_t v; __asm__ volatile("mrs %0, dczid_el0" : "=r"(v)); printf("  DCZID_EL0 = 0x%llx (DC ZVA block = %u B)\n", (unsigned long long)v, 4u << (v & 15)); }
static void t_dccvau(void) { __asm__ volatile("dc cvau, %0" :: "r"(g_buf) : "memory"); }
static void t_dccivac(void){ __asm__ volatile("dc civac, %0" :: "r"(g_buf) : "memory"); }
static void t_icivau(void) { __asm__ volatile("ic ivau, %0" :: "r"(g_buf) : "memory"); }
static void t_iallu(void)  { __asm__ volatile("ic iallu" ::: "memory"); }
static void t_w_rx(void)   { pthread_jit_write_protect_np(1); g_jit[0] = 0xD65F03C0u; }
static void t_x_rw(void)   { pthread_jit_write_protect_np(0); (void)((fn_t)(uintptr_t)g_jit)(1); }
static void t_w_rx_cnt(void){ pthread_jit_write_protect_np(0); g_jit[0] = 0xD65F03C0u; pthread_jit_write_protect_np(1); printf("  (write inside the window ok)\n"); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t cl = 0, sz = sizeof cl;
    sysctlbyname("hw.cachelinesize", &cl, &sz, NULL, 0);
    printf("sysctl hw.cachelinesize = %zu\n", cl);

    printf("[1] system registers / cache maintenance at EL0\n");
    report("mrs ctr_el0", t_ctr);
    report("mrs midr_el1", t_midr);
    report("mrs dczid_el0", t_dczid);
    report("dc cvau (data buffer)", t_dccvau);
    report("dc civac (data buffer)", t_dccivac);
    report("ic ivau (data buffer)", t_icivau);
    report("ic iallu (EL1-only op)", t_iallu);

    printf("[2] executable-memory policy\n");
    void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    printf("  mmap RWX without MAP_JIT                 -> %s\n", p == MAP_FAILED ? strerror(errno) : "ok");
    uint32_t *r = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    r[0] = 0x91000400u; r[1] = 0xD65F03C0u;                          /* add x0,x0,#1 ; ret */
    int rc1 = mprotect(r, 16384, PROT_READ | PROT_EXEC);
    sys_icache_invalidate(r, 8);
    printf("  mprotect RW -> RX                        -> %s, f(41) = %llu\n", rc1 ? strerror(errno) : "ok",
           rc1 ? 0ull : (unsigned long long)((fn_t)(uintptr_t)r)(41));
    int rc2 = mprotect(r, 16384, PROT_READ | PROT_WRITE | PROT_EXEC);
    printf("  mprotect -> RWX                          -> %s\n", rc2 ? strerror(errno) : "ok");
    g_jit = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    printf("  mmap RWX with MAP_JIT                    -> %s\n", g_jit == MAP_FAILED ? strerror(errno) : "ok");
    printf("  pthread_jit_write_protect_supported_np() = %d\n", pthread_jit_write_protect_supported_np());
    pthread_jit_write_protect_np(0); g_jit[0] = 0xD65F03C0u; pthread_jit_write_protect_np(1);
    sys_icache_invalidate(g_jit, 4);

    printf("[3] W^X on a MAP_JIT page (per-thread toggle)\n");
    report("write while write-protected (RX)", t_w_rx);
    report("execute while writable (RW)", t_x_rw);
    report("write inside jit_write_protect(0) window", t_w_rx_cnt);
    return 0;
}
