/* RTOS-B09 카운팅 세마포어: 리소스 풀과 이벤트 카운트
 *
 *   미니 커널 (하네스 제공): OS 스레드 1개, tick 구동, 완전히 결정적.
 *     매 tick:  ① ISR 대용 함수(tick hook)  ② release / sleep 끝난 태스크 → READY
 *               ③ READY 중 우선순위가 가장 높은 태스크 1개가 "한 걸음(step)" 실행
 *                  (같은 우선순위는 먼저 READY 된 순 = FIFO, time slicing 없음)
 *   Part A  카운팅 세마포어: 대기 리스트(우선순위 → 같은 우선순위는 FIFO), take/give,
 *           직접 넘겨주기(handoff), 최대값 포화(SEM_FULL), ISR 용 give (블록 안 함 + "더 높은 태스크 깨움")
 *   Part B  DMA 채널 3개 × 태스크 5개 (동시 보유 ≤ 3, 모두 서비스, 깨우는 순서 검증, 무작위 300 시나리오),
 *           이벤트 카운트 (take 전에 give N번 → take N번은 블록 없이), 최대값 포화
 *   Part C  DMA 채널 드라이버: 세마포어 = "몇 개 남았나", 비트맵 = "어느 채널인가", DMA 완료 ISR 이 반납
 *
 * 체크는 타임라인/상태만 본다 (시간 측정 없음). 틀린 코드도 tick 상한에서 멈춘다 (무한 루프 없음).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st       (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (하네스 제공) ======================= */
#define MAX_TASKS 8
#define TL_LEN    64          /* 타임라인은 앞 64 tick 만 기록 */

enum { ST_NOTREL, ST_READY, ST_BLOCKED, ST_SLEEP, ST_DONE };
#define SEM_OK   0
#define SEM_FULL 1            /* 이미 max: 이번 give 는 버려졌다 (FreeRTOS: errQUEUE_FULL) */

typedef struct task task_t;
typedef struct ksem ksem_t;
typedef void (*step_fn)(task_t *t);

struct task {
    const char *name;
    int prio;                 /* 클수록 높다 (FreeRTOS 와 같은 방향). idle = 0 */
    int state, pc, release, wake_at;
    step_fn step;
    unsigned ready_seq;       /* READY 가 된 순번: 같은 우선순위 FIFO */
    unsigned wait_seq;        /* 블록된 순번: 하네스가 깨우는 순서 검증에 씀 */
    ksem_t *waiting_on;       /* 하네스 기록용 */
    task_t *wnext;            /* 대기 리스트 링크 — 세마포어 코드(학생)가 관리 */
    int token;                /* 블록된 채로 세마포어를 넘겨받으면 1 (= 커널이 돌려주는 "성공") */
    /* 태스크 프로그램이 쓰는 칸 */
    int hold, think, rounds, served, holding, ch, first_wait, got_at;
    int ran, blocked_ticks;
    char tl[TL_LEN + 1];
};

struct ksem {
    int count;                /* 남은 토큰 수 */
    int max;                  /* 포화점 */
    task_t *wait_head;        /* 대기 리스트: 우선순위 높은 순, 같으면 먼저 온 순 */
    int nwait;
    long full_drops;          /* SEM_FULL 로 버려진 give 수 */
};

static task_t   g_task[MAX_TASKS];
static int      g_ntask, g_now;
static task_t  *g_cur;        /* step 중인 태스크. ISR 중에는 "끼어들기 직전에 돌던" 태스크 (NULL = idle) */
static bool     g_in_isr;
static unsigned g_seq;
static int      g_isr_take_err;   /* ISR 에서 블록 가능한 take 를 부른 횟수 (configASSERT 자리) */
static int      g_order_err;      /* 깨운 태스크가 "우선순위 → FIFO" 1등이 아니었던 횟수 */
static char     g_wakes[256];     /* 깨어난 순서 로그 */
static void   (*g_tick_hook)(int now);
static void   (*g_check_hook)(void);
static ksem_t  *g_watch;          /* 타임라인 'sem' 줄에 count 를 찍을 세마포어 */
static char     g_sem_tl[TL_LEN + 1];
static char     g_hold_tl[TL_LEN + 1];    /* 리소스 풀: tick 끝의 보유자 수 (pool_check 가 기록) */

