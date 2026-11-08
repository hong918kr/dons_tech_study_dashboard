/* RTOS-M03 추가 연습 — 한 번에 검증 (커널은 solution.c 와 같고, 연습 3 용 OP_LOCK/OP_UNLOCK 만 추가)
 *
 * 원래 머리말:
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

typedef enum { OP_WORK, OP_TAKE, OP_GIVE, OP_PRIO, OP_END, OP_LOCK, OP_UNLOCK } op_kind_t;
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
static int      g_lock_owner = -1;                /* 연습 3: 스케줄러 잠금 소유자 (vTaskSuspendAll) */

typedef enum { RUN_WORK, RUN_BLOCKED, RUN_DONE, RUN_PREEMPTED } run_t;

/* ---------------- 커널 바탕 (주어진 코드) ---------------- */
static void kernel_reset(void) {
    memset(g_task, 0, sizeof g_task);
    memset(g_sem, 0, sizeof g_sem);
    memset(g_row, 0, sizeof g_row);
    for (int t = 0; t < MAX_TICKS; t++) g_run[t] = -1;
    g_ntask = 0; g_tick = 0; g_seq = 0; g_isr = NULL; g_lock_owner = -1;
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
    int best = -1;
    /* 연습 3: 스케줄러가 잠겨 있으면 잠근 태스크만 돈다 (READY 인 동안) */
    if (g_lock_owner >= 0 && g_task[g_lock_owner].st == ST_READY) return g_lock_owner;
    for (int i = 0; i < g_ntask; i++) {
        const task_t *t = &g_task[i];
        if (t->st != ST_READY) continue;
        if (best < 0 || t->prio > g_task[best].prio ||
            (t->prio == g_task[best].prio && t->ready_seq < g_task[best].ready_seq))
            best = i;
    }
    return best;
}

/* 태스크 i 가 세마포어 s 를 take. 카운트가 있으면 하나 줄이고 true.
 * 없으면 i 를 BLOCKED 로 만들고 대기 리스트에 넣은 뒤(wait_sem, wait_seq) false. */
static bool ksem_take(int i, int s) {
    if (g_sem[s].count > 0) { g_sem[s].count--; return true; }
    g_task[i].st = ST_BLOCKED;
    g_task[i].wait_sem = s;
    g_task[i].wait_seq = ++g_seq;
    return false;
}

/* 세마포어 s 를 give (태스크든 ISR 이든 누구나 — 소유자 검사 없음, 절대 막히지 않음).
 * 대기자가 있으면 prio 가 가장 높은(같으면 먼저 기다린) 대기자에게 직접 넘긴다:
 * 그 태스크의 TAKE 를 완료 처리(pc++)하고 READY 로. 깨운 태스크 번호를 반환.
 * 대기자가 없으면 count++ (max 에서 포화) 하고 -1. */
static int ksem_give(int s) {
    int w = -1;
    for (int i = 0; i < g_ntask; i++) {
        const task_t *t = &g_task[i];
        if (t->st != ST_BLOCKED || t->wait_sem != s) continue;
        if (w < 0 || t->prio > g_task[w].prio ||
            (t->prio == g_task[w].prio && t->wait_seq < g_task[w].wait_seq))
            w = i;
    }
    if (w >= 0) {
        g_task[w].pc++;                /* 기다리던 TAKE 가 이제 성공한 것으로 */
        g_task[w].wait_sem = -1;
        make_ready(w);
        return w;
    }
    if (g_sem[s].count < g_sem[s].max) g_sem[s].count++;
    return -1;
}

/* 태스크 i 의 시간 0 명령(TAKE/GIVE/PRIO/END, WORK 0)을 WORK 를 만날 때까지 실행한다.
 * now = 지금 시각 (END 이면 finish = now).
 * may_preempt 이면 GIVE/PRIO 직후 pick_next() != i 일 때 RUN_PREEMPTED (i 는 READY 로 남는다). */
