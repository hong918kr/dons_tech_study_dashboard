/* RTOS-M09 스택 오버플로 탐지: 카나리와 패턴 검사
 *
 * 호스트 모델: OS 스레드 1개로 도는 "결정적 미니 커널 + 가짜 CPU".
 *  - 모든 태스크 스택과 TCB 는 큰 배열 1개(arena, 4 KB = MCU 의 SRAM 흉내) 안에 heap_4 처럼 차례로 놓인다:
 *      [ .bss 64 B ][ stack0 ][ TCB0 ][ stack1 ][ TCB1 ] ...
 *    (FreeRTOS 는 스택이 아래로 자라면 "스택 먼저, TCB 나중" 으로 할당한다 — tasks.c:1640-1642, 1681-1689)
 *  - SP 는 진짜 레지스터가 아니라 arena 안의 정수 오프셋이다. 그래서 "오버플로" 가 이웃 TCB/스택을 덮어도
 *    전부 우리 배열 안의 쓰기라 실제 UB 가 없고 ASan 도 깨끗하다. arena 밖 접근은 모델의 BusFault 로 멈춘다.
 *  - 태스크 = 작은 스크립트: CALL(n) 프레임 n 바이트 push(+기록), RET, WORK, YIELD, END. op 1개 = step 1.
 *    CALL 은 프레임 맨 위 8 B(저장된 r7/lr 프레임 레코드)와, 지역 배열 몫으로 "바닥부터 w 바이트" 를 쓴다.
 *  - 전환(YIELD, END, 또는 slice step 마다 tick 선점) = Cortex-M PendSV 흉내:
 *      ① 나가는 태스크 스택에 문맥 68 B push (HW 8 word + r4-r11,r14 9 word, ARM_CM4F port.c:520)
 *      ② TCB 의 saved_sp(= pxTopOfStack, TCB 첫 필드) 갱신
 *      ③ taskCHECK_FOR_STACK_OVERFLOW() — 나가는 태스크만 검사 (tasks.c:5259)
 *      ④ 라운드 로빈으로 다음 태스크, TCB 에서 saved_sp 를 읽어 문맥 pop.
 *  - 그림자(shadow) 정보 min_sp / oob 쓰기 수 = "진실". 실제 커널은 모르는 값이고, 탐지기 평가에만 쓴다.
 *
 * 이 레슨에서 채우는 것 (TODO 1~4):
 *  1. check_method1()  : configCHECK_FOR_STACK_OVERFLOW == 1 — 저장된 SP 가 [stack_base, stack_top] 안인가
 *  2. check_method2()  : == 2 — 방법 1 + 스택 맨 아래 16 바이트가 아직 0xA5 인가 (stack_macros.h:101-118)
 *  3. cpu_sp_sub()     : ARMv8-M PSPLIM 모델 — SP 를 내리는 순간 한계 아래면 그 자리에서 fault (쓰기 전에)
 *  4. tcb_valid()      : switch-in 때 TCB 자체 검증 (magic + saved_sp/스택 범위 일관성) — Part C
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (-v: 이벤트 로그 + hexdump)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARENA_SIZE    4096
#define RAM_BASE      0x20000000u   /* 출력용: arena[0] 을 STM32 SRAM 0x2000_0000 처럼 보이게 */
#define RSV_BYTES     64            /* arena 맨 앞 = 다른 전역 변수(.bss: 센서 보정 테이블) */
#define TCB_BYTES     32
#define GUARD_BYTES   16            /* 방법 2 가 보는 바이트 수: pulStack[0..3] (stack_macros.h:106-113) */
#define FILL_BYTE     0xA5u         /* tskSTACK_FILL_BYTE (tasks.c:121) */
#define CTX_BYTES     68            /* Cortex-M4, FPU 미사용: HW 프레임 32 B + r4-r11,r14 36 B */
#define FRAME_REC     8             /* 함수 프레임 맨 위 = push {r7, lr} */
#define LIMIT_PADDING 0u            /* portSTACK_LIMIT_PADDING 기본값 (stack_macros.h:52-54) */
#define TCB_MAGIC     0x7CB0C0DEu   /* 이 모델의 추가 필드 — FreeRTOS TCB 에는 없다 (tasks.c:377-) */
#define MAXT          4
#define MAXOPS        24
#define MAXDEPTH      32
#define MAXSTEPS      200
#define LOGSZ         4096

/* TCB 이미지 (arena 안, 리틀 엔디안). saved_sp 가 첫 필드 = FreeRTOS pxTopOfStack (tasks.c:379) */
enum { T_SP = 0, T_PRIO = 4, T_STATE = 5, T_LO = 8, T_HI = 12, T_NAME = 16, T_NAMELEN = 12, T_MAGIC = 28 };

