/* ENV: linux */
/* OS-A02 디버거는 어떻게 동작하나: ptrace로 미니 디버거 — 추가 연습 (aarch64 Linux 전용)
 *
 * solution.c 의 Part A 디버거 코어(PEEK/POKE, BP, 레지스터, 이벤트, step/cont)를 그대로 담고 위에 네 가지를 얹는다:
 *   연습 1  소프트웨어 워치포인트: 한 스텝마다 변수를 읽어 "누가 썼나" (쓴 명령 PC, 스텝 수)
 *   연습 2  프로그램 자신의 BRK (#0x123): 내 BP 가 아니면 imm16 을 읽고 PC += 4 로 넘긴다
 *   연습 3  미니 strace: PTRACE_SYSCALL + TRACESYSGOOD + PTRACE_GET_SYSCALL_INFO 로 write 횟수·반환값
 *   연습 4  강제 함수 호출 (gdb 의 call): 레지스터 저장 → pc/x0/x30/sp 바꿔 실행 → 반환값 → 원상복구
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _GNU_SOURCE /* -std=c11 에서 ptrace 요청 상수·siginfo 필드·TRAP_* 가 보이게 */
#include <elf.h>    /* NT_PRSTATUS */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <asm/ptrace.h> /* struct user_pt_regs { regs[31], sp, pc, pstate } */

#if !defined(__aarch64__)
#error "this lesson targets aarch64 Linux (BRK #0, NT_PRSTATUS = struct user_pt_regs)"
#endif

/* ======================= 디버기 쪽: 같은 바이너리 안의 타깃 ======================= */
#define BRK0 0xd4200000u /* BRK #0 (imm16 = 0). x86 의 int3(0xCC, 1 바이트)에 해당, arm64 는 4 바이트 고정 */

/* 명령 수를 컴파일러에 맡기지 않으려고 asm 으로 쓴 두 함수. 8 바이트 정렬 → 입구는 워드의 아래 절반. */
__asm__(".text\n"
        ".p2align 3\n"
        ".globl asm_leaf4\n.type asm_leaf4, %function\n"
        "asm_leaf4:\n"         /* (x + 1) * 2 - 3 : 정확히 4 명령 */
        "  add w0, w0, #1\n"
        "  add w0, w0, w0\n"
        "  sub w0, w0, #3\n"
        "  ret\n"
        ".size asm_leaf4, .-asm_leaf4\n"
        ".p2align 3\n"
        ".globl asm_loop\n.type asm_loop, %function\n"
        "asm_loop:\n"          /* n >= 1 번 도는 빈 루프, w0 = n 그대로: 1 + 2n + 1 명령 */
        "  mov w1, w0\n"
        "1: subs w1, w1, #1\n"
        "  b.ne 1b\n"
        "  ret\n"
        ".size asm_loop, .-asm_loop\n");
int asm_leaf4(int x);
int asm_loop(int n);

#define TARGET __attribute__((noinline, used))
volatile long g_counter;                            /* 디버거가 PEEKDATA 로 읽는 변수 */
volatile uint64_t g_magic = 0x1122334455667788ull;  /* 디버거가 POKEDATA 로 바꾸는 변수 */

TARGET int target_add(int a, int b) { __asm__ volatile(""); return a + b; }
TARGET int c_poly(int x) { return x * x * 3 + x * 5 - 7; }
TARGET int sensor_read(int ch) { __asm__ volatile(""); return ch * 10 + 1; } /* 정상값 ch*10+1 */

/* volatile 함수 포인터로 부른다: 상수 전파 복제본(target_add.constprop.0)이나 인라인이 생기면
 * &target_add 에 심은 BP 를 아무도 안 지나간다. */
static int (*volatile p_add)(int, int) = target_add;
static int (*volatile p_poly)(int) = c_poly;

