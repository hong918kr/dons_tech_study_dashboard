/* RTOS-B03 ucontext로 컨텍스트 스위치 체험
 *
 *   ucontext(getcontext / makecontext / swapcontext)로 "태스크마다 자기 스택 + 저장된 레지스터" 를 만들고
 *   두 태스크가 task_yield() 로 번갈아 실행되게 한다. 실제 스레드는 없다 — OS 스레드 1개 안에서
 *   sp 와 callee-saved 레지스터만 바꿔 끼운다 (= RTOS 컨텍스트 스위치의 본질). 완전히 결정적.
 *
 *   Part A  task_create / sched_pick_next / task_yield / kernel_run : 두 태스크 ping-pong,
 *           지역 변수 보존 + 지역 변수 주소와 저장된 sp 가 각자 자기 스택 배열 안에 있는지 검사
 *   Part B  makecontext 요건(uc_stack, uc_link, 정렬, 인자) 과 return 동작:
 *           uc_link = 커널 문맥 → 커널이 회수 / uc_link = NULL → 프로세스 종료 (fork 로 검사)
 *   Part C  스택 칠하기(0xA5) → high-water mark, 카나리 검사 (switch-out 때, FreeRTOS 방식)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st      (solution.c 도 같은 명령)
 *   (_XOPEN_SOURCE 600 은 파일 첫 줄에서 정의: macOS <ucontext.h> 는 이것이 없으면 #error.
 *    ucontext 는 macOS 10.6 부터 deprecated 라서 그 경고 하나만 아래 pragma 로 끈다.)
 *   ASan: cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c  (md 의 6. 정답 코드 참고)
 */
#define _XOPEN_SOURCE 600
#include <ucontext.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* getcontext 등: macOS 10.6 deprecated */
#endif

/* ======================= 미니 커널 자료구조 (하네스 제공) ======================= */
typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DELETED } task_state_t;

typedef struct tcb {
    const char *name;
    uint8_t prio;                 /* 이 레슨에선 안 씀 (RTOS-B04 에서 비트맵 스케줄러) */
    task_state_t state;
    uint32_t wake_tick;           /* 안 씀 (RTOS-B06) */
    void *wait_obj;               /* 안 씀 (RTOS-B08~) */
    struct tcb *next, *prev;      /* 안 씀 (RTOS-B04 ready list) */
    int id;                       /* g_tasks[] 안의 번호 = 라운드 로빈 순서 */
    ucontext_t ctx;               /* 저장된 컨텍스트: callee-saved 레지스터 + sp + 복귀 주소 + 시그널 마스크 */
    uint8_t *stack;               /* 이 태스크만의 스택 [stack, stack + stack_size) — uc_stack 은 swap 때 덮어써진다 */
    size_t stack_size;
    void (*fn)(void *);           /* 태스크 본체와 인자: makecontext 인자는 int 뿐이라 TCB 로 넘긴다 */
    void *arg;
} tcb_t;

#define MAX_TASKS   4
#define STACK_SIZE  (64 * 1024)   /* 호스트 스택: printf 등 libc 가 수 KB 를 쓴다. MCU 에선 0.5~4 KB */
#define MIN_STACK   (16 * 1024)
#define FILL_BYTE   0xA5u         /* FreeRTOS tskSTACK_FILL_BYTE 와 같은 값 (tasks.c:121) */
#define CANARY_LEN  16            /* FreeRTOS 방법 2 는 스택 끝 4 word = 16 바이트를 본다 */

static tcb_t     *g_tasks[MAX_TASKS];
static int        g_ntask;
static tcb_t     *g_cur;          /* 지금 CPU 를 가진 태스크 (NULL = 커널/main 문맥) */
static ucontext_t g_main_ctx;     /* kernel_run 이 swapcontext 로 저장하는 "커널 문맥" = 태스크의 uc_link */
static unsigned   g_switches;     /* task_yield 가 실제로 한 swapcontext 수 */
static unsigned   g_overflow_hooks;
static const char *g_overflow_name;