typedef enum { OP_END, OP_CALL, OP_RET, OP_WORK, OP_YIELD } opk_t;
typedef struct { opk_t k; int32_t size; int32_t low; int rep; const char *data; } op_t;
#define CALL(n)        { OP_CALL, (n), -1, 1, NULL }  /* 프레임 전체를 쓴다 (지역 배열 초기화)      */
#define CALL_R(n, r)   { OP_CALL, (n), -1, (r), NULL } /* 같은 프레임 r 번 = 재귀 r 단계              */
#define CALL_W(n, w)   { OP_CALL, (n), (w), 1, NULL }  /* 프레임 레코드 + 바닥부터 w 바이트만         */
#define CALL_WR(n, w, r) { OP_CALL, (n), (w), (r), NULL }
#define CALL_S(n, s)   { OP_CALL, (n), 0, 1, (s) }     /* 프레임 레코드 + 바닥부터 문자열 (snprintf)  */
#define RET_           { OP_RET, 0, 0, 1, NULL }
#define RET_R(r)       { OP_RET, 0, 0, (r), NULL }
#define WORK_          { OP_WORK, 0, 0, 1, NULL }
#define YIELD_         { OP_YIELD, 0, 0, 1, NULL }
#define END_           { OP_END, 0, 0, 1, NULL }

typedef enum { F_NONE, F_STKOF, F_BADCTX, F_BADRET, F_TCB, F_BUS } fault_t;
static const char *const FAULT_NAME[] = { "-", "STKOF", "HardFault-ctx", "HardFault-ret", "TCB-corrupt", "BusFault" };

typedef struct {
    const char *name;
    const op_t *prog; int pc; bool done;
    int32_t lo, hi, tcb;            /* 생성 때 정한 진짜 위치 (모델/그림자용 — 커널 검사는 TCB 이미지를 읽는다) */
    int32_t frames[MAXDEPTH]; int depth;
    /* 그림자(진실): 실제 커널은 모른다 */
    int32_t min_sp; int oob; int first_oob_step;
} task_t;

typedef struct kernel kernel_t;
struct kernel {
    uint8_t mem[ARENA_SIZE];
    task_t t[MAXT]; int nt; int32_t brk;
    int method;          /* configCHECK_FOR_STACK_OVERFLOW 0 / 1 / 2 */
    bool psplim;         /* ARMv8-M 스택 한계 레지스터 모델 */
    bool validate_tcb;   /* switch-in 때 tcb_valid() */
    int slice;           /* 0 = YIELD/END 때만 전환, n = n step 마다 tick 선점 */
    int cur; int32_t sp; uint32_t splim; int ran; int step;
    bool halted, finished;
    int hooks, hook_task, hook_step;
    fault_t fault; int fault_task, fault_step;
    void (*overflow_hook)(kernel_t *, int);   /* vApplicationStackOverflowHook 자리 (교체 가능) */
    char log[LOGSZ]; size_t loglen;
};

/* ---------------- 주어진 코드: arena 접근 ---------------- */
static int peek(const kernel_t *k, uint32_t a) { return a < ARENA_SIZE ? k->mem[a] : -1; }
static uint32_t tcb_get(const kernel_t *k, int t, int off) {
    uint32_t a = (uint32_t)k->t[t].tcb + (uint32_t)off, v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | k->mem[a + (uint32_t)i];
    return v;
}
static void tcb_put(kernel_t *k, int t, int off, uint32_t v) {      /* 커널 자신의 TCB 갱신 (태스크 쓰기 아님) */
    uint32_t a = (uint32_t)k->t[t].tcb + (uint32_t)off;
    for (int i = 0; i < 4; i++) { k->mem[a + (uint32_t)i] = (uint8_t)v; v >>= 8; }
}
static void klog(kernel_t *k, const char *fmt, ...) {
    if (k->loglen + 128 >= LOGSZ) return;
    int n = snprintf(k->log + k->loglen, LOGSZ - k->loglen, "  step %3d  ", k->step);
    k->loglen += (size_t)n;
    va_list ap; va_start(ap, fmt);
    n = vsnprintf(k->log + k->loglen, LOGSZ - k->loglen, fmt, ap);
    va_end(ap);
    k->loglen += (size_t)n;
    k->log[k->loglen++] = '\n'; k->log[k->loglen] = 0;
}
static void kfault(kernel_t *k, fault_t f, int t) {
    if (k->halted) return;
    k->halted = true; k->fault = f; k->fault_task = t; k->fault_step = k->step;
    klog(k, "FAULT %s in %s", FAULT_NAME[f], k->t[t].name);
}
static void default_hook(kernel_t *k, int t) {    /* 실제 훅: 로그 남기고 리셋 → 모델: 기록하고 멈춤 */
    k->hooks++; k->hook_task = t; k->hook_step = k->step; k->halted = true;
    klog(k, "vApplicationStackOverflowHook(%s)  saved_sp=%08x", k->t[t].name, RAM_BASE + tcb_get(k, t, T_SP));
}
static bool cpu_write(kernel_t *k, int32_t a, uint8_t v) {        /* 지금 실행 중인 태스크의 쓰기 */
    task_t *t = &k->t[k->cur];
    if (a < 0 || a >= ARENA_SIZE) { kfault(k, F_BUS, k->cur); return false; }
    if (a < t->lo || a >= t->hi) { if (t->oob++ == 0) t->first_oob_step = k->step; }
    k->mem[a] = v;
    return true;
}