/* ======================= 디버거 쪽: 주어진 뼈대 ======================= */
#define MAX_BP 8
#define MAX_STEPS 100000L
typedef struct { uintptr_t addr; uint32_t orig; int on; } bp_t;
typedef struct { pid_t pid; int pending_sig; bp_t bp[MAX_BP]; } dbg_t;

typedef enum { EVK_NONE, EVK_BREAK, EVK_STEP, EVK_SIGNAL, EVK_EXIT, EVK_KILLED, EVK_TIMEOUT, EVK_OTHER } evk_t;
typedef struct { evk_t kind; int code; int si_code; uintptr_t pc, faddr; } ev_t;
/* kind EVK_EXIT: code = 종료 코드 · EVK_KILLED / EVK_SIGNAL: code = 시그널 · EVK_BREAK/EVK_STEP: SIGTRAP + si_code */

static volatile sig_atomic_t g_alarm;
static int g_timeouts;
static void on_alarm(int s) { (void)s; g_alarm = 1; }

/* 감시 붙은 waitpid: secs 안에 상태 변화가 없으면 자식을 죽이고 -1. (SIGALRM 은 SA_RESTART 없이 설치) */
static int wait_raw(pid_t pid, int *st, unsigned secs) {
    alarm(secs);
    pid_t r = waitpid(pid, st, 0);
    int e = errno;
    alarm(0);
    if (r == pid) return 0;
    if (r < 0 && e == EINTR) {
        g_timeouts++;
        kill(pid, SIGKILL);
        waitpid(pid, st, 0);
    }
    return -1;
}

static int dbg_kill(dbg_t *d) { /* 정리: 멈춰 있든 돌고 있든 SIGKILL + 회수. 반환 = wait 상태 */
    int st = 0;
    if (d->pid > 0) {
        kill(d->pid, SIGKILL);
        waitpid(d->pid, &st, 0);
        d->pid = 0;
    }
    return st;
}

static int dbg_spawn(dbg_t *d, int (*fn)(int), int arg) {
    memset(d, 0, sizeof *d);
    fflush(stdout); /* 자식이 부모 버퍼를 두 번 찍지 않게 */
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* 일부러 SIGTRAP 으로 죽는 테스트가 core 를 안 만들게. 0 이 아니라 1: core_pattern 이 파이프(apport)면
         * 커널은 RLIMIT_CORE 를 무시하고 핸들러를 띄우는데(수 초), 1 은 "재귀 방지" 특수값이라 즉시 포기한다
         * (fs/coredump.c:978–994, dmesg 에 경고 한 줄). 파일 core 도 1 < 페이지 크기라 안 쓴다 (:884). */
        struct rlimit no_core = { 1, 1 };
        setrlimit(RLIMIT_CORE, &no_core);
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) _exit(126);
        raise(SIGSTOP); /* 여기서 부모가 BP 를 심을 시간을 얻는다 */
        _exit(fn(arg) & 0xff);
    }
    d->pid = pid;
    int st;
    if (wait_raw(pid, &st, 2) != 0) { d->pid = 0; return -1; }
    if (!WIFSTOPPED(st) || WSTOPSIG(st) != SIGSTOP) { dbg_kill(d); return -1; }
    /* 디버거가 죽으면 디버기도 죽인다 (kernel/ptrace.c exit_ptrace: PT_EXITKILL → SIGKILL) */
    if (ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)(long)PTRACE_O_EXITKILL) != 0) { dbg_kill(d); return -1; }
    return 0; /* 자식은 SIGSTOP 정지 상태. 다음 cont 는 시그널 0 으로 → SIGSTOP 은 삼킨다 */
}

__attribute__((unused)) /* 시작 코드에서는 아직 안 쓰인다 */
static bp_t *bp_find(dbg_t *d, uintptr_t addr) {
    for (int i = 0; i < MAX_BP; i++)
        if (d->bp[i].on && d->bp[i].addr == addr) return &d->bp[i];
    return NULL;
}