static void kernel_reset(void) {
    memset(g_tasks, 0, sizeof g_tasks);
    g_ntask = 0; g_cur = NULL; g_switches = 0; g_overflow_hooks = 0; g_overflow_name = NULL;
}

/* 스택 오버플로 훅 (FreeRTOS vApplicationStackOverflowHook 대용): 세기만 한다 */
static void stack_overflow_hook(tcb_t *t) { g_overflow_hooks++; g_overflow_name = t->name; }

/* 모든 태스크의 첫 진입점. makecontext 는 int 인자만 넘길 수 있어서(macOS 는 64비트 포인터를
 * 32비트로 자른다 — md 7장) fn/arg 는 TCB 에 두고 g_cur 로 찾는다. return 하면 uc_link 로 간다. */
static void task_entry(void) { g_cur->fn(g_cur->arg); }

/* ======================= Part A/B: 컨텍스트 만들기와 바꾸기 ======================= */

/* t 를 만들어 READY 로 등록. stack 은 16 바이트 정렬, size >= MIN_STACK.
 * link = 태스크 함수가 return 했을 때 이어서 실행할 문맥 (NULL 이면 프로세스 종료).
 * 성공 0, 잘못된 인자 -1. */
static int task_create(tcb_t *t, const char *name, void (*fn)(void *), void *arg,
                       uint8_t *stack, size_t size, ucontext_t *link) {
    if (!t || !fn || !stack || size < MIN_STACK || ((uintptr_t)stack & 15u) != 0 || g_ntask >= MAX_TASKS)
        return -1;
    memset(t, 0, sizeof *t);
    t->name = name; t->fn = fn; t->arg = arg; t->stack = stack; t->stack_size = size;
    if (getcontext(&t->ctx) != 0) return -1;          /* ① 먼저 (getcontext 는 uc_stack/uc_link 를 덮어쓴다) */
    t->ctx.uc_stack.ss_sp = stack;                    /* ② 이 문맥이 쓸 스택 */
    t->ctx.uc_stack.ss_size = size;
    t->ctx.uc_stack.ss_flags = 0;
    t->ctx.uc_link = link;                            /* ③ return 하면 갈 곳 */
    makecontext(&t->ctx, task_entry, 0);              /* ④ pc=시작 트램펄린, sp=스택 꼭대기 */
    t->id = g_ntask;
    t->state = TASK_READY;
    g_tasks[g_ntask++] = t;
    return 0;
}

/* 라운드 로빈: g_cur 다음 번호부터 한 바퀴 돌며 첫 READY 태스크. 없으면 NULL.
 * g_cur == NULL (커널 문맥) 이면 0 번부터. g_cur 자신은 고르지 않는다. */
static tcb_t *sched_pick_next(void) {
    int start = g_cur ? g_cur->id : g_ntask - 1;
    for (int k = 1; k <= g_ntask; k++) {
        tcb_t *t = g_tasks[(start + k) % g_ntask];
        if (t != g_cur && t->state == TASK_READY) return t;
    }
    return NULL;
}

static bool stack_canary_ok(const tcb_t *t);

/* 협력형 양보: 나가는 태스크의 카나리 검사 → 다음 READY 태스크로 swapcontext.
 * 다른 READY 가 없으면 아무것도 안 하고 return (혼자면 계속 돈다). 커널 문맥(g_cur==NULL)에선 무시. */
static void task_yield(void) {
    tcb_t *prev = g_cur;
    if (!prev) return;
    if (!stack_canary_ok(prev)) stack_overflow_hook(prev);   /* FreeRTOS: vTaskSwitchContext 에서 검사 */
    tcb_t *next = sched_pick_next();
    if (!next) return;
    prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    g_cur = next;
    g_switches++;
    swapcontext(&prev->ctx, &next->ctx);   /* 여기서 "멈췄다가" 누군가 나를 고르면 여기서 이어진다 */
}

