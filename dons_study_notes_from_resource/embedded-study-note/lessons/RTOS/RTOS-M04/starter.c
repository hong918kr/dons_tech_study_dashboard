/* RTOS-M04 우선순위 상속 구현과 연쇄 상속
 *
 * 호스트 모델: OS 스레드 1개로 도는 "tick 단위 결정적 미니 커널".
 *  - 태스크 = 작은 스크립트 (WORK n / LOCK m / UNLOCK m / END). WORK 1단위 = CPU 1 tick.
 *  - LOCK/UNLOCK/END 는 0 tick. 태스크가 그 op 에 도달하는 순간(실행 중일 때) 바로 실행된다.
 *  - 스케줄러: 매 tick 마다 READY 중 우선순위(prio, 클수록 높음)가 가장 높은 태스크 1개가 1 tick 실행.
 *    같은 우선순위는 먼저 READY 가 된 쪽(seq 작은 쪽) — 실행 중인 태스크는 같은 우선순위에 밀리지 않는다
 *    (time slicing 없음 = FreeRTOS configUSE_TIME_SLICING 0 과 같은 규칙).
 *  - UNLOCK 은 소유권을 최고 우선순위 대기자에게 바로 넘긴다(handoff). 대기 순서 = 우선순위, 같으면 FIFO.
 *  - 타임라인: R=실행, B=뮤텍스 대기(blocked), r=READY 인데 못 돎, .=아직 안 나왔거나 끝남.
 *
 * 이 레슨에서 채우는 것 (TODO 1~3):
 *  1. inherited_prio()  : 상속 우선순위 = max(base, 내가 가진 모든 뮤텍스의 대기자 prio)
 *  2. pi_on_block()     : 대기자가 막히는 순간 소유자를 올린다. chain=1 이면 소유자가 또 막혀 있을 때
 *                         그 뮤텍스의 소유자로 계속 올라간다 (A→B→C 연쇄 상속). max_hops 로 상한.
 *  3. pi_on_unlock()    : RESTORE_EXACT 분기 — 해제 후 "아직 가진 뮤텍스" 기준으로 정확히 복원.
 * 그 밖의 커널(스케줄러, 대기열, 다른 복원 정책)은 주어진 코드다.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXT    8
#define MAXM    4
#define MAXOPS  16
#define MAXTICK 64

typedef enum { OP_END, OP_WORK, OP_LOCK, OP_UNLOCK } opk_t;
typedef struct { opk_t k; int a; } op_t;
#define WORK(n)   { OP_WORK, (n) }
#define LOCK(m)   { OP_LOCK, (m) }
#define UNLOCK(m) { OP_UNLOCK, (m) }
#define END_      { OP_END, 0 }

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
} task_t;

typedef struct { int owner; } mutex_t;

typedef struct kernel kernel_t;
struct kernel {
    task_t t[MAXT]; int nt;
    mutex_t m[MAXM]; int nm;
    proto_t proto; restore_t restore; int chain; int max_hops;
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
    t->blocked_on = -1; t->ran_at = -1; t->finish = -1;
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
    /* TODO 1: k->m[m].owner == t 인 모든 m 에 대해, blocked_on == m 인 BLOCKED 태스크들의 prio 최대값 */
    return k->t[t].base;
}

/* ---------------- TODO 2: 막히는 순간의 상속 (연쇄 포함) ---------------- */
/* w 가 방금 뮤텍스 k->t[w].blocked_on 에서 막혔다. 그 소유자의 prio 를 올린다.
 * chain == 0: 소유자 1명만 (FreeRTOS xTaskPriorityInherit 처럼).
 * chain == 1: 소유자가 또 다른 뮤텍스에서 막혀 있으면 그 소유자로 계속 (최대 max_hops 번).
 * 올릴 게 없으면(새 값 <= 현재 prio) 거기서 멈춘다 — 위쪽은 이미 충분히 높다. 절대 낮추지 않는다. */
static void pi_on_block(kernel_t *k, int w) {
    /* TODO 2: cur = w 에서 시작해 (blocked_on → owner) 를 따라 올라가며 owner 의 prio 를 올린다 */
    (void)k; (void)w;
}

