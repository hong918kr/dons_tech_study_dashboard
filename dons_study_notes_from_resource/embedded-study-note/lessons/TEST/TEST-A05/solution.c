/* TEST-A05 결정론적 시뮬레이션 테스트: 가상 시간과 재현 가능한 seed
 *
 * 구성 (한 파일, 스레드 없음, 벽시계 없음)
 *   [코어] 가상 시계 + 이벤트 큐 + 결정 테이프(decision tape) + trace hash
 *   [HAL]  펌웨어가 바깥을 보는 유일한 창구. 시뮬에서는 코어가, 타깃에서는 레지스터가 구현
 *   [FW]   시험 대상. 아래 FW 구간은 HAL만 호출한다 (Cortex-M용으로 그대로 컴파일됨)
 *   [시험] 앱 + 장치 모델 + 불변식 + seed sweep + shrink
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o s && ./s
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ===================== Part A: PRNG · 이벤트 큐 · 해시 ===================== */

typedef struct { uint64_t s; } rng_t;

/* splitmix64 finalizer: 작은 seed(1,2,3,...)도 상태 전체로 퍼뜨린다. */
static void rng_seed(rng_t *r, uint64_t seed)
{
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    r->s = z ? z : 1u;              /* xorshift는 0 상태에서 영원히 0 */
}

/* TODO(A1): xorshift64 (13, 7, 17) 한 스텝. 상태를 갱신하고 새 상태를 돌려준다. */
static uint64_t rng_next(rng_t *r)
{
    uint64_t x = r->s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    r->s = x;
    return x;
}

enum { EV_APP, EV_RX, EV_TMR };
enum { PRIO_IDLE = -1, PRIO_APP = 0 };

typedef struct {
    uint64_t t;         /* 가상 시각(us) */
    uint32_t tie;       /* 같은 시각 이벤트의 순서를 정하는 난수 */
    uint32_t seq;       /* push 순번: 전순서(total order)를 보장 */
    int      prio;      /* 클수록 급하다 */
    int      kind;
    uint32_t arg;
} event_t;

#define EQ_MAX 24
typedef struct { event_t a[EQ_MAX]; int n; uint32_t next_seq; } evq_t;

/* TODO(A2): x가 y보다 먼저면 true. (t, tie, seq) 사전식 비교. */
static bool ev_before(const event_t *x, const event_t *y)
{
    if (x->t != y->t) return x->t < y->t;
    if (x->tie != y->tie) return x->tie < y->tie;
    return x->seq < y->seq;
}

static bool eq_push(evq_t *q, uint64_t t, int prio, int kind, uint32_t arg, uint32_t tie)
{
    if (q->n == EQ_MAX) return false;
    event_t e = { t, tie, q->next_seq++, prio, kind, arg };
    q->a[q->n++] = e;
    return true;
}

/* 가장 이른 이벤트의 인덱스 (없으면 -1). 시계를 어디로 점프할지 정할 때 쓴다. */
static int eq_earliest(const evq_t *q)
{
    int best = -1;
    for (int i = 0; i < q->n; i++)
        if (best < 0 || ev_before(&q->a[i], &q->a[best])) best = i;
    return best;
}

/* TODO(A3): 시각이 now 이하(기한 도달)이고 prio > above_prio 인 이벤트 중
 * prio가 가장 높은 것, 같으면 ev_before로 가장 앞선 것의 인덱스. 없으면 -1.
 * (NVIC가 pending 인터럽트 중 하나를 고르는 것과 같은 규칙) */
static int eq_pick(const evq_t *q, uint64_t now, int above_prio)
{
    int best = -1;
    for (int i = 0; i < q->n; i++) {
        const event_t *e = &q->a[i];
        if (e->t > now || e->prio <= above_prio) continue;
        if (best < 0 || e->prio > q->a[best].prio ||
            (e->prio == q->a[best].prio && ev_before(e, &q->a[best])))
            best = i;
    }
    return best;
}

/* swap-remove: 배열 순서는 바뀌지만 선택 규칙이 전순서라 결과에 영향이 없다. */
static event_t eq_take(evq_t *q, int i)
{
    event_t e = q->a[i];
    q->a[i] = q->a[--q->n];
    return e;
}

#define FNV_BASIS 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull

/* TODO(A4): FNV-1a 64를 v의 8바이트에 낮은 바이트부터 적용해 새 h를 돌려준다.
 * (구조체를 통째로 해시하면 padding 바이트가 섞여서 결과가 흔들린다) */
