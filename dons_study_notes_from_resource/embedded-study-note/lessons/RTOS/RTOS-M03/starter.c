/* RTOS-M03 우선순위 역전 재현: Mars Pathfinder 시나리오
 *
 * 호스트 모델: OS 스레드 하나에서 도는 tick 구동 미니 커널 (결정적 — 매번 같은 타임라인).
 *  - 태스크 = "명령 스크립트" (WORK n / TAKE s / GIVE s / PRIO p / END). 1 tick = WORK 1단위.
 *  - TAKE/GIVE/PRIO 는 커널 호출이라 시간 0 (실제 칩에서도 µs 단위, tick 은 ms 단위).
 *  - 선점(preemption): 매 tick 커널은 READY 중 우선순위가 가장 높은 태스크를 돌린다.
 *    같은 우선순위는 READY 가 된 순서(FIFO). 같은 우선순위끼리 시분할(time slicing)은 없다.
 *  - 세마포어는 "일반" 세마포어: 카운트와 대기 리스트만 있고 소유자(owner)가 없다
 *    → 커널은 누가 R 을 쥐고 있는지 모르므로 우선순위 상속을 할 수 없다 (RTOS-M04 의 출발점).
 *  - ISR 대역 = 하네스가 매 tick 시작에 부르는 함수 (g_isr). ISR 의 give 는 절대 막히지 않는다.
 * 우선순위 숫자는 클수록 높다 (FreeRTOS 방식). H=3, M=2, L=1.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { MAX_TASKS = 6, MAX_OPS = 12, MAX_TICKS = 64, MAX_SEMS = 3, MAX_CM = 40 };

typedef enum { OP_WORK, OP_TAKE, OP_GIVE, OP_PRIO, OP_END } op_kind_t;
typedef struct { op_kind_t k; int arg; } op_t;
typedef enum { ST_NEW, ST_READY, ST_BLOCKED, ST_DONE } state_t;

typedef struct {
    char     name;
    int      base_prio, prio;     /* base = 설계 우선순위, prio = 지금 우선순위 (PRIO 로 바뀜) */
    int      release;             /* 이 tick 시작에 READY 가 된다                          */
    op_t     ops[MAX_OPS];
    int      nops, pc, left;      /* left: 진행 중인 WORK 의 남은 tick (0 = 시작 전/끝남)  */
    state_t  st;
    unsigned ready_seq;           /* READY 가 된 순서 → 같은 우선순위 FIFO                 */
    int      wait_sem;            /* BLOCKED 일 때 기다리는 세마포어 (-1 = 없음)            */
    unsigned wait_seq;            /* 대기 리스트에 들어간 순서                              */
    int      finish;              /* 끝난 시각 (마지막 WORK tick + 1), -1 = 아직            */
} task_t;

typedef struct { int count, max; } ksem_t;   /* owner 필드 없음 — 이것이 "일반" 세마포어 */

static task_t   g_task[MAX_TASKS];
static int      g_ntask;
static ksem_t   g_sem[MAX_SEMS];
static int      g_tick;
static unsigned g_seq;
static char     g_row[MAX_TASKS][MAX_TICKS + 1];  /* R=실행, B=세마포어 대기, r=준비됐지만 못 돎, .=없음 */
static int      g_run[MAX_TICKS];                 /* tick 별 실행한 태스크 (-1 = idle)             */
static void   (*g_isr)(int tick);                 /* ISR 대역 (NULL = 없음)                         */

typedef enum { RUN_WORK, RUN_BLOCKED, RUN_DONE, RUN_PREEMPTED } run_t;

