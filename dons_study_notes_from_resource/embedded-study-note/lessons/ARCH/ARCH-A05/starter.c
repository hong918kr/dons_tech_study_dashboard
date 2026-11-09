/* ARCH-A05 메모리 순서 리트머스 테스트 on Apple M2 (이 맥은 M6 에서 측정)
 *
 * litmus7 스타일 하네스 (Alglave, Maranget et al., "Litmus: running tests against hardware", TACAS 2011):
 * 스레드 2~4 개가 N 번 반복한다. 반복마다 **새 변수** (배열 칸, 128 B 간격) 를 쓰고, 반복마다 스핀 장벽으로
 * 같이 출발하고, 결과(레지스터 값)는 배열에 적어 두었다가 끝난 뒤 히스토그램으로 센다.
 *   Part A  하네스: 변형별 memory_order / fence 표, 스핀 장벽, 결과 키 + 히스토그램, 실제 스레드 실행
 *   Part B  강제 스케줄: 프로그램 순서를 지키는 모든 인터리빙을 만들어 (= SC 가 허용하는 결과 전부)
 *           같은 연산 코드를 한 스레드로도, 실제 스레드 + 차례 토큰으로도 돌려 히스토그램을 정확히 맞춘다
 *   Part C  실리콘 검증 게이트: (테스트, 변형) 마다 C11 / ARMv8 / x86-TSO 가 허용하는지 표 + 판정
 *           (금지인데 나옴 = 버그, 허용인데 0 = "안 보였을 뿐")
 *
 * 테스트 (괄호 = "이상한" 결과, weird):
 *   SB   T0: x=1; r0=y          T1: y=1; r1=x                  (r0=0 r1=0)
 *   MP   T0: x=1; y=1  (data,flag)   T1: r0=y; r1=x            (r0=1 r1=0)
 *   LB   T0: r0=x; y=1          T1: r1=y; x=1                  (r0=1 r1=1)
 *   IRIW T0: x=1  T1: y=1  T2: r0=x; r1=y  T3: r2=y; r3=x      (1 0 1 0)        — 추가 (연습 1)
 *   2+2W T0: x=1; y=2           T1: y=1; x=2   최종값 관찰      (x=1 y=1)        — 추가 (연습 1)
 * 변형: RLX (전부 relaxed) / ACQREL (store=release, load=acquire) / SC (전부 seq_cst) /
 *       FENCE (relaxed + 테스트별 최소 atomic_thread_fence)
 *
 * 체크는 매 실행 항상 성립하는 것만 본다: 표 값, 장벽 세대 수, 강제 스케줄의 정확한 히스토그램,
 * "C11 이 금지한 결과 = 정확히 0" (seq_cst SB, acq/rel MP, ...), 반복 수 합계. "허용된 이상한 결과가
 * 몇 번 나왔나" 는 정보 줄(info)일 뿐 체크하지 않는다 — demo_litmus.c 가 100만 회 이상으로 센다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>          /* mach_absolute_time: 모든 코어가 같이 보는 시간축 (M_TIME 모드) */

#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define LIT_SANITIZED 1
#endif
#endif
#ifndef LIT_SANITIZED
#define LIT_SANITIZED 0
#endif
#ifndef N_CHECK
#define N_CHECK (LIT_SANITIZED ? 4000u : 100000u)   /* 실제 스레드 체크 실행 1 회의 반복 수 */
#endif

#define LINE 128                 /* sysctl hw.cachelinesize = 128 (Apple M 계열). 변수 한 칸 = 한 줄 */
#define MAXT 4                   /* 스레드 최대 */
#define MAXOPS 2                 /* 스레드당 연산 최대 */
#define MAXSTEPS (MAXT * MAXOPS)
#define MAXREG 4                 /* 레지스터 + 최종값 슬롯 */
#define NLOC 2                   /* 변수 x, y */
#define X 0
#define Y 1
#define NKEY 256                 /* 결과 키 = 슬롯마다 2 비트 */

enum { T_SB, T_MP, T_LB, T_IRIW, T_22W, T_N };
enum { V_RLX, V_ACQREL, V_SC, V_FENCE, V_N };
enum { K_ST, K_LD };
static const char *const VNAME[V_N] = { "relaxed", "acq/rel", "seq_cst", "fence" };

#ifndef CELL
#define CELL LINE                /* 변수 한 칸 크기. demo 에서 -DCELL=4 = 빽빽한 배열 (이웃 반복이 같은 줄) */
#endif
typedef struct { _Alignas(CELL) _Atomic int v; } cell_t;     /* sizeof == 128: 반복마다 따로 노는 줄 */
_Static_assert(sizeof(cell_t) == CELL, "cell size");

typedef struct {
    cell_t *loc[NLOC];           /* loc[k][i]: 변수 k 의 i 번째 사본 (배치 안 인덱스) */
    uint8_t *reg[MAXREG];        /* reg[r][j]: 전체 반복 j 의 레지스터 r (또는 최종값) */
} lit_ctx;