/* ======================= Part A — 메모리 · 명령 · BP · 레지스터 · 이벤트 ======================= */

/* A1. PEEKDATA 는 "읽은 워드" 를 반환값으로 준다 → -1 이 정상 데이터일 수 있다. errno 로 구분한다.
 * 반환 0 성공, -errno 실패 (매핑 안 된 주소 → -EIO). */
static int peek_word(pid_t pid, uintptr_t addr, uint64_t *out) {
    errno = 0;
    long w = ptrace(PTRACE_PEEKDATA, pid, (void *)addr, NULL);
    if (w == -1 && errno != 0) return -errno;
    *out = (uint64_t)w;
    return 0;
}
static int poke_word(pid_t pid, uintptr_t addr, uint64_t w) {
    return ptrace(PTRACE_POKEDATA, pid, (void *)addr, (void *)w) == 0 ? 0 : -errno;
}

/* A2. 명령은 4 바이트, PEEK/POKE 는 8 바이트 워드. 정렬된 워드를 읽고 → 해당 4 바이트만 바꾸고 → 다시 쓴다.
 * 리틀엔디언: addr % 8 == 4 이면 워드의 위쪽 절반. addr % 4 != 0 → -EINVAL. */
static int read_insn(pid_t pid, uintptr_t addr, uint32_t *out) {
    if (addr & 3) return -EINVAL;
    uint64_t w;
    int r = peek_word(pid, addr & ~(uintptr_t)7, &w);
    if (r) return r;
    memcpy(out, (unsigned char *)&w + (addr & 7), 4);
    return 0;
}
__attribute__((unused))
static int write_insn(pid_t pid, uintptr_t addr, uint32_t insn) {
    if (addr & 3) return -EINVAL;
    uintptr_t base = addr & ~(uintptr_t)7;
    uint64_t w;
    int r = peek_word(pid, base, &w); /* 지금 워드를 읽는다: 옆 절반에 다른 BP 가 있을 수 있다 */
    if (r) return r;
    memcpy((unsigned char *)&w + (addr & 7), &insn, 4);
    return poke_word(pid, base, w);
}

/* A3. BP 심기: 원래 명령 4 바이트를 저장하고 BRK #0 을 쓴다. 이미 있으면 0. 지우기: 원래 4 바이트 복원. */
static int bp_set(dbg_t *d, uintptr_t addr) {
    if (bp_find(d, addr)) return 0;
    bp_t *slot = NULL;
    for (int i = 0; i < MAX_BP && !slot; i++)
        if (!d->bp[i].on) slot = &d->bp[i];
    if (!slot) return -ENOSPC;
    uint32_t orig;
    int r = read_insn(d->pid, addr, &orig);
    if (r) return r;
    if ((r = write_insn(d->pid, addr, BRK0)) != 0) return r;
    slot->addr = addr; slot->orig = orig; slot->on = 1;
    return 0;
}
static int bp_clear(dbg_t *d, uintptr_t addr) {
    bp_t *b = bp_find(d, addr);
    if (!b) return -ENOENT;
    int r = write_insn(d->pid, addr, b->orig); /* 저장한 8 바이트 워드 전체가 아니라 내 4 바이트만 */
    if (r) return r;
    b->on = 0;
    return 0;
}

/* A4. arm64 에는 PTRACE_GETREGS 가 없다 → PTRACE_GETREGSET + NT_PRSTATUS + iovec. */
static int get_regs(pid_t pid, struct user_pt_regs *r) {
    struct iovec iov = { r, sizeof *r };
    if (ptrace(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) != 0) return -errno;
    return iov.iov_len == sizeof *r ? 0 : -EIO; /* 커널이 실제로 채운 길이를 돌려준다 (272) */
}
static int set_regs(pid_t pid, const struct user_pt_regs *r) {
    struct iovec iov = { (void *)r, sizeof *r };
    return ptrace(PTRACE_SETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) == 0 ? 0 : -errno;
}