/* ---------------- TODO 1: 방법 1 — 저장된 SP 범위 검사 ---------------- */
/* 나가는 태스크 t 의 TCB 에서 saved_sp(pxTopOfStack), lo(pxStack), hi 를 읽어
 *   saved_sp <= lo + LIMIT_PADDING  → 오버플로 (FreeRTOS 와 같은 <=, stack_macros.h:74)
 *   saved_sp >  hi                  → 범위 밖 (덤: SP/TCB 손상)
 * 이면 true. 값은 uint32_t 로 비교한다 (깨진 TCB 의 큰 값도 정의된 동작). */
static bool check_method1(const kernel_t *k, int t) {
    (void)k; (void)t;                                /* TODO 1 */
    return false;
}

/* ---------------- TODO 2: 방법 2 — 방법 1 + 끝 16 바이트 패턴 ---------------- */
/* 방법 1 이 잡으면 true. 아니면 TCB 의 lo 부터 GUARD_BYTES 바이트가 전부 FILL_BYTE 인지 본다.
 * arena 밖을 가리키는 lo(깨진 TCB)는 peek() 가 -1 → "패턴 아님" 으로 취급. */
static bool check_method2(const kernel_t *k, int t) {
    (void)k; (void)t; (void)peek;                    /* TODO 2 */
    return false;
}

/* ---------------- TODO 3: SP 내리기 + PSPLIM 모델 ---------------- */
/* k->psplim 이면 새 SP 가 k->splim(= switch-in 때 TCB 의 lo 로 설정) 보다 아래로 가는 순간
 * kfault(k, F_STKOF, k->cur) 하고 false — SP 도 메모리도 바뀌지 않는다. 아니면 SP 를 내리고 true. */
static bool cpu_sp_sub(kernel_t *k, int32_t n) {
    int32_t nsp = k->sp - n;
    /* TODO 3: PSPLIM 검사 */
    k->sp = nsp;
    return true;
}

/* ---------------- TODO 4: switch-in 전에 TCB 검증 ---------------- */
/* 깨진 TCB 로 문맥을 복원하면 엉뚱한 곳을 pop 해 HardFault 가 난다. 그 전에 TCB 가 스스로 일관적인지 본다:
 *   magic == TCB_MAGIC, lo < hi, hi <= ARENA_SIZE, lo < saved_sp, saved_sp + CTX_BYTES <= hi,
 *   이름 칸 안에 NUL 이 있다.  하나라도 어기면 false. (진짜 위치 task_t.lo/hi 는 보지 않는다 — 커널은 모른다) */
static bool tcb_valid(const kernel_t *k, int t) {
    (void)k; (void)t;                                /* TODO 4 */
    return true;
}

/* ---------------- 주어진 커널 코드 ---------------- */
static void k_init(kernel_t *k, int method, bool psplim, bool validate, int slice) {
    memset(k, 0, sizeof *k);
    k->method = method; k->psplim = psplim; k->validate_tcb = validate; k->slice = slice;
    k->overflow_hook = default_hook; k->hook_task = k->fault_task = -1;
    memset(k->mem, 0x3C, RSV_BYTES);                /* 남의 전역 데이터 */
    k->brk = RSV_BYTES;
}
/* heap_4 처럼 차례로: 스택 [lo,hi) 다음에 그 태스크의 TCB [hi, hi+32) */
static int task_create(kernel_t *k, const char *name, int prio, int32_t depth, const op_t *prog) {
    if (k->nt >= MAXT || k->brk + depth + TCB_BYTES > ARENA_SIZE || depth < CTX_BYTES + GUARD_BYTES) return -1;
    int id = k->nt++;
    task_t *t = &k->t[id];
    t->name = name; t->prog = prog;
    t->lo = k->brk; t->hi = t->lo + depth; t->tcb = t->hi; k->brk = t->tcb + TCB_BYTES;
    t->min_sp = t->hi; t->first_oob_step = -1;
    memset(k->mem + t->lo, FILL_BYTE, (size_t)depth);                 /* tskSET_NEW_STACKS_TO_KNOWN_VALUE */
    memset(k->mem + t->hi - CTX_BYTES, 0xC0 | id, CTX_BYTES);         /* pxPortInitialiseStack: 첫 문맥 */
    memset(k->mem + t->tcb, 0, TCB_BYTES);
    tcb_put(k, id, T_SP, (uint32_t)(t->hi - CTX_BYTES));
    k->mem[t->tcb + T_PRIO] = (uint8_t)prio;
    tcb_put(k, id, T_LO, (uint32_t)t->lo); tcb_put(k, id, T_HI, (uint32_t)t->hi);
    snprintf((char *)k->mem + t->tcb + T_NAME, T_NAMELEN, "%s", name);
    tcb_put(k, id, T_MAGIC, TCB_MAGIC);
    return id;
}
static void note_sp(kernel_t *k) { task_t *t = &k->t[k->cur]; if (k->sp < t->min_sp) t->min_sp = k->sp; }

