/* ARCH-M09 추가 연습 — 한 번에 검증
 *   연습 1  도착 시각 스윕 → 최우선 IRQ 의 최악 지연과 그 시각 (exit 중에 오면 X-1+E)
 *   연습 2  pop-preemption: exit(꺼내기) 중 더 급한 IRQ → 꺼내기를 버리고 tail 로 (최악 지연 = E)
 *   연습 3  decode_prio: encode 의 역함수 + 전수 왕복, PRIGROUP 별 (그룹 레벨 수, 서브 레벨 수) 표
 *   연습 4  잃어버린 인터럽트: pending 은 1비트 → 이미 pending 일 때 온 사건은 합쳐진다 (UART FIFO 비우기)
 * 엔진은 solution.c 와 같고, timing_t.pop 과 sim_t.lost 만 더했다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NIRQ        32
#define PRIO_THREAD 256u            /* 활성 예외가 없을 때의 실행 우선순위 = 어떤 IRQ 보다 낮다 */
#define NEVER       0xFFFFFFFFu
#define TRACE_MAX   1024u

typedef struct {
    uint32_t enabled, pending, active;  /* 비트 k = IRQk */
    uint8_t  prio[NIRQ];                /* 저장된 값 (구현 안 된 하위 비트는 0) */
    unsigned prio_bits;                 /* 구현된 우선순위 비트 수 2..8 (상위 비트부터) */
    unsigned prigroup;                  /* 0..7: 비트 [7:prigroup+1] = 그룹, [prigroup:0] = 서브 */
    uint8_t  primask;                   /* 1 이면 설정 가능한 모든 IRQ 차단 (실행 우선순위 0) */
    uint8_t  basepri;                   /* 0 = 꺼짐, 아니면 그룹 우선순위가 group(basepri) 이상인 IRQ 차단 */
} nvic_t;

/* ======================= Part A: 우선순위 산수 ======================= */

/* A1. 저장되는 우선순위 값: 구현된 상위 prio_bits 비트만 남기고 하위는 0 (쓰기 무시, 0 으로 읽힘). */
static unsigned prio_store(const nvic_t *n, unsigned p) {
    return p & (0xFFu << (8u - n->prio_bits)) & 0xFFu;
}

/* A2. 그룹 우선순위 = 서브 비트 [prigroup:0] 를 0 으로 만든 값 (prigroup 7 → 그룹 비트 없음 → 0). */
static unsigned group_of(const nvic_t *n, unsigned p) {
    return p & (0xFFu << (n->prigroup + 1u)) & 0xFFu;
}

/* A3. 실행 우선순위 (작을수록 급함): active IRQ 들의 그룹 우선순위 최솟값, BASEPRI(≠0)의 그룹 값,
 *     PRIMASK=1 이면 0. 아무것도 없으면 PRIO_THREAD(256). */
static unsigned exec_prio(const nvic_t *n) {
    unsigned ep = PRIO_THREAD;
    for (unsigned k = 0; k < NIRQ; k++)
        if (n->active >> k & 1u) {
            unsigned g = group_of(n, n->prio[k]);
            if (g < ep) ep = g;
        }
    if (n->basepri && group_of(n, n->basepri) < ep) ep = group_of(n, n->basepri);
    if (n->primask) ep = 0;
    return ep;
}

/* A4. 대기 승자: enabled & pending 중 (우선순위 값, IRQ 번호) 가 가장 작은 것. 없으면 -1.
 *     (값 = 그룹+서브 전체 → 같은 그룹이면 서브가, 그것도 같으면 번호가 순서를 정한다) */
static int pick_pending(const nvic_t *n) {
    uint32_t cand = n->enabled & n->pending;
    int best = -1;
    for (unsigned k = 0; k < NIRQ; k++)
        if ((cand >> k & 1u) && (best < 0 || n->prio[k] < n->prio[best])) best = (int)k;
    return best;
}

/* 주어짐: irq 가 지금 코어를 빼앗을 수 있나 = 그룹 우선순위가 실행 우선순위보다 "엄격히" 급한가 */
static bool can_take(const nvic_t *n, int irq) {
    return irq >= 0 && group_of(n, n->prio[irq]) < exec_prio(n);
}
static void nvic_set_prio(nvic_t *n, unsigned irq, unsigned p) { n->prio[irq] = (uint8_t)prio_store(n, p); }

/* ======================= 시뮬레이터 (solution.c 와 같음 + pop/lost) ======================= */