/* A5. waitpid 상태 해석. 종료/시그널 사망은 그대로, 정지는 시그널 번호로, SIGTRAP 은 si_code 로 나눈다:
 * TRAP_BRKPT(1) = BRK 실행, TRAP_TRACE(2) = 싱글스텝 완료. pc 는 정지 시점 PC (레지스터에서). */
static ev_t classify(pid_t pid, int st) {
    ev_t e = { EVK_OTHER, 0, 0, 0, 0 };
    if (WIFEXITED(st)) { e.kind = EVK_EXIT; e.code = WEXITSTATUS(st); return e; }
    if (WIFSIGNALED(st)) { e.kind = EVK_KILLED; e.code = WTERMSIG(st); return e; }
    if (!WIFSTOPPED(st)) return e;
    struct user_pt_regs r;
    if (get_regs(pid, &r) == 0) e.pc = r.pc;
    e.code = WSTOPSIG(st);
    if (e.code != SIGTRAP) { e.kind = EVK_SIGNAL; return e; }
    siginfo_t si;
    if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &si) != 0) return e;
    e.si_code = si.si_code;
    e.faddr = (uintptr_t)si.si_addr;
    e.kind = si.si_code == TRAP_BRKPT ? EVK_BREAK : si.si_code == TRAP_TRACE ? EVK_STEP : EVK_OTHER;
    return e;
}

/* (주어진 코드) 감시 붙은 대기 + 해석. 회수 여부는 raw 상태로 판단한다 (classify 가 틀려도 pid 재사용 사고 없음). */
static ev_t dbg_wait(dbg_t *d) {
    ev_t e = { EVK_NONE, 0, 0, 0, 0 };
    int st;
    if (d->pid <= 0) return e;
    if (wait_raw(d->pid, &st, 2) != 0) { d->pid = 0; e.kind = EVK_TIMEOUT; return e; }
    e = classify(d->pid, st);
    if (WIFEXITED(st) || WIFSIGNALED(st)) d->pid = 0;
    else if (e.kind == EVK_SIGNAL) d->pending_sig = e.code; /* 다음 재개 때 디버기에게 돌려준다 */
    return e;
}

/* A6. 한 명령 실행. PC 에 우리 BP 가 있으면: 원래 명령 복원 → SINGLESTEP → 다시 BRK.
 * arm64 는 BRK 트랩 후 PC 가 BRK 주소 그대로라서 (x86 int3 처럼 PC-1 로 되감을 필요가 없다) 그냥 복원하면 된다.
 * 보류 중인 시그널(pending_sig)은 이 재개와 함께 전달한다. */
static ev_t dbg_step(dbg_t *d) {
    ev_t e = { EVK_NONE, 0, 0, 0, 0 };
    struct user_pt_regs r;
    if (d->pid <= 0 || get_regs(d->pid, &r) != 0) return e;
    bp_t *b = bp_find(d, r.pc);
    if (b && write_insn(d->pid, b->addr, b->orig) != 0) return e;
    int sig = d->pending_sig;
    d->pending_sig = 0;
    if (ptrace(PTRACE_SINGLESTEP, d->pid, NULL, (void *)(long)sig) != 0) return e;
    e = dbg_wait(d);
    if (b && d->pid > 0) write_insn(d->pid, b->addr, BRK0); /* 다시 심는다 */
    return e;
}

/* A7. 계속 실행. BRK 위에 서 있으면 먼저 dbg_step 으로 원래 명령을 넘긴 뒤 CONT (step-over-breakpoint).
 * 그냥 CONT 하면 같은 BRK 를 다시 실행해서 영원히 같은 자리에서 멈춘다. */
