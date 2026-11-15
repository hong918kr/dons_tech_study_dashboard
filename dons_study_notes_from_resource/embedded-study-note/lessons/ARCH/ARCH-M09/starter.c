/* ARCH-M09 인터럽트 컨트롤러 모델: 우선순위 · 마스킹 · Pending
 *
 * NVIC 와 비슷한 모델 (레지스터 비트 위치 = BM-M01, 스택 프레임 = ARM-M09 은 다루지 않는다):
 *   IRQ 32개, IRQ 마다 enable / pending / active 비트 + 8비트 우선순위 (구현 비트 수 설정 가능)
 *   PRIGROUP: 그룹 우선순위(선점 결정) / 서브 우선순위(대기 순서만)
 *   PRIMASK(전역 마스크) · BASEPRI(우선순위 문턱 마스크)
 *   이산 사건(discrete-event) 시뮬레이터: 도착 시각, 핸들러 길이, 핸들러가 다른 IRQ pending set/clear
 *   선점 · 중첩 · tail-chaining · late arrival → 로그를 기대 문자열과 글자 단위로 비교한다.
 *
 * 시간 단위 = 코어 사이클. entry 12 / tail-chain 6 / exit 10 은 Cortex-M3/M4 (0-wait 메모리) 에서
 * 흔히 인용되는 값을 "모델 파라미터" 로 쓴 것뿐이다 (실제 칩은 메모리 대기·FPU 등으로 달라진다).
 *
 *   Part A  우선순위 산수: prio_store · group_of · exec_prio · pick_pending
 *   Part B  코어 상태 기계: try_preempt · late_arrival · handler_return  (settle/run 루프는 주어짐)
 *   Part C  펌웨어 설정: encode_prio · fw_config (모터/ADC/UART/틱 + RTOS BASEPRI) · burst_overhead
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter      (solution.c 도 같은 방식, -v : 로그 + 타임라인)
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
    (void)n;
    return p; /* TODO: 구현된 상위 prio_bits 비트만 남긴다 */
}

/* A2. 그룹 우선순위 = 서브 비트 [prigroup:0] 를 0 으로 만든 값 (prigroup 7 → 그룹 비트 없음 → 0). */
static unsigned group_of(const nvic_t *n, unsigned p) {
    (void)n;
    return p; /* TODO: 서브 비트 [prigroup:0] 를 0 으로 */
}

/* A3. 실행 우선순위 (작을수록 급함): active IRQ 들의 그룹 우선순위 최솟값, BASEPRI(≠0)의 그룹 값,
 *     PRIMASK=1 이면 0. 아무것도 없으면 PRIO_THREAD(256). */
static unsigned exec_prio(const nvic_t *n) {
    (void)n;
    return PRIO_THREAD; /* TODO: active 그룹 최솟값, BASEPRI, PRIMASK */
}

/* A4. 대기 승자: enabled & pending 중 (우선순위 값, IRQ 번호) 가 가장 작은 것. 없으면 -1.
 *     (값 = 그룹+서브 전체 → 같은 그룹이면 서브가, 그것도 같으면 번호가 순서를 정한다) */
static int pick_pending(const nvic_t *n) {
    (void)n;
    return -1; /* TODO: enabled & pending 중 (우선순위 값, 번호) 최소 */
}

/* 주어짐: irq 가 지금 코어를 빼앗을 수 있나 = 그룹 우선순위가 실행 우선순위보다 "엄격히" 급한가 */
static bool can_take(const nvic_t *n, int irq) {
    return irq >= 0 && group_of(n, n->prio[irq]) < exec_prio(n);
}
static void nvic_set_prio(nvic_t *n, unsigned irq, unsigned p) { n->prio[irq] = (uint8_t)prio_store(n, p); }

/* ======================= 시뮬레이터 (주어짐, Part B 의 세 함수만 TODO) ======================= */

enum { PH_THREAD, PH_ENTRY, PH_HANDLER, PH_TAIL, PH_EXIT };
enum { EV_PEND, EV_ENABLE, EV_DISABLE, EV_PRIMASK, EV_BASEPRI };   /* 외부 사건 (시각 순 정렬) */
enum { ACT_SETPEND, ACT_CLRPEND };                                  /* 핸들러 안의 동작 */

typedef struct { uint32_t t; uint8_t kind, arg; } ev_t;            /* arg = IRQ 번호 또는 값 */
typedef struct { uint16_t at; uint8_t op, irq; } act_t;            /* 핸들러가 at 사이클 진행했을 때 */
typedef struct { uint16_t dur; uint8_t nact; act_t act[4]; } handler_t;
typedef struct { uint8_t irq, next_act; uint16_t progress; } frame_t;
typedef struct { unsigned entry, tail, exit; bool tailchain; } timing_t;