enum { PH_THREAD, PH_ENTRY, PH_HANDLER, PH_TAIL, PH_EXIT };
enum { EV_PEND, EV_ENABLE, EV_DISABLE, EV_PRIMASK, EV_BASEPRI };   /* 외부 사건 (시각 순 정렬) */
enum { ACT_SETPEND, ACT_CLRPEND };                                  /* 핸들러 안의 동작 */

typedef struct { uint32_t t; uint8_t kind, arg; } ev_t;            /* arg = IRQ 번호 또는 값 */
typedef struct { uint16_t at; uint8_t op, irq; } act_t;            /* 핸들러가 at 사이클 진행했을 때 */
typedef struct { uint16_t dur; uint8_t nact; act_t act[4]; } handler_t;
typedef struct { uint8_t irq, next_act; uint16_t progress; } frame_t;
typedef struct { unsigned entry, tail, exit; bool tailchain, pop; } timing_t;   /* pop = 연습 2 */

static const timing_t TM = { 12, 6, 10, true, false };   /* Cortex-M3/M4 에서 흔히 인용되는 값 (모델 파라미터) */

typedef struct {
    nvic_t n;
    timing_t tm;
    const handler_t *h;                 /* h[irq] */
    const ev_t *ev; unsigned nev, ei;
    uint32_t now;
    int phase, target;                  /* target = ENTRY/TAIL 이 들어갈 IRQ */
    unsigned remaining;                 /* ENTRY/TAIL/EXIT 남은 사이클 */
    frame_t stk[NIRQ]; unsigned depth;  /* 활성 핸들러 문맥 (맨 위 = 실행 중 또는 막 끝난 것) */
    unsigned n_entry, n_tail, n_late, n_exit, n_pop, max_depth;
    uint32_t overhead, thread_cyc, handler_cyc;
    uint32_t pend_t[NIRQ], lat_max[NIRQ], runs[NIRQ], nested_in[NIRQ], lost[NIRQ];   /* lost = 연습 4 */
    int err;
    char log[8192]; size_t loglen;
    signed char who[TRACE_MAX]; uint32_t act_tr[TRACE_MAX];
} sim_t;

static void slog(sim_t *s, const char *fmt, ...) {
    if (s->loglen + 64 >= sizeof s->log) { s->err = 5; return; }
    s->loglen += (size_t)snprintf(s->log + s->loglen, sizeof s->log - s->loglen, "@%u ", s->now);
    va_list ap;
    va_start(ap, fmt);
    s->loglen += (size_t)vsnprintf(s->log + s->loglen, sizeof s->log - s->loglen, fmt, ap);
    va_end(ap);
    s->log[s->loglen++] = '\n';
    s->log[s->loglen] = '\0';
}

static void set_pend(sim_t *s, unsigned irq) {
    if (!(s->n.pending >> irq & 1u)) s->pend_t[irq] = s->now;   /* 처음 pending 된 시각 = 지연 기준 */
    else s->lost[irq]++;                                        /* 연습 4: 이미 pending → 이번 사건은 합쳐져 사라짐 */
    s->n.pending |= 1u << irq;
}

static void apply_ext(sim_t *s) {
    while (s->ei < s->nev && s->ev[s->ei].t == s->now) {
        const ev_t *e = &s->ev[s->ei++];
        switch (e->kind) {
        case EV_PEND:    set_pend(s, e->arg); slog(s, "pend IRQ%u", e->arg); break;
        case EV_ENABLE:  s->n.enabled |= 1u << e->arg; slog(s, "enable IRQ%u", e->arg); break;
        case EV_DISABLE: s->n.enabled &= ~(1u << e->arg); slog(s, "disable IRQ%u", e->arg); break;
        case EV_PRIMASK: s->n.primask = e->arg & 1u; slog(s, "primask %u", s->n.primask); break;
        default:         s->n.basepri = (uint8_t)prio_store(&s->n, e->arg); slog(s, "basepri 0x%02X", s->n.basepri); break;
        }
    }
}

static void run_actions(sim_t *s, frame_t *f) {
    const handler_t *h = &s->h[f->irq];
    while (f->next_act < h->nact && h->act[f->next_act].at == f->progress) {
        const act_t *a = &h->act[f->next_act++];
        if (a->op == ACT_SETPEND) { set_pend(s, a->irq); slog(s, "IRQ%u setpend IRQ%u", f->irq, a->irq); }
        else { s->n.pending &= ~(1u << a->irq); slog(s, "IRQ%u clrpend IRQ%u", f->irq, a->irq); }
    }
}