static int sched_pick_next(const kernel_t *k) {      /* 라운드 로빈: cur 다음부터, 끝나지 않은 첫 태스크 */
    for (int i = 1; i <= k->nt; i++) {
        int n = (k->cur + i) % k->nt;
        if (!k->t[n].done) return n;
    }
    return -1;
}
static void switch_in(kernel_t *k, int n) {
    const task_t *t = &k->t[n];
    if (k->validate_tcb && !tcb_valid(k, n)) { k->cur = n; kfault(k, F_TCB, n); return; }
    uint32_t ssp = tcb_get(k, n, T_SP);
    /* 하드웨어 현실: CPU 는 SP 가 어디든 거기서 문맥을 pop 한다. 깨진 SP(엉뚱한 주소)나 덮어써진 문맥이면
     * 쓰레기 PC/xPSR 로 복귀 → HardFault. 모델: 저장한 문맥 패턴 0xC0|n 이 그대로인지로 판정한다. */
    if (ssp > ARENA_SIZE - CTX_BYTES) { k->cur = n; kfault(k, F_BADCTX, n); return; }
    for (uint32_t i = 0; i < CTX_BYTES; i++)
        if (k->mem[ssp + i] != (0xC0 | n)) { k->cur = n; kfault(k, F_BADCTX, n); return; }
    k->splim = tcb_get(k, n, T_LO);                  /* ARMv8-M 포트: 문맥과 함께 PSPLIM 복원 (portasm.c:511-512) */
    k->cur = n; k->sp = (int32_t)ssp + CTX_BYTES; k->ran = 0;
    klog(k, "switch-in  %s", t->name);
}
static void task_switch(kernel_t *k) {
    int c = k->cur;
    if (!k->t[c].done) {
        if (!cpu_sp_sub(k, CTX_BYTES)) return;       /* ① 예외 진입 + PendSV 가 문맥을 태스크 스택에 */
        note_sp(k);
        for (int32_t i = 0; i < CTX_BYTES; i++) if (!cpu_write(k, k->sp + i, (uint8_t)(0xC0 | c))) return;
        tcb_put(k, c, T_SP, (uint32_t)k->sp);         /* ② pxTopOfStack = 저장된 SP */
        klog(k, "switch-out %s saved_sp=%08x", k->t[c].name, RAM_BASE + (uint32_t)k->sp);
        bool bad = k->method == 1 ? check_method1(k, c) : k->method >= 2 ? check_method2(k, c) : false;
        if (bad) { k->overflow_hook(k, c); if (k->halted) return; }   /* ③ taskCHECK_FOR_STACK_OVERFLOW */
    }
    int n = sched_pick_next(k);                      /* ④ */
    if (n < 0) { k->finished = true; return; }
    switch_in(k, n);
}
static void exec(kernel_t *k, const op_t *op) {
    int c = k->cur; task_t *t = &k->t[c];
    switch (op->k) {
    case OP_CALL:
        for (int r = 0; r < op->rep; r++) {
            if (t->depth >= MAXDEPTH) { kfault(k, F_BUS, c); return; }
            if (!cpu_sp_sub(k, op->size)) return;
            note_sp(k);
            t->frames[t->depth++] = op->size;
            int32_t b = k->sp;
            if (b < t->lo && r == op->rep - 1)
                klog(k, "%s CALL %d -> SP=%08x, %d B below its stack", t->name, (int)op->size, RAM_BASE + (uint32_t)b, (int)(t->lo - b));
            for (int32_t i = op->size - FRAME_REC; i < op->size; i++) if (!cpu_write(k, b + i, (uint8_t)(0xF0 | c))) return;
            if (op->data) {
                for (int32_t i = 0; i <= (int32_t)strlen(op->data); i++) if (!cpu_write(k, b + i, (uint8_t)op->data[i])) return;
            } else {
                int32_t w = op->low < 0 ? op->size - FRAME_REC : op->low;
                for (int32_t i = 0; i < w; i++) if (!cpu_write(k, b + i, (uint8_t)(0xD0 | c))) return;
            }
        }
        break;
    case OP_RET:
        for (int r = 0; r < op->rep; r++) {
            if (t->depth == 0) { kfault(k, F_BUS, c); return; }
            int32_t sz = t->frames[--t->depth];
            for (int32_t i = sz - FRAME_REC; i < sz; i++)        /* 깨진 lr 로 복귀 = HardFault */
                if (k->mem[k->sp + i] != (0xF0 | c)) { kfault(k, F_BADRET, c); return; }
            k->sp += sz;
        }
        break;
    case OP_END: t->done = true; klog(k, "%s END", t->name); break;
    case OP_WORK: case OP_YIELD: break;
    }
}
static void kernel_run(kernel_t *k) {
    k->cur = 0;
    switch_in(k, 0);                                 /* vTaskStartScheduler → 첫 태스크 복원 */
    while (!k->halted && !k->finished && k->step < MAXSTEPS) {
        task_t *t = &k->t[k->cur];
        const op_t *op = &t->prog[t->pc++];
        k->step++;
        exec(k, op);
        if (k->halted) break;
        if (op->k == OP_YIELD || op->k == OP_END) { task_switch(k); continue; }
        if (k->slice > 0 && ++k->ran >= k->slice) task_switch(k);
    }
}