static ev_t dbg_cont(dbg_t *d) {
    ev_t e = { EVK_NONE, 0, 0, 0, 0 };
    struct user_pt_regs r;
    if (d->pid <= 0 || get_regs(d->pid, &r) != 0) return e;
    if (bp_find(d, r.pc)) {
        e = dbg_step(d);
        if (e.kind != EVK_STEP) return e; /* 스텝 중 종료·시그널 → 그대로 보고 */
    }
    int sig = d->pending_sig;
    d->pending_sig = 0;
    if (ptrace(PTRACE_CONT, d->pid, NULL, (void *)(long)sig) != 0) { e.kind = EVK_NONE; return e; }
    return dbg_wait(d);
}

/* ======================= 추가 연습 타깃 ======================= */
__asm__(".text\n"
        ".p2align 3\n"
        ".globl asm_store\n.type asm_store, %function\n"
        "asm_store:\n"         /* *p = v + 1 : 세 번째 명령(str)이 메모리를 바꾼다 */
        "  add w1, w1, #1\n"
        "  nop\n"
        "  str w1, [x0]\n"
        "  ret\n"
        ".size asm_store, .-asm_store\n"
        ".p2align 3\n"
        ".globl call_pad\n.type call_pad, %function\n"
        "call_pad:\n"          /* 디버기는 절대 부르지 않는다: 강제 함수 호출이 돌아올 착지점 */
        "  nop\n"
        "  ret\n"
        ".size call_pad, .-call_pad\n");
void asm_store(volatile int *p, int v);
void call_pad(void);

_Alignas(8) volatile int g_watch;
static int sc_watch(int v) { g_watch = 0; asm_store(&g_watch, v); return g_watch; } /* 41 → 42 */
static int sc_selftrap(int arg) { __asm__ volatile("brk #0x123"); return arg; }    /* 프로그램 자신의 BRK */
static int sc_writes(int n) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) return 98;
    for (int i = 0; i < n; i++)
        if (write(fd, "abc", 3) != 3) return 99;
    close(fd);
    return 0;
}
static int sc_infcall(int arg) { (void)arg; return p_add(1, 2); } /* 3 */

/* ======================= 연습 1 — 소프트웨어 워치포인트 ======================= */
/* 함수 입구에서 멈춘 상태. 한 스텝마다 addr 의 4 바이트를 읽어 바뀌면, 그 스텝 직전 PC(= 쓴 명령)와 스텝 수를 돌려준다.
 * 복귀 주소까지 안 바뀌면 -1. gdb 가 하드웨어 슬롯이 없을 때 쓰는 방식 (느리다: 명령마다 트랩 2 번). */
static int read_u32(pid_t pid, uintptr_t addr, uint32_t *v) {
    uint64_t w;
    int r = peek_word(pid, addr & ~(uintptr_t)7, &w);
    if (r) return r;
    memcpy(v, (unsigned char *)&w + (addr & 7), 4);
    return 0;
}
static int sw_watch(dbg_t *d, uintptr_t addr, uintptr_t *writer_pc, long *steps, uint32_t *newval) {
    struct user_pt_regs r;
    uint32_t old, now;
    if (get_regs(d->pid, &r) != 0 || read_u32(d->pid, addr, &old) != 0) return -1;
    uint64_t ret = r.regs[30];
    for (long n = 1; n <= MAX_STEPS; n++) {
        uintptr_t pc_before = (uintptr_t)r.pc;
        ev_t e = dbg_step(d);
        if (e.kind != EVK_STEP || read_u32(d->pid, addr, &now) != 0) return -1;
        if (now != old) { *writer_pc = pc_before; *steps = n; *newval = now; return 0; }
        if (e.pc == ret) return -1;
        r.pc = e.pc;
    }
    return -1;
}

/* ======================= 연습 2 — 내 BP 가 아닌 BRK 넘기기 ======================= */
/* arm64 는 BRK 후 PC 가 BRK 를 가리킨다 → 프로그램 자신의 BRK 는 PC += 4 를 해 줘야 지나간다.
 * (우리 BP 면 원래 명령을 실행해야 하므로 절대 +4 하면 안 된다.) imm16 을 돌려준다. */