static void activate(sim_t *s) {             /* ENTRY/TAIL 끝: 벡터 확정 → 핸들러 시작 */
    unsigned k = (unsigned)s->target;
    if (s->depth) s->nested_in[k] |= 1u << s->stk[s->depth - 1].irq;
    s->n.pending &= ~(1u << k);
    s->n.active |= 1u << k;
    s->stk[s->depth++] = (frame_t){ .irq = (uint8_t)k };
    if (s->depth > s->max_depth) s->max_depth = s->depth;
    if (s->now - s->pend_t[k] > s->lat_max[k]) s->lat_max[k] = s->now - s->pend_t[k];
    s->runs[k]++;
    s->phase = PH_HANDLER;
    s->target = -1;
    slog(s, "start IRQ%u", k);
}

static void resume(sim_t *s) {               /* EXIT 끝: 쌓아 둔 문맥으로 돌아간다 */
    if (s->depth == 0) { s->phase = PH_THREAD; slog(s, "resume thread"); }
    else { s->phase = PH_HANDLER; slog(s, "resume IRQ%u", s->stk[s->depth - 1].irq); }
}

/* ======================= Part B: 코어 상태 기계 ======================= */

/* B1. THREAD 또는 HANDLER 실행 중: 대기 승자가 코어를 빼앗을 수 있으면 ENTRY 시작 + true.
 *     로그 "entry IRQb" (thread 에서) / "entry IRQb over IRQa" (핸들러 a 를 선점). n_entry++. */
static bool try_preempt(sim_t *s) {
    int b = pick_pending(&s->n);
    if (!can_take(&s->n, b)) return false;
    if (s->phase == PH_HANDLER) slog(s, "entry IRQ%d over IRQ%u", b, s->stk[s->depth - 1].irq);
    else slog(s, "entry IRQ%d", b);
    s->phase = PH_ENTRY;
    s->remaining = s->tm.entry;
    s->target = b;
    s->n_entry++;
    return true;
}

/* B2. ENTRY/TAIL 이 아직 안 끝났을 때 (remaining > 0): 대기 승자의 그룹 우선순위가 지금 목표보다
 *     엄격히 급하면 목표를 바꾼다 (쌓기는 그대로, 벡터만 바뀜). 로그 "late IRQb (was IRQa)". n_late++. */
static void late_arrival(sim_t *s) {
    int b = pick_pending(&s->n);
    if (b < 0 || b == s->target) return;
    if (group_of(&s->n, s->n.prio[b]) < group_of(&s->n, s->n.prio[s->target])) {
        slog(s, "late IRQ%d (was IRQ%d)", b, s->target);
        s->target = b;
        s->n_late++;
    }
}

/* B3. 핸들러가 끝났다: 프레임 pop + active 해제, 로그 "done IRQk". 그다음
 *     tm.tailchain 이고 대기 승자가 (돌아갈 문맥 기준으로) 코어를 빼앗을 수 있으면 → TAIL (tm.tail, "tail IRQb", n_tail++)
 *     아니면 → EXIT (tm.exit, "exit", n_exit++). */
static void handler_return(sim_t *s) {
    unsigned k = s->stk[--s->depth].irq;
    s->n.active &= ~(1u << k);
    slog(s, "done IRQ%u", k);
    int b = pick_pending(&s->n);
    if (s->tm.tailchain && can_take(&s->n, b)) {
        s->phase = PH_TAIL;
        s->remaining = s->tm.tail;
        s->target = b;
        s->n_tail++;
        slog(s, "tail IRQ%d", b);
    } else {
        s->phase = PH_EXIT;
        s->remaining = s->tm.exit;
        s->n_exit++;
        slog(s, "exit");
    }
}

/* 한 시각에서 더 바뀔 것이 없을 때까지 상태를 진행한다 (외부 사건은 이미 반영됨). */
static void settle(sim_t *s) {
    for (int step = 0; step < 200; step++) {
        if (s->err) return;
        switch (s->phase) {
        case PH_THREAD:
            if (!try_preempt(s)) return;
            break;
        case PH_ENTRY: case PH_TAIL:
            if (s->remaining) { late_arrival(s); return; }
            activate(s);
            break;
        case PH_HANDLER: {
            frame_t *f = &s->stk[s->depth - 1];
            run_actions(s, f);
            if (f->progress >= s->h[f->irq].dur) handler_return(s);
            else if (!try_preempt(s)) return;
            break;
        }
        default:                                            /* PH_EXIT */
            if (s->remaining && s->tm.pop) {                /* 연습 2: pop-preemption — 꺼내기를 버리고 바로 tail */
                int b = pick_pending(&s->n);
                if (!can_take(&s->n, b)) return;
                slog(s, "pop IRQ%d", b);
                s->phase = PH_TAIL; s->remaining = s->tm.tail; s->target = b; s->n_pop++;
                break;
            }
            if (s->remaining) return;
            resume(s);
            break;
        }
    }
    s->err = 3;                                             /* 한 시각에서 전이가 끝나지 않음 */
}

