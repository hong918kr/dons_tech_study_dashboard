/* RTOS-M04 추가 연습 — 한 번에 검증
 *
 * solution.c 의 미니 커널(상속 + 연쇄 + RESTORE_EXACT 완성본)을 그대로 복사하고 다음을 더했다:
 *  - op LOCK_TO(m, n): 최대 n tick 만 기다리는 lock (타임아웃되면 그냥 다음 op 로)          — 연습 2
 *  - k->deadlock_check: lock 하기 전에 소유자 사슬을 따라가 자기 자신이 나오면 EDEADLK      — 연습 3
 *  - k->max_hops: 연쇄 상속 최대 홉 수 (Zephyr MUTEX_CHAIN_WALK_MAX_HOPS, Linux max_lock_depth) — 연습 4
 * 연습 1 은 커널 변경 없이 측정만 한다 (chained blocking).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXT    8
#define MAXM    4
#define MAXOPS  16
#define MAXTICK 64

typedef enum { OP_END, OP_WORK, OP_LOCK, OP_UNLOCK } opk_t;
typedef struct { opk_t k; int a; int to; } op_t;   /* to = 타임아웃 tick (0 = 무한) */
#define WORK(n)      { OP_WORK, (n), 0 }
#define LOCK(m)      { OP_LOCK, (m), 0 }
#define LOCK_TO(m,n) { OP_LOCK, (m), (n) }
#define UNLOCK(m)    { OP_UNLOCK, (m), 0 }
#define END_         { OP_END, 0, 0 }

typedef enum { T_NEW, T_READY, T_BLOCKED, T_DONE } tstate_t;
typedef enum { PROTO_NONE, PROTO_PI } proto_t;           /* NONE = 일반 세마포어처럼 상속 없음 (M03) */
typedef enum {
    RESTORE_EXACT,          /* max(base, 아직 가진 뮤텍스들의 대기자 prio) — Linux rt_mutex, Zephyr v4.5 */
    RESTORE_BASE,           /* 해제하면 무조건 base 로 — 흔한 오답                                       */
    RESTORE_ALL_RELEASED    /* 가진 뮤텍스가 0 개가 될 때만 base 로 — FreeRTOS xTaskPriorityDisinherit 모델 */
} restore_t;

typedef struct {
    char name[4];
    int base, prio, release;
    op_t prog[MAXOPS];
    int pc, left;
    tstate_t st;
    int blocked_on;          /* 기다리는 뮤텍스 (-1 = 없음) */
    unsigned seq;            /* READY 가 된 순서 / 대기열에 들어간 순서 */
    int nheld;               /* 가진 뮤텍스 수 (FreeRTOS uxMutexesHeld) */
    int ran_at, finish, blocked_ticks, ready_ticks;
    int deadline;            /* 타임아웃 tick (-1 = 없음) */
    int timed_out, deadlk;   /* 연습 2 / 연습 3 결과 */
} task_t;

typedef struct { int owner; } mutex_t;

typedef struct kernel kernel_t;
struct kernel {
    task_t t[MAXT]; int nt;
    mutex_t m[MAXM]; int nm;
    proto_t proto; restore_t restore; int chain; int max_hops;
    int deadlock_check;      /* 연습 3 */
    int now; unsigned seq;
    char row[MAXT][MAXTICK + 1];
    int prio_at[MAXT][MAXTICK];
    int ticks_run;
    void (*on_tick)(const kernel_t *);   /* 테스트용 훅: tick 끝마다 불변식 검사 */
};