/* ---------------- 시나리오 ---------------- */
/* Part A/B: V(이웃, 먼저 생성) 와 O(범인) 스택 512 B 씩. O 의 스택은 V 의 TCB 바로 위에 있다. */
static const op_t PV[] = { CALL(96), YIELD_, WORK_, YIELD_, WORK_, YIELD_, RET_, END_ };
static const op_t S1[] = { CALL(96), CALL_R(64, 3), YIELD_, RET_R(3), YIELD_, RET_, END_ };            /* 정상 */
static const op_t S2[] = { CALL(96), CALL_R(64, 7), YIELD_, RET_R(7), RET_, END_ };                    /* 깊은 재귀, 바닥에서 전환 */
static const op_t S3[] = { CALL(96), CALL(560), RET_, YIELD_, RET_, END_ };                            /* 큰 배열 전부 기록, 전환 전 해제 */
static const op_t S4[] = { CALL(96), CALL_S(560, "T=23.5"), RET_, YIELD_, RET_, END_ };                /* 큰 배열 바닥만 기록 (가드 건너뜀) */
static const op_t S5[] = { CALL(96), CALL_R(64, 5), CALL(40), YIELD_, RET_, RET_R(5), RET_, END_ };    /* 거의 꽉 참 + 문맥 68 B 가 넘김 */
static const op_t S6[] = { CALL(96), CALL_WR(64, 0, 8), WORK_, WORK_, RET_R(8), YIELD_, RET_, END_ };  /* 재귀, 레코드만 기록 */
typedef struct { const char *name; const op_t *prog; int slice; } scen_t;
static const scen_t SCEN[] = {
    { "S1 normal",              S1, 0 }, { "S2 deep recursion",    S2, 0 },
    { "S3 big array, full",     S3, 0 }, { "S4 big array, sparse", S4, 0 },
    { "S5 ctx frame tips over", S5, 0 }, { "S6 recursion, slice=0", S6, 0 },
    { "S6 recursion, slice=1",  S6, 1 },
};
#define NSCEN ((int)(sizeof SCEN / sizeof SCEN[0]))
enum { VI = 0, OI = 1 };
static void run_scen(kernel_t *k, int s, int method, bool psplim) {
    k_init(k, method, psplim, false, SCEN[s].slice);
    task_create(k, "V", 1, 512, PV);
    task_create(k, "O", 1, 512, SCEN[s].prog);
    kernel_run(k);
}

/* Part C: STM32 펌웨어 — 생성 순서 ctrl → comms → imu. comms 의 상태 응답 포맷터가 char reply[376] 에
 * snprintf 로 짧은 문자열만 쓴다 = 큰 프레임의 "바닥" 만 기록 → 가드를 건너뛰어 ctrl 의 TCB 를 덮는다. */
static const op_t PCTRL[]  = { CALL(128), YIELD_, WORK_, YIELD_, WORK_, YIELD_, RET_, END_ };
static const op_t PCOMMS[] = { CALL(96), YIELD_, CALL(64), CALL_S(384, "ST T=23.5 V=11.9 I=0.42"), RET_, RET_, YIELD_, RET_, END_ };
static const op_t PIMU[]   = { CALL(80), YIELD_, WORK_, YIELD_, WORK_, YIELD_, RET_, END_ };
enum { CTRL = 0, COMMS = 1, IMU = 2 };
static void run_fw(kernel_t *k, int method, bool psplim, bool validate, int32_t comms_stack) {
    k_init(k, method, psplim, validate, 0);
    task_create(k, "ctrl", 4, 512, PCTRL);
    task_create(k, "comms", 2, comms_stack, PCOMMS);
    task_create(k, "imu", 3, 384, PIMU);
    kernel_run(k);
}