/* 커널: READY 태스크를 하나 골라 들어가고, 태스크가 return 하면 (uc_link = &g_main_ctx 로) 여기로
 * 돌아와 그 태스크를 DELETED 로 회수한다. READY 가 없으면 끝.
 * 반환: 회수한 태스크 수, swapcontext 실패 시 -1. */
static int kernel_run(void) {
    int reaped = 0;
    for (;;) {
        tcb_t *n = sched_pick_next();
        if (!n) break;
        n->state = TASK_RUNNING;
        g_cur = n;
        if (swapcontext(&g_main_ctx, &n->ctx) != 0) { g_cur = NULL; return -1; }
        /* 태스크가 return → _ctx_done → setcontext(uc_link) → 여기. g_cur = 방금 끝난 태스크 */
        if (!stack_canary_ok(g_cur)) stack_overflow_hook(g_cur);
        g_cur->state = TASK_DELETED;
        reaped++;
    }
    g_cur = NULL;
    return reaped;
}

/* ======================= Part C: 스택 칠하기 / high-water mark / 카나리 ======================= */

/* 스택 전체를 FILL_BYTE 로 칠한다. 반드시 makecontext 뒤에 (macOS makecontext 는 스택을 0 으로 지운다). */
static void stack_paint(tcb_t *t) {
    memset(t->stack, FILL_BYTE, t->stack_size);
}

/* 한 번도 쓰이지 않은 바이트 수 = 낮은 주소(스택 끝)부터 FILL_BYTE 가 이어지는 길이.
 * 스택은 높은 주소 → 낮은 주소로 자라므로 낮은 쪽에 남은 칠이 "여유" 다 (FreeRTOS prvTaskCheckFreeStackSpace). */
static size_t stack_free_bytes(const tcb_t *t) {
    size_t n = 0;
    while (n < t->stack_size && t->stack[n] == FILL_BYTE) n++;
    return n;
}

/* 스택 맨 끝(가장 낮은 주소) CANARY_LEN 바이트가 아직 칠 그대로인가 (FreeRTOS configCHECK_FOR_STACK_OVERFLOW 2) */
static bool stack_canary_ok(const tcb_t *t) {
    for (size_t i = 0; i < CANARY_LEN; i++)
        if (t->stack[i] != FILL_BYTE) return false;
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static _Alignas(16) uint8_t g_stackA[STACK_SIZE];
static _Alignas(16) uint8_t g_stackB[STACK_SIZE];
static _Alignas(16) uint8_t g_stackC[STACK_SIZE];

static bool in_stack(const tcb_t *t, uintptr_t a) {
    return a >= (uintptr_t)t->stack && a < (uintptr_t)t->stack + t->stack_size;
}

/* 저장된 컨텍스트의 sp (macOS arm64 만: mcontext 의 __ss.__sp). 다른 플랫폼은 0 = 검사 생략 */
static uintptr_t ctx_saved_sp(const ucontext_t *uc) {
#if defined(__APPLE__) && defined(__aarch64__)
    return (uintptr_t)uc->uc_mcontext->__ss.__sp;
#else
    (void)uc; return 0;
#endif
}

static char   g_order[256];
static size_t g_olen;
#define TL_MAX 16
static char   g_tl[MAX_TASKS][TL_MAX + 1];    /* 타임라인: step 마다 R=실행 r=READY .=끝남 */
static int    g_nslot;
static void log_step(const char *s) {
    size_t n = strlen(s);
    if (g_olen + n + 2 < sizeof g_order) {
        if (g_olen) g_order[g_olen++] = ' ';
        memcpy(g_order + g_olen, s, n); g_olen += n; g_order[g_olen] = 0;
    }
    if (g_nslot < TL_MAX) {
        for (int i = 0; i < g_ntask; i++) {
            const tcb_t *t = g_tasks[i];
            g_tl[i][g_nslot] = t == g_cur ? 'R' : t->state == TASK_READY ? 'r' : t->state == TASK_DELETED ? '.' : '?';
            g_tl[i][g_nslot + 1] = 0;
        }
        g_nslot++;
    }
}
static void tl_reset(void) { g_olen = 0; g_order[0] = 0; g_nslot = 0; memset(g_tl, 0, sizeof g_tl); }
static void tl_print(void) {
    printf("step : "); for (int k = 0; k < g_nslot; k++) printf("%d ", k % 10); printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%-4s : ", g_tasks[i]->name);
        for (int k = 0; k < g_nslot; k++) printf("%c ", g_tl[i][k]);
        printf("\n");
    }
}

