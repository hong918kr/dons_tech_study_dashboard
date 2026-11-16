/* ENV: linux */
/* OS-A02 디버거는 어떻게 동작하나: ptrace로 미니 디버거   (aarch64 Linux 전용)
 *
 * 디버거 = "자식을 멈춰 두고, 그 메모리와 레지스터를 대신 읽고 쓰는 부모 프로세스".
 * 이 파일 하나가 디버거이자 디버기(debuggee)다: fork 한 자식이 PTRACE_TRACEME → raise(SIGSTOP) 로
 * 멈추고, 그 뒤 같은 바이너리 안의 타깃 함수를 부른다. fork 는 주소 공간 배치를 그대로 복사하므로
 * 부모가 아는 &target_add 가 자식에서도 같은 주소다 (ASLR 은 exec 때만 다시 굴린다).
 *
 *   Part A  PEEK/POKE (8 바이트 워드) · 4 바이트 BRK #0 을 워드 안에 read-modify-write 로 심기/지우기
 *           · NT_PRSTATUS 레지스터 · waitpid 상태 + PTRACE_GETSIGINFO si_code 해석
 *           · continue (BRK 위에 서 있으면 원래 명령으로 한 스텝 넘기고 다시 심기) · 히트 카운터
 *   Part B  싱글스텝: 함수 입구부터 복귀 주소까지 명령 수 세기 (asm 함수 = 정확, C 함수 = 범위)
 *   Part C  스크립트형 디버깅: 조건부 BP · finish 후 반환값(x0) 바꿔 끼우기(fault injection)
 *           · 시그널 통과 · detach(BP 제거 후 놓아주기) · kill 정리
 *
 * CHECK 는 매번 성립하는 것만 본다 (주소·명령 워드·히트 수·종료 코드). 시간은 재지 않는다.
 * 모든 waitpid 는 alarm() 감시(최대 2 s)를 받고, 자식은 PTRACE_O_EXITKILL + 테스트 끝 kill 로 반드시 죽는다.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define _GNU_SOURCE /* -std=c11 에서 ptrace 요청 상수·siginfo 필드·TRAP_* 가 보이게 */
#include <elf.h>    /* NT_PRSTATUS */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
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
static volatile sig_atomic_t g_usr1;

TARGET int target_add(int a, int b) { __asm__ volatile(""); return a + b; }
TARGET int c_poly(int x) { return x * x * 3 + x * 5 - 7; }
TARGET int sensor_read(int ch) { __asm__ volatile(""); return ch * 10 + 1; } /* 정상값 ch*10+1 */

/* volatile 함수 포인터로 부른다: 상수 전파 복제본(target_add.constprop.0)이나 인라인이 생기면
 * &target_add 에 심은 BP 를 아무도 안 지나간다. */
static int (*volatile p_add)(int, int) = target_add;
static int (*volatile p_poly)(int) = c_poly;
static int (*volatile p_sensor)(int) = sensor_read;

enum { N_LOOP = 10, N_CH = 16, BAD_CH = 5 };

static int sc_magic(int arg) { (void)arg; return (int)(g_magic & 0x7f); }
static int sc_loop(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) { g_counter = i + 1; s += p_add(i, 7); }
    return s & 0xff; /* N_LOOP=10 → 45 + 70 = 115 */
}
static int sc_asm(int arg) {
    (void)arg;
    int a = asm_leaf4(5) + asm_leaf4(5);              /* 9 + 9 */
    int b = asm_loop(1) + asm_loop(5) + asm_loop(32); /* 1 + 5 + 32 */
    int c = p_poly(5);                                /* 93 */
    return (a + b + c) & 0xff;                        /* 149 */
}
static int sc_sensor(int n) {
    int err = 0;
    long sum = 0;
    for (int ch = 0; ch < n; ch++) {
        int v = p_sensor(ch);
        if (v < 0) err++; else sum += v;
    }
    return err * 10 + (int)(sum % 10); /* 정상: 0*10 + 1216%10 = 6, ch5 실패 주입: 10 + 1165%10 = 15 */
}
static void on_usr1(int s) { (void)s; g_usr1 = 1; }
static int sc_signal(int arg) {
    (void)arg;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);
    raise(SIGUSR1);
    return g_usr1 ? 42 : 1; /* 디버거가 시그널을 삼키면 핸들러가 안 돈다 → 1 */
}

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