static uint32_t core_dt(const sim_t *s) {                   /* 코어 쪽 다음 사건까지 남은 사이클 */
    if (s->phase == PH_THREAD) return NEVER;
    if (s->phase != PH_HANDLER) return s->remaining;
    const frame_t *f = &s->stk[s->depth - 1];
    const handler_t *h = &s->h[f->irq];
    unsigned m = f->next_act < h->nact ? h->act[f->next_act].at : h->dur;
    return m > f->progress ? m - f->progress : 0;
}

static void advance(sim_t *s, uint32_t dt) {
    for (uint32_t i = 0; i < dt && s->now + i < TRACE_MAX; i++) {
        s->who[s->now + i] = (signed char)(s->phase == PH_HANDLER ? (int)s->stk[s->depth - 1].irq : -1 - s->phase);
        s->act_tr[s->now + i] = s->n.active;
    }
    if (s->phase == PH_THREAD) s->thread_cyc += dt;
    else if (s->phase == PH_HANDLER) { s->stk[s->depth - 1].progress = (uint16_t)(s->stk[s->depth - 1].progress + dt); s->handler_cyc += dt; }
    else { s->remaining -= dt; s->overhead += dt; }
    s->now += dt;
}

static void sim_init(sim_t *s, const nvic_t *n, timing_t tm, const handler_t *h, const ev_t *ev, unsigned nev) {
    memset(s, 0, sizeof *s);
    s->n = *n; s->tm = tm; s->h = h; s->ev = ev; s->nev = nev;
    s->phase = PH_THREAD; s->target = -1;
    for (unsigned i = 1; i < nev; i++) if (ev[i].t < ev[i - 1].t) s->err = 1;       /* 사건은 시각 순 */
    for (unsigned k = 0; k < NIRQ; k++) {
        if (h[k].nact > 4) s->err = 2;
        for (unsigned j = 0; j < h[k].nact && j < 4; j++)
            if (h[k].act[j].at >= h[k].dur || (j && h[k].act[j].at < h[k].act[j - 1].at)) s->err = 2;
    }
}

/* 시뮬레이션: 다음 사건(외부 사건 / 코어 단계 끝 / 핸들러 동작) 시각으로 건너뛴다. 반환 0 = 정상 */
static int sim_run(sim_t *s, uint32_t limit) {
    if (s->err) return s->err;
    apply_ext(s);
    settle(s);
    while (!s->err) {
        uint32_t dte = s->ei < s->nev ? s->ev[s->ei].t - s->now : NEVER;
        uint32_t dtc = core_dt(s);
        uint32_t dt = dte < dtc ? dte : dtc;
        if (dt == NEVER) break;                             /* 남은 사건 없음, thread 에서 쉰다 */
        if (dt == 0 || s->now + dt > limit) { s->err = 4; break; }
        advance(s, dt);
        apply_ext(s);
        settle(s);
    }
    return s->err;
}

/* ======================= encode_prio (solution.c 와 같음) ======================= */

/* C1. (그룹, 서브) → 저장될 8비트 우선순위 값 (CMSIS NVIC_EncodePriority + 시프트를 합친 것).
 *     서브 필드 = 비트 [prigroup : 8-prio_bits] (구현된 것만), 그룹 필드 = 그 위 구현 비트.
 *     각 값은 자기 필드 폭으로 잘라 넣는다. 서브 필드가 없으면 sub 는 무시. */