/* ---- Part A/B 태스크: 지역 변수를 들고 양보를 반복 ---- */
typedef struct {
    char tag; int steps; int base;
    tcb_t *self, *peer;
    uintptr_t addr[8];        /* 매 step 의 &sum */
    int bad_locals;           /* yield 뒤 지역 변수가 바뀐 횟수 */
    int peer_sp_checked, peer_sp_bad;
    uintptr_t peer_sp_last;   /* 마지막으로 본 상대의 저장된 sp */
    int done;
} job_t;

static void worker(void *p) {
    job_t *j = p;
    volatile int sum = 0;                     /* volatile: 레지스터가 아니라 스택에 살게 해서 주소를 잴 수 있다 */
    char note[8] = { j->tag, '-', 't', 'a', 's', 'k', 0, 0 };
    double acc = 1.0;                         /* 부동소수 지역 변수: d8-d15 에 살 수도 있다 */
    for (int i = 0; i < j->steps; i++) {
        sum += j->base + i;
        acc *= 2.0;
        j->addr[i] = (uintptr_t)&sum;
        char s[8]; snprintf(s, sizeof s, "%c%d", j->tag, i);
        log_step(s);
        /* 상대 태스크는 지금 멈춰 있다: 그 저장된 sp 는 상대 스택 안이어야 한다 */
        if (j->peer && j->peer->state == TASK_READY) {
            uintptr_t sp = ctx_saved_sp(&j->peer->ctx);
            if (sp) { j->peer_sp_checked++; j->peer_sp_last = sp; if (!in_stack(j->peer, sp) || in_stack(j->self, sp)) j->peer_sp_bad++; }
        }
        task_yield();
        int expect = 0; for (int k = 0; k <= i; k++) expect += j->base + k;
        if (sum != expect || acc != (double)(1u << (i + 1)) || note[0] != j->tag || strcmp(note + 1, "-task") != 0)
            j->bad_locals++;
    }
    j->done = 1;
}

/* ---- Part C 태스크: 스택을 다르게 쓰는 두 태스크 ---- */
static volatile int g_sink;
static void imu_task(void *p) {               /* IMU 이동평균 필터: 256-샘플 창 (1 KB) */
    int *rounds = p;
    for (int r = 0; r < *rounds; r++) {
        volatile float window[256];
        for (int i = 0; i < 256; i++) window[i] = (float)(i * r);
        float s = 0; for (int i = 0; i < 256; i++) s += window[i];
        g_sink += (int)s;
        task_yield();
    }
}
static void uart_task(void *p) {              /* UART 명령 줄: 32 바이트 버퍼 */
    int *rounds = p;
    for (int r = 0; r < *rounds; r++) {
        volatile char line[32];
        for (int i = 0; i < 32; i++) line[i] = (char)('a' + (i + r) % 26);
        g_sink += line[r % 32];
        task_yield();
    }
}

/* ---- Part B6: uc_link = NULL 인 태스크가 return 하면? (자식 프로세스에서) ---- */
static int g_pipe_w = -1;
static void returner(void *p) { (void)p; (void)!write(g_pipe_w, "R", 1); }