static void k_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = g_now = 0; g_cur = NULL; g_in_isr = false; g_seq = 0;
    g_isr_take_err = g_order_err = 0; g_wakes[0] = '\0';
    g_tick_hook = NULL; g_check_hook = NULL; g_watch = NULL;
    memset(g_sem_tl, 0, sizeof g_sem_tl);
    memset(g_hold_tl, 0, sizeof g_hold_tl);
}
static task_t *k_add(const char *name, int prio, int release, step_fn fn) {
    task_t *t = &g_task[g_ntask++];
    t->name = name; t->prio = prio; t->release = release; t->step = fn;
    t->state = ST_NOTREL; t->first_wait = t->got_at = -1;
    return t;
}
/* 세마포어 코드가 부르는 커널 서비스 */
static void k_block(task_t *t, ksem_t *s) { t->state = ST_BLOCKED; t->waiting_on = s; t->wait_seq = ++g_seq; }
static void k_unblock(task_t *t) {
    /* 오라클: 같은 세마포어에서 기다리는 태스크 중 (우선순위 최고, 그중 가장 먼저 온) 태스크였나 */
    for (int i = 0; i < g_ntask; i++) {
        task_t *o = &g_task[i];
        if (o != t && o->state == ST_BLOCKED && o->waiting_on == t->waiting_on &&
            (o->prio > t->prio || (o->prio == t->prio && o->wait_seq < t->wait_seq)))
            g_order_err++;
    }
    size_t n = strlen(g_wakes);
    snprintf(g_wakes + n, sizeof g_wakes - n, "%s%s", n ? " " : "", t->name);
    t->state = ST_READY; t->waiting_on = NULL; t->ready_seq = ++g_seq;
}
static void k_sleep(task_t *t, int n) { t->state = ST_SLEEP; t->wake_at = g_now + (n > 0 ? n : 1); }
static void k_done(task_t *t) { t->state = ST_DONE; }

static task_t *k_pick(void) {
    task_t *best = NULL;
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        if (t->state != ST_READY) continue;
        if (!best || t->prio > best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq))
            best = t;
    }
    return best;
}
static void k_tick(void) {
    g_in_isr = true;                                  /* ① ISR: 끼어든 대상은 g_cur */
    if (g_tick_hook) g_tick_hook(g_now);
    g_in_isr = false;
    for (int i = 0; i < g_ntask; i++) {               /* ② release / sleep 만료 */
        task_t *t = &g_task[i];
        if ((t->state == ST_NOTREL && t->release <= g_now) || (t->state == ST_SLEEP && t->wake_at <= g_now)) {
            t->state = ST_READY; t->ready_seq = ++g_seq;
        }
    }
    task_t *run = k_pick();                           /* ③ 한 걸음 */
    g_cur = run;
    if (run) { run->step(run); run->ran++; }
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        char c = t == run ? 'R' : t->state == ST_READY ? 'r' : t->state == ST_BLOCKED ? 'B'
               : t->state == ST_SLEEP ? 's' : '.';
        if (t != run && t->state == ST_BLOCKED) t->blocked_ticks++;
        if (g_now < TL_LEN) t->tl[g_now] = c;
    }
    if (g_watch && g_now < TL_LEN) g_sem_tl[g_now] = (char)('0' + (g_watch->count % 10));
    if (g_check_hook) g_check_hook();
    g_cur = k_pick();                                 /* 다음 ISR 이 끼어들 대상 */
    g_now++;
}
static bool k_all_done(void) {
    for (int i = 0; i < g_ntask; i++) if (g_task[i].state != ST_DONE) return false;
    return true;
}
static int k_run(int limit) { while (g_now < limit && !k_all_done()) k_tick(); return g_now; }
static void k_print(int to) {
    if (to > TL_LEN) to = TL_LEN;
    printf("tick : ");
    for (int t = 0; t < to; t++) printf("%d ", t % 10);
    printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%-5.5s: ", g_task[i].name);
        for (int t = 0; t < to; t++) printf("%c ", g_task[i].tl[t] ? g_task[i].tl[t] : '.');
        printf("\n");
    }
    if (g_watch) {
        printf("sem  : ");
        for (int t = 0; t < to; t++) printf("%c ", g_sem_tl[t] ? g_sem_tl[t] : ' ');
        printf("\n");
    }
    if (g_hold_tl[0]) {
        printf("hold : ");
        for (int t = 0; t < to; t++) printf("%c ", g_hold_tl[t] ? g_hold_tl[t] : ' ');
        printf("\n");
    }
}