static unsigned encode_prio(const nvic_t *n, unsigned group, unsigned sub) {
    unsigned lsb = 8u - n->prio_bits;                       /* 구현된 가장 낮은 비트 */
    unsigned split = n->prigroup + 1u;                      /* 그룹 필드가 시작하는 비트 */
    if (split < lsb) split = lsb;                           /* 서브 비트가 모두 미구현 → 서브 필드 없음 */
    unsigned gbits = 8u - split, sbits = split - lsb;
    unsigned g = gbits ? (group & ((1u << gbits) - 1u)) << split : 0u;
    unsigned sv = sbits ? (sub & ((1u << sbits) - 1u)) << lsb : 0u;
    return g | sv;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                          /* xorshift32, 고정 시드 → 결정적 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static sim_t S;
static handler_t H[NIRQ];
static nvic_t mk(unsigned bits, unsigned pg) {
    nvic_t n;
    memset(&n, 0, sizeof n);
    n.prio_bits = bits; n.prigroup = pg; n.enabled = 0xFFFFFFFFu;
    return n;
}
static void hreset(void) { memset(H, 0, sizeof H); for (int k = 0; k < NIRQ; k++) H[k].dur = 1; }
static int run(const nvic_t *n, timing_t tm, const ev_t *ev, unsigned nev) {
    sim_init(&S, n, tm, H, ev, nev);
    return sim_run(&S, 100000);
}
#define RUN(n, tm, ev) run(&(n), (tm), (ev), (unsigned)(sizeof(ev) / sizeof(ev)[0]))

static bool log_is(const char *exp) {
    if (strcmp(S.log, exp) == 0) return true;
    const char *a = S.log, *b = exp;                        /* 처음 다른 줄을 보여 준다 */
    int line = 1;
    while (*a && *a == *b) { if (*a == '\n') line++; a++; b++; }
    while (a > S.log && a[-1] != '\n') a--;
    while (b > exp && b[-1] != '\n') b--;
    printf("  log line %d: got \"%.*s\" expected \"%.*s\"\n", line,
           (int)strcspn(a, "\n"), a, (int)strcspn(b, "\n"), b);
    return false;
}

/* ---------- 연습 1: 도착 시각 스윕 ---------- */
/* IRQ5 (0x40, 20 사이클) 가 t=0 에 오고, 최우선 IRQ0 (0x00, 5 사이클) 의 도착 시각 a 를 0..amax 로 옮긴다.
 * 최악 지연과 처음 그 값이 나온 a 를 돌려준다. lat[a] 에 지연 기록. */
static unsigned sweep(timing_t tm, unsigned amax, unsigned *arg, unsigned *lat) {
    unsigned worst = 0;
    for (unsigned a = 0; a <= amax; a++) {
        hreset(); H[5].dur = 20; H[0].dur = 5;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 5, 0x40); nvic_set_prio(&n, 0, 0x00);
        ev_t ev[2] = { {0, EV_PEND, 5}, {a, EV_PEND, 0} };
        run(&n, tm, ev, 2);
        lat[a] = S.lat_max[0];
        if (lat[a] > worst) { worst = lat[a]; *arg = a; }
    }
    return worst;
}

/* ---------- 연습 3: decode_prio ---------- */
static void decode_prio(const nvic_t *n, unsigned v, unsigned *group, unsigned *sub) {
    unsigned lsb = 8u - n->prio_bits, split = n->prigroup + 1u;
    if (split < lsb) split = lsb;
    unsigned sbits = split - lsb;
    *group = split < 8u ? v >> split : 0u;
    *sub = sbits ? (v >> lsb) & ((1u << sbits) - 1u) : 0u;
}
static void levels(const nvic_t *n, unsigned *ng, unsigned *ns) {   /* 그룹 레벨 수, 서브 레벨 수 */
    unsigned lsb = 8u - n->prio_bits, split = n->prigroup + 1u;
    if (split < lsb) split = lsb;
    *ng = 1u << (8u - split);
    *ns = 1u << (split - lsb);
}