int main(void) {
    int main_local = 0;
    tcb_t A, B, C;
    memset(&A, 0, sizeof A); memset(&B, 0, sizeof B); memset(&C, 0, sizeof C);
    (void)task_entry; (void)stack_overflow_hook; (void)sched_pick_next;   /* starter 에서 아직 안 쓰는 함수: unused 경고 방지 */

    /* ================= Part A: 두 태스크 ping-pong ================= */
    printf("== Part A: two tasks, own stacks, yield ping-pong ==\n");
    kernel_reset(); tl_reset();
    job_t ja = { .tag = 'A', .steps = 5, .base = 100 }, jb = { .tag = 'B', .steps = 5, .base = 7 };
    CHECK(task_create(&A, "A", worker, &ja, g_stackA, sizeof g_stackA, &g_main_ctx) == 0);
    CHECK(task_create(&B, "B", worker, &jb, g_stackB, sizeof g_stackB, &g_main_ctx) == 0);
    ja.self = &A; ja.peer = &B; jb.self = &B; jb.peer = &A;
    int reaped = kernel_run();
    printf("order    : %s\n", g_order);
    tl_print();
    printf("switches : %u, reaped %d\n", g_switches, reaped);
    printf("stack A  : [%p, %p)   &sum in A = %p\n", (void *)g_stackA, (void *)(g_stackA + STACK_SIZE), (void *)ja.addr[0]);
    printf("stack B  : [%p, %p)   &sum in B = %p\n", (void *)g_stackB, (void *)(g_stackB + STACK_SIZE), (void *)jb.addr[0]);
    printf("main     : &main_local = %p\n", (void *)&main_local);
    printf("saved sp : A checked %d bad %d, B checked %d bad %d\n", jb.peer_sp_checked, jb.peer_sp_bad, ja.peer_sp_checked, ja.peer_sp_bad);
    if (ja.peer_sp_last && jb.peer_sp_last)
        printf("example  : A.ctx sp = %#lx (top-%ld), B.ctx sp = %#lx (top-%ld)\n",
               (unsigned long)jb.peer_sp_last, (long)((uintptr_t)(g_stackA + STACK_SIZE) - jb.peer_sp_last),
               (unsigned long)ja.peer_sp_last, (long)((uintptr_t)(g_stackB + STACK_SIZE) - ja.peer_sp_last));
    CHECK(reaped == 2 && ja.done && jb.done);
    CHECK(strcmp(g_order, "A0 B0 A1 B1 A2 B2 A3 B3 A4 B4") == 0);
    CHECK(g_switches == 10);
    CHECK(ja.bad_locals == 0 && jb.bad_locals == 0);                 /* 지역 변수 보존 */
    bool own = true, same = true;
    for (int i = 0; i < 5; i++) {
        own  = own && in_stack(&A, ja.addr[i]) && !in_stack(&B, ja.addr[i])
                   && in_stack(&B, jb.addr[i]) && !in_stack(&A, jb.addr[i]);
        same = same && ja.addr[i] == ja.addr[0] && jb.addr[i] == jb.addr[0];
    }
    CHECK(own);                                                      /* 각자 자기 스택 배열 안 */
    CHECK(same);                                                     /* 양보해도 프레임은 그 자리 */
    CHECK(!in_stack(&A, (uintptr_t)&main_local) && !in_stack(&B, (uintptr_t)&main_local));
    CHECK(A.state == TASK_DELETED && B.state == TASK_DELETED && g_cur == NULL);
#if defined(__APPLE__) && defined(__aarch64__)
    CHECK(ja.peer_sp_checked >= 4 && jb.peer_sp_checked >= 4 && ja.peer_sp_bad == 0 && jb.peer_sp_bad == 0);
#endif

    /* ================= Part B: makecontext 요건과 return 동작 ================= */
    printf("== Part B: makecontext needs / return via uc_link ==\n");
    /* B1 길이가 다른 두 태스크: 먼저 끝난 A 는 uc_link 로 커널에 돌아가 회수되고 B 는 혼자 계속 */
    kernel_reset(); tl_reset();
    job_t ja2 = { .tag = 'A', .steps = 2, .base = 1 }, jb2 = { .tag = 'B', .steps = 4, .base = 2 };
    CHECK(task_create(&A, "A", worker, &ja2, g_stackA, sizeof g_stackA, &g_main_ctx) == 0);
    CHECK(task_create(&B, "B", worker, &jb2, g_stackB, sizeof g_stackB, &g_main_ctx) == 0);
    reaped = kernel_run();
    printf("B1 order : %s  (switches %u, reaped %d)\n", g_order, g_switches, reaped);
    tl_print();
    CHECK(strcmp(g_order, "A0 B0 A1 B1 B2 B3") == 0 && g_switches == 4 && reaped == 2);
    CHECK(ja2.bad_locals == 0 && jb2.bad_locals == 0);

    /* B2 끝난 문맥으로 다시 swap 하면? (macOS 관찰: _ctx_done 이 uc_mcsize 를 0 으로 → EINVAL) */
#if defined(__APPLE__)
    ucontext_t tmp;
    int r = 0; errno = 0;
    if (A.state == TASK_DELETED && ja2.done)       /* 정말 끝난 문맥일 때만 시도 (starter 에서 쓰레기 문맥 방지) */
        r = swapcontext(&tmp, &A.ctx);
    printf("B2 swap to finished A: ret=%d errno=%d, A.ctx.uc_mcsize=%zu\n", r, errno, (size_t)A.ctx.uc_mcsize);
    CHECK(r == -1 && errno == EINVAL);
#endif

    /* B3 잘못된 스택은 거부 */
    kernel_reset();
    CHECK(task_create(&C, "C", worker, &ja2, NULL, STACK_SIZE, &g_main_ctx) == -1);
    CHECK(task_create(&C, "C", worker, &ja2, g_stackC, 1024, &g_main_ctx) == -1);
    CHECK(task_create(&C, "C", worker, &ja2, g_stackC + 8, STACK_SIZE - 16, &g_main_ctx) == -1);
    CHECK(g_ntask == 0);

    /* B4 uc_link / uc_stack 이 실제로 들어갔나 (getcontext 를 나중에 부르면 덮어써진다) */
    CHECK(task_create(&C, "C", worker, &ja2, g_stackC, STACK_SIZE, &g_main_ctx) == 0);
    CHECK(C.ctx.uc_link == &g_main_ctx && C.ctx.uc_stack.ss_sp == (void *)g_stackC && C.ctx.uc_stack.ss_size == STACK_SIZE);
    CHECK(C.state == TASK_READY && g_ntask == 1);

    /* B5 64비트 포인터 인자가 그대로 도착하나 (TCB 경유) */
    printf("B5 &ja2 = %p (upper 32 bits %s zero)\n", (void *)&ja2, ((uintptr_t)&ja2 >> 32) ? "non" : "");
    kernel_reset(); tl_reset();
    job_t one = { .tag = 'P', .steps = 1, .base = 0 };
    CHECK(task_create(&C, "P", worker, &one, g_stackC, STACK_SIZE, &g_main_ctx) == 0);
    CHECK(kernel_run() == 1 && one.done == 1 && strcmp(g_order, "P0") == 0);

    /* B6 uc_link = NULL: 태스크가 return 하면 프로세스가 끝난다 → 자식 프로세스에서 확인 */
    int fds[2];
    CHECK(pipe(fds) == 0);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]); g_pipe_w = fds[1];
        kernel_reset();
        if (task_create(&C, "R", returner, NULL, g_stackC, STACK_SIZE, NULL) == 0) {
            kernel_run();
            (void)!write(g_pipe_w, "K", 1);       /* 여기 오면 안 된다 */
        }
        _exit(42);
    }
    close(fds[1]);
    char buf[8] = { 0 }; ssize_t got = 0, n;
    while ((n = read(fds[0], buf + got, sizeof buf - 1 - (size_t)got)) > 0) got += n;
    close(fds[0]);
    int st = 0; waitpid(pid, &st, 0);
    printf("B6 uc_link=NULL child: pipe=\"%s\" exited=%d status=%d\n", buf, WIFEXITED(st), WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    CHECK(strcmp(buf, "R") == 0);                /* 태스크는 돌았고, 커널로 돌아오지 않았다 */
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0); /* macOS: _ctx_done → __exit(0) */

    /* ================= Part C: 스택 high-water mark 와 카나리 ================= */
    printf("== Part C: stack painting, high-water mark, canary ==\n");
    kernel_reset();
    int rounds = 3;
    CHECK(task_create(&A, "imu", imu_task, &rounds, g_stackA, STACK_SIZE, &g_main_ctx) == 0);
    CHECK(task_create(&B, "uart", uart_task, &rounds, g_stackB, STACK_SIZE, &g_main_ctx) == 0);
    stack_paint(&A); stack_paint(&B);                       /* makecontext 뒤에 칠한다 */
    CHECK(stack_free_bytes(&A) == STACK_SIZE && stack_free_bytes(&B) == STACK_SIZE);
    CHECK(stack_canary_ok(&A) && stack_canary_ok(&B));
    CHECK(kernel_run() == 2);
    size_t usedA = STACK_SIZE - stack_free_bytes(&A), usedB = STACK_SIZE - stack_free_bytes(&B);
    printf("task  stack   used   free   used%%\n");
    printf("imu   %5d  %5zu  %5zu  %5.1f\n", STACK_SIZE, usedA, (size_t)STACK_SIZE - usedA, 100.0 * (double)usedA / STACK_SIZE);
    printf("uart  %5d  %5zu  %5zu  %5.1f\n", STACK_SIZE, usedB, (size_t)STACK_SIZE - usedB, 100.0 * (double)usedB / STACK_SIZE);
    CHECK(usedA >= 1024 && usedA < STACK_SIZE / 2);         /* 1 KB 창 + 프레임들 */
    CHECK(usedB > 0 && usedB < usedA);                      /* uart 가 덜 쓴다 */
    CHECK(usedA - usedB >= 768);                            /* 차이 ≈ 창 1 KB - 줄 32 B (ASan 에선 프레임이 커져 조금 줄어든다) */
    CHECK(g_overflow_hooks == 0);

    /* C2 오버플로 흉내: uart 가 멈춘 사이 스택 끝 4 바이트가 망가짐 (넘친 지역 배열 / 잘못된 DMA 주소) */
    kernel_reset();
    CHECK(task_create(&A, "imu", imu_task, &rounds, g_stackA, STACK_SIZE, &g_main_ctx) == 0);
    CHECK(task_create(&B, "uart", uart_task, &rounds, g_stackB, STACK_SIZE, &g_main_ctx) == 0);
    stack_paint(&A); stack_paint(&B);
    g_stackB[0] = 0; g_stackB[3] = 0x5A;                    /* 가장 낮은 주소 = 스택이 마지막에 닿는 곳 */
    CHECK(!stack_canary_ok(&B) && stack_canary_ok(&A));
    CHECK(kernel_run() == 2);
    printf("C2 corrupted canary: hooks=%u (task %s)\n", g_overflow_hooks, g_overflow_name ? g_overflow_name : "-");
    CHECK(g_overflow_hooks == (unsigned)rounds + 1 && g_overflow_name && strcmp(g_overflow_name, "uart") == 0);
    CHECK(stack_free_bytes(&B) == 0);                       /* 맨 아래가 깨졌으니 "여유 0" 으로 보인다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