/* ======================= Part A — 카운팅 세마포어 (학생 구현) ==================== */

/* A0. 만들기: max ≥ 1, 0 ≤ initial ≤ max 가 아니면 false (FreeRTOS 는 NULL 을 돌려준다) */
static bool ksem_init(ksem_t *s, int initial, int max) {
    memset(s, 0, sizeof *s);
    (void)initial; (void)max;                   /* TODO A0 */
    return true;
}

/* A1. 대기 리스트: 우선순위 높은 순, 같은 우선순위는 먼저 온 순(뒤에 붙인다) */
static void wait_insert(ksem_t *s, task_t *t) {
    (void)s; (void)t;                           /* TODO A1 */
}
static task_t *wait_pop(ksem_t *s) {
    (void)s;                                    /* TODO A1 */
    return NULL;
}

/* A2. take: 토큰이 있으면 하나 가져가고 true. 없으면 대기 리스트에 넣고 블록, false.
 *     블록된 태스크는 나중에 give 가 토큰을 직접 넘겨줄 때(token = 1) READY 가 된다.
 *     ISR 에서는 블록할 수 없다 → 거부하고 g_isr_take_err++ */
static bool ksem_take(ksem_t *s, task_t *self) {
    (void)s; (void)self;                        /* TODO A2 */
    return true;
}

/* A3. give: 기다리는 태스크가 있으면 맨 앞 태스크에게 직접 넘긴다 (count 는 그대로).
 *     없으면 count++ — 단 max 이면 SEM_FULL (포화: 이벤트가 버려진다) */
static int ksem_give(ksem_t *s) {
    (void)s;                                    /* TODO A3 */
    return SEM_OK;
}

/* A4. ISR 용 give: 절대 블록하지 않는다. 깨운 태스크가 끼어든 태스크(g_cur, idle=0)보다
 *     우선순위가 높으면 *higher_woken = true (false 로는 절대 덮어쓰지 않는다) */
static int ksem_give_from_isr(ksem_t *s, bool *higher_woken) {
    (void)s; (void)higher_woken;                /* TODO A4 */
    return SEM_OK;
}

/* ======================= Part C — DMA 채널 드라이버 (학생 구현) ==================== */
#define DMA_NCH 3
typedef struct {
    ksem_t free;              /* 몇 개 남았나 */
    unsigned used;            /* 어느 채널인가: bit i = 채널 i 사용 중 (태스크가 set, ISR 이 clear) */
    task_t *owner[DMA_NCH];
    int left[DMA_NCH];        /* DMA 하드웨어 모델: 남은 전송 tick */
    int xfers[DMA_NCH];
} dma_t;
static dma_t g_dma;
static int g_crit_depth, g_crit_err;
static void crit_enter(void) { g_crit_depth++; }     /* = taskENTER_CRITICAL() / __disable_irq() */
static void crit_exit(void)  { g_crit_depth--; }
static void dma_mark_used(dma_t *d, int ch) { if (!g_crit_depth && !g_in_isr) g_crit_err++; d->used |= 1u << ch; }
static void dma_mark_free(dma_t *d, int ch) { if (!g_crit_depth && !g_in_isr) g_crit_err++; d->used &= ~(1u << ch); }

/* C1. 토큰(세마포어)을 가진 태스크가 빈 채널 하나를 잡는다: 가장 낮은 번호, 임계 구역 안에서.
 *     빈 채널이 없으면 -1 (토큰을 가졌다면 일어나면 안 된다) */