int main(void) {
    /* 연습 1 */
    unsigned lat[61], arg = 0;
    unsigned w = sweep(TM, 60, &arg, lat);
    CHECK(w == 21 && arg == 33 && w == TM.exit - 1 + TM.entry);
    CHECK(lat[0] == 12 && lat[5] == 7 && lat[12] == 12 && lat[20] == 12 && lat[32] == 6 && lat[41] == 13 && lat[42] == 12);
    printf("  sweep (pop 없음): a=0..60 지연 =");
    for (unsigned a = 0; a <= 60; a += 3) printf(" %u", lat[a]);
    printf("  → 최악 %u @a=%u\n", w, arg);

    /* 연습 2 */
    timing_t pp = TM; pp.pop = true;
    unsigned lat2[61], arg2 = 0;
    unsigned w2 = sweep(pp, 60, &arg2, lat2);
    CHECK(w2 == 12 && lat2[33] == 6 && lat2[41] == 6);
    printf("  sweep (pop 있음): 최악 %u, a=33 → %u (꺼내기를 버리고 tail 6)\n", w2, lat2[33]);
    {
        hreset(); H[5].dur = 20; H[0].dur = 5;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 5, 0x40); nvic_set_prio(&n, 0, 0x00);
        static const ev_t ev[] = { {0, EV_PEND, 5}, {35, EV_PEND, 0} };
        CHECK(RUN(n, pp, ev) == 0 && S.n_pop == 1);
        CHECK(log_is("@0 pend IRQ5\n@0 entry IRQ5\n@12 start IRQ5\n@32 done IRQ5\n@32 exit\n@35 pend IRQ0\n@35 pop IRQ0\n"
                     "@41 start IRQ0\n@46 done IRQ0\n@46 exit\n@56 resume thread\n"));
    }

    /* 연습 3 */
    {
        unsigned bad = 0;
        for (unsigned bits = 2; bits <= 8; bits++)
            for (unsigned pg = 0; pg < 8; pg++) {
                nvic_t n = mk(bits, pg);
                unsigned ng, ns;
                levels(&n, &ng, &ns);
                if (ng * ns != (1u << bits)) bad++;                     /* 레벨 곱 = 2^구현비트 */
                for (unsigned v = 0; v < 256; v++) {
                    if (prio_store(&n, v) != v) continue;               /* 저장 가능한 값만 */
                    unsigned g, s;
                    decode_prio(&n, v, &g, &s);
                    if (g >= ng || s >= ns || encode_prio(&n, g, s) != v || group_of(&n, v) != encode_prio(&n, g, 0)) bad++;
                }
            }
        CHECK(bad == 0);
        static const unsigned exp_ng[8] = { 16, 16, 16, 16, 8, 4, 2, 1 };   /* 구현 4비트 (STM32F4 류) */
        bad = 0;
        for (unsigned pg = 0; pg < 8; pg++) {
            nvic_t n = mk(4, pg); unsigned ng, ns;
            levels(&n, &ng, &ns);
            if (ng != exp_ng[pg] || ns != 16u / exp_ng[pg]) bad++;
        }
        CHECK(bad == 0);
        nvic_t n = mk(4, 5); unsigned g, s;
        decode_prio(&n, 0x90, &g, &s);
        CHECK(g == 2 && s == 1);
    }

    /* 연습 4 */
    {
        hreset(); H[2].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 2, 0x80);
        static const ev_t ev[] = { {0, EV_PRIMASK, 1}, {10, EV_PEND, 2}, {20, EV_PEND, 2}, {30, EV_PEND, 2}, {40, EV_PEND, 2},
                                   {50, EV_PEND, 2}, {60, EV_PRIMASK, 0}, {75, EV_PEND, 2}, {78, EV_PEND, 2} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(S.runs[2] == 2 && S.lost[2] == 5);                        /* 바이트 7개 → 핸들러 2번 */
        CHECK(log_is("@0 primask 1\n@10 pend IRQ2\n@20 pend IRQ2\n@30 pend IRQ2\n@40 pend IRQ2\n@50 pend IRQ2\n@60 primask 0\n"
                     "@60 entry IRQ2\n@72 start IRQ2\n@75 pend IRQ2\n@78 pend IRQ2\n@82 done IRQ2\n@82 tail IRQ2\n@88 start IRQ2\n"
                     "@98 done IRQ2\n@98 exit\n@108 resume thread\n"));
        unsigned bad = 0;                                               /* 퍼즈: 사건 수 = 실행 + 잃음 */
        for (int it = 0; it < 2000; it++) {
            hreset();
            nvic_t m = mk(3, 0);
            for (unsigned k = 0; k < 6; k++) { nvic_set_prio(&m, k, xs32() & 0xE0u); H[k].dur = (uint16_t)(1 + xs32() % 20u); }
            ev_t e[12]; uint32_t t = 0; unsigned cnt[6] = {0};
            for (int i = 0; i < 12; i++) { t += xs32() % 8u; unsigned k = xs32() % 6u; e[i] = (ev_t){ t, EV_PEND, (uint8_t)k }; cnt[k]++; }
            timing_t tm = TM; tm.pop = xs32() & 1u;
            if (run(&m, tm, e, 12)) bad++;
            for (unsigned k = 0; k < 6; k++) if (cnt[k] != S.runs[k] + S.lost[k]) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
