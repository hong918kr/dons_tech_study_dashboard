// C06_timer_preempt.c — "타이머 인터럽트로 제어권 되찾기"를 유저 공간에서 흉내 낸다.
//   하드웨어 타이머 인터럽트  ≈ setitimer() 가 주기적으로 보내는 SIGALRM
//   OS의 trap handler         ≈ 시그널 핸들러 on_tick()
//   switch() 루틴             ≈ swtch() (arm64 어셈블리, C06_swtch.c 와 동일)
// 두 '프로세스'는 절대 yield 하지 않는 무한 루프다. 협력형이었다면 A가 CPU를 영원히 독점한다.
// 교육용 데모: 시그널 핸들러 안에서 스택을 바꾸는 것은 POSIX가 보장하지 않는다(macOS arm64에서 동작 확인).
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
struct context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, fp, lr, sp;
    double d8, d9, d10, d11, d12, d13, d14, d15;
};
void swtch(struct context *old, struct context *new);
__asm__(".text\n.globl _swtch\n.p2align 2\n_swtch:\n"
        "  stp x19, x20, [x0, #0]\n  stp x21, x22, [x0, #16]\n  stp x23, x24, [x0, #32]\n"
        "  stp x25, x26, [x0, #48]\n  stp x27, x28, [x0, #64]\n  stp x29, x30, [x0, #80]\n"
        "  mov x9, sp\n  str x9, [x0, #96]\n"
        "  stp d8, d9, [x0, #104]\n  stp d10, d11, [x0, #120]\n"
        "  stp d12, d13, [x0, #136]\n  stp d14, d15, [x0, #152]\n"
        "  ldp x19, x20, [x1, #0]\n  ldp x21, x22, [x1, #16]\n  ldp x23, x24, [x1, #32]\n"
        "  ldp x25, x26, [x1, #48]\n  ldp x27, x28, [x1, #64]\n  ldp x29, x30, [x1, #80]\n"
        "  ldr x9, [x1, #96]\n  mov sp, x9\n"
        "  ldp d8, d9, [x1, #104]\n  ldp d10, d11, [x1, #120]\n"
        "  ldp d12, d13, [x1, #136]\n  ldp d14, d15, [x1, #152]\n"
        "  ret\n");

#define NPROC 2
#define STACK_SIZE (256 * 1024)
#define TICK_MS 10
#define MAX_TICKS 12

struct proc { char name; volatile uint64_t work; struct context ctx; char *stack; };
static struct proc procs[NPROC];
static struct context sched_ctx;
static int cur = -1;
static volatile int ticks;
static char trace[MAX_TICKS + 1];

// "타이머 인터럽트 핸들러": 하드웨어가 레지스터를 저장해 주는 대신 커널이 시그널 프레임을 이미 만들어 줬다.
static void on_tick(int sig) {
    (void)sig;
    ticks++;
    if (cur >= 0)
        swtch(&procs[cur].ctx, &sched_ctx);   // A의 문맥 저장 → 스케줄러로
    // 나중에 이 프로세스가 다시 선택되면 여기서부터 이어서 핸들러를 리턴 = "return-from-trap"
}

static void spin_forever(void) {             // 절대 yield 하지 않는 악성(?) 프로세스
    for (;;) procs[cur].work++;
}
static void entry(void) { spin_forever(); }

int main(void) {
    for (int i = 0; i < NPROC; i++) {
        procs[i].name = 'A' + i;
        procs[i].stack = malloc(STACK_SIZE);
        memset(&procs[i].ctx, 0, sizeof procs[i].ctx);
        procs[i].ctx.sp = ((uintptr_t)procs[i].stack + STACK_SIZE) & ~(uintptr_t)15;
        procs[i].ctx.lr = (uint64_t)entry;
    }
    // "부팅 시": trap table(=시그널 핸들러) 등록, 타이머 시작
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tick;
    sa.sa_flags = SA_NODEFER;                // 새로 시작하는 프로세스가 SIGALRM 막힌 채로 돌지 않게
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval it = { {0, TICK_MS * 1000}, {0, TICK_MS * 1000} };
    setitimer(ITIMER_REAL, &it, NULL);

    // 스케줄러: 라운드 로빈. 타이머가 프로세스를 끊을 때마다 여기로 돌아온다.
    int next = 0;
    while (ticks < MAX_TICKS) {
        cur = next;
        swtch(&sched_ctx, &procs[cur].ctx);  // "return-from-trap into B"
        trace[ticks - 1] = procs[cur].name;  // 이 tick 동안 누가 돌았나
        cur = -1;
        next = (next + 1) % NPROC;
    }
    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &off, NULL);

    printf("timer tick = %d ms, %d ticks\n", TICK_MS, ticks);
    printf("who ran in each tick: %s\n", trace);
    for (int i = 0; i < NPROC; i++)
        printf("proc %c: work = %llu loop iterations (never called yield)\n",
               procs[i].name, (unsigned long long)procs[i].work);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