static void dump_regs(const struct user_pt_regs *r) {
    for (int i = 0; i < 31; i++)
        printf("  x%-2d %016llx%s", i, (unsigned long long)r->regs[i], i % 4 == 3 ? "\n" : "");
    printf("\n  sp  %016llx  pc  %016llx  pstate %08llx (NZCV=%llx, M[4:0]=%llx)\n",
           (unsigned long long)r->sp, (unsigned long long)r->pc, (unsigned long long)r->pstate,
           (unsigned long long)(r->pstate >> 28), (unsigned long long)(r->pstate & 0x1f));
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

/* ======================= Part B — 싱글스텝 명령 수 ======================= */

/* B1. 함수 입구 BRK 에서 멈춘 상태로 불린다. 입구의 x30(LR) = 복귀 주소. PC 가 거기 닿을 때까지 스텝 수.
 * (재귀 함수면 sp 도 비교해야 한다 — 7. 자주 하는 실수) 실패 -1. */
static long count_steps(dbg_t *d) {
    struct user_pt_regs r;
    if (get_regs(d->pid, &r) != 0) return -1;
    uint64_t ret = r.regs[30];
    for (long n = 1; n <= MAX_STEPS; n++) {
        ev_t e = dbg_step(d);
        if (e.kind != EVK_STEP) return -1;
        if (e.pc == ret) return n;
    }
    return -1;
}

/* ======================= Part C — 스크립트형 디버깅 ======================= */

/* C1. finish: 입구 BP 에서 멈춘 상태 → 복귀 주소(LR)에 임시 BP → 계속 → 거기서 멈추면 임시 BP 제거.
 * sp 가 입구 때와 같을 때만 "이 호출의 복귀" (재귀 대비). out = 복귀 직후 레지스터 (x0 = 반환값). 실패 -1. */
static int dbg_finish(dbg_t *d, struct user_pt_regs *out) {
    struct user_pt_regs r;
    if (get_regs(d->pid, &r) != 0) return -1;
    uintptr_t ret = (uintptr_t)r.regs[30];
    int temp = bp_find(d, ret) == NULL;
    if (temp && bp_set(d, ret) != 0) return -1;
    ev_t e;
    for (;;) {
        e = dbg_cont(d);
        if (e.kind != EVK_BREAK || e.pc != ret) break;
        if (get_regs(d->pid, out) != 0) return -1;
        if (out->sp == r.sp) break; /* 이 호출의 복귀 */
    }
    if (temp && d->pid > 0) bp_clear(d, ret);
    return (e.kind == EVK_BREAK && e.pc == ret) ? 0 : -1;
}

/* C2. 조건부 BP: BP 에 걸릴 때마다 *hits++ 하고 pred(레지스터)가 참이면 거기서 멈춘 채 돌려준다.
 * 거짓이면 사용자에게 보이지 않게 계속. pred == NULL 이면 끝까지 (종료·사망·시그널 이벤트에서 반환). */
typedef int (*pred_fn)(const struct user_pt_regs *r);
static ev_t run_cond(dbg_t *d, pred_fn pred, int *hits) {
    for (;;) {
        ev_t e = dbg_cont(d);
        if (e.kind != EVK_BREAK || !bp_find(d, e.pc)) return e;
        (*hits)++;
        struct user_pt_regs r;
        if (pred && get_regs(d->pid, &r) == 0 && pred(&r)) return e;
    }
}

/* C3. detach: 모든 BP 를 지우고(원래 명령 복원) PTRACE_DETACH, 그다음 자식을 회수(감시 붙여서).
 * 반환 = wait 상태 (정상이면 WIFEXITED), 실패 -1. BP 위에서 멈춰 있어도 PC 는 원래 명령을 가리키므로 그대로 놓아준다. */
static int dbg_detach(dbg_t *d) {
    for (int i = 0; i < MAX_BP; i++)
        if (d->bp[i].on && bp_clear(d, d->bp[i].addr) != 0) return -1;
    pid_t pid = d->pid;
    if (ptrace(PTRACE_DETACH, pid, NULL, (void *)(long)d->pending_sig) != 0) return -1;
    d->pid = 0;
    int st;
    if (wait_raw(pid, &st, 2) != 0) return -1;
    return st;
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static int pred_ch5(const struct user_pt_regs *r) { return r->regs[0] == BAD_CH; }

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm; /* SA_RESTART 없음 → waitpid 가 EINTR 로 깨어난다 */
    sigaction(SIGALRM, &sa, NULL);

    const uintptr_t LEAF = (uintptr_t)asm_leaf4, LOOP = (uintptr_t)asm_loop, POLY = (uintptr_t)c_poly;
    const uintptr_t ADD = (uintptr_t)target_add, SENSOR = (uintptr_t)sensor_read;
    dbg_t d;
    ev_t e;
    struct user_pt_regs r;
    uint64_t w = 0;
    uint32_t ins = 0;

    printf("== Part A: 메모리 · 명령 워드 · BP 심기/지우기 ==\n");
    CHECK(dbg_spawn(&d, sc_magic, 0) == 0);
    CHECK(peek_word(d.pid, (uintptr_t)&g_magic, &w) == 0 && w == 0x1122334455667788ull);
    CHECK(peek_word(d.pid, 8, &w) == -EIO);                         /* 매핑 안 된 주소 */
    CHECK(poke_word(d.pid, (uintptr_t)&g_magic, 0x2a) == 0);         /* 자식의 g_magic 만 바뀐다 */
    CHECK(g_magic == 0x1122334455667788ull);                         /* 부모(디버거) 것은 그대로 */
    uint32_t leaf0 = 0, leaf1 = 0;
    memcpy(&leaf0, (const void *)LEAF, 4);                           /* 부모 자신의 코드 = 자식의 원래 코드 */
    memcpy(&leaf1, (const void *)(LEAF + 4), 4);
    CHECK(read_insn(d.pid, LEAF, &ins) == 0 && ins == leaf0);
    CHECK(read_insn(d.pid, LEAF + 12, &ins) == 0 && ins == 0xd65f03c0u); /* ret */
    CHECK(read_insn(d.pid, LEAF + 2, &ins) == -EINVAL);
    CHECK(bp_set(&d, LEAF) == 0 && bp_set(&d, LEAF + 4) == 0);       /* 같은 8 바이트 워드의 두 절반 */
    CHECK(peek_word(d.pid, LEAF, &w) == 0 && w == ((uint64_t)BRK0 << 32 | BRK0));
    CHECK(bp_clear(&d, LEAF) == 0 && read_insn(d.pid, LEAF, &ins) == 0 && ins == leaf0);
    CHECK(read_insn(d.pid, LEAF + 4, &ins) == 0 && ins == BRK0);     /* 이웃 BP 는 살아 있다 */
    CHECK(bp_clear(&d, LEAF + 4) == 0 && peek_word(d.pid, LEAF, &w) == 0 &&
          w == ((uint64_t)leaf1 << 32 | leaf0));
    CHECK(bp_clear(&d, LEAF) == -ENOENT);
    memcpy(&ins, (const void *)LEAF, 4);
    CHECK(ins == leaf0);                                             /* 부모 코드는 안 바뀜: COW */
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 0x2a);                      /* 자식은 바뀐 g_magic 을 봤다 */
    dbg_kill(&d);

    printf("== Part A: BP 히트 · PC · 레지스터 · 변수 읽기 · 히트 카운터 ==\n");
    CHECK(dbg_spawn(&d, sc_loop, N_LOOP) == 0);
    CHECK(bp_set(&d, ADD) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.si_code == TRAP_BRKPT);
    CHECK(e.pc == ADD && e.faddr == ADD);                          /* arm64: PC 는 BRK 주소 그대로 */
    CHECK(get_regs(d.pid, &r) == 0 && r.regs[0] == 0 && r.regs[1] == 7);
    CHECK((r.pstate & 0x1f) == 0 && r.sp % 16 == 0 && r.regs[30] != 0); /* EL0t, AArch64, sp 정렬 */
    CHECK(peek_word(d.pid, (uintptr_t)&g_counter, &w) == 0 && w == 1);
    if (g_pass == g_total) dump_regs(&r);
    int hits = 0, args_ok = 1;
    while (e.kind == EVK_BREAK && hits < 100) {
        hits++;
        if (get_regs(d.pid, &r) != 0 || r.regs[0] != (uint64_t)(hits - 1) ||
            peek_word(d.pid, (uintptr_t)&g_counter, &w) != 0 || w != (uint64_t)hits)
            args_ok = 0;
        e = dbg_cont(&d);
    }
    printf("  hits=%d last event kind=%d code=%d\n", hits, (int)e.kind, e.code);
    CHECK(hits == N_LOOP && args_ok);                                /* 매 히트: x0 = i, g_counter = i+1 */
    CHECK(e.kind == EVK_EXIT && e.code == 115);                       /* BP 가 프로그램 결과를 바꾸지 않았다 */
    dbg_kill(&d);

    printf("== Part B: 싱글스텝 ==\n");
    CHECK(dbg_spawn(&d, sc_asm, 0) == 0);
    CHECK(bp_set(&d, LEAF) == 0 && bp_set(&d, LOOP) == 0 && bp_set(&d, POLY) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.pc == LEAF);
    e = dbg_step(&d);
    CHECK(e.kind == EVK_STEP && e.si_code == TRAP_TRACE && e.pc == LEAF + 4);
    CHECK(get_regs(d.pid, &r) == 0 && r.regs[0] == 6);               /* add w0,w0,#1 이 실제로 실행됨 */
    CHECK(read_insn(d.pid, LEAF, &ins) == 0 && ins == BRK0);         /* 스텝 뒤 BP 재삽입 */
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_BREAK && e.pc == LEAF);                       /* 두 번째 asm_leaf4 호출 */
    CHECK(count_steps(&d) == 4);
    long loops[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        e = dbg_cont(&d);
        loops[i] = (e.kind == EVK_BREAK && e.pc == LOOP) ? count_steps(&d) : -1;
    }
    printf("  asm_loop steps n=1,5,32 → %ld %ld %ld\n", loops[0], loops[1], loops[2]);
    CHECK(loops[0] == 4 && loops[1] == 12 && loops[2] == 66);        /* 1 + 2n + 1 */
    e = dbg_cont(&d);
    long poly = (e.kind == EVK_BREAK && e.pc == POLY) ? count_steps(&d) : -1;
    printf("  c_poly steps = %ld (컴파일러마다 다름: 범위로 검사)\n", poly);
    CHECK(poly >= 3 && poly <= 200);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 149);
    dbg_kill(&d);

    printf("== Part C: 조건부 BP + 반환값 주입 ==\n");
    CHECK(dbg_spawn(&d, sc_sensor, N_CH) == 0 && bp_set(&d, SENSOR) == 0);
    hits = 0;
    e = run_cond(&d, pred_ch5, &hits);
    CHECK(e.kind == EVK_BREAK && e.pc == SENSOR && hits == BAD_CH + 1);
    CHECK(dbg_finish(&d, &r) == 0 && r.regs[0] == BAD_CH * 10 + 1);  /* 원래 반환값 51 */
    r.regs[0] = (uint64_t)(int64_t)-EIO;                             /* -5 로 바꿔 끼운다 */
    CHECK(set_regs(d.pid, &r) == 0);
    e = run_cond(&d, NULL, &hits);
    CHECK(hits == N_CH && e.kind == EVK_EXIT && e.code == 15);        /* 오류 1 건이 처리됐다 */
    dbg_kill(&d);

    printf("== Part C: 시그널 통과 · detach · kill ==\n");
    CHECK(dbg_spawn(&d, sc_signal, 0) == 0);
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_SIGNAL && e.code == SIGUSR1);                 /* signal-delivery-stop */
    e = dbg_cont(&d);
    CHECK(e.kind == EVK_EXIT && e.code == 42);                        /* 다시 넘겨줘서 핸들러가 돌았다 */
    dbg_kill(&d);

    CHECK(dbg_spawn(&d, sc_sensor, N_CH) == 0 && bp_set(&d, SENSOR) == 0);
    e = dbg_cont(&d);
    int st = dbg_detach(&d);
    CHECK(e.kind == EVK_BREAK && st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 6);
    dbg_kill(&d);

    /* (주어진 대조군) BP 를 남긴 채 놓아주면: 다음 BRK 를 아무도 안 받는다 → SIGTRAP 으로 죽는다 */
    CHECK(dbg_spawn(&d, sc_sensor, N_CH) == 0 && bp_set(&d, SENSOR) == 0);
    e = dbg_cont(&d);
    pid_t pid = d.pid;
    st = 0;
    if (e.kind == EVK_BREAK && ptrace(PTRACE_DETACH, pid, NULL, NULL) == 0) {
        d.pid = 0;
        if (wait_raw(pid, &st, 2) != 0) st = 0;
    }
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTRAP);
    dbg_kill(&d);

    CHECK(dbg_spawn(&d, sc_loop, N_LOOP) == 0 && bp_set(&d, ADD) == 0);
    e = dbg_cont(&d);
    pid = d.pid;
    st = dbg_kill(&d);
    CHECK(e.kind == EVK_BREAK && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    CHECK(waitpid(pid, &st, WNOHANG) == -1 && errno == ECHILD);      /* 좀비 없음 */

    printf("timeouts=%d\n%d/%d passed\n", g_timeouts, g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