static uint64_t hash_u64(uint64_t h, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        h ^= (v >> (8 * i)) & 0xffu;
        h *= FNV_PRIME;
    }
    return h;
}

/* ===================== HAL: 펌웨어가 보는 세계의 전부 ===================== */

uint32_t hal_now_us(void);
void     hal_timer_start(uint32_t delay_us);
void     hal_timer_stop(void);              /* 아직 만료 전인 타이머만 취소된다 */
void     hal_uart_send(uint32_t req_id);
uint32_t hal_irq_save(void);
void     hal_irq_restore(uint32_t key);
void     hal_window(void);                  /* 명령 실행 시간. 타깃에서는 빈 함수 */
void     hal_mark(uint32_t tag, uint32_t id); /* 시험용 표식. 타깃에서는 빈 함수 */

enum { MARK_RX_CHECK, MARK_RX_COMMIT, MARK_TMR_CHECK, MARK_TMR_COMMIT };

/* ===================== FW: 시험 대상 (HAL만 호출) ===================== */
/* FW_BEGIN */
#define FW_TIMEOUT_US 1000u
#define FW_NO_RESP    0xFFFFFFFFu

/* 고침 세트. 실제 제품에서는 FW_F_ALL로 고정한다. */
enum { FW_F_NONE = 0, FW_F_CLAIM = 1, FW_F_MATCH_ID = 2, FW_F_STOP_FIRST = 4, FW_F_ALL = 7 };

typedef enum { FW_OK = 0, FW_TIMEOUT = 1 } fw_status;
typedef void (*fw_done_fn)(uint32_t req_id, fw_status st, uint32_t resp_id);

static struct {
    volatile bool     active;
    volatile uint32_t id;
    uint32_t          fixes;
    fw_done_fn        done;
} g_fw;

void fw_init(uint32_t fixes, fw_done_fn done)
{
    g_fw.active = false;
    g_fw.id = 0;
    g_fw.fixes = fixes;
    g_fw.done = done;
}

/* 메인 문맥 또는 완료 콜백(ISR 문맥)에서 호출된다. */
bool fw_submit(uint32_t id)
{
    if (g_fw.active) return false;
    g_fw.id = id;
    g_fw.active = true;
    hal_window();
    hal_timer_start(FW_TIMEOUT_US);
    hal_uart_send(id);
    return true;
}

/* 요청을 "이 경로가 끝낸다"고 확정한다. 성공하면 true, *id_out = 끝낸 요청. */
static bool fw_claim(bool from_rx, uint32_t resp, uint32_t *id_out)
{
    bool atomic = (g_fw.fixes & FW_F_CLAIM) != 0;
    uint32_t key = 0;
    if (atomic) key = hal_irq_save();
    bool ok = g_fw.active;
    uint32_t id = g_fw.id;
    if (ok && from_rx && (g_fw.fixes & FW_F_MATCH_ID)) ok = (resp == id);
    hal_mark(from_rx ? MARK_RX_CHECK : MARK_TMR_CHECK, id);
    hal_window();           /* ← 확인과 확정 사이: 출시본은 여기가 열려 있다 */
    if (ok) {
        g_fw.active = false;
        hal_mark(from_rx ? MARK_RX_COMMIT : MARK_TMR_COMMIT, id);
    }
    if (atomic) hal_irq_restore(key);
    *id_out = id;
    return ok;
}

/* UART RX-complete ISR에서 호출 */
void fw_on_uart_rx(uint32_t resp)
{
    uint32_t id;
    if (!fw_claim(true, resp, &id)) return;
    if (g_fw.fixes & FW_F_STOP_FIRST) hal_timer_stop();
    g_fw.done(id, FW_OK, resp);
    if (!(g_fw.fixes & FW_F_STOP_FIRST)) hal_timer_stop();
}

/* 타이머 만료 ISR에서 호출 */
void fw_on_timer(void)
{
    uint32_t id;
    if (!fw_claim(false, 0, &id)) return;
    g_fw.done(id, FW_TIMEOUT, FW_NO_RESP);
}
/* FW_END */

/* ===================== 코어: 시뮬레이터 상태 ===================== */

