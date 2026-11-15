/* ARM-B10 demo_bugs: run the Part B fuzz + page-end test against planted bugs (each in a child process)
 *   cc -std=c11 -O2 demo_bugs.c demo_bugs.S solution.S -o /tmp/ARM-B10_bugs && /tmp/ARM-B10_bugs
 */
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef void *(*copy_fn)(void *, const void *, size_t);
extern void *bug_nozero_byte(void *, const void *, size_t);
extern void *bug_tail_no_bit0(void *, const void *, size_t);
extern void *bug_tail_round_up(void *, const void *, size_t);
extern void *bug_clobber_x0(void *, const void *, size_t);
extern void *bug_head_and(void *, const void *, size_t);
extern size_t bug_strlen_plus1(const char *);
extern void *memcpy_16(void *, const void *, size_t);

enum { MAXLEN = 300, GUARD = 32, CANARY = 0xA5 };

static void fuzz(copy_fn f) {
    static uint8_t src[16 + MAXLEN + 16];
    static _Alignas(16) uint8_t dst[GUARD + 16 + MAXLEN + GUARD], exp[GUARD + 16 + MAXLEN + GUARD];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 37 + 11) == CANARY ? 0x5A : (uint8_t)(i * 37 + 11);
    long bad = 0, bad_ret = 0, total = 0;
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t sa = 0; sa < 16; sa++)
            for (size_t da = 0; da < 16; da++, total++) {
                memset(dst, CANARY, sizeof dst); memset(exp, CANARY, sizeof exp);
                memcpy(exp + GUARD + da, src + sa, len);
                void *r = f(dst + GUARD + da, src + sa, len);
                if (r != dst + GUARD + da) bad_ret++;
                if (memcmp(dst, exp, sizeof dst) != 0) {
                    if (bad == 0) {
                        size_t first = 0;
                        while (dst[first] == exp[first]) first++;
                        printf("  fuzz: first mismatch len=%zu sa=%zu da=%zu at dst_buf[%zu] (dst starts at %zu, ends at %zu)\n",
                               len, sa, da, first, (size_t)GUARD + da, GUARD + da + len);
                    }
                    bad++;
                }
            }
    printf("  fuzz: %ld/%ld content mismatches, %ld wrong return values\n", bad, total, bad_ret);
    fflush(stdout);
    size_t pg = (size_t)getpagesize();
    uint8_t *p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *q = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    mprotect(p + pg, pg, PROT_NONE); mprotect(q + pg, pg, PROT_NONE);
    for (size_t n = 1; n <= MAXLEN; n++) f(q + pg - 2 * MAXLEN, p + pg - n, n);   /* src ends at page end */
    printf("  page-end src: survived\n");
    for (size_t n = 1; n <= MAXLEN; n++) f(q + pg - n, p, n);                     /* dst ends at page end */
    printf("  page-end dst: survived\n");
}

static void run(const char *name, void (*body)(void)) {
    printf("%s\n", name); fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { body(); fflush(stdout); _exit(0); }
    int st; waitpid(pid, &st, 0);
    if (WIFSIGNALED(st)) printf("  -> child killed by signal %d (%s)\n", WTERMSIG(st), strsignal(WTERMSIG(st)));
}

static void t1(void) { fuzz(bug_tail_no_bit0); }
static void t0(void) {
    static char d[64]; static const char s[64] = "x";
    printf("  calling with n = 0 ...\n"); fflush(stdout);
    bug_nozero_byte(d, s, 0);
    printf("  returned (unexpected)\n");
}
static void t2(void) { fuzz(bug_tail_round_up); }
static void t3(void) { fuzz(bug_clobber_x0); }
static void t4(void) { fuzz(bug_head_and); }
static void t5(void) {
    printf("  strlen(\"\") = %zu, strlen(\"abc\") = %zu\n", bug_strlen_plus1(""), bug_strlen_plus1("abc"));
}

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmpu(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static double ns4k(copy_fn f, uint8_t *d, const uint8_t *s) {
    uint64_t t[31];
    for (int k = 0; k < 31; k++) {
        uint64_t t0 = now_ns();
        for (int r = 0; r < 2000; r++) f(d, s, 4096);
        t[k] = now_ns() - t0;
    }
    qsort(t, 31, sizeof t[0], cmpu);
    return t[15] / 2000.0;
}

int main(void) {
    run("bug 1: byte loop, no n==0 check, n = 0", t0);
    run("bug 2: tail without the 1-byte step", t1);
    run("bug 3: tail rounded up to a full 16-byte block", t2);
    run("bug 4: x0 used as cursor", t3);
    run("bug 5: head count dst&15 instead of -dst&15", t4);
    run("bug 6: strlen counts the NUL", t5);
    static _Alignas(64) uint8_t s[4096 + 64], d[4096 + 64];
    printf("bug 5 timing, 4096 B, dst+3 src+1 (median ns/call):\n");
    for (int rep = 0; rep < 3; rep++)
        printf("  memcpy_16 %.1f   bug_head_and %.1f\n", ns4k(memcpy_16, d + 3, s + 1), ns4k(bug_head_and, d + 3, s + 1));
    return 0;
}