/* ---------------- 커널 바탕 (주어진 코드) ---------------- */
static void kernel_reset(void) {
    memset(g_task, 0, sizeof g_task);
    memset(g_sem, 0, sizeof g_sem);
    memset(g_row, 0, sizeof g_row);
    for (int t = 0; t < MAX_TICKS; t++) g_run[t] = -1;
    g_ntask = 0; g_tick = 0; g_seq = 0; g_isr = NULL;
}
static int task_new(char name, int prio, int release) {
    task_t *t = &g_task[g_ntask];
    t->name = name; t->base_prio = t->prio = prio; t->release = release;
    t->st = ST_NEW; t->wait_sem = -1; t->finish = -1;
    return g_ntask++;
}
static void op(int ti, op_kind_t k, int arg) {
    task_t *t = &g_task[ti];
    if (t->nops < MAX_OPS) t->ops[t->nops++] = (op_t){ k, arg };
}
static void make_ready(int i) { g_task[i].st = ST_READY; g_task[i].ready_seq = ++g_seq; }

/* ---------------- Part A: 스케줄러 · 일반 세마포어 · 디스패치 ---------------- */
/* READY 중 prio 가 가장 높은 태스크, 같으면 ready_seq 가 작은(먼저 READY 된) 태스크. 없으면 -1. */
static int pick_next(void) {
    return -1; /* TODO */
}

/* 태스크 i 가 세마포어 s 를 take. 카운트가 있으면 하나 줄이고 true.
 * 없으면 i 를 BLOCKED 로 만들고 대기 리스트에 넣은 뒤(wait_sem, wait_seq) false. */
static bool ksem_take(int i, int s) {
    (void)i; (void)s;
    return false; /* TODO */
}

/* 세마포어 s 를 give (태스크든 ISR 이든 누구나 — 소유자 검사 없음, 절대 막히지 않음).
 * 대기자가 있으면 prio 가 가장 높은(같으면 먼저 기다린) 대기자에게 직접 넘긴다:
 * 그 태스크의 TAKE 를 완료 처리(pc++)하고 READY 로. 깨운 태스크 번호를 반환.
 * 대기자가 없으면 count++ (max 에서 포화) 하고 -1. */
static int ksem_give(int s) {
    (void)s;
    return -1; /* TODO */
}

/* 태스크 i 의 시간 0 명령(TAKE/GIVE/PRIO/END, WORK 0)을 WORK 를 만날 때까지 실행한다.
 * now = 지금 시각 (END 이면 finish = now).
 * may_preempt 이면 GIVE/PRIO 직후 pick_next() != i 일 때 RUN_PREEMPTED (i 는 READY 로 남는다). */
static run_t run_ops(int i, bool may_preempt, int now) {
    (void)i; (void)may_preempt; (void)now;
    return RUN_DONE; /* TODO */
}

/* 한 tick 의 디스패치: 가장 높은 READY 태스크를 골라 run_ops(…, true, g_tick).
 * 막힘/끝/선점이면 같은 tick 안에서 다시 고른다. RUN_WORK 이면 WORK 1단위를 소비하고
 * (left 가 0 이면 ops[pc].arg 로 시작, 0 이 되면 pc++), 그 태스크 번호를 반환. 아무도 없으면 -1.
 * 재선택 횟수에는 상한을 둔다 (잘못된 구현이 무한 루프에 빠지지 않게). */
static int dispatch(void) {
    return -1; /* TODO */
}

/* 1 tick 진행 (주어진 코드): 릴리스 → ISR → 디스패치 → 타임라인 기록 → 끝난 WORK 뒤의 시간 0 명령. */
static void run_tick(void) {
    for (int i = 0; i < g_ntask; i++)
        if (g_task[i].st == ST_NEW && g_task[i].release == g_tick) make_ready(i);
    if (g_isr) g_isr(g_tick);
    int ran = dispatch();
    g_run[g_tick] = ran;
    for (int i = 0; i < g_ntask; i++) {
        state_t s = g_task[i].st;
        g_row[i][g_tick] = i == ran ? 'R' : s == ST_READY ? 'r' : s == ST_BLOCKED ? 'B' : '.';
    }
    if (ran >= 0 && g_task[ran].left == 0 && g_task[ran].st == ST_READY)
        (void)run_ops(ran, false, g_tick + 1);         /* tick 끝(= 다음 시각)에 실행 */
    g_tick++;
}
static bool all_done(void) {
    for (int i = 0; i < g_ntask; i++) if (g_task[i].st != ST_DONE) return false;
    return true;
}
static void run_until_done(void) {
    while (g_tick < MAX_TICKS && !all_done()) run_tick();
}
static void print_timeline(const char *title) {
    printf("%s\n", title);
    if (g_tick > 10) {
        printf("       ");
        for (int t = 0; t < g_tick; t++) printf("%c ", t >= 10 ? (char)('0' + t / 10) : ' ');
        printf("\n");
    }
    printf("tick : ");
    for (int t = 0; t < g_tick; t++) printf("%d ", t % 10);
    printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%c    : ", g_task[i].name);
        for (int t = 0; t < g_tick; t++) printf("%c ", g_row[i][t]);
        printf("\n");
    }
}