/* ---------------- 주어진 커널 코드 ---------------- */
static void k_init(kernel_t *k, proto_t proto, restore_t restore, int chain) {
    memset(k, 0, sizeof *k);
    k->proto = proto; k->restore = restore; k->chain = chain; k->max_hops = MAXT;
    k->nm = MAXM;
    for (int i = 0; i < MAXM; i++) k->m[i].owner = -1;
}
static int k_task(kernel_t *k, const char *name, int prio, int release, const op_t *prog) {
    int id = k->nt++;
    task_t *t = &k->t[id];
    snprintf(t->name, sizeof t->name, "%s", name);
    t->base = t->prio = prio; t->release = release;
    int i = 0;
    do { t->prog[i] = prog[i]; } while (prog[i++].k != OP_END && i < MAXOPS);
    t->prog[MAXOPS - 1] = (op_t)END_;
    t->blocked_on = -1; t->ran_at = -1; t->finish = -1; t->deadline = -1;
    return id;
}
static int pick(const kernel_t *k) {                 /* 최고 prio READY, 같으면 seq 작은 쪽 */
    int best = -1;
    for (int i = 0; i < k->nt; i++) {
        const task_t *t = &k->t[i];
        if (t->st != T_READY) continue;
        if (best < 0 || t->prio > k->t[best].prio ||
            (t->prio == k->t[best].prio && t->seq < k->t[best].seq)) best = i;
    }
    return best;
}
static int top_waiter(const kernel_t *k, int m) {    /* 대기열: prio 순, 같으면 FIFO */
    int best = -1;
    for (int i = 0; i < k->nt; i++) {
        const task_t *t = &k->t[i];
        if (t->st != T_BLOCKED || t->blocked_on != m) continue;
        if (best < 0 || t->prio > k->t[best].prio ||
            (t->prio == k->t[best].prio && t->seq < k->t[best].seq)) best = i;
    }
    return best;
}
static int max_i(int a, int b) { return a > b ? a : b; }

/* ---------------- TODO 1: 상속 우선순위 계산 ---------------- */
/* t 가 지금 가져야 할 우선순위 = max(t 의 base, t 가 소유한 모든 뮤텍스에서 기다리는 태스크들의 prio).
 * 대기자의 prio 는 그 대기자가 상속받은 값까지 포함한다 (그래서 연쇄가 성립한다). */
static int inherited_prio(const kernel_t *k, int t) {
    int p = k->t[t].base;
    for (int m = 0; m < k->nm; m++) {
        if (k->m[m].owner != t) continue;
        for (int w = 0; w < k->nt; w++)
            if (k->t[w].st == T_BLOCKED && k->t[w].blocked_on == m) p = max_i(p, k->t[w].prio);
    }
    return p;
}

/* ---------------- TODO 2: 막히는 순간의 상속 (연쇄 포함) ---------------- */
/* w 가 방금 뮤텍스 k->t[w].blocked_on 에서 막혔다. 그 소유자의 prio 를 올린다.
 * chain == 0: 소유자 1명만 (FreeRTOS xTaskPriorityInherit 처럼).
 * chain == 1: 소유자가 또 다른 뮤텍스에서 막혀 있으면 그 소유자로 계속 (최대 max_hops 번).
 * 올릴 게 없으면(새 값 <= 현재 prio) 거기서 멈춘다 — 위쪽은 이미 충분히 높다. 절대 낮추지 않는다. */
static void pi_on_block(kernel_t *k, int w) {
    int cur = w;
    for (int hops = 0; hops < k->max_hops; hops++) {
        int m = k->t[cur].blocked_on;
        if (m < 0) break;
        int o = k->m[m].owner;
        if (o < 0) break;
        int np = inherited_prio(k, o);
        if (np <= k->t[o].prio) break;
        k->t[o].prio = np;
        if (!k->chain || o == w) break;          /* o == w: 교착 사이클 — 더 돌 필요 없음 */
        cur = o;
    }
}