enum { V_NONE, V_DOUBLE, V_STALE, V_HANG, V_BUSY, V_QFULL, V_RUNAWAY, V_COUNT };
static const char *const VIOL_NAME[V_COUNT] = {
    "none", "DOUBLE_COMPLETE", "STALE_RESP", "HANG", "BUSY", "QFULL", "RUNAWAY"
};

enum { TR_APP, TR_RX, TR_TMR, TR_TX, TR_DROP, TR_T_START, TR_T_STOP,
       TR_DONE_OK, TR_DONE_TO, TR_MARK0, TR_VIOL = TR_MARK0 + 4 };
static const char *const TR_NAME[] = {
    "EV app", "EV rx", "EV timer", "uart_tx", "uart_drop", "timer_start", "timer_stop",
    "DONE ok", "DONE timeout", "rx.check", "rx.commit", "tmr.check", "tmr.commit", "VIOLATION"
};

typedef struct { uint64_t t; int kind; uint32_t arg; int depth; } trace_rec;

#define MAX_REQ    13
#define MAX_TAPE   4096
#define MAX_TRACE  2048
#define MAX_STEPS  5000u

/* 시나리오 설정. 코어 밖에서 바꿔 실험한다 (seed와 함께 재현 조건의 일부). */
static struct { uint32_t window_max; int prio_rx, prio_tmr; bool faults; } g_cfg = { 4, 1, 2, true };

static struct {
    evq_t    q;
    rng_t    rng;
    uint64_t now;
    int      cur_prio;
    bool     masked;
    uint64_t hash;
    uint32_t n_trace;
    /* 결정 테이프 */
    bool            replay;
    const uint32_t *replay_tape;
    int             tape_len, tape_pos;
    /* 앱 · 불변식 */
    uint32_t n_req, next_id, n_done;
    uint8_t  done_count[MAX_REQ];
    int      viol;
    uint32_t viol_id;
} g;

static uint32_t  g_tape[MAX_TAPE];
static trace_rec g_trace[MAX_TRACE];

/* 시뮬에서 쓰는 모든 무작위 결정이 이 함수 하나를 지난다. */
/* TODO(A5): replay면 테이프의 tape_pos번째 값 % bound (테이프가 끝났으면 0).
 * 아니면 rng_next % bound를 g_tape에 기록하고 돌려준다 (MAX_TAPE를 넘기면 기록만 멈춘다). */
static uint32_t sim_choice(uint32_t bound)
{
    uint32_t v;
    if (g.replay) {
        v = g.tape_pos < g.tape_len ? g.replay_tape[g.tape_pos] : 0u;
        g.tape_pos++;
        return bound ? v % bound : 0u;
    }
    v = bound ? (uint32_t)(rng_next(&g.rng) % bound) : 0u;
    if (g.tape_len < MAX_TAPE) g_tape[g.tape_len++] = v;
    g.tape_pos++;
    return v;
}

static void trace_log(int kind, uint32_t arg)
{
    g.hash = hash_u64(g.hash, g.now);
    g.hash = hash_u64(g.hash, ((uint64_t)(uint32_t)kind << 32) | arg);
    g.hash = hash_u64(g.hash, (uint64_t)(int64_t)g.cur_prio);
    if (g.n_trace < MAX_TRACE) {
        trace_rec r = { g.now, kind, arg, g.cur_prio };
        g_trace[g.n_trace] = r;
    }
    g.n_trace++;
}

static void violate(int v, uint32_t id)
{
    if (g.viol) return;                 /* 첫 위반만 남긴다 */
    g.viol = v;
    g.viol_id = id;
    trace_log(TR_VIOL, ((uint32_t)v << 16) | id);
}

static void push_ev(uint64_t t, int prio, int kind, uint32_t arg)
{
    if (!eq_push(&g.q, t, prio, kind, arg, sim_choice(1u << 16))) violate(V_QFULL, arg);
}

static void dispatch(event_t e);

/* TODO(B1): masked가 아니고 위반이 없는 동안, eq_pick(now, cur_prio)로 고른 이벤트를
 * eq_take로 꺼내 dispatch 한다. 꺼낸 이벤트마다 다시 고른다 (같은 시각에 여럿일 수 있다). */
static void dispatch_due(void)
{
    while (!g.masked && !g.viol) {
        int i = eq_pick(&g.q, g.now, g.cur_prio);
        if (i < 0) return;
        dispatch(eq_take(&g.q, i));
    }
}

/* ===================== HAL 구현 (시뮬레이터 쪽) ===================== */