static run_t run_ops(int i, bool may_preempt, int now) {
    task_t *t = &g_task[i];
    for (int guard = 0; guard < MAX_OPS + 1; guard++) {
        if (t->pc >= t->nops) { t->st = ST_DONE; t->finish = now; return RUN_DONE; }
        op_t o = t->ops[t->pc];
        switch (o.k) {
        case OP_WORK:
            if (o.arg > 0) return RUN_WORK;
            t->pc++;                                   /* WORK 0 은 건너뛴다 */
            break;
        case OP_TAKE:
            if (!ksem_take(i, o.arg)) return RUN_BLOCKED;
            t->pc++;
            break;
        case OP_GIVE:
            t->pc++;
            (void)ksem_give(o.arg);
            if (may_preempt && pick_next() != i) return RUN_PREEMPTED;
            break;
        case OP_PRIO:
            t->pc++;
            t->prio = o.arg;
            if (may_preempt && pick_next() != i) return RUN_PREEMPTED;
            break;
        case OP_END:
            t->st = ST_DONE; t->finish = now;
            return RUN_DONE;
        case OP_LOCK:   t->pc++; g_lock_owner = i; break;
        case OP_UNLOCK:
            t->pc++; g_lock_owner = -1;
            if (may_preempt && pick_next() != i) return RUN_PREEMPTED;
            break;
        }
    }
    t->st = ST_DONE; t->finish = now;
    return RUN_DONE;
}

/* 한 tick 의 디스패치: 가장 높은 READY 태스크를 골라 run_ops(…, true, g_tick).
 * 막힘/끝/선점이면 같은 tick 안에서 다시 고른다. RUN_WORK 이면 WORK 1단위를 소비하고
 * (left 가 0 이면 ops[pc].arg 로 시작, 0 이 되면 pc++), 그 태스크 번호를 반환. 아무도 없으면 -1.
 * 재선택 횟수에는 상한을 둔다 (잘못된 구현이 무한 루프에 빠지지 않게). */