typedef void (*body_fn)(lit_ctx *c, size_t i, size_t j);           /* 한 스레드의 반복 1 회 전체 */
typedef void (*step_fn)(lit_ctx *c, size_t i, size_t j, int k);    /* 그 스레드의 k 번째 연산 하나 */

typedef struct {
    const char *name;
    int nthreads, nops[MAXT], nsteps;   /* nsteps = 연산 총수 */
    int nregs, nfinal;                  /* 레지스터 개수, 그 뒤 최종값 슬롯 개수 */
    int final_loc[NLOC];                /* 최종값 슬롯 → 변수 */
    const char *slot[MAXREG];           /* 출력용 이름 */
    unsigned weird;                     /* "이상한" 결과의 키 */
    body_fn body[MAXT][V_N];
    step_fn step[MAXT];
} lit_test;

/* ======================= Part A-1: 변형 → memory_order / fence ======================= */
/* TODO A1: 변형 V 의 store / load 순서. FENCE 변형은 접근 자체는 relaxed 이고 순서는 펜스가 만든다. */
static inline memory_order ord(int V, int kind) {
    (void)V; (void)kind;
    return memory_order_relaxed;                                /* TODO */
}
/* TODO A1: 테스트 T 의 스레드 t 가 두 연산 사이에 넣는 펜스 (FENCE 변형만). relaxed 펜스 = 아무것도 아님.
 * "그 테스트를 금지시키는 가장 약한 C11 펜스" 를 고른다 (IRIW 는 C11 로는 acquire 펜스로 부족 — 그대로 둔다). */
static inline memory_order fence_ord(int T, int V, int t) {
    (void)T; (void)V; (void)t;
    return memory_order_relaxed;                                /* TODO: 지금은 펜스 없음 */
}

/* ---- 테스트 코드: 연산 k 를 [from, to) 범위일 때만 실행. 실제 스레드는 (0, MAXOPS), 강제 스케줄은 (k, k+1).
 *      V, from, to 가 상수로 인라인되면 if 와 switch 가 접혀 깨끗한 기계어만 남는다 (5. 풀이의 -S). */
#define STEP(n) if (from <= (n) && (n) < to)
#define ST(k, val) atomic_store_explicit(&c->loc[k][i].v, (val), ord(V, K_ST))
#define LD(k) ((uint8_t)atomic_load_explicit(&c->loc[k][i].v, ord(V, K_LD)))
#define FENCE(T, t) atomic_thread_fence(fence_ord(T, V, t))
#define TFN(name) static inline void name(lit_ctx *c, size_t i, size_t j, int V, int from, int to)

TFN(sb_t0)   { STEP(0) ST(X, 1); STEP(1) { FENCE(T_SB, 0); c->reg[0][j] = LD(Y); } }
TFN(sb_t1)   { STEP(0) ST(Y, 1); STEP(1) { FENCE(T_SB, 1); c->reg[1][j] = LD(X); } }
TFN(mp_t0)   { (void)j; STEP(0) ST(X, 1); STEP(1) { FENCE(T_MP, 0); ST(Y, 1); } }                 /* data, flag */
TFN(mp_t1)   { STEP(0) c->reg[0][j] = LD(Y); STEP(1) { FENCE(T_MP, 1); c->reg[1][j] = LD(X); } }
TFN(lb_t0)   { STEP(0) c->reg[0][j] = LD(X); STEP(1) { FENCE(T_LB, 0); ST(Y, 1); } }
TFN(lb_t1)   { STEP(0) c->reg[1][j] = LD(Y); STEP(1) { FENCE(T_LB, 1); ST(X, 1); } }
TFN(iriw_t0) { (void)j; STEP(0) ST(X, 1); }
TFN(iriw_t1) { (void)j; STEP(0) ST(Y, 1); }
TFN(iriw_t2) { STEP(0) c->reg[0][j] = LD(X); STEP(1) { FENCE(T_IRIW, 2); c->reg[1][j] = LD(Y); } }
TFN(iriw_t3) { STEP(0) c->reg[2][j] = LD(Y); STEP(1) { FENCE(T_IRIW, 3); c->reg[3][j] = LD(X); } }
TFN(w22_t0)  { (void)j; STEP(0) ST(X, 1); STEP(1) { FENCE(T_22W, 0); ST(Y, 2); } }
TFN(w22_t1)  { (void)j; STEP(0) ST(Y, 1); STEP(1) { FENCE(T_22W, 1); ST(X, 2); } }

#define BODY(fn, sfx, V) static void body_##fn##_##sfx(lit_ctx *c, size_t i, size_t j) { fn(c, i, j, V, 0, MAXOPS); }
#define BODIES(fn) BODY(fn, rlx, V_RLX) BODY(fn, ar, V_ACQREL) BODY(fn, sc, V_SC) BODY(fn, fe, V_FENCE) \
    static void step_##fn(lit_ctx *c, size_t i, size_t j, int k) { fn(c, i, j, V_SC, k, k + 1); }
