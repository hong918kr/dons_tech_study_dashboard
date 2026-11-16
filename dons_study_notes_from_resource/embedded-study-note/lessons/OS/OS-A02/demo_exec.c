/* ENV: linux */
/* OS-A02 demo — execve 한 별도 프로그램 디버깅: PIE 로드 베이스 + ELF 심볼표 + BRK
 *
 *   ./dexec ./target checksum_packet
 *   1) fork → 자식: PTRACE_TRACEME → execv(target). exec 가 성공하면 커널이 SIGTRAP 으로 세운다.
 *      이 시점 PC 는 main 이 아니라 동적 로더(ld-linux)의 진입점이다 → /proc/<pid>/maps 로 확인.
 *   2) PTRACE_O_EXITKILL. 실행 파일의 로드 베이스 = maps 에서 경로가 같고 offset 0 인 첫 줄의 시작 주소.
 *   3) 대상 파일의 .symtab 에서 이름 → st_value (PIE 는 0 기준 상대 주소) → base + st_value 에 BRK #0.
 *      교차 확인: /proc/<pid>/auxv 의 AT_ENTRY == base + e_entry.
 *   4) 히트마다 x0(char *) 문자열을 PEEKDATA 로 읽고 x1 출력 → step-over → 계속 → 종료 코드.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_exec.c -o dexec
 */
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <asm/ptrace.h>

#define BRK0 0xd4200000u