/* ---------------- 정준 역전 시나리오 (M03 정의 — M04/M05 가 같은 숫자로 재사용) ----------------
 * H=3, M=2, L=1. L: t=0 릴리스, WORK 1 뒤(t=1 시각) R 을 잡고 4 tick 작업, give, 꼬리 WORK 1.
 * H: t=2 릴리스, 첫 단계에서 R 요청, WORK 2, give.  M: t=3 릴리스, R 없이 CPU 작업 C_M tick.
 * boost = Part C 미리보기: L 이 R 을 잡기 전 스스로 prio 3 으로 올리고 give 뒤 1 로 내린다
 *         (vTaskPrioritySet 수동 부스트 — 커널 프로토콜이 아니다. 자동화 = M04 PI / M05 IPCP). */
enum { SEM_R = 0, SEM_EV = 1 };
enum { TH = 0, TM = 1, TL = 2 };
enum { L_CS = 4, H_WORK = 2 };

static void build_L(bool boost) {
    int l = task_new('L', 1, 0);
    op(l, OP_WORK, 1);
    if (boost) op(l, OP_PRIO, 3);
    op(l, OP_TAKE, SEM_R); op(l, OP_WORK, L_CS); op(l, OP_GIVE, SEM_R);
    if (boost) op(l, OP_PRIO, 1);
    op(l, OP_WORK, 1); op(l, OP_END, 0);
}
static void build_canonical(int cm, bool boost) {
    kernel_reset();
    g_sem[SEM_R] = (ksem_t){ 1, 1 };
    int h = task_new('H', 3, 2);
    op(h, OP_TAKE, SEM_R); op(h, OP_WORK, H_WORK); op(h, OP_GIVE, SEM_R); op(h, OP_END, 0);
    int m = task_new('M', 2, 3);
    op(m, OP_WORK, cm); op(m, OP_END, 0);
    build_L(boost);
}

/* ---------------- Part B: 타임라인에서 H 의 대기를 분해 ---------------- */
typedef struct { int wait, blocked, by_holder, by_lower, by_higher, idle; } hstats_t;

/* g_row[h] 에서 H 가 기다린 tick ('B' 또는 'r') 을 세고, 그 tick 에 누가 돌았는지(g_run)로 분해:
 * by_holder = 자원 보유자(holder)가 돎 (피할 수 없는 대기),
 * by_lower  = holder 가 아니고 base_prio 가 H 보다 낮은 태스크가 돎 (= unbounded 역전),
 * by_higher = base_prio 가 H 이상인 태스크가 돎 (정상 선점), idle = 아무도 안 돎. */
static hstats_t h_stats(int h, int holder) {
    hstats_t s = { 0, 0, 0, 0, 0, 0 };
    (void)h; (void)holder; /* TODO */
    return s;
}
static int response(int i) { return g_task[i].finish < 0 ? -1 : g_task[i].finish - g_task[i].release; }

/* ---------------- Part C: Pathfinder-lite (bc_dist / comm / ASI-MET + 워치독) ----------------
 * bc_dist(H, 3): t=2 릴리스(버스 주기 시작), 정보 버스 파이프 세마포어 R 을 잡고 2 tick, 다음 주기
 *                시작(DEADLINE=10) 전에 끝나야 한다. bc_sched 가 주기 시작에 확인 → 못 끝났으면 리셋.
 * comm(M, 2):    t=0 부터 이벤트 세마포어 EV 를 기다린다. 무선 ISR 이 t=3 에 EV 를 give → C_M tick.
 * ASI/MET(L, 1): 정준 시나리오의 L 과 같다 (select() 안에서 R 을 잡은 채 선점됨). */
