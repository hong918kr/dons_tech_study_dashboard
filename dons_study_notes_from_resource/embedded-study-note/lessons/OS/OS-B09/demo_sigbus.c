/* OS-B09 demo_sigbus.c — 매핑 접근 실패가 어떤 시그널(si_signo / si_code / si_addr)로 오는지 본다.
 *
 * 각 시나리오는 자식 프로세스에서 실행한다. 자식은 SA_SIGINFO 핸들러로 시그널을 잡아
 * write(2) 로 정보를 찍고 _exit 한다 (printf 는 async-signal-safe 가 아니라 직접 16진수로 만든다).
 * 잡아서 끝내므로 macOS 크래시 리포트(.ips)도 남지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sigbus.c -o dsb && ./dsb
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static long PG;
static volatile char *g_base;          /* 핸들러가 "매핑 시작에서 몇 바이트" 를 계산할 기준 */

static size_t put_str(char *b, size_t i, const char *s) { while (*s) b[i++] = *s++; return i; }
static size_t put_dec(char *b, size_t i, long v) {
    char t[24]; int n = 0;
    if (v < 0) { b[i++] = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) b[i++] = t[--n];
    return i;
}
static size_t put_hex(char *b, size_t i, uintptr_t v) {
    i = put_str(b, i, "0x");
    for (int s = (int)(sizeof v * 8) - 4; s >= 0; s -= 4) b[i++] = "0123456789abcdef"[(v >> s) & 15];
    return i;
}

static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    char b[160]; size_t i = 0;
    i = put_str(b, i, "    caught signo=");  i = put_dec(b, i, si->si_signo);
    i = put_str(b, i, sig == SIGBUS ? " (SIGBUS)" : sig == SIGSEGV ? " (SIGSEGV)" : " (?)");
    i = put_str(b, i, " si_code=");          i = put_dec(b, i, si->si_code);
    i = put_str(b, i, " si_addr=");          i = put_hex(b, i, (uintptr_t)si->si_addr);
    i = put_str(b, i, " = base+");           i = put_dec(b, i, (long)((uintptr_t)si->si_addr - (uintptr_t)g_base));
    b[i++] = '\n';
    ssize_t w = write(1, b, i); (void)w;
    _exit(100 + sig);
}

static void run(const char *title, volatile char *base, size_t off, int is_write) {
    printf("%s\n", title);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGSEGV, &sa, NULL);
        alarm(2);
        g_base = base;
        if (is_write) base[off] = 'x'; else { char c = base[off]; (void)c; }
        ssize_t w = write(1, "    no fault\n", 13); (void)w;
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    printf("    parent: child exit code %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
}

int main(void) {
    PG = sysconf(_SC_PAGESIZE);
    printf("page size %ld, SIGBUS=%d SIGSEGV=%d, BUS_ADRALN=%d BUS_ADRERR=%d BUS_OBJERR=%d SEGV_MAPERR=%d SEGV_ACCERR=%d\n",
           PG, SIGBUS, SIGSEGV, BUS_ADRALN, BUS_ADRERR, BUS_OBJERR, SEGV_MAPERR, SEGV_ACCERR);
    char path[] = "/tmp/OS-B09_demo_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    if (ftruncate(fd, 100) != 0) { perror("ftruncate"); return 1; }    /* 100 바이트 파일 */

    char *m = mmap(NULL, 2 * (size_t)PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    run("1) 100-byte file mapped as 2 pages: read base+200 (same page as EOF)", m, 200, 0);
    run("2) read base+PG+10 (whole page beyond EOF)", m, (size_t)PG + 10, 0);
    if (ftruncate(fd, 0) != 0) return 1;
    run("3) ftruncate(fd, 0) while mapped, then read base+0", m, 0, 0);
    munmap(m, 2 * (size_t)PG);

    char *ro = mmap(NULL, (size_t)PG, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
    run("4) write to a PROT_READ anonymous page", ro, 0, 1);
    munmap(ro, (size_t)PG);
    run("5) read after munmap", ro, 0, 0);

    close(fd);
    unlink(path);
    return 0;
}