static void on_alarm(int s) { (void)s; }
static int wait_stop(pid_t p, int *st) {
    alarm(3);
    pid_t r = waitpid(p, st, 0);
    alarm(0);
    if (r != p) { kill(p, SIGKILL); waitpid(p, st, 0); return -1; }
    return 0;
}
static int regs(pid_t p, struct user_pt_regs *r) {
    struct iovec iov = { r, sizeof *r };
    return (int)ptrace(PTRACE_GETREGSET, p, (void *)(long)NT_PRSTATUS, &iov);
}
static int insn_rw(pid_t p, uintptr_t a, uint32_t *old, uint32_t neu) { /* 워드 RMW: old 로 읽고 neu 를 쓴다 */
    errno = 0;
    long w = ptrace(PTRACE_PEEKTEXT, p, (void *)(a & ~7UL), NULL);
    if (w == -1 && errno) return -1;
    if (old) memcpy(old, (char *)&w + (a & 7), 4);
    memcpy((char *)&w + (a & 7), &neu, 4);
    return (int)ptrace(PTRACE_POKETEXT, p, (void *)(a & ~7UL), (void *)w);
}
static void read_str(pid_t p, uintptr_t a, char *buf, size_t n) {
    size_t i = 0;
    while (i + 1 < n) {
        errno = 0;
        long w = ptrace(PTRACE_PEEKDATA, p, (void *)(a + i), NULL);
        if (w == -1 && errno) break;
        for (int k = 0; k < 8 && i + 1 < n; k++, i++) {
            buf[i] = ((char *)&w)[k];
            if (!buf[i]) return;
        }
    }
    buf[i] = 0;
}
/* ELF64 파일에서 심볼 값 (먼저 .symtab, 없으면 .dynsym). e_entry 도 돌려준다. */
static int elf_lookup(const char *path, const char *name, uint64_t *value, uint64_t *entry, int *etype) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat sb;
    fstat(fd, &sb);
    unsigned char *m = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return -1;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)m;
    int rc = -1;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64) {
        *entry = eh->e_entry;
        *etype = eh->e_type;
        const Elf64_Shdr *sh = (const Elf64_Shdr *)(m + eh->e_shoff);
        for (int pass = 0; pass < 2 && rc; pass++) {
            for (int i = 0; i < eh->e_shnum && rc; i++) {
                if (sh[i].sh_type != (pass == 0 ? SHT_SYMTAB : SHT_DYNSYM)) continue;
                const Elf64_Sym *sym = (const Elf64_Sym *)(m + sh[i].sh_offset);
                const char *str = (const char *)(m + sh[sh[i].sh_link].sh_offset);
                for (size_t k = 0; k < sh[i].sh_size / sizeof *sym; k++)
                    if (ELF64_ST_TYPE(sym[k].st_info) == STT_FUNC && strcmp(str + sym[k].st_name, name) == 0) {
                        *value = sym[k].st_value;
                        rc = 0;
                        break;
                    }
            }
        }
    }
    munmap(m, (size_t)sb.st_size);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <program> <function>\n", argv[0]); return 2; }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);

    pid_t p = fork();
    if (p == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execv(argv[1], &argv[1]);
        _exit(127);
    }
    int st;
    if (wait_stop(p, &st) || !WIFSTOPPED(st)) { printf("no exec stop\n"); return 1; }
    ptrace(PTRACE_SETOPTIONS, p, NULL, (void *)(long)PTRACE_O_EXITKILL);
    struct user_pt_regs r;
    regs(p, &r);
    printf("exec stop: sig=%d pc=%#llx\n", WSTOPSIG(st), (unsigned long long)r.pc);

    char path[64], exe[512], line[768];
    snprintf(path, sizeof path, "/proc/%d/exe", (int)p);
    ssize_t n = readlink(path, exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
    snprintf(path, sizeof path, "/proc/%d/maps", (int)p);
    FILE *f = fopen(path, "r");
    uint64_t base = 0;
    while (f && fgets(line, sizeof line, f)) {
        unsigned long lo, hi, off;
        char perm[8], file[512] = "";
        if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %511s", &lo, &hi, perm, &off, file) < 4) continue;
        int has_pc = r.pc >= lo && r.pc < hi;
        if (!base && off == 0 && strcmp(file, exe) == 0) base = lo;
        if (strcmp(file, exe) == 0 || has_pc) printf("  maps: %s%s", line, has_pc ? "        ^ PC is here (dynamic loader)\n" : "");
    }
    if (f) fclose(f);

    uint64_t sym = 0, entry = 0, at_entry = 0;
    int etype = 0;
    if (elf_lookup(argv[1], argv[2], &sym, &entry, &etype) != 0) { printf("symbol %s not found\n", argv[2]); kill(p, SIGKILL); waitpid(p, NULL, 0); return 1; }
    snprintf(path, sizeof path, "/proc/%d/auxv", (int)p);
    f = fopen(path, "r");
    Elf64_auxv_t av;
    while (f && fread(&av, sizeof av, 1, f) == 1 && av.a_type != AT_NULL)
        if (av.a_type == AT_ENTRY) at_entry = av.a_un.a_val;
    if (f) fclose(f);
    uintptr_t bp = (uintptr_t)(base + sym);
    printf("e_type=%s base=%#" PRIx64 " %s st_value=%#" PRIx64 " → bp=%#lx\n", etype == ET_DYN ? "ET_DYN(PIE)" : "ET_EXEC",
           base, argv[2], sym, (unsigned long)bp);
    printf("AT_ENTRY=%#" PRIx64 " == base + e_entry(%#" PRIx64 ") ? %s\n", at_entry, entry, at_entry == base + entry ? "yes" : "no");

    uint32_t orig;
    if (insn_rw(p, bp, &orig, BRK0) != 0) { printf("poke failed: %s\n", strerror(errno)); return 1; }
    int hits = 0, reaped = 0;
    for (;;) {
        ptrace(PTRACE_CONT, p, NULL, NULL);
        if (wait_stop(p, &st)) { printf("timeout\n"); return 1; } /* wait_stop 이 죽이고 회수했다 */
        if (WIFEXITED(st) || WIFSIGNALED(st)) reaped = 1;
        if (WIFEXITED(st)) { printf("exited code=%d after %d hits\n", WEXITSTATUS(st), hits); break; }
        if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGTRAP) { printf("stop sig %d\n", WSTOPSIG(st)); break; }
        regs(p, &r);
        if (r.pc != bp) { printf("unexpected trap at %#llx\n", (unsigned long long)r.pc); break; }
        char s[32];
        read_str(p, (uintptr_t)r.regs[0], s, sizeof s);
        printf("hit %d: %s(name=\"%s\", len=%llu)  lr=%#llx\n", ++hits, argv[2], s, (unsigned long long)r.regs[1],
               (unsigned long long)r.regs[30]);
        insn_rw(p, bp, NULL, orig); /* step-over */
        ptrace(PTRACE_SINGLESTEP, p, NULL, NULL);
        if (wait_stop(p, &st)) return 1;
        if (!WIFSTOPPED(st)) { reaped = 1; break; }
        insn_rw(p, bp, NULL, BRK0);
    }
    if (!reaped) { kill(p, SIGKILL); waitpid(p, NULL, 0); } /* 회수된 pid 에는 kill 하지 않는다 (재사용 위험) */
    return 0;
}