/* ---------------- 출력 도우미 ---------------- */
static void outcome(const kernel_t *k, char *buf, size_t n) {
    if (k->hooks) snprintf(buf, n, "hook(%s)@%d", k->t[k->hook_task].name, k->hook_step);
    else if (k->fault) snprintf(buf, n, "%s(%s)@%d", FAULT_NAME[k->fault], k->t[k->fault_task].name, k->fault_step);
    else if (k->finished) {
        int oob = 0; for (int t = 0; t < k->nt; t++) oob += k->t[t].oob;
        snprintf(buf, n, oob ? "finished, SILENT" : "finished");
    }
    else snprintf(buf, n, "step limit");
}
static void hexdump(const kernel_t *k, int32_t from, int32_t len) {
    for (int32_t a = from; a < from + len; a += 16) {
        printf("    %08x:", RAM_BASE + (uint32_t)a);
        for (int32_t i = 0; i < 16 && a + i < from + len; i++) printf(" %02x", k->mem[a + i]);
        printf("  |");
        for (int32_t i = 0; i < 16 && a + i < from + len; i++) { int c = k->mem[a + i]; putchar(c >= 32 && c < 127 ? c : '.'); }
        printf("|\n");
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total, g_verbose;
#define CHECK(e) do { g_total++; if (e) { g_pass++; if (g_verbose) printf("PASS %s\n", #e); } \
                      else printf("FAIL %s (line %d)\n", #e, __LINE__); } while (0)

static uint32_t g_rng = 0x2026111Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rnd(int lo, int hi) { return lo + (int)(xorshift32() % (uint32_t)(hi - lo + 1)); }
static int g_count_hook;
static void counting_hook(kernel_t *k, int t) { (void)t; g_count_hook++; k->hooks++; k->hook_task = t; k->hook_step = k->step; k->halted = true; }

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    static kernel_t k;

    /* ---- Part A: 방법 1·2 기본 동작 ---- */
    run_scen(&k, 0, 2, false);                                   /* S1 정상: 오탐 없음, 끝까지 */
    CHECK(k.finished && k.hooks == 0 && k.fault == F_NONE);
    CHECK(k.t[OI].min_sp - k.t[OI].lo == 156 && k.t[OI].oob == 0);   /* 최저 SP 도 lo 보다 156 B 위 */
    run_scen(&k, 0, 1, false); CHECK(k.finished && k.hooks == 0);
    run_scen(&k, 1, 1, false);                                   /* S2: 바닥에서 YIELD → 방법 1 */
    CHECK(k.hooks == 1 && k.hook_task == OI && k.hook_step == 5);
    CHECK(tcb_get(&k, OI, T_SP) < (uint32_t)k.t[OI].lo);        /* 저장된 SP 가 스택 아래 */
    CHECK(k.t[OI].oob == 100 && k.t[OI].first_oob_step == 4);   /* 탐지(step 5) 전에 이미 남의 메모리 100 B */
    CHECK(tcb_get(&k, VI, T_MAGIC) != TCB_MAGIC);                /* 이웃 V 의 TCB 는 이미 깨짐 */
    run_scen(&k, 2, 1, false);                                   /* S3: 방법 1 은 놓친다 */
    CHECK(k.hooks == 0 && k.fault == F_BADCTX && k.fault_task == VI);
    run_scen(&k, 2, 2, false);                                   /* S3: 방법 2 는 잡는다 (가드가 깨짐) */
    CHECK(k.hooks == 1 && k.hook_task == OI && k.hook_step == 6 && k.t[OI].oob == 144);
    run_scen(&k, 4, 1, false);                                   /* S5: 코드는 안 넘었지만 문맥 push 가 넘김 */
    CHECK(k.hooks == 1 && k.hook_step == 6 && k.t[OI].min_sp == k.t[OI].lo - 12);
    run_scen(&k, 4, 0, false); CHECK(k.t[OI].oob == 12 && k.finished);   /* 검사 없음: 12 B 가 V 의 TCB 를 덮고 조용히 끝 */
    CHECK(tcb_get(&k, VI, T_MAGIC) != TCB_MAGIC);

    /* ---- Part B: 둘 다 놓치는 경우, 타이밍, PSPLIM ---- */
    run_scen(&k, 3, 2, false);                                   /* S4: 바닥만 기록 → 가드 무사 → 둘 다 놓침 */
    CHECK(k.hooks == 0 && k.fault == F_BADCTX && k.fault_task == VI && k.fault_step == 6);
    CHECK(k.t[OI].oob == 7);
    { bool guard_ok = true; for (int i = 0; i < GUARD_BYTES; i++) guard_ok &= k.mem[k.t[OI].lo + i] == FILL_BYTE; CHECK(guard_ok); }
    CHECK(memcmp(k.mem + k.t[OI].hi - 96 - 560, "T=23.5", 7) == 0);   /* 프레임 바닥 = V 의 저장된 문맥 위에 문자열 */
    run_scen(&k, 5, 2, false);                                   /* S6 slice=0: 레코드만 → 둘 다 놓침, V 가 RET 에서 죽음 */
    CHECK(k.hooks == 0 && k.fault == F_BADRET && k.fault_task == VI);
    run_scen(&k, 6, 1, false);                                   /* S6 slice=1: 깊은 곳에서 선점 → 방법 1 이 잡음 */
    CHECK(k.hooks == 1 && k.hook_task == OI);
    for (int s = 0; s < NSCEN; s++) {                            /* PSPLIM: 넘치는 시나리오는 전부 그 자리에서, 쓰기 0 */
        run_scen(&k, s, 0, true);
        bool over = s != 0;
        CHECK(over ? (k.fault == F_STKOF && k.fault_task == OI && k.t[OI].oob == 0) : (k.finished && k.fault == F_NONE));
    }

    /* 무작위 300 세트: 오탐 없음 / 방법2 ⊇ 방법1 / PSPLIM 은 남의 메모리를 한 바이트도 안 쓴다 */
    int bad = 0, m1_hits = 0, m2_hits = 0, both_miss = 0, edge = 0;
    for (int it = 0; it < 300; it++) {
        static op_t prog[MAXOPS];
        int n = 0, depth = 0;
        prog[n++] = (op_t)CALL(rnd(32, 128)); depth = 1;
        while (n < MAXOPS - 4) {
            int c = rnd(0, 5);
            if (c <= 1 && depth < 6) {
                int32_t sz = rnd(2, 40) * 8;
                int w = rnd(0, 2) == 0 ? -1 : rnd(0, 32);
                prog[n++] = (op_t){ OP_CALL, sz, w, 1, NULL }; depth++;
            } else if (c == 2 && depth > 1) { prog[n++] = (op_t)RET_; depth--; }
            else if (c == 3) prog[n++] = (op_t)YIELD_;
            else prog[n++] = (op_t)WORK_;
        }
        while (depth > 0 && n < MAXOPS - 1) { prog[n++] = (op_t)RET_; depth--; }
        prog[n] = (op_t)END_;
        int slice = rnd(0, 3);
        static kernel_t a, b, c2, d;                                        /* a=방법1 b=방법2 c2=PSPLIM d=검사 없음(진실) */
        k_init(&a, 1, false, false, slice); task_create(&a, "V", 1, 512, PV); task_create(&a, "O", 1, 512, prog); kernel_run(&a);
        k_init(&b, 2, false, false, slice); task_create(&b, "V", 1, 512, PV); task_create(&b, "O", 1, 512, prog); kernel_run(&b);
        k_init(&c2, 0, true, false, slice); task_create(&c2, "V", 1, 512, PV); task_create(&c2, "O", 1, 512, prog); kernel_run(&c2);
        k_init(&d, 0, false, false, slice); task_create(&d, "V", 1, 512, PV); task_create(&d, "O", 1, 512, prog); kernel_run(&d);
        bool truth = d.t[OI].min_sp < d.t[OI].lo;
        if (a.hooks && b.hooks && b.hook_step > a.hook_step) bad++;           /* 방법 2 가 더 늦을 수 없다 */
        if (a.hooks && !b.hooks) bad++;                                     /* 방법 2 ⊇ 방법 1 */
        if (a.hooks && a.t[OI].min_sp >= a.t[OI].lo + GUARD_BYTES) bad++;   /* 오탐 없음: 마지막 16 B 까지는 썼다 */
        if (b.hooks && b.t[OI].min_sp >= b.t[OI].lo + GUARD_BYTES) bad++;
        if (c2.t[OI].oob != 0 || c2.t[VI].oob != 0) bad++;                  /* PSPLIM: 남의 메모리 0 바이트 */
        if (truth != (c2.fault == F_STKOF)) bad++;                          /* PSPLIM = 진실과 일치 */
        edge += a.hooks && !truth;                                          /* saved_sp == lo 같은 경계 (넘치진 않음) */
        m1_hits += a.hooks; m2_hits += b.hooks; both_miss += truth && !a.hooks && !b.hooks;
    }
    CHECK(bad == 0);
    CHECK(m2_hits >= m1_hits && both_miss > 0);

    /* ---- Part C: STM32 펌웨어 — comms 가 ctrl 의 TCB 를 덮는다 ---- */
    run_fw(&k, 2, false, false, 512);
    CHECK(k.hooks == 0);                                          /* 방법 2 도 못 봤다 */
    CHECK(k.fault == F_BADCTX && k.fault_task == CTRL);           /* ctrl 로 복귀하다 HardFault — 범인은 comms */
    CHECK(tcb_get(&k, CTRL, T_SP) == 0x54205453u);                /* saved_sp = "ST T" */
    CHECK(tcb_get(&k, CTRL, T_MAGIC) == TCB_MAGIC);               /* 문자열이 짧아 magic 은 무사 → magic 만으론 부족 */
    CHECK(k.t[COMMS].oob == 24 && k.t[COMMS].min_sp == k.t[COMMS].lo - 32);   /* 23 글자 + NUL */
    run_fw(&k, 2, false, true, 512);                              /* tcb_valid: 같은 손상을 switch-in 전에 진단 */
    CHECK(k.fault == F_TCB && k.fault_task == CTRL);
    run_fw(&k, 0, true, false, 512);                              /* PSPLIM: comms 의 CALL 에서 바로 */
    CHECK(k.fault == F_STKOF && k.fault_task == COMMS && k.t[COMMS].oob == 0 && tcb_valid(&k, CTRL));
    run_fw(&k, 2, false, true, 1024);                             /* 고친 것: comms 스택 1 KB */
    CHECK(k.finished && k.hooks == 0 && k.fault == F_NONE && k.t[COMMS].oob == 0);
    for (int t = 0; t < 3; t++) CHECK(tcb_valid(&k, t));
    k_init(&k, 2, false, true, 0);                                /* tcb_valid 단위 검사: 필드 하나씩 깨기 */
    task_create(&k, "ctrl", 4, 512, PCTRL);
    CHECK(tcb_valid(&k, 0));
    tcb_put(&k, 0, T_MAGIC, 0); CHECK(!tcb_valid(&k, 0)); tcb_put(&k, 0, T_MAGIC, TCB_MAGIC);
    tcb_put(&k, 0, T_SP, (uint32_t)k.t[0].lo); CHECK(!tcb_valid(&k, 0));
    tcb_put(&k, 0, T_SP, (uint32_t)k.t[0].hi - CTX_BYTES + 4); CHECK(!tcb_valid(&k, 0));
    tcb_put(&k, 0, T_SP, (uint32_t)k.t[0].hi - CTX_BYTES); CHECK(tcb_valid(&k, 0));
    memset(k.mem + k.t[0].tcb + T_NAME, 'x', T_NAMELEN); CHECK(!tcb_valid(&k, 0));
    /* 훅 교체 (configASSERT 처럼): 세는 훅으로도 같은 결과 */
    k_init(&k, 2, false, false, 0); k.overflow_hook = counting_hook;
    task_create(&k, "V", 1, 512, PV); task_create(&k, "O", 1, 512, S3); kernel_run(&k);
    CHECK(g_count_hook == 1 && k.hook_task == OI);

    /* ---- 표 ---- */
    printf("Part A/B  scenario                min_sp-lo  oob | method 1              method 2              PSPLIM\n");
    for (int s = 0; s < NSCEN; s++) {
        char o1[48], o2[48], o3[48];
        run_scen(&k, s, 1, false); outcome(&k, o1, sizeof o1);
        int32_t ov = k.t[OI].min_sp - k.t[OI].lo;
        run_scen(&k, s, 2, false); outcome(&k, o2, sizeof o2);
        run_scen(&k, s, 0, false); int oob = k.t[OI].oob;
        run_scen(&k, s, 0, true);  outcome(&k, o3, sizeof o3);
        printf("          %-24s %6d  %5d | %-21s %-21s %s\n", SCEN[s].name, (int)ov, oob, o1, o2, o3);
    }
    printf("random 300 sets: method1 hooks=%d, method2 hooks=%d, overflowed but both missed=%d, hooks without overflow=%d, violations=%d\n",
           m1_hits, m2_hits, both_miss, edge, bad);
    printf("Part C  config                      outcome\n");
    const char *cn[] = { "method 2", "method 2 + tcb_valid", "PSPLIM", "method 2, comms 1 KB" };
    for (int c = 0; c < 4; c++) {
        char o[48];
        run_fw(&k, c == 2 ? 0 : 2, c == 2, c == 1 || c == 3, c == 3 ? 1024 : 512); outcome(&k, o, sizeof o);
        printf("        %-27s %s   (comms oob=%d B)\n", cn[c], o, k.t[COMMS].oob);
    }
    if (g_verbose) {
        run_fw(&k, 2, false, false, 512);
        printf("Part C, method 2 — arena map\n");
        for (int t = 0; t < k.nt; t++)
            printf("    %-5s stack %08x..%08x  TCB %08x..%08x\n", k.t[t].name, RAM_BASE + (uint32_t)k.t[t].lo,
                   RAM_BASE + (uint32_t)k.t[t].hi - 1, RAM_BASE + (uint32_t)k.t[t].tcb, RAM_BASE + (uint32_t)k.t[t].tcb + TCB_BYTES - 1);
        printf("  ctrl TCB + comms guard after the run:\n");
        hexdump(&k, k.t[CTRL].tcb, TCB_BYTES + GUARD_BYTES);
        printf("  event log:\n%s", k.log);
        for (int s = 0; s < NSCEN; s++) {
            run_scen(&k, s, 2, false);
            printf("%s (method 2) — V TCB / O guard:\n", SCEN[s].name);
            if (s == 3) hexdump(&k, k.t[VI].tcb - 128, 128 + TCB_BYTES + GUARD_BYTES);
            else hexdump(&k, k.t[VI].tcb, TCB_BYTES + GUARD_BYTES);
            printf("%s", k.log);
        }
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
