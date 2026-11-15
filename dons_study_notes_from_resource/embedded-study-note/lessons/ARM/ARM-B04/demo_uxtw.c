/* ARM-B04 demo_uxtw.c — uxtw/sxtw 혼동이 실제로 어떤 크래시가 되는지
 * 빌드: cc -std=c11 -O2 demo_uxtw.c demo_uxtw.S -o /tmp/ARM-B04_uxtw && /tmp/ARM-B04_uxtw */
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int64_t gather_one_uxtw(const int32_t *base, int32_t k);
int64_t gather_one_sxtw(const int32_t *base, int32_t k);

static const int32_t *g_base;
static void on_segv(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    char buf[160];   /* 데모라서 snprintf 사용 (엄밀히는 async-signal-safe 아님) */
    int n = snprintf(buf, sizeof buf, "signal %d (SIGSEGV=%d, SIGBUS=%d) fault addr = base + 0x%llx\n", sig, SIGSEGV, SIGBUS,
                     (unsigned long long)((uintptr_t)si->si_addr - (uintptr_t)g_base));
    write(1, buf, (size_t)n);
    _exit(0);
}
int main(void) {
    static const int32_t arr[4] = { 111, -222, 333, 444 };
    g_base = arr + 2;                                     /* base[-1] == -222 */
    printf("sxtw: base[-1] = %lld\n", (long long)gather_one_sxtw(g_base, -1));
    fflush(stdout);
    struct sigaction sa = { 0 };
    sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    printf("uxtw: base[-1] = %lld\n", (long long)gather_one_uxtw(g_base, -1));
    return 1;
}