enum { DEADLINE = 10, RADIO_IRQ_TICK = 3 };

/* 무선 수신 ISR 대역: RADIO_IRQ_TICK 에서 EV 를 give (xSemaphoreGiveFromISR 상당). */
static void radio_isr(int tick) {
    (void)tick; /* TODO */
}
static void build_pathfinder(int cm, bool boost) {
    kernel_reset();
    g_sem[SEM_R]  = (ksem_t){ 1, 1 };
    g_sem[SEM_EV] = (ksem_t){ 0, 1 };
    int h = task_new('H', 3, 2);
    op(h, OP_TAKE, SEM_R); op(h, OP_WORK, H_WORK); op(h, OP_GIVE, SEM_R); op(h, OP_END, 0);
    int m = task_new('M', 2, 0);
    op(m, OP_TAKE, SEM_EV); op(m, OP_WORK, cm); op(m, OP_END, 0);
    build_L(boost);
    g_isr = radio_isr;
}
/* bc_sched 의 검사: deadline 시각까지 태스크 h 가 끝났으면(finish <= deadline) -1,
 * 아니면 리셋이 일어나는 시각 deadline 을 반환. */
static int watchdog_check(int h, int deadline) {
    (void)h; (void)deadline;
    return -1; /* TODO */
}
/* C_M = 0..MAX_CM 을 차례로 돌려(build_pathfinder → run_until_done → watchdog_check)
 * 리셋이 안 나는 가장 큰 C_M 을 반환 (C_M=0 부터 리셋이면 -1, 끝까지 안 나면 MAX_CM). */