#define VROW(fn) { body_##fn##_rlx, body_##fn##_ar, body_##fn##_sc, body_##fn##_fe }
BODIES(sb_t0) BODIES(sb_t1) BODIES(mp_t0) BODIES(mp_t1) BODIES(lb_t0) BODIES(lb_t1)
BODIES(iriw_t0) BODIES(iriw_t1) BODIES(iriw_t2) BODIES(iriw_t3) BODIES(w22_t0) BODIES(w22_t1)

#define K2(a, b) ((unsigned)(a) | (unsigned)(b) << 2)
#define K4(a, b, c, d) (K2(a, b) | K2(c, d) << 4)
static const lit_test TESTS[T_N] = {
    [T_SB] = { "SB", 2, {2, 2}, 4, 2, 0, {0}, {"r0", "r1"}, K2(0, 0),
               {VROW(sb_t0), VROW(sb_t1)}, {step_sb_t0, step_sb_t1} },
    [T_MP] = { "MP", 2, {2, 2}, 4, 2, 0, {0}, {"flag", "data"}, K2(1, 0),
               {VROW(mp_t0), VROW(mp_t1)}, {step_mp_t0, step_mp_t1} },
    [T_LB] = { "LB", 2, {2, 2}, 4, 2, 0, {0}, {"r0", "r1"}, K2(1, 1),
               {VROW(lb_t0), VROW(lb_t1)}, {step_lb_t0, step_lb_t1} },
    [T_IRIW] = { "IRIW", 4, {1, 1, 2, 2}, 6, 4, 0, {0}, {"r0", "r1", "r2", "r3"}, K4(1, 0, 1, 0),
                 {VROW(iriw_t0), VROW(iriw_t1), VROW(iriw_t2), VROW(iriw_t3)},
                 {step_iriw_t0, step_iriw_t1, step_iriw_t2, step_iriw_t3} },
    [T_22W] = { "2+2W", 2, {2, 2}, 4, 0, 2, {X, Y}, {"x", "y"}, K2(1, 1),
                {VROW(w22_t0), VROW(w22_t1)}, {step_w22_t0, step_w22_t1} },
};

/* ======================= Part A-2: 스핀 장벽 ======================= */
#define SPIN_LIMIT (1u << 30)            /* 하네스가 망가져도 멈추지 않게: 약 1~수 초 뒤 포기 */
static _Atomic int g_timeout;
typedef struct {
    _Alignas(LINE) _Atomic unsigned count;   /* 이번 세대에 도착한 스레드 수 */
    _Alignas(LINE) _Atomic unsigned gen;     /* 세대 번호: 마지막 도착자가 올린다 */
    unsigned n;
} spin_barrier;
static void sbar_init(spin_barrier *b, unsigned n) {
    atomic_init(&b->count, 0);
    atomic_init(&b->gen, 0);
    b->n = n;
}
/* TODO A2: n 개 스레드가 모두 도착할 때까지 돈다. 마지막 도착자는 count 를 0 으로 되돌리고 gen 을 올린다.
 * 장벽 앞의 모든 쓰기가 장벽 뒤의 모든 읽기보다 happens-before 여야 한다 (셀 초기화가 다음 반복에 보이게). */
static void sbar_wait(spin_barrier *b) {
    (void)b;                                                    /* TODO: 지금은 아무도 기다리지 않는다 */
}

/* ======================= Part A-3: 결과 키와 히스토그램 ======================= */
typedef struct { uint64_t cnt[NKEY]; uint64_t total; } hist_t;
/* TODO A3: 반복 j 의 슬롯 0..nslots-1 을 2 비트씩 묶어 키로: 슬롯 s 는 비트 2s..2s+1. */
static unsigned outcome_key(const lit_ctx *c, int nslots, size_t j) {
    (void)c; (void)nslots; (void)j;
    return 0;                                                   /* TODO */
}
/* TODO A3: 반복 0..n-1 을 모두 키로 바꿔 히스토그램에 더한다 (total 도). */
static void lit_tally(const lit_ctx *c, int nslots, size_t n, hist_t *h) {
    (void)c; (void)nslots; (void)n; (void)h;                    /* TODO */
}
static void key_str(const lit_test *T, unsigned key, char *buf, size_t cap) {
    size_t off = 0;
    buf[0] = '\0';
    for (int s = 0; s < T->nregs + T->nfinal && off < cap; s++)
        off += (size_t)snprintf(buf + off, cap - off, "%s%s=%u", s ? " " : "", T->slot[s], (key >> (2 * s)) & 3u);
}