/* ---------------- TODO 3: 해제 후 복원 ---------------- */
/* c 가 뮤텍스를 하나 풀었다 (nheld 는 이미 줄었음). new_owner 는 소유권을 넘겨받은 태스크(-1 = 없음). */
static void pi_on_unlock(kernel_t *k, int c, int new_owner) {
    task_t *t = &k->t[c];
    switch (k->restore) {
    case RESTORE_BASE:          t->prio = t->base; break;
    case RESTORE_ALL_RELEASED:  if (t->nheld == 0) t->prio = t->base; break;
    case RESTORE_EXACT:
        t->prio = inherited_prio(k, c);       /* 아직 가진 뮤텍스의 대기자만 남는다 */
        break;
    }
    if (new_owner >= 0)                       /* 넘겨받은 쪽은 남은 대기자를 상속 (주어진 코드) */
        k->t[new_owner].prio = max_i(k->t[new_owner].prio, inherited_prio(k, new_owner));
}

/* ---------------- 주어진 커널 코드 (계속) ---------------- */
/* 연습 3: c 가 m 을 기다리면 사이클이 생기는가? 소유자 → 그가 기다리는 뮤텍스의 소유자 → ... */
static int would_deadlock(const kernel_t *k, int c, int m) {
    int o = k->m[m].owner;
    for (int hops = 0; o >= 0 && hops < MAXT; hops++) {
        if (o == c) return 1;
        int m2 = k->t[o].blocked_on;
        if (k->t[o].st != T_BLOCKED || m2 < 0) return 0;
        o = k->m[m2].owner;
    }
    return 0;
}
static int mutex_lock(kernel_t *k, int c, int m, int to) {   /* 1 = 얻음, 0 = 막힘, -1 = EDEADLK */
    if (k->m[m].owner < 0) { k->m[m].owner = c; k->t[c].nheld++; return 1; }
    if (k->deadlock_check && would_deadlock(k, c, m)) { k->t[c].deadlk = 1; return -1; }
    k->t[c].deadline = to > 0 ? k->now + to : -1;
    k->t[c].st = T_BLOCKED; k->t[c].blocked_on = m; k->t[c].seq = ++k->seq;
    if (k->proto == PROTO_PI) pi_on_block(k, c);
    return 0;
}
static void mutex_unlock(kernel_t *k, int c, int m) {
    k->m[m].owner = -1; k->t[c].nheld--;
    int w = top_waiter(k, m);
    if (w >= 0) {                                    /* handoff: 대기자가 바로 소유자 */
        k->m[m].owner = w; k->t[w].nheld++;
        k->t[w].st = T_READY; k->t[w].blocked_on = -1; k->t[w].seq = ++k->seq;
    }
    if (k->proto == PROTO_PI) pi_on_unlock(k, c, w);
}
/* c 의 0-tick op 들을 실행. 1 = c 가 막힘/끝남/선점됨(다시 고른다), 0 = WORK 에 도착 */
static int exec_zero_ops(kernel_t *k, int c) {
    for (int guard = 0; guard < MAXOPS + 1; guard++) {
        task_t *t = &k->t[c];
        op_t op = t->prog[t->pc];
        switch (op.k) {
        case OP_WORK:
            if (op.a <= 0) { t->pc++; break; }
            if (t->left == 0) t->left = op.a;
            return 0;
        case OP_END:    t->st = T_DONE; t->finish = t->ran_at + 1; return 1;
        case OP_LOCK: {
            t->pc++;
            int r = mutex_lock(k, c, op.a, op.to);
            if (r == 0) return 1;
            if (r < 0) {                     /* EDEADLK: 가진 뮤텍스를 모두 놓고 포기(에러 경로) */
                for (int mm = 0; mm < k->nm; mm++)
                    if (k->m[mm].owner == c) mutex_unlock(k, c, mm);
                t->st = T_DONE; t->finish = t->ran_at + 1;
                return 1;
            }
            break;
        }
        case OP_UNLOCK: t->pc++; mutex_unlock(k, c, op.a); break;
        }
        if (pick(k) != c) return 1;                  /* 더 높은 태스크가 깨어났다 → 선점 */
    }
    return 1;
}
static void snapshot(kernel_t *k, int ran) {
    for (int i = 0; i < k->nt; i++) {
        task_t *t = &k->t[i];
        char ch = '.';
        if (i == ran) ch = 'R';
        else if (t->st == T_BLOCKED) { ch = 'B'; t->blocked_ticks++; }
        else if (t->st == T_READY) { ch = 'r'; t->ready_ticks++; }
        k->row[i][k->now] = ch;
        k->prio_at[i][k->now] = t->prio;
    }
}
/* 연습 2: 대기자 w 가 뮤텍스 m 에서 타임아웃 → 소유자의 상속을 되돌린다 (소유자 1명만, 아래 방향 연쇄 없음) */
static void pi_on_timeout(kernel_t *k, int w, int m) {
    int o = k->m[m].owner;
    (void)w;
    if (o < 0 || k->proto != PROTO_PI) return;
    task_t *t = &k->t[o];
    if (k->restore == RESTORE_EXACT) {
        t->prio = inherited_prio(k, o);                 /* w 는 이미 대기열에서 빠졌다 */
    } else if (k->restore == RESTORE_ALL_RELEASED) {    /* FreeRTOS vTaskPriorityDisinheritAfterTimeout */
        int tw = top_waiter(k, m);
        int p = max_i(t->base, tw >= 0 ? k->t[tw].prio : t->base);
        if (t->nheld == 1) t->prio = p;                 /* 뮤텍스를 1개만 가졌을 때만 */
    } else {
        t->prio = t->base;
    }
}
static void process_timeouts(kernel_t *k) {
    for (int i = 0; i < k->nt; i++) {
        task_t *t = &k->t[i];
        if (t->st != T_BLOCKED || t->deadline != k->now) continue;
        int m = t->blocked_on;
        t->st = T_READY; t->blocked_on = -1; t->deadline = -1; t->timed_out = 1; t->seq = ++k->seq;
        pi_on_timeout(k, i, m);
    }
}
static void run_tick(kernel_t *k) {
    process_timeouts(k);
    for (int i = 0; i < k->nt; i++)
        if (k->t[i].st == T_NEW && k->t[i].release == k->now) { k->t[i].st = T_READY; k->t[i].seq = ++k->seq; }
    for (int guard = 0; guard < MAXT * MAXOPS; guard++) {
        int c = pick(k);
        if (c < 0) break;
        if (exec_zero_ops(k, c)) continue;
        task_t *t = &k->t[c];
        t->left--; t->ran_at = k->now;
        snapshot(k, c);                              /* tick 의 기록은 뒤따르는 0-tick op 전에 */
        if (t->left == 0) { t->pc++; (void)exec_zero_ops(k, c); }
        return;
    }
    snapshot(k, -1);                                 /* idle tick */
}
static void k_run(kernel_t *k, int ticks) {
    if (ticks > MAXTICK) ticks = MAXTICK;
    for (k->now = 0; k->now < ticks; k->now++) {
        run_tick(k);
        if (k->on_tick) k->on_tick(k);
    }
    k->ticks_run = ticks;
    for (int i = 0; i < k->nt; i++) k->row[i][ticks] = '\0';
}
static void print_timeline(const kernel_t *k, const char *title, int prio_of) {
    printf("%s\n tick :", title);
    for (int x = 0; x < k->ticks_run; x++) printf(" %d", x % 10);
    printf("\n");
    for (int i = 0; i < k->nt; i++) {
        printf(" %-4s :", k->t[i].name);
        for (int x = 0; x < k->ticks_run; x++) printf(" %c", k->row[i][x]);
        printf("\n");
    }
    if (prio_of >= 0) {
        char lab[8];
        snprintf(lab, sizeof lab, "%sp", k->t[prio_of].name);
        printf(" %-4s :", lab);
        for (int x = 0; x < k->ticks_run; x++) printf(" %d", k->prio_at[prio_of][x]);
        printf("\n");
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 연습 1: 'B' 가 연속된 구간의 개수 = 몇 번 따로 막혔나 */
static int block_episodes(const char *row) {
    int n = 0;
    for (int i = 0; row[i]; i++)
        if (row[i] == 'B' && (i == 0 || row[i - 1] != 'B')) n++;
    return n;
}
enum { R1 = 0, R2 = 1 };
static void ex1(kernel_t *k) {            /* H 가 R1, R2 를 차례로 쓴다. L1 이 R1, L2 가 R2 를 쥐고 있다 */
    k_init(k, PROTO_PI, RESTORE_EXACT, 1);
    k_task(k, "H",  3, 2, (op_t[]){ LOCK(R1), WORK(1), UNLOCK(R1), LOCK(R2), WORK(1), UNLOCK(R2), END_ });
    k_task(k, "L2", 2, 1, (op_t[]){ LOCK(R2), WORK(4), UNLOCK(R2), END_ });
    k_task(k, "L1", 1, 0, (op_t[]){ LOCK(R1), WORK(4), UNLOCK(R1), END_ });
    k_run(k, 14);
}
static void ex2(kernel_t *k, restore_t rs, int extra_mutex) {
    k_init(k, PROTO_PI, rs, 1);
    k_task(k, "H2", 4, 3, (op_t[]){ LOCK_TO(R1, 2), WORK(1), END_ });          /* 2 tick 만 기다리고 포기 */
    k_task(k, "H1", 3, 2, (op_t[]){ LOCK(R1), WORK(1), UNLOCK(R1), END_ });
    if (extra_mutex)
        k_task(k, "L", 1, 0, (op_t[]){ LOCK(R2), WORK(1), LOCK(R1), WORK(6), UNLOCK(R1), UNLOCK(R2), END_ });
    else
        k_task(k, "L", 1, 0, (op_t[]){ WORK(1), LOCK(R1), WORK(6), UNLOCK(R1), END_ });
    k_run(k, 14);
}
static void ex3(kernel_t *k, int detect) {   /* 반대 순서로 두 뮤텍스 */
    k_init(k, PROTO_PI, RESTORE_EXACT, 1);
    k->deadlock_check = detect;
    k_task(k, "A", 1, 0, (op_t[]){ LOCK(R1), WORK(2), LOCK(R2), WORK(1), UNLOCK(R2), UNLOCK(R1), END_ });
    k_task(k, "B", 2, 1, (op_t[]){ LOCK(R2), WORK(1), LOCK(R1), WORK(1), UNLOCK(R1), UNLOCK(R2), END_ });
    k_run(k, 12);
}
static int all_stuck(const kernel_t *k) {     /* 끝나지 않은 태스크가 모두 BLOCKED */
    int live = 0;
    for (int i = 0; i < k->nt; i++) {
        if (k->t[i].st == T_DONE) continue;
        live++;
        if (k->t[i].st != T_BLOCKED) return 0;
    }
    return live > 0;
}
enum { M0 = 0, M1 = 1, M2 = 2 };
static void ex4(kernel_t *k, int hops, int c_x) {   /* T4 → T3 → T2 → T1 사슬, X 는 CPU */
    k_init(k, PROTO_PI, RESTORE_EXACT, 1);
    k->max_hops = hops;
    k_task(k, "T4", 5, 3, (op_t[]){ LOCK(M2), WORK(1), UNLOCK(M2), END_ });
    k_task(k, "X",  4, 4, (op_t[]){ WORK(c_x), END_ });
    k_task(k, "T3", 3, 2, (op_t[]){ LOCK(M2), LOCK(M1), WORK(1), UNLOCK(M1), UNLOCK(M2), END_ });
    k_task(k, "T2", 2, 1, (op_t[]){ LOCK(M1), LOCK(M0), WORK(1), UNLOCK(M0), UNLOCK(M1), END_ });
    k_task(k, "T1", 1, 0, (op_t[]){ LOCK(M0), WORK(6), UNLOCK(M0), END_ });
    k_run(k, 16 + c_x);
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    kernel_t k;

    /* 연습 1 — chained blocking: 상속이 있어도 H 는 "낮은 태스크 수만큼" 따로 막힌다 */
    ex1(&k);
    if (verbose) print_timeline(&k, "ex1 chained blocking", -1);
    CHECK(block_episodes(k.row[0]) == 2);
    CHECK(k.t[0].blocked_ticks == 6);                 /* L1 의 남은 3 + L2 의 남은 3 */
    CHECK(k.prio_at[2][2] == 3 && k.prio_at[1][6] == 3);

    /* 연습 2 — 대기자 타임아웃 후 상속 되돌리기 */
    enum { E2H2 = 0, E2H1 = 1, E2L = 2 };
    ex2(&k, RESTORE_EXACT, 0);
    if (verbose) print_timeline(&k, "ex2 timeout, EXACT, L holds 1 mutex", E2L);
    CHECK(k.t[E2H2].timed_out == 1);
    CHECK(k.prio_at[E2L][3] == 4 && k.prio_at[E2L][4] == 4);   /* H2 대기 중 4 */
    CHECK(k.prio_at[E2L][5] == 3);                             /* t5 시작에 타임아웃 → 남은 대기자 H1 = 3 */
    ex2(&k, RESTORE_ALL_RELEASED, 0);
    CHECK(k.prio_at[E2L][5] == 3);                             /* FreeRTOS 도 뮤텍스 1개면 3 으로 */
    ex2(&k, RESTORE_EXACT, 1);
    CHECK(k.prio_at[E2L][5] == 3);
    ex2(&k, RESTORE_ALL_RELEASED, 1);
    if (verbose) print_timeline(&k, "ex2 timeout, FreeRTOS model, L holds 2 mutexes", E2L);
    CHECK(k.prio_at[E2L][5] == 4);                             /* 2개 쥐고 있으면 4 에 머문다 (과상속) */

    /* 연습 3 — 교착: 감지 없으면 "모두 막힘", 감지하면 EDEADLK */
    ex3(&k, 0);
    if (verbose) print_timeline(&k, "ex3 opposite order, no detection", -1);
    CHECK(all_stuck(&k));
    CHECK(k.t[0].st == T_BLOCKED && k.t[1].st == T_BLOCKED);
    ex3(&k, 1);
    if (verbose) print_timeline(&k, "ex3 opposite order, detection", -1);
    CHECK(k.t[0].deadlk == 1 && k.t[1].deadlk == 0);           /* 사이클을 닫으려던 A 가 에러를 받는다 */
    CHECK(!all_stuck(&k) && k.t[1].st == T_DONE);              /* A 가 R1 을 놓았으니 B 는 끝난다 */

    /* 연습 4 — 홉 수 제한: 사슬 끝(T1)까지 못 올리면 X 가 끼어든다 */
    enum { T4 = 0, TX = 1, T3 = 2, T2 = 3, T1 = 4 };
    ex4(&k, MAXT, 4);
    if (verbose) print_timeline(&k, "ex4 chain of 4, hops=MAXT", T1);
    CHECK(k.prio_at[T1][3] == 5);
    int full = k.t[T4].blocked_ticks;
    CHECK(full == 5);
    ex4(&k, 2, 4);
    if (verbose) print_timeline(&k, "ex4 chain of 4, hops=2", T1);
    CHECK(k.prio_at[T1][3] == 3 && k.prio_at[T2][3] == 5);
    CHECK(k.t[T4].blocked_ticks == full + 4);
    ex4(&k, 2, 8);
    CHECK(k.t[T4].blocked_ticks == full + 8);
    ex4(&k, MAXT, 8);
    CHECK(k.t[T4].blocked_ticks == full);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