static int max_safe_cm(int deadline, bool boost) {
    (void)deadline; (void)boost;
    return 0; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static bool row_is(int i, const char *s) {        /* "..BBB" 처럼 공백 없이 비교 */
    return strlen(s) == (size_t)g_tick && memcmp(g_row[i], s, (size_t)g_tick) == 0;
}

int main(void) {
    /* Part A-1: pick_next — 최고 우선순위, 같으면 FIFO */
    kernel_reset();
    for (int p = 0; p < 4; p++) (void)task_new((char)('a' + p), (int[]){ 1, 3, 3, 2 }[p], 0);
    CHECK(pick_next() == -1);                        /* 아무도 READY 아님 → idle */
    make_ready(2); make_ready(0); make_ready(1); make_ready(3);
    CHECK(pick_next() == 2);                         /* prio 3 둘 중 먼저 READY 된 c */
    g_task[2].st = ST_BLOCKED;
    CHECK(pick_next() == 1);
    g_task[1].st = ST_DONE;
    CHECK(pick_next() == 3);

    /* Part A-2: 일반 세마포어 take/give, 대기 리스트 순서, 포화, ISR give */
    kernel_reset();
    g_sem[0] = (ksem_t){ 1, 1 };
    for (int p = 0; p < 4; p++) { (void)task_new((char)('a' + p), (int[]){ 1, 2, 2, 3 }[p], 0); make_ready(p); }
    CHECK(ksem_take(3, 0) == true && g_sem[0].count == 0);
    CHECK(ksem_take(0, 0) == false && g_task[0].st == ST_BLOCKED && g_task[0].wait_sem == 0);
    CHECK(ksem_take(1, 0) == false && ksem_take(2, 0) == false);
    CHECK(ksem_give(0) == 1 && g_task[1].st == ST_READY && g_sem[0].count == 0);  /* prio 2 중 먼저 */
    CHECK(ksem_give(0) == 2);
    CHECK(ksem_give(0) == 0 && g_task[0].wait_sem == -1);                        /* 마지막 prio 1 */
    CHECK(ksem_give(0) == -1 && g_sem[0].count == 1);
    CHECK(ksem_give(0) == -1 && g_sem[0].count == 1);      /* 이진 세마포어: max 1 에서 포화 */

    /* Part A-3: 정준 시나리오 타임라인 (C_M = 0, 4) */
    build_canonical(0, false);
    run_until_done();
    CHECK(g_tick == 8);
    CHECK(row_is(TH, "..BBBRR."));
    CHECK(row_is(TM, "........"));
    CHECK(row_is(TL, "RRRRRrrR"));
    build_canonical(4, false);
    run_until_done();
    print_timeline("canonical, plain semaphore, C_M=4");
    CHECK(g_tick == 12);
    CHECK(row_is(TH, "..BBBBBBBRR."));
    CHECK(row_is(TM, "...RRRR....."));
    CHECK(row_is(TL, "RRRrrrrRRrrR"));
    CHECK(g_task[TH].finish == 11 && g_task[TM].finish == 7 && g_task[TL].finish == 12);

    /* Part B: H 의 대기 = L 의 남은 임계구역 3 + C_M  (C_M 에 비례 — 상한 없음) */
    printf("\nC_M | H wait | blocked | by_holder(L) | by_lower(M) | H response\n");
    int cms[] = { 0, 2, 4, 8, 16 }, wait[5];
    for (int k = 0; k < 5; k++) {
        build_canonical(cms[k], false);
        run_until_done();
        hstats_t s = h_stats(TH, TL);
        wait[k] = s.wait;
        printf("%3d | %6d | %7d | %12d | %11d | %10d\n",
               cms[k], s.wait, s.blocked, s.by_holder, s.by_lower, response(TH));
        CHECK(s.wait == 3 + cms[k] && s.blocked == 3 + cms[k]);
        CHECK(s.by_holder == 3 && s.by_lower == cms[k] && s.by_higher == 0 && s.idle == 0);
        CHECK(response(TH) == 5 + cms[k]);
    }
    build_canonical(16, false);
    run_until_done();
    print_timeline("\ncanonical, plain semaphore, C_M=16");
    CHECK(wait[4] - wait[0] == 16 && wait[2] - wait[1] == 2);   /* 기울기 1: M 1 tick = H 1 tick */

    /* Part C: Pathfinder-lite — 워치독 리셋, 그리고 수동 부스트 미리보기 */
    build_pathfinder(2, false);
    run_until_done();
    CHECK(row_is(TM, "BBBRR....."));    /* ISR 이 t=3 에 comm 을 깨움 */
    CHECK(g_task[TH].finish == 9 && watchdog_check(TH, DEADLINE) == -1);
    build_pathfinder(4, false);
    run_until_done();
    print_timeline("\npathfinder-lite, plain semaphore, C_M=4 (deadline t=10)");
    CHECK(g_task[TH].finish == 11 && watchdog_check(TH, DEADLINE) == DEADLINE);   /* 리셋! */
    CHECK(max_safe_cm(DEADLINE, false) == 3);
    CHECK(max_safe_cm(8, false) == 1 && max_safe_cm(6, false) == -1);

    printf("\nC_M | plain: H wait  reset | boost: H wait  blocked  reset\n");
    for (int k = 0; k < 5; k++) {
        build_pathfinder(cms[k], false);
        run_until_done();
        hstats_t p = h_stats(TH, TL);
        int rp = watchdog_check(TH, DEADLINE);
        build_pathfinder(cms[k], true);
        run_until_done();
        hstats_t b = h_stats(TH, TL);
        int rb = watchdog_check(TH, DEADLINE);
        printf("%3d | %12d  %5s | %12d  %7d  %5s\n", cms[k], p.wait, rp < 0 ? "no" : "YES",
               b.wait, b.blocked, rb < 0 ? "no" : "YES");
        CHECK(b.wait == 3 && b.by_holder == 3 && b.blocked == 0 && rb == -1);
    }
    build_pathfinder(4, true);
    run_until_done();
    print_timeline("\npathfinder-lite, L boosted to 3 by hand, C_M=4");
    CHECK(row_is(TH, "..rrrRR....."));
    CHECK(max_safe_cm(DEADLINE, true) == MAX_CM);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