/* ======================= Part A-4: 실제 스레드 실행 ======================= */
/* 반복마다 장벽 / 배치 시작에만 장벽 / 공통 시간축에 맞춰 출발 (litmus7 "timebase" 와 같은 생각) / 강제 스케줄 (Part B) */
enum { M_EACH, M_BATCH, M_TIME, M_FORCED };
typedef struct {
    const lit_test *T;
    int V, mode;
    bool reset;                          /* 배치 사이에 셀을 0 으로 (연습 3: 끄면 무슨 일이?) */
    size_t n, batch;
    lit_ctx c;
    spin_barrier bar;
    const uint8_t (*sched)[MAXSTEPS];    /* M_FORCED: 반복 j 는 sched[j % nsched] 차례로 */
    size_t nsched;
    uint64_t gap_ns;                     /* M_TIME: 반복 사이 간격 (기본 200 ns) */
    _Alignas(LINE) _Atomic uint64_t t_start;   /* M_TIME: 이번 배치 반복 0 의 출발 시각 (ticks) */
    _Alignas(LINE) _Atomic size_t turn;  /* M_FORCED: 다음 차례 번호 = j * nsteps + pos */
} lit_run_t;
typedef struct { lit_run_t *R; int t; } lit_targ;

static void forced_iter(lit_run_t *R, int t, size_t i, size_t j) {
    const lit_test *T = R->T;
    const uint8_t *s = R->sched[j % R->nsched];
    for (int pos = 0, k = 0; pos < T->nsteps; pos++) {
        if (s[pos] != t) continue;
        size_t target = j * (size_t)T->nsteps + (size_t)pos;
        for (uint32_t spins = 0; atomic_load_explicit(&R->turn, memory_order_acquire) != target;)
            if (++spins > SPIN_LIMIT) { atomic_store(&g_timeout, 1); return; }
        T->step[t](&R->c, i, j, k++);
        atomic_store_explicit(&R->turn, target + 1, memory_order_release);
    }
}
static void reset_cells(lit_run_t *R) {
    for (int k = 0; k < NLOC; k++)
        for (size_t i = 0; i < R->batch; i++) atomic_store_explicit(&R->c.loc[k][i].v, 0, memory_order_relaxed);
}
static void *lit_thread(void *arg) {
    lit_targ *a = arg;
    lit_run_t *R = a->R;
    const lit_test *T = R->T;
    const int t = a->t;
    const body_fn body = T->body[t][R->V];
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    const uint64_t gap = (R->gap_ns ? R->gap_ns : 200) * tb.denom / tb.numer + 1;   /* ns → ticks */
    for (size_t base = 0; base < R->n; base += R->batch) {
        size_t len = R->n - base < R->batch ? R->n - base : R->batch;
        sbar_wait(&R->bar);                                   /* 배치 시작: 셀은 모두 0 */
        uint64_t t0 = 0;
        if (R->mode == M_TIME) {
            if (t == 0) atomic_store_explicit(&R->t_start, mach_absolute_time() + 100 * gap, memory_order_relaxed);
            sbar_wait(&R->bar);                               /* 출발 시각 공유 (장벽이 happens-before) */
            t0 = atomic_load_explicit(&R->t_start, memory_order_relaxed);
        }
        for (size_t i = 0; i < len; i++) {
            if (R->mode == M_EACH) sbar_wait(&R->bar);        /* 반복마다 같이 출발 */
            if (R->mode == M_TIME) while (mach_absolute_time() < t0 + i * gap) { }   /* 늦으면 그냥 지나감 */
            if (R->mode == M_FORCED) forced_iter(R, t, i, base + i);
            else body(&R->c, i, base + i);
        }
        sbar_wait(&R->bar);                                   /* 배치 끝: 모두 끝났다 */
        if (t == 0) {                                         /* 최종값 관찰자 + 다음 배치 준비 */
            for (int f = 0; f < T->nfinal; f++)
                for (size_t i = 0; i < len; i++)
                    R->c.reg[T->nregs + f][base + i] =
                        (uint8_t)atomic_load_explicit(&R->c.loc[T->final_loc[f]][i].v, memory_order_relaxed);
            if (R->reset) reset_cells(R);
        }
    }
    return NULL;
}
/* 실행 1 회: n 반복 → 히스토그램 h 에 더함. 반환 0 = 정상, -1 = 메모리/스레드 실패 또는 시간 초과. */
static int lit_run(lit_run_t *R, hist_t *h) {
    const lit_test *T = R->T;
    int rc = -1, nslots = T->nregs + T->nfinal;
    memset(&R->c, 0, sizeof R->c);
    if (R->batch == 0 || R->batch > R->n) R->batch = R->n;
    for (int k = 0; k < NLOC; k++) {
        void *p = NULL;
        if (posix_memalign(&p, LINE, R->batch * sizeof(cell_t)) != 0) goto out;
        R->c.loc[k] = p;
        for (size_t i = 0; i < R->batch; i++) atomic_init(&R->c.loc[k][i].v, 0);
    }
    for (int s = 0; s < nslots; s++)
        if (!(R->c.reg[s] = calloc(R->n, 1))) goto out;
    sbar_init(&R->bar, (unsigned)T->nthreads);
    atomic_init(&R->turn, 0);
    atomic_init(&R->t_start, 0);
    atomic_store(&g_timeout, 0);
    pthread_t th[MAXT];
    lit_targ args[MAXT];
    int started = 0;
    for (; started < T->nthreads; started++) {
        args[started] = (lit_targ){ R, started };
        if (pthread_create(&th[started], NULL, lit_thread, &args[started]) != 0) break;
    }
    for (int t = 0; t < started; t++) pthread_join(th[t], NULL);
    if (started == T->nthreads && !atomic_load(&g_timeout)) {
        lit_tally(&R->c, nslots, R->n, h);
        rc = 0;
    }
out:
    for (int k = 0; k < NLOC; k++) free(R->c.loc[k]);
    for (int s = 0; s < MAXREG; s++) free(R->c.reg[s]);
    memset(&R->c, 0, sizeof R->c);
    return rc;
}
static int run_simple(int test, int V, int mode, size_t n, hist_t *h) {
    lit_run_t R = { .T = &TESTS[test], .V = V, .mode = mode, .reset = true, .n = n, .batch = 4096 };
    return lit_run(&R, h);
}