uint32_t hal_now_us(void) { return (uint32_t)g.now; }

void hal_timer_start(uint32_t delay_us)
{
    trace_log(TR_T_START, delay_us);
    push_ev(g.now + delay_us, g_cfg.prio_tmr, EV_TMR, 0);
}

/* 이미 만료된(t <= now) 타이머는 pending 비트가 선 것과 같아서 취소되지 않는다. */
void hal_timer_stop(void)
{
    trace_log(TR_T_STOP, 0);
    for (int i = g.q.n - 1; i >= 0; i--)
        if (g.q.a[i].kind == EV_TMR && g.q.a[i].t > g.now) (void)eq_take(&g.q, i);
}

/* 장치 모델: 응답을 언제 돌려줄지(또는 안 돌려줄지)를 seed가 정한다. */
void hal_uart_send(uint32_t req_id)
{
    trace_log(TR_TX, req_id);
    uint32_t r = sim_choice(16);
    if (g_cfg.faults && r == 0) {           /* 응답 유실 */
        trace_log(TR_DROP, req_id);
        return;
    }
    uint32_t lat = (r <= 4) ? FW_TIMEOUT_US - 100u + sim_choice(200)    /* 경계에 걸리는 느린 응답 */
                            : 100u + sim_choice(500);                   /* 정상 응답 */
    push_ev(g.now + lat, g_cfg.prio_rx, EV_RX, req_id);
}

uint32_t hal_irq_save(void)
{
    uint32_t old = g.masked;
    g.masked = true;
    return old;
}

void hal_irq_restore(uint32_t key)
{
    g.masked = key != 0;
    if (!g.masked) dispatch_due();
}

/* 코드가 실행되는 동안 흐르는 시간. 그 사이 도착한 더 급한 인터럽트가 끼어든다. */
void hal_window(void)
{
    if (g_cfg.window_max == 0) return;
    g.now += sim_choice(g_cfg.window_max + 1);
    dispatch_due();
}

void hal_mark(uint32_t tag, uint32_t id) { trace_log(TR_MARK0 + (int)tag, id); }

/* ===================== 시험: 앱 · 불변식 · 실행기 ===================== */

static void app_submit_next(void)
{
    if (g.next_id >= g.n_req) return;
    uint32_t id = g.next_id++;
    if (!fw_submit(id)) violate(V_BUSY, id);
}

/* 펌웨어의 완료 콜백. 여기가 독립 checker다. */
static void app_done(uint32_t id, fw_status st, uint32_t resp)
{
    trace_log(st == FW_OK ? TR_DONE_OK : TR_DONE_TO, id);
    if (id >= MAX_REQ) { violate(V_STALE, id); return; }
    if (++g.done_count[id] > 1) { violate(V_DOUBLE, id); return; }
    if (st == FW_OK && resp != id) { violate(V_STALE, id); return; }
    g.n_done++;
    if (g.next_id < g.n_req) {
        if (sim_choice(2))                  /* 콜백 안에서 바로 다음 요청 */
            app_submit_next();
        else
            push_ev(g.now + sim_choice(200), PRIO_APP, EV_APP, 0);
    }
}

static void dispatch(event_t e)
{
    int saved = g.cur_prio;
    g.cur_prio = e.prio;
    trace_log(e.kind == EV_APP ? TR_APP : e.kind == EV_RX ? TR_RX : TR_TMR, e.arg);
    switch (e.kind) {
    case EV_APP: app_submit_next(); break;
    case EV_RX:  fw_on_uart_rx(e.arg); break;
    default:     fw_on_timer(); break;
    }
    g.cur_prio = saved;
}

typedef struct {
    int viol;
    uint32_t viol_id;
    uint64_t hash, end_us;
    uint32_t events;
    int tape_used;
} result_t;

static void sim_begin(uint32_t fixes)
{
    memset(&g, 0, sizeof g);
    g.hash = FNV_BASIS;
    g.cur_prio = PRIO_IDLE;
    fw_init(fixes, app_done);
}