/* ---------------- TODO 3: 해제 후 복원 ---------------- */
/* c 가 뮤텍스를 하나 풀었다 (nheld 는 이미 줄었음). new_owner 는 소유권을 넘겨받은 태스크(-1 = 없음). */
static void pi_on_unlock(kernel_t *k, int c, int new_owner) {
    task_t *t = &k->t[c];
    switch (k->restore) {
    case RESTORE_BASE:          t->prio = t->base; break;
    case RESTORE_ALL_RELEASED:  if (t->nheld == 0) t->prio = t->base; break;
    case RESTORE_EXACT:
        t->prio = t->base;                    /* TODO 3: 이건 RESTORE_BASE 와 같은 오답이다 */
        break;
    }
    if (new_owner >= 0)                       /* 넘겨받은 쪽은 남은 대기자를 상속 (주어진 코드) */
        k->t[new_owner].prio = max_i(k->t[new_owner].prio, inherited_prio(k, new_owner));
}

/* ---------------- 주어진 커널 코드 (계속) ---------------- */
static int mutex_lock(kernel_t *k, int c, int m) {   /* 1 = 얻음, 0 = 막힘 */
    if (k->m[m].owner < 0) { k->m[m].owner = c; k->t[c].nheld++; return 1; }
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
        case OP_LOCK:   t->pc++; if (!mutex_lock(k, c, op.a)) return 1; break;
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
static void run_tick(kernel_t *k) {
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

/* ---------------- 시나리오 ---------------- */
enum { R = 0 };
/* 정준 역전 시나리오 (M03 과 같은 숫자): H=3, M=2, L=1. L t0 출발, t1 에 R 획득, R 안에서 4 tick.
 * H t2 출발, 첫 step 에 R 요청. M t3 출발, R 없이 CPU 만 c_m tick. */
static void canonical(kernel_t *k, proto_t proto, int c_m) {
    k_init(k, proto, RESTORE_EXACT, 1);
    k_task(k, "H", 3, 2, (op_t[]){ LOCK(R), WORK(2), UNLOCK(R), END_ });
    k_task(k, "M", 2, 3, (op_t[]){ WORK(c_m), END_ });
    k_task(k, "L", 1, 0, (op_t[]){ WORK(1), LOCK(R), WORK(4), UNLOCK(R), WORK(1), END_ });
    k_run(k, 8 + c_m + 4);
}
enum { R1 = 0, R2 = 1 };
/* Part B: L 이 R1, R2 를 함께 쥔다. M(3) 은 R2, H(4) 는 R1 을 기다린다. X(2) 는 CPU 만 c_x tick. */
static void multi(kernel_t *k, restore_t rs, int c_x) {
    k_init(k, PROTO_PI, rs, 1);
    k_task(k, "H", 4, 2, (op_t[]){ LOCK(R1), WORK(2), UNLOCK(R1), END_ });
    k_task(k, "M", 3, 1, (op_t[]){ LOCK(R2), WORK(2), UNLOCK(R2), END_ });
    k_task(k, "X", 2, 3, (op_t[]){ WORK(c_x), END_ });
    k_task(k, "L", 1, 0, (op_t[]){ LOCK(R1), LOCK(R2), WORK(3), UNLOCK(R1), WORK(3), UNLOCK(R2), WORK(1), END_ });
    k_run(k, 12 + c_x);
}
enum { D = 0, S = 1 };
/* Part C: 모터 제어 A(4) → 센서 데이터 뮤텍스 D (센서 태스크 B(2) 소유) → B 는 SPI 버스 S 를 기다림
 * (플래시 로거 C(1) 소유). 텔레메트리 X(3) 는 CPU 만 c_x tick. */
static void chain3(kernel_t *k, int chain, int c_x) {
    k_init(k, PROTO_PI, RESTORE_EXACT, chain);
    k_task(k, "A", 4, 2, (op_t[]){ LOCK(D), WORK(2), UNLOCK(D), END_ });
    k_task(k, "X", 3, 3, (op_t[]){ WORK(c_x), END_ });
    k_task(k, "B", 2, 1, (op_t[]){ LOCK(D), WORK(1), LOCK(S), WORK(2), UNLOCK(S), UNLOCK(D), END_ });
    k_task(k, "C", 1, 0, (op_t[]){ LOCK(S), WORK(5), UNLOCK(S), WORK(1), END_ });
    k_run(k, 14 + c_x);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define ROW_IS(k, i, s) (strcmp((k)->row[i], (s)) == 0)

/* 불변식 (RESTORE_EXACT + chain): 모든 태스크의 prio 가 "base 와 대기자들의 최대값" 과 정확히 같다.
 * 아래쪽(>= 모든 대기자)과 위쪽(어떤 대기자나 base 와 같다)으로 나눠 검사한다. */
static int g_inv_bad, g_inv_ticks, g_free_wait, g_chain_ticks;
static void check_invariant(const kernel_t *k) {
    g_inv_ticks++;
    for (int o = 0; o < k->nt; o++) {
        const task_t *t = &k->t[o];
        if (t->st == T_NEW || t->st == T_DONE) continue;
        int witness = (t->prio == t->base);
        for (int w = 0; w < k->nt; w++) {
            const task_t *u = &k->t[w];
            if (u->st != T_BLOCKED || k->m[u->blocked_on].owner != o) continue;
            if (u->prio > t->prio) g_inv_bad++;          /* 상속 부족 */
            if (u->prio == t->prio) witness = 1;
        }
        if (!witness || t->prio < t->base) g_inv_bad++;  /* 상속 과다 */
        if (t->st == T_BLOCKED && k->m[t->blocked_on].owner < 0) g_free_wait++;
        if (t->st == T_BLOCKED)                          /* 막힌 태스크가 또 대기자를 가짐 = 2단 이상 사슬 */
            for (int w = 0; w < k->nt; w++)
                if (k->t[w].st == T_BLOCKED && k->m[k->t[w].blocked_on].owner == o) { g_chain_ticks++; break; }
    }
}
static uint32_t g_rng = 0x2026110Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rnd(int lo, int hi) { return lo + (int)(xorshift32() % (uint32_t)(hi - lo + 1)); }
/* 무작위 태스크 세트: 뮤텍스는 항상 번호 오름차순으로 잡는다(교착 없음), 해제 순서는 무작위(비 LIFO 포함).
 * inverted = 1 이면 우선순위가 낮은 태스크부터 먼저 출발시켜 상속·연쇄가 자주 생기게 한다. */
static void random_set(kernel_t *k, int inverted) {
    k_init(k, PROTO_PI, RESTORE_EXACT, 1);
    int nt = rnd(4, 7);
    for (int i = 0; i < nt; i++) {
        op_t p[MAXOPS]; int n = 0;
        int held[MAXM], nh = 0;
        p[n++] = (op_t)WORK(rnd(0, 1));
        for (int m = 0; m < 3; m++)
            if (rnd(0, 2)) { p[n++] = (op_t)LOCK(m); p[n++] = (op_t)WORK(rnd(1, 3)); held[nh++] = m; }
        while (nh > 0) {
            int j = rnd(0, nh - 1);
            p[n++] = (op_t)UNLOCK(held[j]); held[j] = held[--nh];
            if (nh > 0) p[n++] = (op_t)WORK(rnd(1, 2));
        }
        p[n++] = (op_t)END_;
        char name[4] = { (char)('a' + i), 0, 0, 0 };
        if (inverted) k_task(k, name, i + 1, i + rnd(0, 1), p);   /* 낮은 태스크가 먼저: 역전이 잘 생김 */
        else          k_task(k, name, rnd(1, 5), rnd(0, 4), p);
    }
    k->on_tick = check_invariant;
    k_run(k, MAXTICK);
}
static int all_done(const kernel_t *k) {
    for (int i = 0; i < k->nt; i++) if (k->t[i].st != T_DONE) return 0;
    return 1;
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    kernel_t k;
    enum { H_ = 0, M_ = 1, L_ = 2 };
    static const int cm[] = { 0, 2, 4, 8, 16 };

    /* ---- Part A: 정준 시나리오, 상속 없음(M03) vs 상속 ---- */
    printf("Part A  C_M | H blocked (none)  H blocked (PI)  H finish (PI)\n");
    for (int i = 0; i < 5; i++) {
        kernel_t a, b;
        canonical(&a, PROTO_NONE, cm[i]);
        canonical(&b, PROTO_PI, cm[i]);
        printf("        %3d | %17d  %14d  %13d\n", cm[i], a.t[H_].blocked_ticks, b.t[H_].blocked_ticks, b.t[H_].finish);
        CHECK(a.t[H_].blocked_ticks == 3 + cm[i]);          /* 주어진 baseline: M 길이만큼 늘어난다 */
        CHECK(b.t[H_].blocked_ticks == 3);                  /* 상속: L 의 남은 임계구역(3 tick) 으로 고정 */
        CHECK(b.t[H_].finish == 7);
    }
    canonical(&k, PROTO_PI, 4);
    if (verbose) { kernel_t a; canonical(&a, PROTO_NONE, 4); print_timeline(&a, "canonical C_M=4, plain (no PI)", L_); }
    print_timeline(&k, "canonical C_M=4, PI", L_);
    CHECK(ROW_IS(&k, H_, "..BBBRR........."));
    CHECK(ROW_IS(&k, M_, "...rrrrRRRR....."));
    CHECK(ROW_IS(&k, L_, "RRRRRrrrrrrR...."));
    CHECK(k.prio_at[L_][1] == 1 && k.prio_at[L_][2] == 3 && k.prio_at[L_][4] == 3 && k.prio_at[L_][5] == 1);

    /* ---- Part B: 여러 뮤텍스를 쥔 채 하나만 풀 때의 복원 ---- */
    enum { BH = 0, BM = 1, BX = 2, BL = 3 };
    multi(&k, RESTORE_EXACT, 6);
    print_timeline(&k, "Part B, RESTORE_EXACT (C_X=6)", BL);
    CHECK(k.prio_at[BL][1] == 3 && k.prio_at[BL][2] == 4);   /* M 이 R2 대기 → 3, H 가 R1 대기 → 4 */
    CHECK(k.prio_at[BL][5] == 3);                            /* R1 해제 후: 남은 R2 의 대기자 M = 3 */
    CHECK(k.prio_at[BL][8] == 1);                            /* R2 도 해제 → base */
    CHECK(k.t[BH].blocked_ticks == 1 && k.t[BH].ready_ticks == 0);
    CHECK(k.t[BM].blocked_ticks == 7);
    CHECK(ROW_IS(&k, BX, "...rrrrrrrRRRRRR.."));
    for (int cx = 2; cx <= 10; cx += 4) { multi(&k, RESTORE_EXACT, cx); CHECK(k.t[BM].blocked_ticks == 7); }
    if (verbose) { multi(&k, RESTORE_BASE, 6); print_timeline(&k, "Part B, RESTORE_BASE (C_X=6)", BL); }
    for (int cx = 2; cx <= 10; cx += 4) { multi(&k, RESTORE_BASE, cx); CHECK(k.t[BM].blocked_ticks == 7 + cx); }
    multi(&k, RESTORE_ALL_RELEASED, 6);
    if (verbose) print_timeline(&k, "Part B, RESTORE_ALL_RELEASED (FreeRTOS model, C_X=6)", BL);
    CHECK(k.prio_at[BL][5] == 4);                            /* 과상속: H 는 이미 R1 을 가졌는데 L 이 4 */
    CHECK(k.t[BH].blocked_ticks == 1 && k.t[BH].ready_ticks == 3);   /* H 가 L 의 R2 구간만큼 늦는다 */
    CHECK(k.t[BM].blocked_ticks == 5 && k.t[BM].finish == 10);

    /* ---- Part C: A→B→C 연쇄 상속 ---- */
    enum { CA = 0, CX = 1, CB = 2, CC = 3 };
    printf("Part C  C_X | A blocked (chain)  A blocked (no chain)\n");
    for (int i = 0; i < 5; i++) {
        kernel_t a, b;
        chain3(&a, 1, cm[i]);
        chain3(&b, 0, cm[i]);
        printf("        %3d | %17d  %20d\n", cm[i], a.t[CA].blocked_ticks, b.t[CA].blocked_ticks);
        CHECK(a.t[CA].blocked_ticks == 6);                  /* C 의 남은 4 + B 의 2 */
        CHECK(b.t[CA].blocked_ticks == 6 + cm[i]);
    }
    chain3(&k, 1, 4);
    print_timeline(&k, "Part C, chain (C_X=4)", CC);
    CHECK(k.prio_at[CC][1] == 1 && k.prio_at[CC][2] == 4);   /* t1 끝에 B 가 막히고, t2 에 A 가 막혀 연쇄로 4 */
    CHECK(k.prio_at[CB][6] == 4 && k.prio_at[CB][8] == 2);   /* B 는 S 를 넘겨받은 뒤에도 A 때문에 4 */
    CHECK(ROW_IS(&k, CA, "..BBBBBBRR........"));
    chain3(&k, 0, 4);
    if (verbose) print_timeline(&k, "Part C, no chain (C_X=4)", CC);
    CHECK(k.prio_at[CC][2] == 2 && k.prio_at[CB][2] == 4);   /* 비연쇄: B 만 4, C 는 2 에 머문다 */

    /* ---- 무작위 태스크 세트 300개: 불변식 ---- */
    int all_ok = 1;
    for (int s = 0; s < 300; s++) { random_set(&k, s & 1); if (!all_done(&k)) all_ok = 0; }
    printf("random: %d ticks checked (%d with a 2+ level chain), invariant violations=%d, waits-on-free-mutex=%d\n",
           g_inv_ticks, g_chain_ticks, g_inv_bad, g_free_wait);
    CHECK(g_inv_bad == 0);
    CHECK(g_free_wait == 0);
    CHECK(all_ok);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