static const timing_t TM = { 12, 6, 10, true };   /* Cortex-M3/M4 에서 흔히 인용되는 값 (모델 파라미터) */

typedef struct {
    nvic_t n;
    timing_t tm;
    const handler_t *h;                 /* h[irq] */
    const ev_t *ev; unsigned nev, ei;
    uint32_t now;
    int phase, target;                  /* target = ENTRY/TAIL 이 들어갈 IRQ */
    unsigned remaining;                 /* ENTRY/TAIL/EXIT 남은 사이클 */
    frame_t stk[NIRQ]; unsigned depth;  /* 활성 핸들러 문맥 (맨 위 = 실행 중 또는 막 끝난 것) */
    unsigned n_entry, n_tail, n_late, n_exit, max_depth;
    uint32_t overhead, thread_cyc, handler_cyc;
    uint32_t pend_t[NIRQ], lat_max[NIRQ], runs[NIRQ], nested_in[NIRQ];
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
    (void)s;
    return false; /* TODO: 승자가 can_take 이면 ENTRY 시작 */
}

/* B2. ENTRY/TAIL 이 아직 안 끝났을 때 (remaining > 0): 대기 승자의 그룹 우선순위가 지금 목표보다
 *     엄격히 급하면 목표를 바꾼다 (쌓기는 그대로, 벡터만 바뀜). 로그 "late IRQb (was IRQa)". n_late++. */
static void late_arrival(sim_t *s) {
    (void)s; /* TODO: 그룹이 엄격히 더 급한 승자로 target 교체 */
}

/* B3. 핸들러가 끝났다: 프레임 pop + active 해제, 로그 "done IRQk". 그다음
 *     tm.tailchain 이고 대기 승자가 (돌아갈 문맥 기준으로) 코어를 빼앗을 수 있으면 → TAIL (tm.tail, "tail IRQb", n_tail++)
 *     아니면 → EXIT (tm.exit, "exit", n_exit++). */