/* ======================= Part B: 강제 스케줄 = SC 인터리빙 전부 ======================= */
#define MAXSCHED 256
/* TODO B1: 각 스레드의 연산 순서(프로그램 순서)를 지키는 모든 인터리빙을 out[] 에 적는다.
 * out[s][pos] = pos 번째에 실행할 스레드 번호. 사전식 순서 (매 칸에서 작은 스레드 번호 먼저).
 * 반환 = 개수 (cap 을 넘으면 cap 까지만 적고 계속 센다). SB/MP/LB/2+2W = 6, IRIW = 180. */
static size_t lit_enum_scheds(const lit_test *T, uint8_t (*out)[MAXSTEPS], size_t cap) {
    (void)T; (void)out; (void)cap;
    return 0;                                                   /* TODO */
}
/* 스케줄 하나를 한 스레드에서 실행 → 결과 키 (반복 1 개짜리 ctx). */
static unsigned lit_eval_sched(const lit_test *T, const uint8_t *sched) {
    static cell_t cells[NLOC];
    uint8_t regs[MAXREG][1] = {{0}};
    lit_ctx c = { .loc = { &cells[0], &cells[1] } };
    for (int s = 0; s < MAXREG; s++) c.reg[s] = regs[s];
    for (int k = 0; k < NLOC; k++) atomic_store(&cells[k].v, 0);
    int next[MAXT] = {0};
    for (int pos = 0; pos < T->nsteps; pos++) {
        int t = sched[pos];
        T->step[t](&c, 0, 0, next[t]++);
    }
    for (int f = 0; f < T->nfinal; f++) regs[T->nregs + f][0] = (uint8_t)atomic_load(&cells[T->final_loc[f]].v);
    return outcome_key(&c, T->nregs + T->nfinal, 0);
}
/* SC 히스토그램: 인터리빙마다 1 표. 반환 = 인터리빙 수. */
static size_t lit_sc_hist(const lit_test *T, hist_t *h) {
    static uint8_t sc[MAXSCHED][MAXSTEPS];
    size_t n = lit_enum_scheds(T, sc, MAXSCHED);
    for (size_t s = 0; s < n && s < MAXSCHED; s++) { h->cnt[lit_eval_sched(T, sc[s])]++; h->total++; }
    return n;
}
static int distinct(const hist_t *h) {
    int d = 0;
    for (int k = 0; k < NKEY; k++) d += h->cnt[k] != 0;
    return d;
}
/* 관찰된 키가 모두 SC 집합 안인가? (C11 이 SC 만 허용하는 변형용) */
static uint64_t outside_sc(const hist_t *obs, const hist_t *sc) {
    uint64_t bad = 0;
    for (int k = 0; k < NKEY; k++) if (obs->cnt[k] && !sc->cnt[k]) bad += obs->cnt[k];
    return bad;
}