static result_t sim_body(void)
{
    g.n_req = 2 + sim_choice(MAX_REQ - 1);
    push_ev(sim_choice(50), PRIO_APP, EV_APP, 0);
    uint32_t step = 0;
    for (; step < MAX_STEPS && !g.viol; step++) {
        int i = eq_earliest(&g.q);
        if (i < 0) break;
        if (g.q.a[i].t > g.now) g.now = g.q.a[i].t;     /* 가상 시계 점프 */
        dispatch_due();
    }
    if (!g.viol && g.q.n == 0 && g.n_done < g.n_req) violate(V_HANG, g.next_id ? g.next_id - 1 : 0);
    if (!g.viol && step == MAX_STEPS) violate(V_RUNAWAY, 0);
    result_t r = { g.viol, g.viol_id, g.hash, g.now, g.n_trace,
                   g.replay ? (g.tape_pos < g.tape_len ? g.tape_pos : g.tape_len) : g.tape_len };
    return r;
}

static result_t sim_run_seed(uint64_t seed, uint32_t fixes)
{
    sim_begin(fixes);
    rng_seed(&g.rng, seed);
    return sim_body();
}

static result_t sim_run_tape(const uint32_t *tape, int len, uint32_t fixes)
{
    sim_begin(fixes);
    g.replay = true;
    g.replay_tape = tape;
    g.tape_len = len;
    return sim_body();
}

static void print_trace(uint32_t max)
{
    uint32_t n = g.n_trace < MAX_TRACE ? g.n_trace : MAX_TRACE;
    for (uint32_t i = 0; i < n && i < max; i++) {
        const trace_rec *r = &g_trace[i];
        printf("  t=%6llu prio=%2d  %-12s %u\n", (unsigned long long)r->t, r->depth,
               TR_NAME[r->kind], r->arg);
    }
}

/* ===================== Part B: seed sweep ===================== */

typedef struct {
    uint32_t runs, fails;
    uint32_t by_viol[V_COUNT];
    bool     found;
    uint64_t first_seed;
    int      first_viol;
} sweep_t;

/* TODO(B5): seed = first, first+1, ... (n개)를 돌려 위반 종류별 개수와
 * 처음 실패한 seed·위반 종류를 채운다. */
static void sweep(uint64_t first, uint32_t n, uint32_t fixes, sweep_t *o)
{
    memset(o, 0, sizeof *o);
    for (uint32_t i = 0; i < n; i++) {
        result_t r = sim_run_seed(first + i, fixes);
        o->runs++;
        o->by_viol[r.viol]++;
        if (r.viol != V_NONE) {
            o->fails++;
            if (!o->found) {
                o->found = true;
                o->first_seed = first + i;
                o->first_viol = r.viol;
            }
        }
    }
}

/* ===================== Part C: 실패 테이프 줄이기 ===================== */

#define SHRINK_BUDGET 20000

/* TODO(C1): tape[0..len)을 같은 종류(want)로 실패하는 범위에서 줄이고 새 길이를 돌려준다.
 *  1) 덩어리 삭제: chunk = len/2, len/4, ... (12 이하는 1씩 줄이며) 크기로 지워 본다
 *  2) 값 줄이기: 각 칸을 0, v/2, v-1로 낮춰 본다
 *  더 이상 안 줄 때까지 반복. 시뮬 실행 횟수는 SHRINK_BUDGET 이하, *runs_out에 기록. */
static int shrink_tape(uint32_t *tape, int len, uint32_t fixes, int want, int *runs_out)
{
    static uint32_t cand[MAX_TAPE];
    int runs = 0;
    bool progress = true;
    while (progress && runs < SHRINK_BUDGET) {
        progress = false;
        for (int chunk = len / 2; chunk >= 1; chunk = chunk > 12 ? chunk / 2 : chunk - 1) {
            int pos = 0;
            while (pos + chunk <= len && runs < SHRINK_BUDGET) {
                int clen = len - chunk;
                memcpy(cand, tape, (size_t)pos * sizeof *cand);
                memcpy(cand + pos, tape + pos + chunk, (size_t)(len - pos - chunk) * sizeof *cand);
                result_t r = sim_run_tape(cand, clen, fixes);
                runs++;
                if (r.viol == want) {
                    memcpy(tape, cand, (size_t)clen * sizeof *cand);
                    len = r.tape_used < clen ? r.tape_used : clen;
                    progress = true;
                } else {
                    pos += chunk;
                }
            }
        }
        for (int i = 0; i < len && runs < SHRINK_BUDGET; i++) {
            uint32_t orig = tape[i];
            uint32_t tries[3] = { 0, orig / 2, orig ? orig - 1 : 0 };
            for (int k = 0; k < 3 && orig; k++) {
                if (tries[k] >= tape[i]) continue;
                uint32_t keep = tape[i];
                tape[i] = tries[k];
                result_t r = sim_run_tape(tape, len, fixes);
                runs++;
                if (r.viol == want) { progress = true; break; }
                tape[i] = keep;
            }
        }
    }
    *runs_out = runs;
    return len;
}