static void handler_return(sim_t *s) {
    s->err = 6; /* TODO: pop + active 해제, 그다음 TAIL 또는 EXIT */
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

/* ======================= Part C: 펌웨어 설정 ======================= */

/* C1. (그룹, 서브) → 저장될 8비트 우선순위 값 (CMSIS NVIC_EncodePriority + 시프트를 합친 것).
 *     서브 필드 = 비트 [prigroup : 8-prio_bits] (구현된 것만), 그룹 필드 = 그 위 구현 비트.
 *     각 값은 자기 필드 폭으로 잘라 넣는다. 서브 필드가 없으면 sub 는 무시. */
static unsigned encode_prio(const nvic_t *n, unsigned group, unsigned sub) {
    (void)n; (void)group; (void)sub;
    return 0; /* TODO: 서브 필드 [prigroup : 8-prio_bits], 그 위가 그룹 필드 */
}

/* C2. 이 보드의 설정 (prio_bits = 4 는 하드웨어가 정해 둔 값, 이미 들어 있음):
 *     그룹 2비트 + 서브 2비트 가 되도록 prigroup 을 고르고
 *       IRQ0 MOTOR_PWM  그룹 0 서브 0      IRQ1 ADC_DMA  그룹 1 서브 0
 *       IRQ2 UART1_RX   그룹 2 서브 0      IRQ3 UART2_RX 그룹 2 서브 1
 *       IRQ4 TICK       그룹 3 서브 3
 *     IRQ0..4 만 enable. */
enum { MOTOR, ADC, UART1, UART2, TICK };
static void fw_config(nvic_t *n) {
    (void)n; /* TODO: prigroup, 5개 우선순위 (encode_prio), enable */
}

/* C3. k 개의 IRQ 가 동시에 pending (thread 에서, 서로 선점 안 함, 핸들러 사이에 새 사건 없음) 일 때
 *     entry/tail/exit 에 쓰는 사이클 합 (핸들러 본문 제외). tailchain 이 꺼져 있으면 매번 exit + entry. */
static uint32_t burst_overhead(const timing_t *tm, unsigned k) {
    (void)tm; (void)k;
    return 0; /* TODO: tail-chain 있음 / 없음 두 식 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;                          /* xorshift32, 고정 시드 → 결정적 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static sim_t S;
static handler_t H[NIRQ];
static int verbose;

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

static void print_gantt(const sim_t *s, unsigned scale) {   /* 사이클 scale 개 = 한 글자 */
    uint32_t T = s->now < TRACE_MAX ? s->now : TRACE_MAX;
    printf("cycle  ");
    for (uint32_t t = 0; t < T; t += scale) putchar(t % (10 * scale) == 0 ? (char)('0' + t / (10 * scale) % 10) : ' ');
    printf("   (한 글자 = %u 사이클, 숫자 = %u 단위)\ncore   ", scale, 10 * scale);
    for (uint32_t t = 0; t < T; t += scale) putchar(" ETX"[s->who[t] == -2 ? 1 : s->who[t] == -4 ? 2 : s->who[t] == -5 ? 3 : 0]);
    printf("\nthread ");
    for (uint32_t t = 0; t < T; t += scale) putchar(s->who[t] == -1 ? '#' : ' ');
    putchar('\n');
    for (unsigned k = 0; k < NIRQ; k++) {
        if (!s->runs[k]) continue;
        printf("IRQ%-3u ", k);
        for (uint32_t t = 0; t < T; t += scale)
            putchar(s->who[t] == (int)k ? '#' : (s->act_tr[t] >> k & 1u) ? '.' : ' ');
        putchar('\n');
    }
}
static void show(const char *title, unsigned scale) {
    if (!verbose) return;
    printf("\n== %s ==\n%s", title, S.log);
    print_gantt(&S, scale);
}

int main(int argc, char **argv) {
    verbose = argc > 1 && argv[1][0] == '-';

    /* ---------- Part A: 우선순위 산수 ---------- */
    { nvic_t n = mk(4, 0); CHECK(prio_store(&n, 0xFF) == 0xF0 && prio_store(&n, 0x5A) == 0x50); }
    { nvic_t n = mk(3, 0); CHECK(prio_store(&n, 0x5A) == 0x40 && prio_store(&n, 0x1F) == 0x00); }
    { nvic_t n = mk(8, 0); CHECK(prio_store(&n, 0x5A) == 0x5A); }
    { nvic_t n = mk(2, 0); CHECK(prio_store(&n, 0xFF) == 0xC0); }
    {   /* p = 0xB7 = 1011 0111, PRIGROUP 0..7 */
        static const unsigned exp[8] = { 0xB6, 0xB4, 0xB0, 0xB0, 0xA0, 0x80, 0x80, 0x00 };
        unsigned bad = 0;
        for (unsigned pg = 0; pg < 8; pg++) { nvic_t n = mk(8, pg); if (group_of(&n, 0xB7) != exp[pg]) bad++; }
        CHECK(bad == 0);
    }
    {   /* 전수: 8 PRIGROUP x 256 값 — 그룹 + 서브 = 원래 값, 서브 필드 폭 = prigroup+1 */
        unsigned bad = 0;
        for (unsigned pg = 0; pg < 8; pg++)
            for (unsigned p = 0; p < 256; p++) {
                nvic_t n = mk(8, pg);
                unsigned g = group_of(&n, p), sub = p - g;
                if (g != (p >> (pg + 1) << (pg + 1)) || sub >= (1u << (pg + 1))) bad++;
            }
        CHECK(bad == 0);
    }
    {
        nvic_t n = mk(4, 0);
        CHECK(exec_prio(&n) == PRIO_THREAD);
        nvic_set_prio(&n, 3, 0x60); nvic_set_prio(&n, 9, 0x20);
        n.active = 1u << 3;
        CHECK(exec_prio(&n) == 0x60);
        n.active |= 1u << 9;
        CHECK(exec_prio(&n) == 0x20);
        n.active = 1u << 3; n.basepri = 0x40;
        CHECK(exec_prio(&n) == 0x40);                       /* BASEPRI 가 더 급하면 BASEPRI */
        n.active = 1u << 9;
        CHECK(exec_prio(&n) == 0x20);                       /* active 가 더 급하면 active */
        n.primask = 1;
        CHECK(exec_prio(&n) == 0);
        nvic_t m = mk(4, 5);                                /* 그룹 = [7:6] → 0x70 의 그룹은 0x40 */
        nvic_set_prio(&m, 1, 0x70); m.active = 1u << 1;
        CHECK(exec_prio(&m) == 0x40);
        m.active = 0; m.basepri = 0x30;                     /* BASEPRI 도 그룹으로 본다: 0x30 → 0x00 */
        CHECK(exec_prio(&m) == 0x00);
    }
    {
        nvic_t n = mk(4, 0);
        CHECK(pick_pending(&n) == -1);
        nvic_set_prio(&n, 7, 0x40); nvic_set_prio(&n, 2, 0x40); nvic_set_prio(&n, 5, 0x30);
        n.pending = 1u << 7 | 1u << 2;
        CHECK(pick_pending(&n) == 2);                       /* 같은 우선순위 → 번호 작은 쪽 */
        n.pending |= 1u << 5;
        CHECK(pick_pending(&n) == 5);
        n.enabled &= ~(1u << 5);
        CHECK(pick_pending(&n) == 2);                       /* disable 이면 pending 이어도 후보 아님 */
        n.enabled = 0;
        CHECK(pick_pending(&n) == -1);
        nvic_t m = mk(4, 5);                                /* 같은 그룹 0x40, 서브 0x10 vs 0x20 */
        nvic_set_prio(&m, 8, 0x60); nvic_set_prio(&m, 9, 0x50);
        m.pending = 1u << 8 | 1u << 9; m.active = 1u << 8;
        CHECK(pick_pending(&m) == 9 && !can_take(&m, 9));   /* 서브가 순서는 정하지만 선점은 못 함 */
    }
    {   /* 무작위 10만 상태 vs 키 비교 정답 */
        unsigned bad = 0;
        for (int it = 0; it < 100000; it++) {
            nvic_t n = mk(2 + xs32() % 7u, xs32() % 8u);
            for (unsigned k = 0; k < NIRQ; k++) nvic_set_prio(&n, k, xs32() & 0xFFu);
            n.pending = xs32() & xs32(); n.enabled = xs32() | xs32();
            int ref = -1; unsigned key = 0xFFFFu;
            for (unsigned k = 0; k < NIRQ; k++)
                if ((n.pending & n.enabled) >> k & 1u) {
                    unsigned kk = (unsigned)n.prio[k] << 5 | k;
                    if (kk < key) { key = kk; ref = (int)k; }
                }
            if (pick_pending(&n) != ref) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------- Part B: 선점 · 중첩 · tail-chaining · late arrival (로그 비교) ---------- */
    {   /* B1 하나만 */
        hreset(); H[5].dur = 20;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 5, 0x40);
        static const ev_t ev[] = { {10, EV_PEND, 5} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@10 pend IRQ5\n@10 entry IRQ5\n@22 start IRQ5\n@42 done IRQ5\n@42 exit\n@52 resume thread\n"));
        CHECK(S.lat_max[5] == 12 && S.overhead == 22);
    }
    {   /* B2 선점 + 중첩 */
        hreset(); H[7].dur = 30; H[2].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 7, 0x80); nvic_set_prio(&n, 2, 0x20);
        static const ev_t ev[] = { {0, EV_PEND, 7}, {20, EV_PEND, 2} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ7\n@0 entry IRQ7\n@12 start IRQ7\n@20 pend IRQ2\n@20 entry IRQ2 over IRQ7\n"
                     "@32 start IRQ2\n@42 done IRQ2\n@42 exit\n@52 resume IRQ7\n@74 done IRQ7\n@74 exit\n@84 resume thread\n"));
        CHECK(S.max_depth == 2 && S.lat_max[2] == 12);
    }
    {   /* B3 tail-chaining: 낮은 IRQ 는 기다렸다가 exit 없이 바로 */
        hreset(); H[3].dur = 20; H[4].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 3, 0x40); nvic_set_prio(&n, 4, 0x60);
        static const ev_t ev[] = { {0, EV_PEND, 3}, {5, EV_PEND, 4} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ3\n@0 entry IRQ3\n@5 pend IRQ4\n@12 start IRQ3\n@32 done IRQ3\n@32 tail IRQ4\n"
                     "@38 start IRQ4\n@48 done IRQ4\n@48 exit\n@58 resume thread\n"));
        CHECK(S.n_tail == 1 && S.lat_max[4] == 33 && S.now == 58);
        /* 같은 사건, tail-chaining 없는 코어: exit 10 + entry 12 = 22 > tail 6 → 16 사이클 늦게 끝난다 */
        timing_t nt = TM; nt.tailchain = false;
        CHECK(RUN(n, nt, ev) == 0);
        CHECK(log_is("@0 pend IRQ3\n@0 entry IRQ3\n@5 pend IRQ4\n@12 start IRQ3\n@32 done IRQ3\n@32 exit\n"
                     "@42 resume thread\n@42 entry IRQ4\n@54 start IRQ4\n@64 done IRQ4\n@64 exit\n@74 resume thread\n"));
        CHECK(S.now == 58 + 16);
    }
    {   /* B4 late arrival: 쌓는 중에 더 급한 IRQ → 벡터만 바꾼다 */
        hreset(); H[6].dur = 10; H[1].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 6, 0x80); nvic_set_prio(&n, 1, 0x10);
        static const ev_t ev[] = { {0, EV_PEND, 6}, {5, EV_PEND, 1} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ6\n@0 entry IRQ6\n@5 pend IRQ1\n@5 late IRQ1 (was IRQ6)\n@12 start IRQ1\n@22 done IRQ1\n"
                     "@22 tail IRQ6\n@28 start IRQ6\n@38 done IRQ6\n@38 exit\n@48 resume thread\n"));
        CHECK(S.n_late == 1 && S.n_entry == 1 && S.lat_max[1] == 7 && S.lat_max[6] == 28);
        show("B4 late arrival", 1);
        /* 경계: entry 마지막 사이클(11) 에 오면 late, 끝난 시각(12) 에 오면 보통 선점 (entry 한 번 더) */
        static const ev_t e11[] = { {0, EV_PEND, 6}, {11, EV_PEND, 1} };
        static const ev_t e12[] = { {0, EV_PEND, 6}, {12, EV_PEND, 1} };
        CHECK(RUN(n, TM, e11) == 0 && S.n_late == 1 && S.n_entry == 1);
        CHECK(RUN(n, TM, e12) == 0 && S.n_late == 0 && S.n_entry == 2 && S.max_depth == 2 && S.lat_max[1] == 12);
    }
    {   /* B4b 같은 그룹은 late arrival 이 아니다 (선점할 수 없으니) */
        hreset(); H[6].dur = 10; H[1].dur = 10;
        nvic_t n = mk(4, 5); nvic_set_prio(&n, 6, 0x60); nvic_set_prio(&n, 1, 0x40);   /* 둘 다 그룹 0x40 */
        static const ev_t ev[] = { {0, EV_PEND, 6}, {5, EV_PEND, 1} };
        CHECK(RUN(n, TM, ev) == 0 && S.n_late == 0);
        CHECK(log_is("@0 pend IRQ6\n@0 entry IRQ6\n@5 pend IRQ1\n@12 start IRQ6\n@22 done IRQ6\n@22 tail IRQ1\n"
                     "@28 start IRQ1\n@38 done IRQ1\n@38 exit\n@48 resume thread\n"));
    }
    {   /* B5 PRIGROUP 이 결과를 바꾼다: 같은 사건, 그룹 4비트 vs 그룹 2비트 + 서브 2비트 */
        hreset(); H[8].dur = 20; H[9].dur = 10; H[10].dur = 10;
        static const ev_t ev[] = { {0, EV_PEND, 8}, {15, EV_PEND, 10}, {15, EV_PEND, 9} };
        nvic_t a = mk(4, 0);
        nvic_set_prio(&a, 8, 0x60); nvic_set_prio(&a, 9, 0x50); nvic_set_prio(&a, 10, 0x50);
        CHECK(RUN(a, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ8\n@0 entry IRQ8\n@12 start IRQ8\n@15 pend IRQ10\n@15 pend IRQ9\n@15 entry IRQ9 over IRQ8\n"
                     "@27 start IRQ9\n@37 done IRQ9\n@37 tail IRQ10\n@43 start IRQ10\n@53 done IRQ10\n@53 exit\n"
                     "@63 resume IRQ8\n@80 done IRQ8\n@80 exit\n@90 resume thread\n"));
        nvic_t b = mk(4, 5);
        nvic_set_prio(&b, 8, 0x60); nvic_set_prio(&b, 9, 0x50); nvic_set_prio(&b, 10, 0x50);
        CHECK(RUN(b, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ8\n@0 entry IRQ8\n@12 start IRQ8\n@15 pend IRQ10\n@15 pend IRQ9\n@32 done IRQ8\n"
                     "@32 tail IRQ9\n@38 start IRQ9\n@48 done IRQ9\n@48 tail IRQ10\n@54 start IRQ10\n@64 done IRQ10\n"
                     "@64 exit\n@74 resume thread\n"));
        CHECK(S.max_depth == 1 && S.lat_max[9] == 23);
    }
    {   /* B6 핸들러가 다른 IRQ 의 pending 을 set / clear */
        hreset();
        H[2] = (handler_t){ .dur = 20, .nact = 3, .act = { {5, ACT_SETPEND, 0}, {10, ACT_CLRPEND, 12}, {15, ACT_SETPEND, 3} } };
        H[0].dur = 6; H[12].dur = 5; H[3].dur = 4;
        nvic_t n = mk(4, 0);
        nvic_set_prio(&n, 2, 0x40); nvic_set_prio(&n, 0, 0x10); nvic_set_prio(&n, 12, 0x80); nvic_set_prio(&n, 3, 0x80);
        static const ev_t ev[] = { {0, EV_PEND, 2}, {1, EV_PEND, 12} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ2\n@0 entry IRQ2\n@1 pend IRQ12\n@12 start IRQ2\n@17 IRQ2 setpend IRQ0\n"
                     "@17 entry IRQ0 over IRQ2\n@29 start IRQ0\n@35 done IRQ0\n@35 exit\n@45 resume IRQ2\n"
                     "@50 IRQ2 clrpend IRQ12\n@55 IRQ2 setpend IRQ3\n@60 done IRQ2\n@60 tail IRQ3\n@66 start IRQ3\n"
                     "@70 done IRQ3\n@70 exit\n@80 resume thread\n"));
        CHECK(S.runs[12] == 0 && S.n.pending == 0);         /* 지워진 IRQ12 는 한 번도 안 돈다 */
    }
    {   /* B7 active 중에 같은 IRQ 가 다시 pending → 선점은 못 하고, 끝나면 자기 자신으로 tail-chain */
        hreset(); H[4].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 4, 0x40);
        static const ev_t ev[] = { {0, EV_PEND, 4}, {15, EV_PEND, 4} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ4\n@0 entry IRQ4\n@12 start IRQ4\n@15 pend IRQ4\n@22 done IRQ4\n@22 tail IRQ4\n"
                     "@28 start IRQ4\n@38 done IRQ4\n@38 exit\n@48 resume thread\n"));
        CHECK(S.runs[4] == 2 && S.lat_max[4] == 13);
    }
    {   /* B8 PRIMASK: pending 은 기억되고, 풀리는 순간 들어간다 */
        hreset(); H[5].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 5, 0x40);
        static const ev_t ev[] = { {0, EV_PRIMASK, 1}, {10, EV_PEND, 5}, {50, EV_PRIMASK, 0} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@0 primask 1\n@10 pend IRQ5\n@50 primask 0\n@50 entry IRQ5\n@62 start IRQ5\n@72 done IRQ5\n"
                     "@72 exit\n@82 resume thread\n"));
        CHECK(S.lat_max[5] == 52);
    }
    {   /* B9 disable 된 IRQ 의 pending 도 남는다 — enable 하는 순간 들어감, 안 하면 계속 대기 */
        hreset(); H[5].dur = 10; H[6].dur = 10;
        nvic_t n = mk(4, 0); nvic_set_prio(&n, 5, 0x40); nvic_set_prio(&n, 6, 0x20);
        n.enabled = ~(1u << 5 | 1u << 6);
        static const ev_t ev[] = { {10, EV_PEND, 5}, {12, EV_PEND, 6}, {40, EV_ENABLE, 5} };
        CHECK(RUN(n, TM, ev) == 0);
        CHECK(log_is("@10 pend IRQ5\n@12 pend IRQ6\n@40 enable IRQ5\n@40 entry IRQ5\n@52 start IRQ5\n@62 done IRQ5\n"
                     "@62 exit\n@72 resume thread\n"));
        CHECK(S.n.pending == 1u << 6 && S.runs[6] == 0);
    }
    {   /* B10 퍼즈: 무작위 2000 시나리오 — 불변식 (모두 처리됨, 시간 보존, 핸들러 사이클 = Σ runs·dur) */
        unsigned bad = 0, deep = 0;
        for (int it = 0; it < 2000; it++) {
            hreset();
            nvic_t n = mk(3 + xs32() % 6u, xs32() % 8u);
            for (unsigned k = 0; k < 8; k++) {
                nvic_set_prio(&n, k, xs32() & 0xFFu);
                H[k].dur = (uint16_t)(1 + xs32() % 30u);
                H[k].nact = (uint8_t)(xs32() % 3u);
                uint16_t at = 0;
                for (unsigned j = 0; j < H[k].nact; j++) {
                    at = (uint16_t)(at + xs32() % 5u);
                    if (at >= H[k].dur) at = (uint16_t)(H[k].dur - 1);
                    unsigned tgt = k + 1 + xs32() % 8u;     /* 번호가 큰 IRQ 만 건드림 → 끝없는 사슬 없음 */
                    H[k].act[j] = (act_t){ at, (uint8_t)(xs32() % 4u ? ACT_SETPEND : ACT_CLRPEND), (uint8_t)(tgt < 16 ? tgt : 15) };
                }
            }
            for (unsigned k = 8; k < 16; k++) { nvic_set_prio(&n, k, xs32() & 0xFFu); H[k].dur = (uint16_t)(1 + xs32() % 10u); }
            ev_t ev[8];
            uint32_t t = 0;
            for (int i = 0; i < 8; i++) { t += xs32() % 25u; ev[i] = (ev_t){ t, EV_PEND, (uint8_t)(xs32() % 8u) }; }
            timing_t tm = TM; tm.tailchain = xs32() & 1u;
            int err = run(&n, tm, ev, 8);
            uint32_t work = 0;
            for (unsigned k = 0; k < NIRQ; k++) work += S.runs[k] * H[k].dur;
            if (err || S.n.pending || S.n.active || S.depth || S.phase != PH_THREAD) bad++;
            if (S.handler_cyc != work || S.now != S.thread_cyc + S.overhead + S.handler_cyc) bad++;
            if (S.max_depth >= 3) deep++;
        }
        CHECK(bad == 0);
        CHECK(deep > 0);                                    /* 3단 이상 중첩도 실제로 나왔나 (coverage) */
    }

    /* ---------- Part C: 펌웨어 설정 · RTOS 임계 구역 · 오버헤드 ---------- */
    {
        nvic_t n = mk(4, 5);
        CHECK(encode_prio(&n, 2, 1) == 0x90 && encode_prio(&n, 0, 0) == 0x00 && encode_prio(&n, 3, 3) == 0xF0);
        CHECK(encode_prio(&n, 1, 0) == 0x40 && encode_prio(&n, 5, 0) == 0x40);   /* 그룹 2비트로 잘림 */
        nvic_t a = mk(4, 0);                                /* 서브 비트가 모두 미구현 → 그룹 4비트 */
        CHECK(encode_prio(&a, 5, 3) == 0x50);
        nvic_t b = mk(3, 5);                                /* 그룹 [7:6], 서브 [5] */
        CHECK(encode_prio(&b, 2, 1) == 0xA0 && encode_prio(&b, 1, 1) == 0x60);
        nvic_t c = mk(8, 7);                                /* 그룹 비트 없음 → 전부 서브 */
        CHECK(encode_prio(&c, 1, 0x5A) == 0x5A && group_of(&c, encode_prio(&c, 1, 0x5A)) == 0);
        unsigned bad = 0;                                   /* 전수: 저장 가능한 값이고, 그룹 부분 = encode(g, 0) */
        for (unsigned bits = 2; bits <= 8; bits++)
            for (unsigned pg = 0; pg < 8; pg++)
                for (unsigned g = 0; g < 256; g++)
                    for (unsigned s = 0; s < 256; s += 17) {
                        nvic_t m = mk(bits, pg);
                        unsigned v = encode_prio(&m, g, s);
                        if (v > 0xFF || prio_store(&m, v) != v || group_of(&m, v) != encode_prio(&m, g, 0)) bad++;
                    }
        CHECK(bad == 0);
    }
    nvic_t fw = mk(4, 0);
    fw.enabled = 0;
    fw_config(&fw);
    CHECK(fw.prigroup == 5 && fw.enabled == 0x1Fu);
    CHECK(fw.prio[MOTOR] == 0x00 && fw.prio[ADC] == 0x40 && fw.prio[UART1] == 0x80 && fw.prio[UART2] == 0x90 && fw.prio[TICK] == 0xF0);
    const unsigned SYSCALL_MAX = encode_prio(&fw, 1, 0);    /* RTOS 임계 구역: 그룹 1 이하(덜 급함) 차단 */
    CHECK(SYSCALL_MAX == 0x40);
    {   /* C1 RTOS 임계 구역을 BASEPRI 로: 모터는 12 사이클, 나머지는 풀릴 때까지 */
        hreset(); H[MOTOR].dur = 8; H[ADC].dur = 10; H[UART1].dur = 6; H[UART2].dur = 6; H[TICK].dur = 4;
        static ev_t ev[] = { {0, EV_BASEPRI, 0}, {5, EV_PEND, UART1}, {8, EV_PEND, TICK}, {20, EV_PEND, MOTOR},
                             {25, EV_PEND, ADC}, {100, EV_BASEPRI, 0} };
        ev[0].arg = (uint8_t)SYSCALL_MAX;
        CHECK(RUN(fw, TM, ev) == 0);
        CHECK(log_is("@0 basepri 0x40\n@5 pend IRQ2\n@8 pend IRQ4\n@20 pend IRQ0\n@20 entry IRQ0\n@25 pend IRQ1\n"
                     "@32 start IRQ0\n@40 done IRQ0\n@40 exit\n@50 resume thread\n@100 basepri 0x00\n@100 entry IRQ1\n"
                     "@112 start IRQ1\n@122 done IRQ1\n@122 tail IRQ2\n@128 start IRQ2\n@134 done IRQ2\n@134 tail IRQ4\n"
                     "@140 start IRQ4\n@144 done IRQ4\n@144 exit\n@154 resume thread\n"));
        CHECK(S.lat_max[MOTOR] == 12 && S.lat_max[ADC] == 87 && S.lat_max[UART1] == 123 && S.lat_max[TICK] == 132);
        /* 같은 임계 구역을 PRIMASK 로: 모터까지 막힌다 */
        static const ev_t pm[] = { {0, EV_PRIMASK, 1}, {5, EV_PEND, UART1}, {8, EV_PEND, TICK}, {20, EV_PEND, MOTOR},
                                   {25, EV_PEND, ADC}, {100, EV_PRIMASK, 0} };
        CHECK(RUN(fw, TM, pm) == 0);
        CHECK(S.lat_max[MOTOR] == 92 && S.lat_max[ADC] == 101 && S.n_tail == 3 && S.now == 168);
    }
    {   /* C2 4단 중첩: TICK ← UART2 ← ADC ← MOTOR, UART1 은 UART2 와 같은 그룹이라 못 끼어듦 */
        hreset(); H[MOTOR].dur = 8; H[ADC].dur = 15; H[UART1].dur = 6; H[UART2].dur = 20; H[TICK].dur = 30;
        static const ev_t ev[] = { {0, EV_PEND, TICK}, {14, EV_PEND, UART2}, {30, EV_PEND, UART1}, {32, EV_PEND, ADC},
                                   {50, EV_PEND, MOTOR} };
        CHECK(RUN(fw, TM, ev) == 0);
        CHECK(log_is("@0 pend IRQ4\n@0 entry IRQ4\n@12 start IRQ4\n@14 pend IRQ3\n@14 entry IRQ3 over IRQ4\n@26 start IRQ3\n"
                     "@30 pend IRQ2\n@32 pend IRQ1\n@32 entry IRQ1 over IRQ3\n@44 start IRQ1\n@50 pend IRQ0\n"
                     "@50 entry IRQ0 over IRQ1\n@62 start IRQ0\n@70 done IRQ0\n@70 exit\n@80 resume IRQ1\n@89 done IRQ1\n"
                     "@89 exit\n@99 resume IRQ3\n@113 done IRQ3\n@113 tail IRQ2\n@119 start IRQ2\n@125 done IRQ2\n"
                     "@125 exit\n@135 resume IRQ4\n@163 done IRQ4\n@163 exit\n@173 resume thread\n"));
        CHECK(S.max_depth == 4 && S.nested_in[UART2] == 1u << TICK && S.nested_in[UART1] == 1u << TICK);
        show("C2 4단 중첩 (fw_config)", 2);
    }
    {   /* C3 연속 k 개: 시뮬레이터 오버헤드 = burst_overhead 식 */
        unsigned bad = 0;
        for (unsigned k = 1; k <= 8; k++)
            for (int tc = 0; tc < 2; tc++) {
                hreset();
                nvic_t n = mk(4, 0);
                ev_t ev[8];
                for (unsigned i = 0; i < k; i++) { nvic_set_prio(&n, i, 0x40); H[i].dur = 10; ev[i] = (ev_t){ 0, EV_PEND, (uint8_t)(k - 1 - i) }; }
                timing_t tm = TM; tm.tailchain = tc;
                if (run(&n, tm, ev, k) || S.overhead != burst_overhead(&tm, k) || S.now != burst_overhead(&tm, k) + 10 * k) bad++;
            }
        CHECK(bad == 0);
        timing_t on = TM, off = TM; off.tailchain = false;
        CHECK(burst_overhead(&on, 4) == 40 && burst_overhead(&off, 4) == 88 && burst_overhead(&on, 0) == 0);
    }
    {   /* C4 퍼즈 (fw_config): 중첩 깊이 ≤ 그룹 레벨 4, 모터 위에는 아무도, UART 끼리는 서로 안 겹침 */
        static const uint16_t dur[5] = { 8, 15, 6, 6, 30 };
        unsigned bad = 0, maxd = 0;
        for (int it = 0; it < 3000; it++) {
            hreset();
            for (int k = 0; k < 5; k++) H[k].dur = dur[k];
            ev_t ev[10];
            uint32_t t = 0;
            for (int i = 0; i < 10; i++) { t += xs32() % 12u; ev[i] = (ev_t){ t, EV_PEND, (uint8_t)(xs32() % 5u) }; }
            if (run(&fw, TM, ev, 10) || S.max_depth > 4 || S.nested_in[UART1] >> UART2 & 1u || S.nested_in[UART2] >> UART1 & 1u) bad++;
            for (int k = 0; k < 5; k++) if (S.nested_in[k] >> MOTOR & 1u) bad++;
            if (S.max_depth > maxd) maxd = S.max_depth;
        }
        CHECK(bad == 0 && maxd == 4);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