/* ======================= Part C: 모델 표와 판정 ======================= */
enum { MD_C11, MD_ARM, MD_TSO, MD_N };    /* 언어 / 이 맥(arm64, clang 매핑) / x86-64 (Rosetta, clang 매핑) */
/* ALLOW[test][variant][model] = 1 이면 그 모델이 "이상한" 결과를 허용. (MEM-M07 이 공리 모델로 계산하는 표) */
static const uint8_t ALLOW[T_N][V_N][MD_N] = {
    /*            relaxed     acq/rel     seq_cst     fence      */
    [T_SB]   = { {1, 1, 1},  {1, 1, 1},  {0, 0, 0},  {0, 0, 0} },
    [T_MP]   = { {1, 1, 0},  {0, 0, 0},  {0, 0, 0},  {0, 0, 0} },
    [T_LB]   = { {1, 1, 0},  {0, 0, 0},  {0, 0, 0},  {0, 0, 0} },
    [T_IRIW] = { {1, 1, 0},  {1, 0, 0},  {0, 0, 0},  {1, 0, 0} },
    [T_22W]  = { {1, 1, 0},  {1, 0, 0},  {0, 0, 0},  {0, 0, 0} },
};
typedef enum {
    VD_SEEN,        /* 허용, 실제로 나옴 — 재배치의 증거 */
    VD_UNSEEN,      /* 하드웨어 모델이 허용하는데 0 — "안 보였을 뿐", 금지 아님 */
    VD_ZERO,        /* 금지, 0 — 기대대로 */
    VD_HW_BUG,      /* C11 은 허용(프로그램이 이미 틀림)인데 하드웨어 모델은 금지 → 나옴: 칩 또는 컴파일러 확인 */
    VD_LANG_BUG,    /* C11 이 금지했는데 나옴: 컴파일러 매핑 또는 실리콘 버그 (또는 하네스 버그) */
} verdict_t;
static const char *const VDNAME[] = { "seen", "allowed-unseen", "zero(ok)", "HW-BUG", "LANG-BUG" };
/* TODO C1: 관찰된 이상한 결과 수 weird 와 두 모델의 허용 여부로 판정. */
static verdict_t lit_verdict(uint64_t weird, bool lang_allows, bool hw_allows) {
    (void)weird; (void)lang_allows; (void)hw_allows;
    return VD_ZERO;                                             /* TODO */
}
typedef struct { int test, V; uint64_t weird, n; } lit_obs;
/* 회귀 게이트: 관찰 목록 → 버그 개수 (hw = MD_ARM 또는 MD_TSO). bugs_out 에 첫 버그 인덱스. */
static int lit_gate(const lit_obs *o, int n, int hw, int *first_bug) {
    int bugs = 0;
    for (int i = 0; i < n; i++) {
        verdict_t v = lit_verdict(o[i].weird, ALLOW[o[i].test][o[i].V][MD_C11], ALLOW[o[i].test][o[i].V][hw]);
        if (v == VD_HW_BUG || v == VD_LANG_BUG) { if (bugs == 0 && first_bug) *first_bug = i; bugs++; }
    }
    return bugs;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    printf("SKIP %d checks (%s): unit checks failed\n", n, what);
}

typedef struct { _Atomic unsigned seen[2]; spin_barrier b; _Atomic unsigned viol; } bar_shared;
typedef struct { bar_shared *s; int t; } bar_arg;
static void *bar_worker(void *p) {
    bar_arg *a = p;
    bar_shared *s = a->s;
    for (unsigned r = 1; r <= 2000; r++) {
        atomic_store_explicit(&s->seen[a->t], r, memory_order_relaxed);
        sbar_wait(&s->b);                               /* 장벽 뒤: 상대의 이번 라운드 쓰기가 보여야 한다 */
        if (atomic_load_explicit(&s->seen[1 - a->t], memory_order_relaxed) < r) atomic_fetch_add(&s->viol, 1);
        sbar_wait(&s->b);
    }
    return NULL;
}