static int dispatch(void) {
    for (int guard = 0; guard < 4 * MAX_TASKS; guard++) {
        int i = pick_next();
        if (i < 0) return -1;
        if (run_ops(i, true, g_tick) != RUN_WORK) continue;
        task_t *t = &g_task[i];
        if (t->left == 0) t->left = t->ops[t->pc].arg;
        if (--t->left == 0) t->pc++;
        return i;
    }
    return -1;
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

/* ---------------- 정준 시나리오 (변형 가능한 판) ---------------- */
enum { SEM_R = 0, SEM_EV = 1 };
enum { TH = 0, TM = 1, TL = 2 };
enum { L_CS = 4, H_WORK = 2, DEADLINE = 10, RADIO_IRQ_TICK = 3 };
typedef enum { FIX_NONE, FIX_BOOST, FIX_LOCK } fix_t;

static void build_H(int release) {
    int h = task_new('H', 3, release);
    op(h, OP_TAKE, SEM_R); op(h, OP_WORK, H_WORK); op(h, OP_GIVE, SEM_R); op(h, OP_END, 0);
}
static void build_L(fix_t fix) {
    int l = task_new('L', 1, 0);
    op(l, OP_WORK, 1);
    if (fix == FIX_BOOST) op(l, OP_PRIO, 3);
    if (fix == FIX_LOCK)  op(l, OP_LOCK, 0);
    op(l, OP_TAKE, SEM_R); op(l, OP_WORK, L_CS); op(l, OP_GIVE, SEM_R);
    if (fix == FIX_BOOST) op(l, OP_PRIO, 1);
    if (fix == FIX_LOCK)  op(l, OP_UNLOCK, 0);
    op(l, OP_WORK, 1); op(l, OP_END, 0);
}
static void build_M(char name, int prio, int release, int cm) {
    int m = task_new(name, prio, release);
    op(m, OP_WORK, cm); op(m, OP_END, 0);
}
/* H(0) M(1) L(2) 순서는 solution.c 와 같다. 추가 태스크는 3번부터. */
static void build(int h_release, int cm, fix_t fix) {
    kernel_reset();
    g_sem[SEM_R] = (ksem_t){ 1, 1 };
    build_H(h_release);
    build_M('M', 2, 3, cm);
    build_L(fix);
}

typedef struct { int wait, blocked, by_holder, by_lower, by_higher, idle; } hstats_t;
static hstats_t h_stats(int h, int holder) {
    hstats_t s = { 0, 0, 0, 0, 0, 0 };
    for (int t = 0; t < g_tick; t++) {
        char c = g_row[h][t];
        if (c != 'B' && c != 'r') continue;
        s.wait++;
        if (c == 'B') s.blocked++;
        int r = g_run[t];
        if (r < 0) s.idle++;
        else if (r == holder) s.by_holder++;
        else if (g_task[r].base_prio >= g_task[h].base_prio) s.by_higher++;
        else s.by_lower++;
    }
    return s;
}
static int wait_of(int i) {               /* 아무 태스크의 대기 tick ('B' + 'r') */
    int n = 0;
    for (int t = 0; t < g_tick; t++) n += g_row[i][t] == 'B' || g_row[i][t] == 'r';
    return n;
}

/* Pathfinder-lite (solution.c Part C 와 같음) */
static void radio_isr(int tick) { if (tick == RADIO_IRQ_TICK) (void)ksem_give(SEM_EV); }
static void build_pathfinder(int cm) {
    kernel_reset();
    g_sem[SEM_R] = (ksem_t){ 1, 1 };
    g_sem[SEM_EV] = (ksem_t){ 0, 1 };
    build_H(2);
    int m = task_new('M', 2, 0);
    op(m, OP_TAKE, SEM_EV); op(m, OP_WORK, cm); op(m, OP_END, 0);
    build_L(FIX_NONE);
    g_isr = radio_isr;
}
static int max_safe_cm(int deadline) {
    for (int cm = 0; cm <= MAX_CM; cm++) {
        build_pathfinder(cm);
        run_until_done();
        if (!(g_task[TH].finish >= 0 && g_task[TH].finish <= deadline)) return cm - 1;
    }
    return MAX_CM;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    hstats_t s;

    /* 연습 1: Medium 이 여럿 — M1(C=3, t=3), M2(C=5, t=4). 대기 = 3 + 3 + 5 */
    kernel_reset();
    g_sem[SEM_R] = (ksem_t){ 1, 1 };
    build_H(2); build_M('1', 2, 3, 3); build_L(FIX_NONE); build_M('2', 2, 4, 5);
    run_until_done();
    print_timeline("ex1: two medium tasks (C=3, C=5)");
    s = h_stats(TH, TL);
    CHECK(s.wait == 11 && s.by_holder == 3 && s.by_lower == 8 && s.by_higher == 0);

    /* 연습 2: H 보다 높은 X(prio 4, t=3, C=2) 는 역전이 아니라 정상 선점 */
    kernel_reset();
    g_sem[SEM_R] = (ksem_t){ 1, 1 };
    build_H(2); build_M('M', 2, 3, 0); build_L(FIX_NONE); build_M('X', 4, 3, 2);
    run_until_done();
    print_timeline("\nex2: higher-priority X (prio 4, C=2)");
    s = h_stats(TH, TL);
    CHECK(s.wait == 5 && s.by_holder == 3 && s.by_higher == 2 && s.by_lower == 0);

    /* 연습 3: 스케줄러 잠금 vs 수동 부스트 — R 과 무관한 급한 U(prio 4, t=2, C=1) 의 대가 */
    int u_wait[2], h_wait[2];
    for (int f = 0; f < 2; f++) {
        build(2, 4, f == 0 ? FIX_LOCK : FIX_BOOST);
        build_M('U', 4, 2, 1);
        run_until_done();
        print_timeline(f == 0 ? "\nex3: scheduler lock around CS (C_M=4)" : "\nex3: manual boost to 3 (C_M=4)");
        u_wait[f] = wait_of(3);
        h_wait[f] = h_stats(TH, TL).wait;
    }
    CHECK(u_wait[0] == 3 && u_wait[1] == 0);         /* 잠금은 무관한 U 까지 막는다 */
    CHECK(h_wait[0] == 4 && h_wait[1] == 4);         /* H 쪽은 둘 다 상한 있음: CS 3 + U 1 */
    for (int cm = 0; cm <= 16; cm += 4) {            /* 잠금도 C_M 과 무관하게 H 를 지킨다 */
        build(2, cm, FIX_LOCK);
        run_until_done();
        CHECK(h_stats(TH, TL).wait == 3);
    }

    /* 연습 4: 워치독 마감 D 에 대한 닫힌 식 — 안전한 최대 C_M = D - 7 (D < 7 이면 -1) */
    int bad = 0;
    for (int d = 3; d <= 30; d++) {
        int want = d >= 7 ? d - 7 : -1;
        if (max_safe_cm(d) != want) { bad++; printf("D=%d got %d want %d\n", d, max_safe_cm(d), want); }
    }
    CHECK(bad == 0);

    /* 연습 5: H 가 도착하는 시각 r_H = 1..9 (C_M=4) → 대기 = 9 - r_H  (R 이 풀리는 시각 9 고정) */
    printf("\nex5: r_H | H blocked\n");
    bad = 0;
    for (int r = 1; r <= 9; r++) {
        build(r, 4, FIX_NONE);
        run_until_done();
        s = h_stats(TH, TL);
        printf("     %3d | %d\n", r, s.blocked);
        if (s.blocked != 9 - r || s.wait != 9 - r) bad++;
    }
    CHECK(bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