static int skip_foreign_brk(dbg_t *d, ev_t e, unsigned *imm) {
    if (e.kind != EVK_BREAK || bp_find(d, e.pc)) return -1;
    uint32_t ins;
    struct user_pt_regs r;
    if (read_insn(d->pid, e.pc, &ins) != 0 || (ins & 0xffe0001fu) != BRK0) return -1;
    *imm = (ins >> 5) & 0xffff;
    if (get_regs(d->pid, &r) != 0) return -1;
    r.pc += 4;
    return set_regs(d->pid, &r);
}

/* ======================= 연습 3 — 미니 strace (PTRACE_SYSCALL) ======================= */
/* TRACESYSGOOD: 시스템 콜 정지는 WSTOPSIG == (SIGTRAP | 0x80) 로 구분된다. 진입/복귀는 PTRACE_GET_SYSCALL_INFO 의 op.
 * nr 이고 args[2] == len 인 진입 수, 그 복귀값 == len 인 수, exit_group 진입을 봤는지. 반환 = 종료 코드 (-1 실패). */
typedef struct { int entries, exits_ok, saw_exit_group; } sysc_t;
static int trace_syscalls(dbg_t *d, long nr, uint64_t len, sysc_t *out) {
    memset(out, 0, sizeof *out);
    if (ptrace(PTRACE_SETOPTIONS, d->pid, NULL, (void *)(long)(PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD)) != 0)
        return -1;
    int sig = 0, in_target = 0;
    for (int guard = 0; guard < 10000; guard++) {
        int st;
        if (ptrace(PTRACE_SYSCALL, d->pid, NULL, (void *)(long)sig) != 0) return -1;
        sig = 0;
        if (wait_raw(d->pid, &st, 2) != 0) { d->pid = 0; return -1; }
        if (WIFEXITED(st)) { d->pid = 0; return WEXITSTATUS(st); }
        if (WIFSIGNALED(st)) { d->pid = 0; return -1; }
        if (WSTOPSIG(st) != (SIGTRAP | 0x80)) { sig = WSTOPSIG(st) == SIGTRAP ? 0 : WSTOPSIG(st); continue; }
        struct __ptrace_syscall_info si;
        if (ptrace(PTRACE_GET_SYSCALL_INFO, d->pid, (void *)sizeof si, &si) <= 0) return -1;
        if (si.op == PTRACE_SYSCALL_INFO_ENTRY) {
            in_target = (long)si.entry.nr == nr && si.entry.args[2] == len;
            out->entries += in_target;
            if ((long)si.entry.nr == SYS_exit_group) out->saw_exit_group = 1;
        } else if (si.op == PTRACE_SYSCALL_INFO_EXIT) {
            if (in_target && !si.exit.is_error && (uint64_t)si.exit.rval == len) out->exits_ok++;
            in_target = 0;
        }
    }
    return -1;
}

/* ======================= 연습 4 — 강제 함수 호출 (gdb 의 call/print f(x)) ======================= */
/* 멈춘 디버기에서 fn(arg) 를 실행시키고 반환값을 받은 뒤, 레지스터를 통째로 원래대로 되돌린다.
 * x0 = arg, pc = fn, x30 = call_pad (임시 BP), sp 는 아래로 256 바이트 + 16 정렬. */