static void test_part_a(void) {
    /* A1: 변형 표 */
    CHECK(ord(V_RLX, K_ST) == memory_order_relaxed && ord(V_RLX, K_LD) == memory_order_relaxed);
    CHECK(ord(V_ACQREL, K_ST) == memory_order_release && ord(V_ACQREL, K_LD) == memory_order_acquire);
    CHECK(ord(V_SC, K_ST) == memory_order_seq_cst && ord(V_SC, K_LD) == memory_order_seq_cst);
    CHECK(ord(V_FENCE, K_ST) == memory_order_relaxed && ord(V_FENCE, K_LD) == memory_order_relaxed);
    CHECK(fence_ord(T_SB, V_FENCE, 0) == memory_order_seq_cst && fence_ord(T_SB, V_FENCE, 1) == memory_order_seq_cst);
    CHECK(fence_ord(T_MP, V_FENCE, 0) == memory_order_release && fence_ord(T_MP, V_FENCE, 1) == memory_order_acquire);
    CHECK(fence_ord(T_LB, V_FENCE, 0) == memory_order_acq_rel && fence_ord(T_22W, V_FENCE, 1) == memory_order_seq_cst);
    CHECK(fence_ord(T_SB, V_SC, 0) == memory_order_relaxed && fence_ord(T_MP, V_ACQREL, 1) == memory_order_relaxed);
    /* A2: 장벽 — 1 스레드면 부를 때마다 세대가 1 씩 */
    spin_barrier b1;
    sbar_init(&b1, 1);
    for (int k = 0; k < 5; k++) sbar_wait(&b1);
    CHECK(atomic_load(&b1.gen) == 5u && atomic_load(&b1.count) == 0u);
    /* A3: 키와 히스토그램 — 손으로 채운 결과 배열 */
    uint8_t r0[6] = {0, 1, 1, 0, 1, 2}, r1[6] = {0, 0, 1, 1, 1, 1};
    lit_ctx c = { .reg = { r0, r1 } };
    CHECK(outcome_key(&c, 2, 0) == K2(0, 0) && outcome_key(&c, 2, 1) == K2(1, 0) && outcome_key(&c, 2, 5) == K2(2, 1));
    hist_t h = {{0}, 0};
    lit_tally(&c, 2, 6, &h);
    CHECK(h.total == 6 && h.cnt[K2(1, 1)] == 2 && h.cnt[K2(0, 0)] == 1 && h.cnt[K2(0, 1)] == 1 && h.cnt[K2(2, 1)] == 1);
    char buf[64];
    key_str(&TESTS[T_MP], K2(1, 0), buf, sizeof buf);
    CHECK(strcmp(buf, "flag=1 data=0") == 0);
}
static void test_part_a_threads(void) {
    if (FAILS()) { skip(11, "Part A threads"); return; }
    /* 2 스레드 장벽: 라운드 2000 번 × 장벽 2 번 */
    static bar_shared bs;
    atomic_init(&bs.seen[0], 0); atomic_init(&bs.seen[1], 0); atomic_init(&bs.viol, 0);
    sbar_init(&bs.b, 2);
    bar_arg ba[2] = { { &bs, 0 }, { &bs, 1 } };
    pthread_t p0, p1;
    pthread_create(&p0, NULL, bar_worker, &ba[0]);
    pthread_create(&p1, NULL, bar_worker, &ba[1]);
    pthread_join(p0, NULL);
    pthread_join(p1, NULL);
    CHECK(atomic_load(&bs.viol) == 0 && atomic_load(&bs.b.gen) == 4000u && !atomic_load(&g_timeout));
    /* 실제 스레드: C11 이 금지하는 변형은 이상한 결과가 정확히 0, 반복 수는 정확히 N_CHECK */
    static const int forb[][2] = { {T_SB, V_SC}, {T_SB, V_FENCE}, {T_MP, V_ACQREL}, {T_MP, V_SC}, {T_MP, V_FENCE},
                                   {T_LB, V_ACQREL}, {T_LB, V_SC}, {T_LB, V_FENCE} };
    uint64_t weird_any = 0, total_ok = 0;
    for (size_t k = 0; k < sizeof forb / sizeof forb[0]; k++) {
        const lit_test *T = &TESTS[forb[k][0]];
        hist_t h = {{0}, 0}, sc = {{0}, 0};
        int rc = run_simple(forb[k][0], forb[k][1], M_TIME, N_CHECK, &h);
        lit_sc_hist(T, &sc);
        CHECK(rc == 0 && h.cnt[T->weird] == 0 && outside_sc(&h, &sc) == 0);
        weird_any += h.cnt[T->weird];
        total_ok += h.total == N_CHECK;
    }
    CHECK(weird_any == 0 && total_ok == 8);
    /* 정보: 허용 변형에서 몇 번 나왔나 (체크 아님 — 0 이어도 정상) */
    printf("info  N=%u per run, weird outcomes in ALLOWED variants:", N_CHECK);
    static const int allow[][2] = { {T_SB, V_RLX}, {T_SB, V_ACQREL}, {T_MP, V_RLX}, {T_LB, V_RLX} };
    int fails = 0;
    for (size_t k = 0; k < 4; k++) {
        hist_t h = {{0}, 0};
        fails += run_simple(allow[k][0], allow[k][1], M_TIME, N_CHECK, &h) != 0;
        printf("  %s/%s=%llu", TESTS[allow[k][0]].name, VNAME[allow[k][1]], (unsigned long long)h.cnt[TESTS[allow[k][0]].weird]);
    }
    printf("\n");
    CHECK(fails == 0);
}
static void test_part_b(void) {
    static uint8_t sc[MAXSCHED][MAXSTEPS];
    CHECK(lit_enum_scheds(&TESTS[T_SB], sc, MAXSCHED) == 6);
    CHECK(sc[0][0] == 0 && sc[0][1] == 0 && sc[0][2] == 1 && sc[0][3] == 1);       /* 사전식 첫째: T0 T0 T1 T1 */
    CHECK(sc[5][0] == 1 && sc[5][1] == 1 && sc[5][2] == 0 && sc[5][3] == 0);       /* 마지막: T1 T1 T0 T0 */
    CHECK(lit_enum_scheds(&TESTS[T_IRIW], sc, MAXSCHED) == 180);                    /* 6!/(1!1!2!2!) */
    hist_t sb = {{0}, 0}, mp = {{0}, 0}, lb = {{0}, 0};
    lit_sc_hist(&TESTS[T_SB], &sb);
    lit_sc_hist(&TESTS[T_MP], &mp);
    lit_sc_hist(&TESTS[T_LB], &lb);
    /* 손으로 센 값 (5. 풀이 Part B 표) */
    CHECK(sb.total == 6 && sb.cnt[K2(1, 1)] == 4 && sb.cnt[K2(0, 1)] == 1 && sb.cnt[K2(1, 0)] == 1 && sb.cnt[K2(0, 0)] == 0);
    CHECK(mp.total == 6 && mp.cnt[K2(1, 1)] == 1 && mp.cnt[K2(0, 1)] == 4 && mp.cnt[K2(0, 0)] == 1 && mp.cnt[K2(1, 0)] == 0);
    CHECK(lb.total == 6 && lb.cnt[K2(0, 0)] == 4 && lb.cnt[K2(0, 1)] == 1 && lb.cnt[K2(1, 0)] == 1 && lb.cnt[K2(1, 1)] == 0);
    CHECK(distinct(&sb) == 3 && distinct(&mp) == 3 && distinct(&lb) == 3);
}
static void test_part_b_threads(void) {
    if (FAILS()) { skip(4, "Part B threads"); return; }
    /* 같은 스케줄을 실제 스레드 + 차례 토큰으로: 매 반복 결과가 정해진다 → 히스토그램 = 반복 수 × SC 히스토그램 */
    static uint8_t sc[MAXSCHED][MAXSTEPS];
    const size_t reps = LIT_SANITIZED ? 20 : 200;
    for (int test = T_SB; test <= T_LB; test++) {
        const lit_test *T = &TESTS[test];
        size_t ns = lit_enum_scheds(T, sc, MAXSCHED);
        hist_t want = {{0}, 0}, got = {{0}, 0};
        lit_sc_hist(T, &want);
        lit_run_t R = { .T = T, .V = V_SC, .mode = M_FORCED, .reset = true, .n = ns * reps, .batch = 512,
                        .sched = (const uint8_t (*)[MAXSTEPS])sc, .nsched = ns };
        int rc = lit_run(&R, &got);
        bool same = rc == 0 && got.total == want.total * reps;
        for (int k = 0; k < NKEY; k++) same = same && got.cnt[k] == want.cnt[k] * reps;
        CHECK(same);
    }
    CHECK(!atomic_load(&g_timeout));
}
static void test_part_c(void) {
    CHECK(lit_verdict(5, true, true) == VD_SEEN);
    CHECK(lit_verdict(0, true, true) == VD_UNSEEN);
    CHECK(lit_verdict(0, true, false) == VD_ZERO && lit_verdict(0, false, false) == VD_ZERO);
    CHECK(lit_verdict(3, false, false) == VD_LANG_BUG);
    CHECK(lit_verdict(2, true, false) == VD_HW_BUG);
    /* 표의 핵심 칸: SB 는 acq/rel 로 못 막고, MP/LB 는 TSO 가 relaxed 도 막는다, IRIW acq 는 C11 허용·ARMv8 금지 */
    CHECK(ALLOW[T_SB][V_ACQREL][MD_C11] && ALLOW[T_SB][V_ACQREL][MD_TSO] && !ALLOW[T_MP][V_RLX][MD_TSO]);
    CHECK(ALLOW[T_IRIW][V_ACQREL][MD_C11] && !ALLOW[T_IRIW][V_ACQREL][MD_ARM]);
    /* 게이트: 정상 칩 기록 vs 가상의 버그 칩 기록 (합성 데이터) */
    const lit_obs good[] = { {T_SB, V_RLX, 812, 1000000}, {T_SB, V_SC, 0, 1000000}, {T_MP, V_RLX, 0, 1000000},
                             {T_MP, V_ACQREL, 0, 1000000}, {T_LB, V_RLX, 0, 1000000} };
    const lit_obs bad[]  = { {T_SB, V_RLX, 812, 1000000}, {T_MP, V_ACQREL, 3, 1000000}, {T_LB, V_SC, 0, 1000000} };
    int first = -1;
    CHECK(lit_gate(good, 5, MD_ARM, &first) == 0 && first == -1);
    CHECK(lit_gate(bad, 3, MD_ARM, &first) == 1 && first == 1);
    if (first >= 0 && first < 3)
        printf("gate  synthetic chip: %s/%s weird=%llu/%llu -> %s\n", TESTS[bad[first].test].name, VNAME[bad[first].V],
               (unsigned long long)bad[first].weird, (unsigned long long)bad[first].n,
               VDNAME[lit_verdict(bad[first].weird, ALLOW[bad[first].test][bad[first].V][MD_C11],
                                  ALLOW[bad[first].test][bad[first].V][MD_ARM])]);
    CHECK(lit_gate(good, 5, MD_TSO, &first) == 0);
}

int main(void) {
    test_part_a();
    test_part_a_threads();
    test_part_b();
    test_part_b_threads();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