static int dma_claim(dma_t *d) {
    (void)d;                                    /* TODO C1 */
    return -1;
}
/* C2. DMA 완료 ISR: 채널 비트를 먼저 지우고, 세마포어를 ISR 용으로 give */
static void dma_complete_isr(dma_t *d, int ch, bool *woken) {
    (void)d; (void)ch; (void)woken;             /* TODO C2 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- Part B: 리소스 풀 태스크 ---- */
static ksem_t g_pool;
static int g_pool_total, g_max_hold, g_conserve_err, g_idle_tok_err, g_bad_wake;

static void pool_task(task_t *t) {
    switch (t->pc) {
    case 0:                                        /* 채널 요청 */
        if (t->first_wait < 0) t->first_wait = g_now;
        if (!ksem_take(&g_pool, t)) { t->pc = 1; return; }   /* 블록 → 깨어나면 토큰을 가진 상태 */
        t->holding = 1; t->got_at = g_now; k_sleep(t, t->hold); t->pc = 2; return;
    case 1:                                        /* 깨어남: 넘겨받은 토큰이 있어야 한다 */
        if (!t->token) { g_bad_wake++; t->pc = 0; return; }
        t->token = 0; t->holding = 1; t->got_at = g_now; k_sleep(t, t->hold); t->pc = 2; return;
    default:                                       /* 다 썼다 → 반납 */
        t->holding = 0; ksem_give(&g_pool); t->served++;
        if (t->served >= t->rounds) { k_done(t); return; }
        t->pc = 0;
        if (t->think > 0) k_sleep(t, t->think);
        return;
    }
}
static void pool_check(void) {
    int h = 0, w = 0;
    for (int i = 0; i < g_ntask; i++) {
        h += g_task[i].holding + g_task[i].token;          /* 쓰는 중 + 넘겨받았지만 아직 못 돈 것 */
        w += g_task[i].state == ST_BLOCKED && g_task[i].waiting_on == &g_pool;
    }
    if (h > g_max_hold) g_max_hold = h;
    if (g_now < TL_LEN) g_hold_tl[g_now] = (char)('0' + h % 10);
    if (h + g_pool.count != g_pool_total || w != g_pool.nwait) g_conserve_err++;
    if (g_pool.count > 0 && g_pool.nwait > 0) g_idle_tok_err++;   /* 토큰이 남는데 누가 기다린다 */
}
static task_t *pool_add(const char *n, int prio, int rel, int hold, int think, int rounds) {
    task_t *t = k_add(n, prio, rel, pool_task);
    t->hold = hold; t->think = think; t->rounds = rounds;
    return t;
}
static void pool_setup(int tokens) {
    k_reset();
    ksem_init(&g_pool, tokens, tokens);
    g_pool_total = tokens; g_max_hold = g_conserve_err = g_idle_tok_err = g_bad_wake = 0;
    g_check_hook = pool_check; g_watch = &g_pool;
}

/* ---- Part B: 이벤트 카운트 (UART 프레임 ISR → 파서 태스크) ---- */
static ksem_t g_ev;
static int g_fast, g_slow, g_processed, g_target, g_rc_ok, g_rc_full, g_isr_from, g_isr_to, g_late_isr;
static bool g_late_woken;
static void motor_task(task_t *t) { if (++t->served >= t->hold) k_done(t); }   /* hold tick 동안 CPU 계산 */
static void parser_task(task_t *t) {
    if (t->pc == 0) {
        if (ksem_take(&g_ev, t)) { g_fast++; t->pc = 2; } else { g_slow++; t->pc = 1; }
        return;
    }
    if (t->pc == 1) t->token = 0;                  /* 블록 → 넘겨받음 */
    g_processed++;                                 /* 프레임 1개 처리 */
    t->pc = 0;
    if (g_processed >= g_target) k_done(t);
}
static void ev_isr(int now) {
    bool woken = false;
    if (now >= g_isr_from && now <= g_isr_to) {
        if (ksem_give_from_isr(&g_ev, &woken) == SEM_OK) g_rc_ok++; else g_rc_full++;
    }
    if (now == g_late_isr) { ksem_give_from_isr(&g_ev, &woken); g_late_woken = woken; }
}
static void ev_setup(int max, int from, int to, int late, int target) {
    k_reset();
    ksem_init(&g_ev, 0, max);
    g_fast = g_slow = g_processed = g_rc_ok = g_rc_full = 0; g_late_woken = false;
    g_isr_from = from; g_isr_to = to; g_late_isr = late; g_target = target;
    g_tick_hook = ev_isr; g_watch = &g_ev;
    task_t *m = k_add("MOTOR", 3, 0, motor_task); m->hold = 7;
    k_add("PARSE", 2, 0, parser_task);
}

/* ---- Part C: DMA 드라이버 사용자 태스크 + DMA 하드웨어 모델 ---- */
static int g_claim_fail, g_start_err, g_xfer_done, g_woken_true, g_woken_mismatch, g_dma_over;
static void dma_start(dma_t *d, int ch, task_t *t, int len) {
    if (ch < 0 || ch >= DMA_NCH) { g_claim_fail++; return; }
    if (!(d->used & (1u << ch)) || d->owner[ch] || d->left[ch]) { g_start_err++; return; }
    d->owner[ch] = t; d->left[ch] = len;
}
static void dma_user(task_t *t) {
    if (t->pc == 0) {
        if (!ksem_take(&g_dma.free, t)) { t->pc = 1; return; }
    } else {
        t->token = 0;
    }
    t->ch = dma_claim(&g_dma);
    dma_start(&g_dma, t->ch, t, t->hold);          /* 버퍼를 DMA 에 넘기고 (완료는 ISR 이 알린다) */
    if (++t->served >= t->rounds) { k_done(t); return; }
    t->pc = 0;
    k_sleep(t, t->think);                          /* 다음 버퍼 준비 */
}
static void dma_hw_tick(int now) {                 /* tick hook: 전송 진행 → 끝나면 완료 ISR */
    (void)now;
    for (int ch = 0; ch < DMA_NCH; ch++) {
        if (!g_dma.owner[ch] || --g_dma.left[ch] > 0) continue;
        task_t *best = NULL;                       /* 오라클: 기대하는 woken 값 */
        for (int i = 0; i < g_ntask; i++) {
            task_t *o = &g_task[i];
            if (o->state == ST_BLOCKED && o->waiting_on == &g_dma.free &&
                (!best || o->prio > best->prio || (o->prio == best->prio && o->wait_seq < best->wait_seq)))
                best = o;
        }
        bool expect = best && best->prio > (g_cur ? g_cur->prio : 0);
        bool woken = false;
        g_dma.owner[ch] = NULL;
        g_dma.xfers[ch]++;
        g_xfer_done++;
        dma_complete_isr(&g_dma, ch, &woken);
        g_woken_true += woken;
        g_woken_mismatch += woken != expect;
    }
}
static void dma_check(void) {
    int busy = 0, tok = 0;
    for (int ch = 0; ch < DMA_NCH; ch++) busy += (g_dma.used >> ch) & 1u;
    for (int i = 0; i < g_ntask; i++) tok += g_task[i].token;
    if (busy > DMA_NCH || busy + tok + g_dma.free.count != DMA_NCH) g_dma_over++;
}

/* ---- 결정적 PRNG ---- */
static uint32_t g_rng;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rnd(int lo, int hi) { return lo + (int)(xs32() % (uint32_t)(hi - lo + 1)); }

int main(void) {
    static task_t dummy[6];
    ksem_t s;
    (void)k_block; (void)k_unblock; (void)crit_enter; (void)crit_exit;   /* starter 가 아직 안 부르는 */
    (void)dma_mark_used; (void)dma_mark_free;                            /* 커널 서비스: 경고 방지 */

    /* ---------- Part A: 단위 테스트 ---------- */
    CHECK(!ksem_init(&s, 4, 3) && !ksem_init(&s, 0, 0) && !ksem_init(&s, -1, 3));
    CHECK(ksem_init(&s, 0, 3) && s.count == 0 && s.max == 3 && s.nwait == 0);

    const char *nm[] = { "a", "b", "c", "d", "e" };
    int pr[] = { 1, 3, 2, 3, 1 };
    ksem_init(&s, 0, 3);
    for (int i = 0; i < 5; i++) { memset(&dummy[i], 0, sizeof dummy[i]); dummy[i].name = nm[i]; dummy[i].prio = pr[i]; wait_insert(&s, &dummy[i]); }
    char order[16] = "";
    for (task_t *t; (t = wait_pop(&s)) != NULL; ) strcat(order, t->name);
    CHECK(strcmp(order, "bdcae") == 0);            /* 우선순위 3,3,2,1,1 — 같은 우선순위는 넣은 순 */
    CHECK(s.nwait == 0 && s.wait_head == NULL);

    k_reset();
    task_t *x = k_add("X", 2, 0, pool_task), *y = k_add("Y", 3, 0, pool_task), *z = k_add("Z", 1, 0, pool_task);
    ksem_init(&s, 2, 3);
    CHECK(ksem_take(&s, x) && ksem_take(&s, x) && s.count == 0);
    CHECK(!ksem_take(&s, y) && y->state == ST_BLOCKED && s.nwait == 1);
    CHECK(ksem_give(&s) == SEM_OK && y->token == 1 && y->state == ST_READY && s.count == 0);  /* handoff */
    CHECK(ksem_give(&s) == SEM_OK && ksem_give(&s) == SEM_OK && ksem_give(&s) == SEM_OK && s.count == 3);
    CHECK(ksem_give(&s) == SEM_FULL && s.count == 3 && s.full_drops == 1);                     /* 포화 */

    bool woken = false;                            /* ISR give: 끼어든 태스크 X(2) */
    ksem_init(&s, 0, 3); ksem_take(&s, y); ksem_take(&s, z);
    g_cur = x; g_in_isr = true;
    CHECK(ksem_give_from_isr(&s, &woken) == SEM_OK && woken && y->token);   /* Y(3) > X(2) */
    woken = false;
    CHECK(ksem_give_from_isr(&s, &woken) == SEM_OK && !woken && z->token);  /* Z(1) < X(2) */
    woken = true;
    CHECK(ksem_give_from_isr(&s, &woken) == SEM_OK && woken && s.count == 1); /* true 는 지우지 않는다 */
    CHECK(!ksem_take(&s, x) && g_isr_take_err == 1 && s.count == 1 && x->state != ST_BLOCKED);
    g_in_isr = false;

    /* ---------- Part B1: DMA 채널 3개 × 태스크 5개 ---------- */
    pool_setup(3);
    pool_add("A", 3, 0, 6, 1, 3);
    pool_add("B", 2, 0, 7, 1, 3);
    pool_add("C", 2, 1, 5, 2, 3);
    pool_add("D", 1, 1, 6, 0, 3);
    pool_add("E", 1, 2, 4, 1, 3);
    int end = k_run(200);
    printf("Part B1 pool: 3 channels, 5 tasks, 3 rounds each -> all done at tick %d\n", end);
    k_print(end);
    printf("wake order: %s\n", g_wakes);
    printf("blocked ticks: A=%d B=%d C=%d D=%d E=%d\n", g_task[0].blocked_ticks, g_task[1].blocked_ticks,
           g_task[2].blocked_ticks, g_task[3].blocked_ticks, g_task[4].blocked_ticks);
    CHECK(k_all_done() && end < 200);
    int served_ok = 1;
    for (int i = 0; i < 5; i++) served_ok &= g_task[i].served == 3;
    CHECK(served_ok);
    CHECK(g_max_hold == 3);                        /* 3 개까지는 쓰고, 그 이상은 절대 아님 */
    CHECK(g_conserve_err == 0 && g_idle_tok_err == 0 && g_bad_wake == 0);
    CHECK(g_order_err == 0 && g_pool.count == 3 && g_pool.nwait == 0);

    /* ---------- Part B2: 깨우는 순서 = 우선순위 → FIFO ---------- */
    pool_setup(3);
    pool_add("H1", 4, 0, 8, 0, 1);
    pool_add("H2", 4, 0, 9, 0, 1);
    pool_add("H3", 4, 0, 10, 0, 1);
    pool_add("W1", 1, 3, 1, 0, 1);                 /* 블록 순서: W1(1) W2(2) W3(3) W4(1) W5(2) */
    pool_add("W2", 2, 4, 1, 0, 1);
    pool_add("W3", 3, 5, 1, 0, 1);
    pool_add("W4", 1, 6, 1, 0, 1);
    pool_add("W5", 2, 7, 1, 0, 1);
    end = k_run(100);
    printf("Part B2 wake order: %s (done at tick %d)\n", g_wakes, end);
    k_print(end);
    CHECK(strcmp(g_wakes, "W3 W2 W5 W1 W4") == 0);
    CHECK(g_order_err == 0 && k_all_done() && g_max_hold == 3);

    /* ---------- Part B3: 무작위 300 시나리오 (xorshift32, 고정 seed) ---------- */
    int fails = 0, worst_end = 0, total_wakes = 0;
    for (int seed = 1; seed <= 300; seed++) {
        g_rng = 0x9E3779B9u ^ (uint32_t)seed * 2654435761u;
        pool_setup(3);
        static const char *rn[] = { "R0", "R1", "R2", "R3", "R4" };
        for (int i = 0; i < 5; i++) pool_add(rn[i], rnd(1, 3), rnd(0, 3), rnd(1, 8), rnd(0, 3), 4);
        end = k_run(400);
        int ok = k_all_done() && g_max_hold <= 3 && !g_conserve_err && !g_idle_tok_err && !g_bad_wake && !g_order_err;
        for (int i = 0; i < 5; i++) ok &= g_task[i].served == 4;
        fails += !ok;
        if (end > worst_end) worst_end = end;
        for (const char *p = g_wakes; *p; p++) total_wakes += *p == 'R';
    }
    printf("Part B3 random: 300 scenarios, fails=%d, worst finish tick=%d, handoffs=%d\n", fails, worst_end, total_wakes);
    CHECK(fails == 0);
    CHECK(total_wakes > 300);                      /* 실제로 많이 블록되고 넘겨받았다 */

    /* ---------- Part B4: 이벤트 카운트 — take 전에 give 5번 ---------- */
    ev_setup(8, 1, 5, 20, 6);                      /* ISR: tick 1..5 에 give, tick 20 에 한 번 더 */
    end = k_run(40);
    printf("Part B4 event count (max 8): fast takes=%d, blocked takes=%d, processed=%d, done tick %d\n",
           g_fast, g_slow, g_processed, end);
    k_print(end < 24 ? end : 24);
    CHECK(g_rc_ok == 5 && g_rc_full == 0);
    CHECK(g_fast == 5 && g_slow == 1 && g_processed == 6);   /* 5번은 블록 없이, 6번째만 블록 */
    CHECK(g_late_woken && k_all_done());                     /* idle 에 끼어든 ISR → woken */

    /* ---------- Part B5: 최대값 포화 ---------- */
    ev_setup(4, 1, 6, 20, 5);                      /* max 4 인데 6번 give */
    end = k_run(40);
    printf("Part B5 saturation (max 4, 6 gives): ok=%d full=%d drops=%ld fast=%d processed=%d\n",
           g_rc_ok, g_rc_full, g_ev.full_drops, g_fast, g_processed);
    CHECK(g_rc_ok == 4 && g_rc_full == 2 && g_ev.full_drops == 2);
    CHECK(g_fast == 4 && g_processed == 5);        /* 4개 + tick 20 의 1개. 2개는 잃었다 */
    ev_setup(1, 1, 6, 20, 2);                      /* 바이너리(max 1): 합쳐진다 (RTOS-B08) */
    k_run(40);
    printf("Part B5 binary   (max 1, 6 gives): ok=%d full=%d fast=%d processed=%d\n",
           g_rc_ok, g_rc_full, g_fast, g_processed);
    CHECK(g_rc_ok == 1 && g_rc_full == 5 && g_fast == 1);

    /* ---------- Part C: DMA 채널 드라이버 ---------- */
    k_reset();
    memset(&g_dma, 0, sizeof g_dma);
    ksem_init(&g_dma.free, DMA_NCH, DMA_NCH);
    g_crit_depth = g_crit_err = g_claim_fail = g_start_err = g_xfer_done = g_woken_true = g_woken_mismatch = g_dma_over = 0;
    g_tick_hook = dma_hw_tick; g_check_hook = dma_check; g_watch = &g_dma.free;
    static const struct { const char *n; int prio, rel, len, think; } U[5] = {
        { "ADC",   3, 0, 4, 3 },                       /* 모터 전류 ADC 샘플 → 메모리 */
        { "I2S",   3, 0, 6, 3 },                       /* 오디오 버퍼 */
        { "FLASH", 2, 1, 8, 1 },                       /* SPI 플래시 페이지 쓰기 */
        { "UART",  2, 1, 5, 2 },                       /* 텔레메트리 TX */
        { "LOG",   1, 2, 6, 1 },                       /* 로그 덤프 */
    };
    for (int i = 0; i < 5; i++) {
        task_t *t = k_add(U[i].n, U[i].prio, U[i].rel, dma_user);
        t->hold = U[i].len; t->think = U[i].think; t->rounds = 4;
    }
    end = k_run(200);
    while (g_now < 200 && (g_dma.owner[0] || g_dma.owner[1] || g_dma.owner[2])) k_tick();   /* 마지막 전송 마무리 */
    printf("Part C DMA: transfers=%d (ch0=%d ch1=%d ch2=%d), woken=true %d times, tasks done at tick %d\n",
           g_xfer_done, g_dma.xfers[0], g_dma.xfers[1], g_dma.xfers[2], g_woken_true, end);
    k_print(end);
    CHECK(k_all_done() && g_xfer_done == 20 && g_claim_fail == 0 && g_start_err == 0);
    CHECK(g_dma_over == 0 && g_dma.used == 0 && g_dma.free.count == DMA_NCH);
    CHECK(g_crit_err == 0 && g_crit_depth == 0);
    CHECK(g_woken_mismatch == 0 && g_woken_true > 0 && g_woken_true < g_xfer_done);
    CHECK(g_order_err == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
