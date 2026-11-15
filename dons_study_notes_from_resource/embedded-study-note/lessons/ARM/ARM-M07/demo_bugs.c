/* ARM-M07 demo_bugs: what each injected bug (shared_bugs.S) actually returns
 *   cc -std=c11 -O2 demo_bugs.c solution.S shared_bugs.S -o /tmp/ARM-M07_bugs && /tmp/ARM-M07_bugs
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern const void *neon_memchr(const void *p, int c, size_t n);
extern uint64_t    checksum_u8(const uint8_t *p, size_t n);
extern void        minmax_u16(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);
extern const void *bug_memchr_norbit(const void *p, int c, size_t n);
extern const void *bug_memchr_nomask(const void *p, int c, size_t n);
extern const void *bug_memchr_overread(const void *p, int c, size_t n);
extern uint64_t    bug_checksum_nofold(const uint8_t *p, size_t n);
extern void        bug_minmax_signed(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);
extern void        bug_minmax_notail(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);

int main(void) {
    static const char f[32] = "hello~world~~xyz!?";
    printf("memchr(\"hello~world~~xyz\", '~', 16): good=%td  norbit=%td\n",
           (const char *)neon_memchr(f, '~', 16) - f, (const char *)bug_memchr_norbit(f, '~', 16) - f);
    printf("memchr(\"hello\", 0x100+'l', 5):       good=%td  nomask=%p\n",
           (const char *)neon_memchr(f, 0x100 + 'l', 5) - f, (const void *)bug_memchr_nomask(f, 0x100 + 'l', 5));
    printf("memchr(\"hello~world~~xyz\", 'z', 13): good=%p  overread=%td (past n!)\n",
           (const void *)neon_memchr(f, 'z', 13), (const char *)bug_memchr_overread(f, 'z', 13) - f);
    static uint8_t ff[4096];
    memset(ff, 0xFF, sizeof ff);
    size_t ns[] = {2048, 2064, 4096};
    for (int i = 0; i < 3; i++)
        printf("checksum(0xFF x %4zu): good=%llu  nofold=%llu\n", ns[i],
               (unsigned long long)checksum_u8(ff, ns[i]), (unsigned long long)bug_checksum_nofold(ff, ns[i]));
    static const uint16_t adc[10] = {2048, 2100, 0xFFFF, 1990, 2050, 2000, 2010, 2020, 1500, 2600};
    uint16_t mn, mx;
    minmax_u16(adc, 10, &mn, &mx);        printf("minmax good   : mn=%5u mx=%5u\n", mn, mx);
    bug_minmax_signed(adc, 10, &mn, &mx); printf("minmax signed : mn=%5u mx=%5u\n", mn, mx);
    bug_minmax_notail(adc, 10, &mn, &mx); printf("minmax notail : mn=%5u mx=%5u\n", mn, mx);
    /* page guard: 5 bytes that end exactly at a PROT_NONE page */
    size_t pg = (size_t)getpagesize();
    uint8_t *m = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED || mprotect(m + pg, pg, PROT_NONE)) return 2;
    memset(m, 'a', pg);
    const char *names[2] = {"neon_memchr        ", "bug_memchr_overread"};
    for (int k = 0; k < 2; k++) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            const void *r = k ? bug_memchr_overread(m + pg - 5, 'Z', 5) : neon_memchr(m + pg - 5, 'Z', 5);
            printf("%s(page_end-5, 'Z', 5) = %p, no fault\n", names[k], (void *)(uintptr_t)r);
            fflush(stdout);
            _exit(0);
        }
        int st; waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) printf("%s(page_end-5, 'Z', 5) -> killed by signal %d (%s)\n",
                                    names[k], WTERMSIG(st), strsignal(WTERMSIG(st)));
    }
    return 0;
}