/* ===================== 테스트 ===================== */

static void test_part_a(void)
{
    puts("--- Part A: PRNG, event queue, hash");
    rng_t r;
    rng_seed(&r, 42);
    rng_t r2 = r;
    uint64_t a = rng_next(&r), b = rng_next(&r2);
    CHECK(a == b && a != 0);
    r.s = 1;
    CHECK(rng_next(&r) == 0x0000000040822041ull);   /* xorshift64(13,7,17), 상태 1에서 한 스텝 */

    evq_t q;
    memset(&q, 0, sizeof q);
    eq_push(&q, 10, 1, EV_RX, 100, 5);
    eq_push(&q, 5, 1, EV_RX, 101, 9);
    eq_push(&q, 20, 2, EV_TMR, 102, 1);
    int i = eq_earliest(&q);
    CHECK(i >= 0 && q.a[i].arg == 101);
    i = eq_pick(&q, 15, -1);
    CHECK(i >= 0 && q.a[i].arg == 101);             /* 기한 도달한 것 중 가장 이른 것 */
    i = eq_pick(&q, 25, -1);
    CHECK(i >= 0 && q.a[i].arg == 102);             /* prio 우선 */
    CHECK(eq_pick(&q, 25, 2) == -1);                /* 같은 prio는 선점 못 한다 */
    CHECK(eq_pick(&q, 1, -1) == -1);                /* 아직 아무것도 기한 전 */

    /* 같은 시각·같은 prio 3개의 순서는 tie가 정하고, tie는 seed가 정한다 */
    char first[9][4];
    bool any_diff = false, replay_same = true;
    for (uint64_t seed = 1; seed <= 8; seed++) {
        for (int round = 0; round < 2; round++) {
            evq_t q2;
            memset(&q2, 0, sizeof q2);
            rng_t rg;
            rng_seed(&rg, seed);
            for (uint32_t k = 0; k < 3; k++)
                eq_push(&q2, 7, 0, EV_APP, k, (uint32_t)rng_next(&rg) & 0xffff);
            char ord[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < 3; k++) {
                int idx = eq_pick(&q2, 7, -1);
                if (idx < 0) break;
                ord[k] = (char)('a' + eq_take(&q2, idx).arg);
            }
            if (round == 0) memcpy(first[seed], ord, 4);
            else if (memcmp(first[seed], ord, 4) != 0) replay_same = false;
        }
        if (seed > 1 && memcmp(first[seed], first[1], 4) != 0) any_diff = true;
    }
    CHECK(any_diff);        /* seed가 달라지면 순서가 달라진다 */
    CHECK(replay_same);     /* 같은 seed면 순서가 같다 */

    uint64_t h = hash_u64(FNV_BASIS, 0);
    CHECK(h == 0xa8c7f832281a39c5ull);
    CHECK(hash_u64(FNV_BASIS, 1) != h);
}

static uint64_t s_fail_seed;
static int      s_fail_viol;

