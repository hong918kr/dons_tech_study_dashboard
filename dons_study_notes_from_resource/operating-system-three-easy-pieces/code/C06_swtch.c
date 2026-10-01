// C06_swtch.c — xv6 swtch()(OSTEP Figure 6.4)를 arm64로 옮겨 유저 공간에서 돌려 본다.
// "커널"(scheduler) 하나 + "프로세스"(task) 둘. 각 task는 자기 스택을 갖고,
// yield() 하면 swtch()가 레지스터를 저장/복원하고 스택 포인터를 바꿔 다른 흐름으로 '리턴'한다.
// (협력형: task가 스스로 yield해야만 전환된다 — 타이머 인터럽트 버전은 C06_timer_preempt.c)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(__APPLE__) && defined(__aarch64__)
// AAPCS64에서 함수 호출을 건너 살아남아야 하는(callee-saved) 레지스터만 저장하면 된다.
// caller-saved(x0~x18)는 swtch()를 "부른 쪽"이 이미 책임진다 — xv6가 eip/esp/ebx/esi/edi/ebp만 저장하는 이유와 같다.
struct context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;   // x29
    uint64_t lr;   // x30: swtch가 ret 할 주소 (xv6의 eip 역할)
    uint64_t sp;   // 스택 포인터 (xv6의 esp)
    double d8, d9, d10, d11, d12, d13, d14, d15;
};

void swtch(struct context *old, struct context *new);
__asm__(
    ".text\n.globl _swtch\n.p2align 2\n"
    "_swtch:\n"
    // ---- old 에 현재 레지스터 저장 ----
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x9, sp\n"
    "  str x9,       [x0, #96]\n"
    "  stp d8,  d9,  [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    // ---- new 에서 레지스터 복원 ----
    "  ldp x19, x20, [x1, #0]\n"
    "  ldp x21, x22, [x1, #16]\n"
    "  ldp x23, x24, [x1, #32]\n"
    "  ldp x25, x26, [x1, #48]\n"
    "  ldp x27, x28, [x1, #64]\n"
    "  ldp x29, x30, [x1, #80]\n"
    "  ldr x9,       [x1, #96]\n"
    "  mov sp, x9\n"                 // ← 스택이 여기서 바뀐다 (xv6: movl 4(%eax), %esp)
    "  ldp d8,  d9,  [x1, #104]\n"
    "  ldp d10, d11, [x1, #120]\n"
    "  ldp d12, d13, [x1, #136]\n"
    "  ldp d14, d15, [x1, #152]\n"
    "  ret\n");                      // ← new->lr 로 '리턴' = 다른 흐름으로 점프

enum state { RUNNABLE, ZOMBIE };
struct proc {
    const char *name;
    enum state state;
    struct context ctx;
    void (*fn)(void);
    char *stack;
};

#define STACK_SIZE (64 * 1024)
static struct context sched_ctx;     // "커널" 스케줄러의 문맥
static struct proc procs[2];
static struct proc *current;
static int verbose = 1;
static long switches;

static void yield(void) {            // 프로세스 → 스케줄러
    swtch(&current->ctx, &sched_ctx);
}

static void proc_entry(void) {       // 새 프로세스가 처음 '리턴'해 들어오는 곳
    current->fn();
    current->state = ZOMBIE;
    swtch(&current->ctx, &sched_ctx);    // 다시는 돌아오지 않는다
}

static void taskA(void) {
    for (int i = 0; i < 3; i++) {
        int local = i * 10;              // 스택 지역 변수가 전환 후에도 살아있는지 확인
        if (verbose) printf("  A: step %d (local=%d, &local=%p)\n", i, local, (void *)&local);
        yield();
    }
}
static void taskB(void) {
    for (int i = 0; i < 3; i++) {
        double f = 1.5 * i;              // d8~d15 저장도 확인
        if (verbose) printf("  B: step %d (f=%.1f, &f=%p)\n", i, f, (void *)&f);
        yield();
    }
}
static long spin_count;
static void spinner(void) {
    for (;;) { spin_count++; yield(); }
}

static void proc_init(struct proc *p, const char *name, void (*fn)(void)) {
    p->name = name;
    p->fn = fn;
    p->state = RUNNABLE;
    p->stack = malloc(STACK_SIZE);
    uintptr_t top = ((uintptr_t)p->stack + STACK_SIZE) & ~(uintptr_t)15;  // 16바이트 정렬
    p->ctx = (struct context){0};
    p->ctx.sp = top;
    p->ctx.lr = (uint64_t)proc_entry;    // 첫 swtch의 ret이 여기로 간다 (xv6의 forkret 역할)
}

static void scheduler(long max_switches) {  // xv6 scheduler()와 같은 모양
    for (switches = 0; switches < max_switches;) {
        int ran = 0;
        for (int i = 0; i < 2; i++) {
            if (procs[i].state != RUNNABLE) continue;
            current = &procs[i];
            swtch(&sched_ctx, &current->ctx);   // 커널 → 프로세스
            switches += 2;                      // 갔다 오기 = 스위치 2번
            ran = 1;
        }
        if (!ran) break;
    }
}

int main(void) {
    printf("[scheduler] sched_ctx (global) at %p\n", (void *)&sched_ctx);
    proc_init(&procs[0], "A", taskA);
    proc_init(&procs[1], "B", taskB);
    printf("[scheduler] A stack top %p, B stack top %p\n",
           (void *)procs[0].ctx.sp, (void *)procs[1].ctx.sp);
    scheduler(1000);
    printf("[scheduler] all tasks ZOMBIE after %ld switches\n\n", switches);

    // 유저 공간 swtch 비용 측정
    verbose = 0;
    proc_init(&procs[0], "S1", spinner);
    proc_init(&procs[1], "S2", spinner);
    long n = 10 * 1000 * 1000;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    scheduler(n);
    uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    printf("user-level swtch: %ld switches in %.3f ms -> %.2f ns per switch\n",
           switches, (t1 - t0) / 1e6, (double)(t1 - t0) / switches);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