static int infcall(dbg_t *d, uintptr_t fn, uint64_t arg, uint64_t *ret) {
    struct user_pt_regs saved, r;
    const uintptr_t pad = (uintptr_t)call_pad;
    if (get_regs(d->pid, &saved) != 0) return -1;
    r = saved;
    r.regs[0] = arg;
    r.pc = fn;
    r.regs[30] = pad;
    r.sp = (saved.sp - 256) & ~(uint64_t)15;
    if (bp_set(d, pad) != 0) return -1;
    if (set_regs(d->pid, &r) != 0) return -1;
    ev_t e = dbg_cont(d);
    int ok = e.kind == EVK_BREAK && e.pc == pad && get_regs(d->pid, &r) == 0;
    if (ok) *ret = r.regs[0];
    if (d->pid > 0) bp_clear(d, pad);
    if (!ok || set_regs(d->pid, &saved) != 0) return -1;
    return 0;
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);
    dbg_t d;
    ev_t e;
    struct user_pt_regs r;
    uint32_t ins = 0;

    printf("== 연습 1: 소프트웨어 워치포인트 ==\n");
    const uintptr_t STORE = (uintptr_t)asm_store;
    uintptr_t wpc = 0;
    long nst = 0;
    uint32_t nv = 0;
    CHECK(dbg_spawn(&d, sc_watch, 41) == 0 && bp_set(&d, STORE) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.pc == STORE);
    CHECK(sw_watch(&d, (uintptr_t)&g_watch, &wpc, &nst, &nv) == 0);
    printf("  writer pc = asm_store+%ld, steps = %ld, new value = %u\n", (long)(wpc - STORE), nst, nv);
    CHECK(wpc == STORE + 8 && nst == 3 && nv == 42);                 /* str 명령, 세 번째 스텝 */
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 42);
    dbg_kill(&d);

    printf("== 연습 2: 프로그램 자신의 BRK ==\n");
    unsigned imm = 0;
    CHECK(dbg_spawn(&d, sc_selftrap, 7) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.si_code == TRAP_BRKPT && !bp_find(&d, e.pc));
    CHECK(skip_foreign_brk(&d, e, &imm) == 0 && imm == 0x123);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 7);
    dbg_kill(&d);
    fflush(stdout);
    pid_t pid = fork(); /* 대조군: 디버거 없이 BRK → SIGTRAP 기본 동작 = 종료(+core) */
    if (pid == 0) {
        struct rlimit no_core = { 1, 1 }; /* dbg_spawn 과 같은 이유 (apport 파이프 회피) */
        setrlimit(RLIMIT_CORE, &no_core);
        _exit(sc_selftrap(7));
    }
    int st = 0;
    CHECK(pid > 0 && wait_raw(pid, &st, 3) == 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTRAP);

    printf("== 연습 3: 미니 strace ==\n");
    sysc_t sc;
    CHECK(dbg_spawn(&d, sc_writes, 5) == 0);
    int code = trace_syscalls(&d, SYS_write, 3, &sc);
    printf("  write(.., 3) entries=%d exits_ok=%d exit_group=%d code=%d\n", sc.entries, sc.exits_ok,
           sc.saw_exit_group, code);
    CHECK(code == 0 && sc.entries == 5 && sc.exits_ok == 5 && sc.saw_exit_group);
    dbg_kill(&d);

    printf("== 연습 4: 강제 함수 호출 ==\n");
    const uintptr_t ADD = (uintptr_t)target_add, PAD = (uintptr_t)call_pad;
    uint32_t pad0 = 0;
    memcpy(&pad0, (const void *)PAD, 4);
    uint64_t ret = 0;
    CHECK(dbg_spawn(&d, sc_infcall, 0) == 0 && bp_set(&d, ADD) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.pc == ADD);
    CHECK(infcall(&d, (uintptr_t)c_poly, 6, &ret) == 0 && ret == (uint64_t)p_poly(6));
    printf("  c_poly(6) in debuggee = %llu\n", (unsigned long long)ret);
    CHECK(get_regs(d.pid, &r) == 0 && r.pc == ADD && r.regs[0] == 1 && r.regs[1] == 2); /* 원상복구 */
    CHECK(read_insn(d.pid, PAD, &ins) == 0 && ins == pad0);         /* 임시 BP 제거 */
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 3);
    dbg_kill(&d);

    printf("timeouts=%d\n%d/%d passed\n", g_timeouts, g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