static void test_part_b(void)
{
    puts("--- Part B: seed sweep finds the race, replay, fix");
    sweep_t s;
    sweep(1, 4000, FW_F_NONE, &s);
    printf("[B] FW_F_NONE: runs=%u fails=%u double=%u stale=%u hang=%u, first_seed=%llu (%s)\n",
           s.runs, s.fails, s.by_viol[V_DOUBLE], s.by_viol[V_STALE], s.by_viol[V_HANG],
           (unsigned long long)s.first_seed, VIOL_NAME[s.first_viol]);
    CHECK(s.runs == 4000);
    CHECK(s.by_viol[V_DOUBLE] > 0);
    CHECK(s.by_viol[V_STALE] > 0);
    s_fail_seed = s.first_seed;
    s_fail_viol = s.first_viol;

    /* 첫 DOUBLE_COMPLETE seed를 찾아 그것을 재현 대상으로 삼는다 */
    uint64_t dseed = 0;
    for (uint64_t seed = 1; seed <= 4000 && !dseed; seed++)
        if (sim_run_seed(seed, FW_F_NONE).viol == V_DOUBLE) dseed = seed;
    CHECK(dseed != 0);
    s_fail_seed = dseed;
    s_fail_viol = V_DOUBLE;

    result_t r1 = sim_run_seed(dseed, FW_F_NONE);
    int tape_len = g.tape_len;
    static uint32_t tape_copy[MAX_TAPE];
    memcpy(tape_copy, g_tape, sizeof g_tape);
    static trace_rec trace_copy[MAX_TRACE];
    memcpy(trace_copy, g_trace, sizeof g_trace);
    uint32_t n1 = g.n_trace;
    result_t r2 = sim_run_seed(dseed, FW_F_NONE);
    printf("[B] seed %llu: viol=%s id=%u events=%u hash=%016llx / replay hash=%016llx\n",
           (unsigned long long)dseed, VIOL_NAME[r1.viol], r1.viol_id, r1.events,
           (unsigned long long)r1.hash, (unsigned long long)r2.hash);
    CHECK(r1.viol == V_DOUBLE);
    CHECK(r1.hash == r2.hash && r1.events == r2.events);
    CHECK(n1 == g.n_trace && memcmp(trace_copy, g_trace, sizeof(trace_rec) * (n1 < MAX_TRACE ? n1 : MAX_TRACE)) == 0);

    result_t r3 = sim_run_tape(tape_copy, tape_len, FW_F_NONE);
    CHECK(r3.hash == r1.hash && r3.viol == V_DOUBLE);   /* seed 없이 테이프만으로도 같다 */

    /* 고침을 하나씩 켠다: 버그 클래스가 하나씩 사라져야 한다 */
    sweep_t c, cm, all;
    sweep(1, 4000, FW_F_CLAIM, &c);
    sweep(1, 4000, FW_F_CLAIM | FW_F_MATCH_ID, &cm);
    sweep(1, 4000, FW_F_ALL, &all);
    printf("[B] CLAIM          : fails=%u double=%u stale=%u hang=%u\n",
           c.fails, c.by_viol[V_DOUBLE], c.by_viol[V_STALE], c.by_viol[V_HANG]);
    printf("[B] CLAIM+MATCH    : fails=%u double=%u stale=%u hang=%u\n",
           cm.fails, cm.by_viol[V_DOUBLE], cm.by_viol[V_STALE], cm.by_viol[V_HANG]);
    printf("[B] CLAIM+MATCH+STOP: fails=%u\n", all.fails);
    CHECK(c.runs == 4000 && c.by_viol[V_DOUBLE] == 0 && c.by_viol[V_STALE] > 0);
    CHECK(cm.runs == 4000 && cm.by_viol[V_DOUBLE] == 0 && cm.by_viol[V_STALE] == 0 && cm.by_viol[V_HANG] > 0);
    CHECK(all.runs == 4000 && all.fails == 0);
}

static void test_part_c(void)
{
    puts("--- Part C: shrink the failing decision tape");
    static uint32_t tape[MAX_TAPE], tape2[MAX_TAPE];
    result_t r = sim_run_seed(s_fail_seed, FW_F_NONE);
    int len0 = g.tape_len;
    memcpy(tape, g_tape, sizeof g_tape);
    memcpy(tape2, g_tape, sizeof g_tape);

    int runs = 0, runs2 = 0;
    int len = shrink_tape(tape, len0, FW_F_NONE, s_fail_viol, &runs);
    int len2 = shrink_tape(tape2, len0, FW_F_NONE, s_fail_viol, &runs2);
    result_t f = sim_run_tape(tape, len, FW_F_NONE);
    printf("[C] tape %d -> %d decisions, events %u -> %u, %d sim runs\n", len0, len, r.events, f.events, runs);
    CHECK(f.viol == s_fail_viol);
    CHECK(len > 0 && len <= len0 / 2);
    CHECK(f.events < r.events);
    CHECK(len == len2 && runs == runs2 && memcmp(tape, tape2, (size_t)len * sizeof *tape) == 0);
    CHECK(sim_run_tape(tape, len, FW_F_ALL).viol == V_NONE);   /* 같은 결정 테이프로 고친 펌웨어를 돌려도 통과 */
    printf("[C] minimal failing trace (%s):\n", VIOL_NAME[f.viol]);
    (void)sim_run_tape(tape, len, FW_F_NONE);
    print_trace(40);
}

int main(void)
{
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
