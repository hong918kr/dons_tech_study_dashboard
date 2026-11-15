/* RTOS-A09 데드라인 미스 사후 분석: 트레이스 케이스 스터디
 *
 * 구성 (한 파일, 표준 C11 만):
 *  ① 결정적 미니 커널 시뮬레이터 (주어진 코드) — 10 µs 슬롯 단위 고정 우선순위 선점, 뮤텍스(PI 선택), 블로킹 큐,
 *     ISR(버스트 도착), 주기 태스크/이벤트 구동 태스크. 실행하면서 RTOS-A06 포맷 트레이스 텍스트를 쓴다.
 *     시스템 5개: S1 우선순위 역전, S2 ISR 과다, S3 큐 포화, S4 RM 위반 우선순위, S5 정상(대조군).
 *     seed 0 = 공칭 실행 시간, seed ≥ 1 = RUN 길이에 0..10 % 흔들림(xorshift32).
 *  ② 트레이스 파서 (주어진 코드, RTOS-A06 포맷의 축약 사본) — 텍스트만 읽는다. 시뮬레이터 내부는 안 본다.
 *  ③ 분석기 — 트레이스 → 타임라인 재구성 → 미스마다 규칙 4개 → 근거가 붙은 진단.
 *  ④ 수정 — 진단을 받아 시스템 설정을 고치고 다시 시뮬레이션 → 미스 0 확인.
 *
 * 이 레슨에서 채우는 것 (TODO 1~8):
 *  1. unwrap32()        : 32비트 µs 타임스탬프 → 단조 증가 64비트 (wrap 처리)
 *  2. tl_build()        : 이벤트 → 실행 구간(ISR 시간은 태스크에서 뺀다), 블록 구간, 뮤텍스 보유 구간, 작업/미스
 *  3. rule_inversion()  : 높은 X 가 뮤텍스 대기, 주인은 낮은 L, 그 사이 중간 M 이 실행
 *  4. rule_isr()        : 미스 창(release..deadline) 안 ISR 시간 비율 ≥ 40 %
 *  5. rule_queue()      : X 가 꽉 찬 큐에 보내다 블록, 소비자는 그동안 거의 못 돎 (누가 대신 돌았나)
 *  6. rule_rm()         : X 보다 주기가 긴데 우선순위가 높은 태스크가 창 안에서 실행
 *  7. rm_assign() + queue_depth_for()
 *  8. apply_fix()       : 원인별 수정 (PI / ISR 일을 태스크로 / 큐 깊이·소비자 우선순위 / RM 재할당)
 *
 * 실행:
 *   ./solution                 테스트 (N/M passed, 실패 시 exit 1)
 *   ./solution -v              테스트 + 진단 리포트·타임라인 출력
 *   ./solution --emit S [seed] [fixed]   시스템 S(1..5) 트레이스를 표준 출력으로
 *   ./solution FILE...         트레이스 파일 분석 (FreeRTOS 데모가 만든 파일 등)
 * 우선순위는 클수록 높다 (FreeRTOS 방향).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================================
 * ① 시스템 모델 + 결정적 시뮬레이터 (주어진 코드)
 * ===================================================================================== */
#define SLOT_US 10u
#define MAXT    8
#define MAXI    2
#define MAXM    2
#define MAXQ    3
#define MAXOP   8

enum { OP_RUN, OP_LOCK, OP_UNLOCK, OP_SEND, OP_RECV };
typedef struct { uint8_t op, obj; uint16_t n; } op_t;
typedef struct { const char *name; int prio, period, offset, nops; op_t ops[MAXOP]; } task_def_t; /* period 0 = 이벤트 구동 */
typedef struct { const char *name; int first, every, count, gap, cost, q; } isr_def_t;            /* q = -1: 아무것도 안 보냄 */
typedef struct { const char *name; int pi; } mtx_def_t;
typedef struct { const char *name; int cap; } q_def_t;
typedef struct {
    const char *title;
    int horizon;                    /* 슬롯 수 */
    uint32_t t0;                    /* 트레이스 시작 시각 (µs, 32비트 — wrap 시험용) */
    int nt, ni, nm, nq;
    task_def_t t[MAXT];
    isr_def_t isr[MAXI];
    mtx_def_t m[MAXM];
    q_def_t q[MAXQ];
} sys_t;

#define RUN(n)    { OP_RUN, 0, (n) }
#define LOCK(m)   { OP_LOCK, (m), 0 }
#define UNLOCK(m) { OP_UNLOCK, (m), 0 }
#define SEND(q)   { OP_SEND, (q), 0 }
#define RECV(q)   { OP_RECV, (q), 0 }

enum { SYS_INV = 1, SYS_ISR, SYS_QUEUE, SYS_RM, SYS_OK };

static sys_t sys_make(int id) {
    sys_t s;
    memset(&s, 0, sizeof s);
    s.horizon = 3000;
    switch (id) {
    case SYS_INV:   /* 모터 보드: CTRL 이 엔코더를 SPI 로 읽고, LOG 가 같은 SPI 로 플래시에 쓴다 */
        s.title = "S1 SPI shared by CTRL and LOG (binary-semaphore lock)";
        s.nm = 1; s.m[0] = (mtx_def_t){ "spi", 0 };
        s.nt = 3;
        s.t[0] = (task_def_t){ "CTRL", 3, 100, 20, 5, { RUN(10), LOCK(0), RUN(5), UNLOCK(0), RUN(5) } };
        s.t[1] = (task_def_t){ "COMM", 2, 500, 25, 1, { RUN(150) } };
        s.t[2] = (task_def_t){ "LOG", 1, 1000, 0, 4, { LOCK(0), RUN(60), UNLOCK(0), RUN(10) } };
        break;
    case SYS_ISR:   /* 호스트 UART 명령 버스트: 파싱+CRC 를 ISR 안에서 다 한다 */
        s.title = "S2 UART RX burst parsed inside the ISR";
        s.nt = 2;
        s.t[0] = (task_def_t){ "CTRL", 3, 100, 0, 1, { RUN(30) } };
        s.t[1] = (task_def_t){ "TELEM", 1, 1000, 50, 1, { RUN(100) } };
        s.ni = 1;
        s.isr[0] = (isr_def_t){ "UART_RX", 200, 1000, 40, 5, 4, -1 };
        break;
    case SYS_QUEUE: /* CTRL 이 매 주기 로그 레코드를 큐에, LOGGER 가 플래시로. COMM 이 OTA 조각 검증 버스트 */
        s.title = "S3 log queue fills while LOGGER is starved by COMM";
        s.nq = 1; s.q[0] = (q_def_t){ "logq", 2 };
        s.nt = 3;
        s.t[0] = (task_def_t){ "CTRL", 4, 100, 0, 3, { RUN(15), SEND(0), RUN(5) } };
        s.t[1] = (task_def_t){ "COMM", 2, 3000, 500, 1, { RUN(700) } };
        s.t[2] = (task_def_t){ "LOGGER", 1, 0, 0, 2, { RECV(0), RUN(8) } };
        break;
    case SYS_RM:    /* "배터리 감시가 안전에 중요하니 제일 높게" — 주기 4 ms 태스크가 0.5 ms IMU 위에 */
        s.title = "S4 BMS (4 ms) above IMU (0.5 ms): RM order violated";
        s.horizon = 2000;
        s.t0 = 0xFFFFF000u;          /* 4096 µs 뒤 32비트 wrap — 분석기가 견뎌야 한다 */
        s.nt = 3;
        s.t[0] = (task_def_t){ "IMU", 2, 50, 0, 1, { RUN(12) } };
        s.t[1] = (task_def_t){ "BMS", 3, 400, 10, 1, { RUN(100) } };
        s.t[2] = (task_def_t){ "TELEM", 1, 1000, 20, 1, { RUN(100) } };
        break;
    case SYS_OK:    /* 대조군: 같은 재료(뮤텍스·ISR·큐)를 올바르게 */
        s.title = "S5 healthy control (PI mutex, deferred ISR, RM order, deep queue)";
        s.nm = 1; s.m[0] = (mtx_def_t){ "spi", 1 };
        s.nq = 2; s.q[0] = (q_def_t){ "rxq", 32 }; s.q[1] = (q_def_t){ "logq", 8 };
        s.nt = 5;
        s.t[0] = (task_def_t){ "CTRL", 5, 100, 20, 6, { RUN(10), LOCK(0), RUN(4), UNLOCK(0), SEND(1), RUN(4) } };
        s.t[1] = (task_def_t){ "LOGGER", 4, 0, 0, 2, { RECV(1), RUN(6) } };
        s.t[2] = (task_def_t){ "PARSE", 3, 0, 0, 2, { RECV(0), RUN(3) } };
        s.t[3] = (task_def_t){ "COMM", 2, 500, 25, 1, { RUN(120) } };
        s.t[4] = (task_def_t){ "LOG", 1, 1000, 0, 4, { LOCK(0), RUN(40), UNLOCK(0), RUN(10) } };
        s.ni = 1;
        s.isr[0] = (isr_def_t){ "UART_RX", 300, 1000, 20, 10, 1, 0 };
        break;
    default: break;
    }
    return s;
}

/* ---- 트레이스 텍스트 버퍼 ---- */
static char g_tr[1 << 20];
static size_t g_trlen;
static int g_tr_overflow;

static void tr_puts(const char *s) {
    size_t n = strlen(s);
    if (g_trlen + n + 1 >= sizeof g_tr) { g_tr_overflow = 1; return; }
    memcpy(g_tr + g_trlen, s, n + 1);
    g_trlen += n;
}

typedef struct {
    const sys_t *s;
    uint32_t rng;
    int cur;                         /* -2 = 아직 없음, -1 = IDLE, ≥0 = 태스크 */
    int state[MAXT], pc[MAXT], rem[MAXT], eff[MAXT], active[MAXT];
    long deadline[MAXT];
    int owner[MAXM], qcnt[MAXQ];
    int isr_cur, isr_rem, isr_pend[MAXI];
    int misses, skipped, lost;
    int miss_by[MAXT];
} sim_t;
enum { TS_WAIT = 0, TS_READY, TS_BLOCKED };      /* TS_WAIT = 다음 주기 출시 대기 */
enum { W_NONE, W_MTX, W_QSEND, W_QRECV };
static int g_wait_kind[MAXT], g_wait_obj[MAXT];

typedef struct { int misses, skipped, lost; int miss_by[MAXT]; } simres_t;

static void emit(const sim_t *m, long t, const char *ev, const char *who, const char *arg) {
    char line[96];
    uint32_t us = m->s->t0 + (uint32_t)t * SLOT_US;
    if (arg) snprintf(line, sizeof line, "%u %s %s %s\n", us, ev, who, arg);
    else snprintf(line, sizeof line, "%u %s %s\n", us, ev, who);
    tr_puts(line);
}
static void emit_num(const sim_t *m, long t, const char *ev, const char *who, long when) {
    char a[16];
    snprintf(a, sizeof a, "%u", m->s->t0 + (uint32_t)when * SLOT_US);
    emit(m, t, ev, who, a);
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static int run_len(sim_t *m, int n) {
    if (m->rng == 0 || n < 10) return n;
    return n + (int)(xs32(&m->rng) % (uint32_t)(n / 10 + 1));
}
static const char *tname(const sim_t *m, int i) { return i < 0 ? "IDLE" : m->s->t[i].name; }

/* 같은 객체를 기다리는 태스크 중 실효 우선순위가 가장 높은 것 (동률 → 낮은 번호) */
static int best_waiter(const sim_t *m, int kind, int obj) {
    int best = -1;
    for (int i = 0; i < m->s->nt; i++)
        if (m->state[i] == TS_BLOCKED && g_wait_kind[i] == kind && g_wait_obj[i] == obj &&
            (best < 0 || m->eff[i] > m->eff[best])) best = i;
    return best;
}
static void wake(sim_t *m, long t, int i, const char *obj) {
    m->state[i] = TS_READY;
    g_wait_kind[i] = W_NONE;
    emit(m, t, "UNBLOCK", m->s->t[i].name, obj);
}
static void q_put(sim_t *m, long t, int q) {           /* 공간 확인은 호출자가 */
    m->qcnt[q]++;
    int r = best_waiter(m, W_QRECV, q);
    if (r >= 0) wake(m, t, r, m->s->q[q].name);
}

/* 현재 태스크의 0-시간 연산(LOCK/UNLOCK/SEND/RECV, 작업 끝)을 RUN 을 만나거나 블록할 때까지 */
static void exec_zero(sim_t *m, long t, int i) {
    const task_def_t *d = &m->s->t[i];
    for (int guard = 0; guard < 4 * MAXOP && m->state[i] == TS_READY; guard++) {
        if (m->pc[i] >= d->nops) {
            if (d->period > 0) {                      /* 주기 작업 끝: 다음 출시까지 대기 (vTaskDelayUntil) */
                m->active[i] = 0;
                m->state[i] = TS_WAIT;
                emit(m, t, "BLOCK", d->name, "delay");
                return;
            }
            m->pc[i] = 0;                             /* 이벤트 구동: 처음으로 */
            continue;
        }
        const op_t *o = &d->ops[m->pc[i]];
        switch (o->op) {
        case OP_RUN:
            if (m->rem[i] < 0) m->rem[i] = run_len(m, o->n);
            if (m->rem[i] > 0) return;                /* CPU 가 필요 */
            m->rem[i] = -1; m->pc[i]++;
            break;
        case OP_LOCK:
            if (m->owner[o->obj] < 0) {
                m->owner[o->obj] = i;
                emit(m, t, "MUTEX_TAKE", d->name, m->s->m[o->obj].name);
                m->pc[i]++;
            } else {
                int own = m->owner[o->obj];
                m->state[i] = TS_BLOCKED; g_wait_kind[i] = W_MTX; g_wait_obj[i] = o->obj;
                emit(m, t, "BLOCK", d->name, m->s->m[o->obj].name);
                if (m->s->m[o->obj].pi && m->eff[own] < m->eff[i]) m->eff[own] = m->eff[i];  /* 우선순위 상속 */
                return;
            }
            break;
        case OP_UNLOCK: {
            m->owner[o->obj] = -1;
            m->eff[i] = d->prio;                      /* 이 레슨의 태스크는 뮤텍스를 하나만 쥔다 */
            emit(m, t, "MUTEX_GIVE", d->name, m->s->m[o->obj].name);
            int w = best_waiter(m, W_MTX, o->obj);    /* FreeRTOS 처럼 넘겨주지 않고 깨우기만 → 깨어난 쪽이 다시 take */
            if (w >= 0) wake(m, t, w, m->s->m[o->obj].name);
            m->pc[i]++;
            break;
        }
        case OP_SEND:
            if (m->qcnt[o->obj] < m->s->q[o->obj].cap) {
                q_put(m, t, o->obj);
                m->pc[i]++;
            } else {
                m->state[i] = TS_BLOCKED; g_wait_kind[i] = W_QSEND; g_wait_obj[i] = o->obj;
                emit(m, t, "Q_SEND_BLOCK", d->name, m->s->q[o->obj].name);
                return;
            }
            break;
        case OP_RECV:
            if (m->qcnt[o->obj] > 0) {
                m->qcnt[o->obj]--;
                int w = best_waiter(m, W_QSEND, o->obj);
                if (w >= 0) wake(m, t, w, m->s->q[o->obj].name);
                m->pc[i]++;
            } else {
                m->state[i] = TS_BLOCKED; g_wait_kind[i] = W_QRECV; g_wait_obj[i] = o->obj;
                emit(m, t, "Q_RECV_BLOCK", d->name, m->s->q[o->obj].name);
                return;
            }
            break;
        default: return;
        }
    }
}

static int pick(const sim_t *m) {
    int best = -1;
    for (int i = 0; i < m->s->nt; i++)
        if (m->state[i] == TS_READY && (best < 0 || m->eff[i] > m->eff[best])) best = i;
    return best;
}

/* 시스템 s 를 seed 로 실행 → g_tr 에 트레이스, 결과 요약 반환 */
static simres_t sim_run(const sys_t *s, uint32_t seed) {
    static sim_t m;
    memset(&m, 0, sizeof m);
    m.s = s; m.rng = seed ? seed * 2654435761u : 0; if (seed && !m.rng) m.rng = 1;
    m.cur = -2; m.isr_cur = -1;
    g_trlen = 0; g_tr[0] = 0; g_tr_overflow = 0;
    tr_puts("# rtos-trace v1 unit=us\n");
    char line[96];
    snprintf(line, sizeof line, "# system %s\n", s->title); tr_puts(line);
    for (int i = 0; i < s->nt; i++) {
        snprintf(line, sizeof line, "# task %s prio=%d period=%u\n", s->t[i].name, s->t[i].prio,
                 (unsigned)s->t[i].period * SLOT_US);
        tr_puts(line);
    }
    for (int i = 0; i < s->ni; i++) { snprintf(line, sizeof line, "# isr %s\n", s->isr[i].name); tr_puts(line); }
    for (int i = 0; i < MAXM; i++) m.owner[i] = -1;
    for (int i = 0; i < s->nt; i++) {
        m.eff[i] = s->t[i].prio; m.rem[i] = -1; g_wait_kind[i] = W_NONE;
        if (s->t[i].period == 0) { m.state[i] = TS_READY; m.active[i] = 1; }   /* 이벤트 구동: 처음부터 준비 */
    }
    int isr_done = 0;
    for (long t = 0; t < s->horizon; t++) {
        /* 1. 직전 슬롯에서 끝난 ISR: 할 일(큐에 넣기) + ISR_EXIT */
        if (isr_done) {
            const isr_def_t *d = &s->isr[m.isr_cur];
            if (d->q >= 0) {
                if (m.qcnt[d->q] < s->q[d->q].cap) q_put(&m, t, d->q);
                else m.lost++;
            }
            emit(&m, t, "ISR_EXIT", d->name, NULL);
            m.isr_cur = -1; isr_done = 0;
        }
        /* 2. 현재 태스크가 RUN 을 막 끝냈다면 이어지는 0-시간 연산 */
        if (m.cur >= 0 && m.state[m.cur] == TS_READY && m.rem[m.cur] <= 0) exec_zero(&m, t, m.cur);
        /* 3. 데드라인 (D = T): 아직 안 끝난 작업 */
        for (int i = 0; i < s->nt; i++)
            if (s->t[i].period > 0 && m.active[i] && m.deadline[i] == t) {
                emit_num(&m, t, "DEADLINE_MISS", s->t[i].name, t);
                m.misses++; m.miss_by[i]++;
            }
        /* 4. 주기 출시 (작업이 아직 돌고 있으면 이번 출시는 건너뛴다 = 오버런) */
        for (int i = 0; i < s->nt; i++) {
            const task_def_t *d = &s->t[i];
            if (d->period <= 0 || t < d->offset || (t - d->offset) % d->period) continue;
            if (m.active[i]) { m.skipped++; continue; }
            m.active[i] = 1; m.pc[i] = 0; m.rem[i] = -1; m.state[i] = TS_READY;
            m.deadline[i] = t + d->period;
            emit_num(&m, t, "RELEASE", d->name, m.deadline[i]);
        }
        /* 5. 인터럽트 도착 → 펜딩, ISR 이 놀고 있으면 시작 */
        for (int k = 0; k < s->ni; k++) {
            const isr_def_t *d = &s->isr[k];
            if (t < d->first) continue;
            long ph = d->every ? (t - d->first) % d->every : (t - d->first);
            if (ph % d->gap == 0 && ph / d->gap < d->count) m.isr_pend[k]++;
        }
        if (m.isr_cur < 0)
            for (int k = 0; k < s->ni; k++)
                if (m.isr_pend[k]) {
                    m.isr_pend[k]--; m.isr_cur = k; m.isr_rem = s->isr[k].cost;
                    emit(&m, t, "ISR_ENTER", s->isr[k].name, NULL);
                    break;
                }
        if (m.isr_cur >= 0) {                         /* ISR 가 CPU 를 쓴다 — 태스크는 그대로 "현재" */
            if (--m.isr_rem == 0) isr_done = 1;
            continue;
        }
        /* 6. 스케줄: 가장 높은 준비 태스크, 0-시간 연산 처리, 안정될 때까지 */
        for (int guard = 0; guard < 64; guard++) {
            int p = pick(&m);
            if (p != m.cur) {
                if (m.cur != -2) emit(&m, t, "SWITCH_OUT", tname(&m, m.cur), NULL);
                emit(&m, t, "SWITCH_IN", tname(&m, p), NULL);
                m.cur = p;
            }
            if (p < 0) break;
            exec_zero(&m, t, p);
            if (m.state[p] == TS_READY && m.rem[p] > 0 && pick(&m) == p) break;
        }
        if (m.cur >= 0 && m.state[m.cur] == TS_READY && m.rem[m.cur] > 0) m.rem[m.cur]--;
    }
    simres_t r;
    memset(&r, 0, sizeof r);
    r.misses = m.misses; r.skipped = m.skipped; r.lost = m.lost;
    memcpy(r.miss_by, m.miss_by, sizeof r.miss_by);
    return r;
}

static int sys_find_task(const sys_t *s, const char *n) { for (int i = 0; i < s->nt; i++) if (!strcmp(s->t[i].name, n)) return i; return -1; }
static int sys_find_isr(const sys_t *s, const char *n)  { for (int i = 0; i < s->ni; i++) if (!strcmp(s->isr[i].name, n)) return i; return -1; }
static int sys_find_mtx(const sys_t *s, const char *n)  { for (int i = 0; i < s->nm; i++) if (!strcmp(s->m[i].name, n)) return i; return -1; }
static int sys_find_q(const sys_t *s, const char *n)    { for (int i = 0; i < s->nq; i++) if (!strcmp(s->q[i].name, n)) return i; return -1; }

/* =====================================================================================
 * ② 트레이스 파서 — RTOS-A06 포맷 축약 사본 (주어진 코드)
 *    "# rtos-trace v1 unit=us" 헤더, "<t_us> <EVENT> <name> [arg]", '#' 주석.
 *    이 레슨의 확장(주석이라 A06 파서는 무시): "# task NAME prio=P period=US", "# isr NAME".
 * ===================================================================================== */
enum { EV_RELEASE, EV_SWITCH_IN, EV_SWITCH_OUT, EV_BLOCK, EV_UNBLOCK, EV_ISR_ENTER, EV_ISR_EXIT, EV_MUTEX_TAKE,
       EV_MUTEX_GIVE, EV_Q_SEND_BLOCK, EV_Q_RECV_BLOCK, EV_DEADLINE_MISS, EV_DROP, EV_COUNT };
static const char *const k_evname[EV_COUNT] = { "RELEASE", "SWITCH_IN", "SWITCH_OUT", "BLOCK", "UNBLOCK", "ISR_ENTER",
    "ISR_EXIT", "MUTEX_TAKE", "MUTEX_GIVE", "Q_SEND_BLOCK", "Q_RECV_BLOCK", "DEADLINE_MISS", "DROP" };

#define MAXN   32
#define MAXEV  40000
#define NAMEL  16
typedef struct { char s[NAMEL]; int prio; uint64_t period; int is_isr, is_task; } nm_t;
typedef struct { uint64_t t; uint32_t t32; uint8_t ev; int8_t who, obj; uint32_t num; int has_num; } tev_t;
typedef struct {
    int nn, nev, bad_lines, has_header;
    nm_t nm[MAXN];
    tev_t ev[MAXEV];
} trace_t;

/* ---- TODO 1: 32비트 타임스탬프 wrap → 64비트 ---- */
static uint64_t unwrap32(uint64_t prev64, uint32_t t32) {
    /* TODO 1: 직전 64비트 시각 prev64 와 새 32비트 값 t32 로 새 64비트 시각을 만든다 (wrap 1회 허용) */
    (void)prev64;
    return t32;
}

static int name_id(trace_t *tr, const char *s, size_t len) {
    if (len == 0 || len >= NAMEL) return -1;
    for (int i = 0; i < tr->nn; i++) if (strlen(tr->nm[i].s) == len && !strncmp(tr->nm[i].s, s, len)) return i;
    if (tr->nn == MAXN) return -1;
    nm_t *n = &tr->nm[tr->nn];
    memset(n, 0, sizeof *n);
    memcpy(n->s, s, len); n->s[len] = 0; n->prio = -1;
    return tr->nn++;
}
static int is_name_ch(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }
static const char *tok(const char *p, const char **end) {
    while (*p == ' ' || *p == '\t') p++;
    const char *q = p;
    while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') q++;
    *end = q;
    return p;
}

static int trace_parse(const char *txt, trace_t *tr) {
    memset(tr, 0, sizeof *tr);
    const char *p = txt;
    uint64_t prev = 0;
    int first = 1;
    while (*p) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        if (*p == '#') {
            if (!strncmp(p, "# rtos-trace v1 unit=us", 23)) tr->has_header = 1;
            else if (!strncmp(p, "# task ", 7) || !strncmp(p, "# isr ", 6)) {
                int isr = p[2] == 'i';
                const char *e, *s = tok(p + (isr ? 6 : 7), &e);
                int id = name_id(tr, s, (size_t)(e - s));
                if (id >= 0) {
                    if (isr) tr->nm[id].is_isr = 1;
                    else {
                        tr->nm[id].is_task = 1;
                        const char *pp = strstr(s, "prio=");
                        if (pp && pp < eol) tr->nm[id].prio = atoi(pp + 5);
                        const char *pe = strstr(s, "period=");
                        if (pe && pe < eol) tr->nm[id].period = (uint64_t)strtoull(pe + 7, NULL, 10);
                    }
                }
            }
        } else if (eol > p) {
            const char *e1, *s1 = tok(p, &e1), *e2, *s2 = tok(e1, &e2), *e3, *s3 = tok(e2, &e3), *e4, *s4 = tok(e3, &e4);
            int ev = -1;
            for (int k = 0; k < EV_COUNT; k++)
                if ((size_t)(e2 - s2) == strlen(k_evname[k]) && !strncmp(s2, k_evname[k], (size_t)(e2 - s2))) ev = k;
            int ok = s1 < e1 && *s1 >= '0' && *s1 <= '9' && ev >= 0 && s3 < e3 && tr->nev < MAXEV;
            for (const char *c = s3; ok && c < e3; c++) ok = is_name_ch(*c);
            if (ok) {
                tev_t *v = &tr->ev[tr->nev];
                v->t32 = (uint32_t)strtoul(s1, NULL, 10);
                v->t = first ? v->t32 : unwrap32(prev, v->t32);
                if (!first && v->t < prev) v->t = prev;     /* 단조 보장 (unwrap 이 틀려도 크래시 없이) */
                prev = v->t; first = 0;
                v->ev = (uint8_t)ev;
                v->who = (int8_t)name_id(tr, s3, (size_t)(e3 - s3));
                v->obj = -1;
                if (s4 < e4 && s4 < eol) {
                    if (*s4 >= '0' && *s4 <= '9') { v->num = (uint32_t)strtoul(s4, NULL, 10); v->has_num = 1; }
                    else v->obj = (int8_t)name_id(tr, s4, (size_t)(e4 - s4));
                }
                if (v->who >= 0) {
                    if (ev == EV_ISR_ENTER || ev == EV_ISR_EXIT) tr->nm[v->who].is_isr = 1;
                    else if (ev != EV_DROP) tr->nm[v->who].is_task = 1;
                }
                tr->nev++;
            } else tr->bad_lines++;
        }
        p = *eol ? eol + 1 : eol;
    }
    return tr->has_header ? 0 : -1;
}

/* =====================================================================================
 * ③ 분석기: 타임라인 재구성 + 규칙
 * ===================================================================================== */
#define MAXSEG  40000
#define MAXBLK  8000
#define MAXHOLD 4000
#define MAXJOB  4000
#define MAXMISS 1000
#define MAXGAP  64
enum { BK_MUTEX, BK_QSEND, BK_QRECV, BK_DELAY };
typedef struct { int who; uint64_t a, b; } seg_t;              /* who 가 [a,b) 동안 CPU 를 썼다 (태스크 또는 ISR) */
typedef struct { int task, kind, obj; uint64_t a, b; } blk_t;  /* task 가 [a,b) 동안 obj 때문에 블록 */
typedef struct { int obj, owner; uint64_t a, b; } hold_t;      /* owner 가 [a,b) 동안 뮤텍스 obj 보유 */
typedef struct { int task; uint64_t r, d; } job_t;             /* 출시 r, 절대 데드라인 d */
typedef struct { int task; uint64_t r, d; int gap; } miss_t;
typedef struct {
    const trace_t *tr;
    int nseg, nblk, nhold, njob, nmiss, ngap, unmatched;
    int prio[MAXN];
    uint64_t period[MAXN];
    uint64_t t_begin, t_end;
    seg_t seg[MAXSEG];
    blk_t blk[MAXBLK];
    hold_t hold[MAXHOLD];
    job_t job[MAXJOB];
    miss_t miss[MAXMISS];
    uint64_t gap[MAXGAP];
} tl_t;

static void seg_add(tl_t *tl, int who, uint64_t a, uint64_t b) {
    if (b <= a) return;
    if (tl->nseg && tl->seg[tl->nseg - 1].who == who && tl->seg[tl->nseg - 1].b == a) { tl->seg[tl->nseg - 1].b = b; return; }
    if (tl->nseg < MAXSEG) tl->seg[tl->nseg++] = (seg_t){ who, a, b };
}
static uint64_t deadline64(uint64_t t64, uint32_t d32) {   /* 인자(32비트 µs)를 이벤트 시각 기준으로 펼친다 */
    return (uint64_t)((int64_t)t64 + (int32_t)(d32 - (uint32_t)t64));
}

/* ---- TODO 2: 이벤트 → 타임라인 ---- */
static int tl_build(const trace_t *tr, tl_t *tl) {
    memset(tl, 0, offsetof(tl_t, seg));
    tl->tr = tr;
    /* TODO 2: prio/period 복사, 이벤트를 훑으며 seg(ISR 우선, DROP 뒤는 미귀속)·blk·hold·job·miss·gap 채우기,
     *         끝에서 열린 구간 닫기, 주기 메타가 없으면 출시 간격으로 추정, 미스 창에 DROP 이 있으면 gap=1 */
    (void)seg_add; (void)deadline64;
    return 0;
}

/* ---- 주어진 도우미 ---- */
static uint64_t ov(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {   /* [a,b) ∩ [c,d) 길이 */
    uint64_t lo = a > c ? a : c, hi = b < d ? b : d;
    return hi > lo ? hi - lo : 0;
}
static uint64_t run_in(const tl_t *tl, int who, uint64_t a, uint64_t b) {
    uint64_t s = 0;
    for (int i = 0; i < tl->nseg; i++) if (tl->seg[i].who == who) s += ov(tl->seg[i].a, tl->seg[i].b, a, b);
    return s;
}
static int is_isr(const tl_t *tl, int i) { return tl->tr->nm[i].is_isr; }
static int is_task(const tl_t *tl, int i) { return tl->tr->nm[i].is_task && !tl->tr->nm[i].is_isr && strcmp(tl->tr->nm[i].s, "IDLE"); }
static int count_ev(const tl_t *tl, int ev, int who, uint64_t a, uint64_t b) {
    int n = 0;
    for (int k = 0; k < tl->tr->nev; k++) {
        const tev_t *e = &tl->tr->ev[k];
        if (e->ev == ev && e->who == who && e->t >= a && e->t < b) n++;
    }
    return n;
}
static int hold_owner_at(const tl_t *tl, int obj, uint64_t t) {
    for (int i = tl->nhold - 1; i >= 0; i--)
        if (tl->hold[i].obj == obj && tl->hold[i].a <= t && tl->hold[i].b >= t) return tl->hold[i].owner;
    return -1;
}
static const char *nmof(const tl_t *tl, int i) { return i >= 0 ? tl->tr->nm[i].s : "?"; }

enum { C_INVERSION = 1, C_ISR = 2, C_QUEUE = 4, C_RM = 8, C_UNKNOWN = 16 };
typedef struct {
    int cause, task, obj, owner, other;      /* owner: 뮤텍스 주인 / 큐 소비자, other: 대신 돈 태스크·ISR */
    uint64_t r, d, us, blocked_us, starve_us;
    int pct, count, gap;
} diag_t;

static void diag_init(diag_t *g, int cause, const miss_t *m) {
    memset(g, 0, sizeof *g);
    g->cause = cause; g->task = m->task; g->r = m->r; g->d = m->d; g->gap = m->gap;
    g->obj = g->owner = g->other = -1;
}

/* ---- TODO 3: 우선순위 역전 ---- */
static int rule_inversion(const tl_t *tl, const miss_t *m, diag_t *out) {
    /* TODO 3: X 의 뮤텍스 블록 구간 ∩ 창 → 그 시점 주인 L (prio < X) → prio 가 L 과 X 사이인 M 중 가장 많이 돈 것.
     *         M 이 창 길이의 10 % 이상 돌았으면 C_INVERSION (obj, owner=L, other=M, us, blocked_us, pct) */
    (void)tl; (void)m; (void)out;
    return 0;
}

/* ---- TODO 4: ISR 과다 ---- */
static int rule_isr(const tl_t *tl, const miss_t *m, diag_t *out) {
    /* TODO 4: 창 [r,d] 안의 ISR 실행 시간 합 ≥ 40 % → C_ISR (other = 가장 많이 돈 ISR, us, pct, count = 진입 횟수) */
    (void)tl; (void)m; (void)out; (void)count_ev;
    return 0;
}

/* ---- TODO 5: 큐 포화로 인한 블로킹 ---- */
static int rule_queue(const tl_t *tl, const miss_t *m, diag_t *out) {
    /* TODO 5: X 의 Q_SEND 블록 구간 → 소비자 C (그 큐에 Q_RECV_BLOCK 한 태스크) 가 그 구간(창까지)에서 20 % 미만 실행이면
     *         C_QUEUE (obj, owner=C, other=대신 가장 많이 돈 것, us, blocked_us, starve_us = 블록 끝 − C 가 마지막으로 돈 끝, pct) */
    (void)tl; (void)m; (void)out;
    return 0;
}

/* ---- TODO 6: RM 순서 위반 ---- */
static int rule_rm(const tl_t *tl, const miss_t *m, diag_t *out) {
    /* TODO 6: prio(Y) > prio(X) 이고 period(Y) > period(X) 인 태스크 Y 가 창 안에서 돌았으면 C_RM (other=Y, us, pct) */
    (void)tl; (void)m; (void)out;
    return 0;
}

/* ---- 주어진 코드: 미스마다 규칙 네 개를 모두 돌린다 (하나도 안 맞으면 UNKNOWN) ---- */
static int analyze(const tl_t *tl, diag_t *out, int max, unsigned *mask) {
    int n = 0;
    *mask = 0;
    for (int i = 0; i < tl->nmiss; i++) {
        const miss_t *m = &tl->miss[i];
        diag_t g; int hit = 0;
        if (rule_inversion(tl, m, &g)) { hit = 1; *mask |= C_INVERSION; if (n < max) out[n++] = g; }
        if (rule_queue(tl, m, &g))     { hit = 1; *mask |= C_QUEUE;     if (n < max) out[n++] = g; }
        if (rule_isr(tl, m, &g))       { hit = 1; *mask |= C_ISR;       if (n < max) out[n++] = g; }
        if (rule_rm(tl, m, &g))        { hit = 1; *mask |= C_RM;        if (n < max) out[n++] = g; }
        if (!hit) { diag_init(&g, C_UNKNOWN, m); *mask |= C_UNKNOWN; if (n < max) out[n++] = g; }
    }
    return n;
}

static const char *cause_name(int c) {
    switch (c) {
    case C_INVERSION: return "PRIORITY_INVERSION";
    case C_ISR: return "ISR_OVERLOAD";
    case C_QUEUE: return "QUEUE_FULL_BLOCKING";
    case C_RM: return "WRONG_PRIORITY_RM";
    default: return "UNKNOWN";
    }
}
static void diag_print(const tl_t *tl, const diag_t *g) {
    /* 시각은 트레이스에 적힌 32비트 값 그대로 — 원본 파일에서 grep 할 수 있게 */
    printf("  MISS %-6s window [%u, %u] us  -> %s%s\n", nmof(tl, g->task), (unsigned)(uint32_t)g->r,
           (unsigned)(uint32_t)g->d, cause_name(g->cause), g->gap ? "  (trace gap in window: low confidence)" : "");
    switch (g->cause) {
    case C_INVERSION:
        printf("       %s(p%d) blocked on %s for %llu us; owner %s(p%d); meanwhile %s(p%d) ran %llu us (%d%% of the wait in window)\n",
               nmof(tl, g->task), tl->prio[g->task], nmof(tl, g->obj), (unsigned long long)g->blocked_us, nmof(tl, g->owner),
               tl->prio[g->owner], nmof(tl, g->other), tl->prio[g->other], (unsigned long long)g->us, g->pct);
        break;
    case C_ISR:
        printf("       ISR time %llu us = %d%% of window; top ISR %s entered %d times\n", (unsigned long long)g->us, g->pct,
               nmof(tl, g->other), g->count);
        break;
    case C_QUEUE:
        printf("       %s blocked sending to %s for %llu us; consumer %s(p%d) got %d%% of CPU in window; %s(p%d) ran %llu us; consumer starved %llu us\n",
               nmof(tl, g->task), nmof(tl, g->obj), (unsigned long long)g->blocked_us, nmof(tl, g->owner),
               g->owner >= 0 ? tl->prio[g->owner] : -1, g->pct, nmof(tl, g->other), g->other >= 0 ? tl->prio[g->other] : -1,
               (unsigned long long)g->us, (unsigned long long)g->starve_us);
        break;
    case C_RM:
        printf("       %s(p%d, period %llu us) outranks %s(p%d, period %llu us) and ran %llu us (%d%% of window)\n",
               nmof(tl, g->other), tl->prio[g->other], (unsigned long long)tl->period[g->other], nmof(tl, g->task),
               tl->prio[g->task], (unsigned long long)tl->period[g->task], (unsigned long long)g->us, g->pct);
        break;
    default:
        printf("       no rule matched — look at the timeline by hand\n");
        break;
    }
}

/* 타임라인 (하우스 형식): 한 칸 = step µs. R = 그 칸의 절반 이상 실행, B = 칸 중앙에서 블록,
 * r = 출시됐는데 안 끝남(대기), . = 그 외. ISR 줄은 # = 절반 이상 ISR. */
static void tl_ascii(const tl_t *tl, uint64_t a, uint64_t b, uint64_t step) {
    uint64_t base = tl->t_begin;
    printf("  us/%-4llu:", (unsigned long long)step);
    for (uint64_t t = a; t < b; t += step) printf("%c", ((t - base) / step) % 10 == 0 ? '|' : ' ');
    printf("   (from t=%u us)\n", (unsigned)(uint32_t)a);
    for (int i = 0; i < tl->tr->nn; i++) {
        if (!is_task(tl, i) && !is_isr(tl, i)) continue;
        if (!strcmp(nmof(tl, i), "SUP")) continue;   /* FreeRTOS 데모의 감독(덤프) 태스크는 그리지 않는다 */
        printf("  %-8s:", nmof(tl, i));
        for (uint64_t t = a; t < b; t += step) {
            char c = '.';
            if (run_in(tl, i, t, t + step) * 2 >= step) c = is_isr(tl, i) ? '#' : 'R';
            else if (!is_isr(tl, i)) {
                uint64_t mid = t + step / 2;
                for (int k = 0; k < tl->nblk; k++)
                    if (tl->blk[k].task == i && tl->blk[k].kind != BK_DELAY && tl->blk[k].a <= mid && mid < tl->blk[k].b) c = 'B';
                if (c == '.')
                    for (int k = 0; k < tl->njob; k++)
                        if (tl->job[k].task == i && tl->job[k].r <= mid && mid < tl->job[k].d) {
                            int done = 0;
                            for (int q = 0; q < tl->nblk; q++)
                                if (tl->blk[q].task == i && tl->blk[q].kind == BK_DELAY && tl->blk[q].a >= tl->job[k].r && tl->blk[q].a <= mid) done = 1;
                            if (!done) c = 'r';
                        }
            }
            putchar(c);
        }
        putchar('\n');
    }
}

/* =====================================================================================
 * ④ 수정
 * ===================================================================================== */
/* ---- TODO 7a: RM 재할당 — 주기 태스크끼리 기존 우선순위 값들을 주기 오름차순으로 다시 나눠 준다 ---- */
static int rm_assign(sys_t *s) {
    /* TODO 7a: 주기 태스크들의 우선순위 값 집합을 주기 오름차순으로 큰 값부터 다시 배정. 바뀐 태스크 수 반환 */
    (void)s;
    return 0;
}
/* ---- TODO 7b: 버스트 동안 굶은 시간 starve 동안 period 마다 1개씩 쌓인다 → 그만큼 + 1 ---- */
static int queue_depth_for(uint64_t starve_us, uint64_t period_us) {
    /* TODO 7b: ceil(starve / period) + 1, period 0 이면 -1 */
    (void)starve_us; (void)period_us;
    return -1;
}

/* ---- TODO 8: 원인별 수정. variant 0 = 기본 수정, 1 = 대안 (큐: 소비자 우선순위 / ISR: 핸들러를 X 위에 = 틀린 수정) ---- */
static int apply_fix(sys_t *s, const tl_t *tl, const diag_t *g, int variant) {
    /* TODO 8: 원인별 수정 — INVERSION: 그 뮤텍스 pi=1 / ISR: 비용 1 로 줄이고 rxq(64) + PARSE 태스크(RECV, RUN 나머지),
     *         우선순위 = prio(X)−1 (variant 1: +1) / QUEUE: variant 0 깊이 = queue_depth_for(starve, period(X)),
     *         variant 1 소비자 prio = prio(other)+1 / RM: rm_assign. 성공 0, 실패 −1 */
    (void)s; (void)tl; (void)g; (void)variant;
    (void)sys_find_task; (void)sys_find_isr; (void)sys_find_mtx; (void)sys_find_q;
    return -1;
}

/* =====================================================================================
 * 테스트 하네스
 * ===================================================================================== */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static trace_t g_trace;
static tl_t g_tl;
static diag_t g_diag[256];

/* 시스템 → 시뮬레이션 → 파싱 → 타임라인 → 분석. 반환: 진단 수 */
static int run_and_analyze(const sys_t *s, uint32_t seed, simres_t *res, unsigned *mask) {
    *res = sim_run(s, seed);
    trace_parse(g_tr, &g_trace);
    tl_build(&g_trace, &g_tl);
    return analyze(&g_tl, g_diag, 256, mask);
}
static int find_name(const char *n) {
    for (int i = 0; i < g_trace.nn; i++) if (!strcmp(g_trace.nm[i].s, n)) return i;
    return -1;
}
static const diag_t *first_diag(int n, int cause) {
    for (int i = 0; i < n; i++) if (g_diag[i].cause == cause) return &g_diag[i];
    return NULL;
}
/* 시각 t 이상인 첫 이벤트 줄의 시작 (주석 줄은 건너뜀) */
static char *line_at(char *txt, uint32_t t) {
    for (char *p = txt; *p; ) {
        if (*p != '#' && (uint32_t)strtoul(p, NULL, 10) >= t) return p;
        char *e = strchr(p, '\n');
        if (!e) break;
        p = e + 1;
    }
    return NULL;
}
static void report(const char *title, int n) {
    printf("== %s: %d miss(es), %d diagnosis line(s)\n", title, g_tl.nmiss, n);
    for (int i = 0; i < n && i < 3; i++) diag_print(&g_tl, &g_diag[i]);
    if (n > 3) printf("  ... (%d more)\n", n - 3);
}

static int analyze_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("%s: cannot open\n", path); return 1; }
    size_t n = fread(g_tr, 1, sizeof g_tr - 1, f);
    fclose(f);
    g_tr[n] = 0;
    if (trace_parse(g_tr, &g_trace) != 0) { printf("%s: no '# rtos-trace v1 unit=us' header\n", path); return 1; }
    tl_build(&g_trace, &g_tl);
    unsigned mask;
    int nd = analyze(&g_tl, g_diag, 256, &mask);
    printf("%s: %d events, %d bad lines, %d misses (%d unmatched), causes:", path, g_trace.nev, g_trace.bad_lines,
           g_tl.nmiss, g_tl.unmatched);
    if (!mask) printf(" none");
    for (int c = 1; c <= C_UNKNOWN; c <<= 1) if (mask & (unsigned)c) printf(" %s", cause_name(c));
    printf("\n");
    for (int i = 0; i < nd && i < 4; i++) diag_print(&g_tl, &g_diag[i]);
    if (nd > 0) {
        uint64_t a = g_diag[0].r, b = g_diag[0].d;
        uint64_t step = (b - a) / 40 ? (b - a) / 40 : 1;
        tl_ascii(&g_tl, a, b + 10 * step, step);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--emit")) {
        sys_t s = sys_make(atoi(argv[2]));
        uint32_t seed = argc >= 4 ? (uint32_t)atoi(argv[3]) : 0;
        if (argc >= 5) {                               /* fixed: 버그 버전 진단 → 수정 후 다시 */
            simres_t r; unsigned mask;
            int n = run_and_analyze(&s, seed, &r, &mask);
            if (n > 0) apply_fix(&s, &g_tl, &g_diag[0], 0);
        }
        sim_run(&s, seed);
        fputs(g_tr, stdout);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "-v")) {
        int rc = 0;
        for (int i = 1; i < argc; i++) rc |= analyze_file(argv[i]);
        return rc;
    }
    g_verbose = argc >= 2;
    simres_t r;
    unsigned mask;
    int n;

    /* ---------- Part A: 시뮬레이터가 만든 5개 트레이스와 진단 ---------- */
    static const unsigned expect[6] = { 0, C_INVERSION, C_ISR, C_QUEUE, C_RM, 0 };
    for (int id = SYS_INV; id <= SYS_OK; id++) {
        sys_t s = sys_make(id);
        n = run_and_analyze(&s, 0, &r, &mask);
        if (g_verbose) report(s.title, n);
        CHECK(!g_tr_overflow && g_trace.has_header && g_trace.bad_lines == 0);   /* 포맷: 시뮬레이터 ↔ 파서 */
        CHECK(id == SYS_OK ? r.misses == 0 : r.misses > 0);                      /* 버그는 실제로 미스를 낸다 */
        CHECK(g_tl.nmiss == r.misses && g_tl.unmatched == 0);                    /* 트레이스만으로 미스를 모두 찾는다 */
        CHECK(mask == expect[id]);                                               /* 원인 정확히 하나 (대조군은 0) */
    }
    /* A1 재구성 숫자: S1 seed 0 — LOG 가 0 us 에 spi 를 잡고, CTRL 은 300 us 에 막히고, COMM 이 300~1800 us 를 먹는다 */
    {
        sys_t s = sys_make(SYS_INV);
        n = run_and_analyze(&s, 0, &r, &mask);
        int ctrl = find_name("CTRL"), comm = find_name("COMM"), log = find_name("LOG"), spi = find_name("spi");
        CHECK(ctrl >= 0 && comm >= 0 && log >= 0 && spi >= 0);
        CHECK(r.misses == 3 && g_tl.nmiss == 3);
        CHECK(g_tl.miss[0].task == ctrl && g_tl.miss[0].r == 200 && g_tl.miss[0].d == 1200);
        CHECK(run_in(&g_tl, comm, 300, 1800) == 1500 && run_in(&g_tl, ctrl, 200, 1200) == 100);
        CHECK(hold_owner_at(&g_tl, spi, 300) == log);
        const diag_t *g = first_diag(n, C_INVERSION);
        CHECK(g && g->task == ctrl && g->owner == log && g->other == comm && g->obj == spi);
        CHECK(g && g->blocked_us == 1900 && g->us == 900 && g->pct == 100);      /* 막힘 300→2200 us, 창 안 COMM 900 us */
        if (g_verbose) { printf("-- S1 timeline (seed 0)\n"); tl_ascii(&g_tl, 0, 2400, 50); }
    }
    /* A2 ISR: S2 — 200~1200 us 창에서 ISR 이 4/5 를 먹는다 */
    {
        sys_t s = sys_make(SYS_ISR);
        n = run_and_analyze(&s, 0, &r, &mask);
        const diag_t *g = first_diag(n, C_ISR);
        CHECK(g && !strcmp(nmof(&g_tl, g->task), "CTRL") && !strcmp(nmof(&g_tl, g->other), "UART_RX"));
        CHECK(g && g->r == 2000 && g->d == 3000 && g->us == 800 && g->pct == 80 && g->count == 20);
    }
    /* A3 큐: S3 — CTRL 이 7150 us 에 logq 로 막히고, LOGGER 는 0 %, COMM 이 대신 돈다 */
    {
        sys_t s = sys_make(SYS_QUEUE);
        n = run_and_analyze(&s, 0, &r, &mask);
        const diag_t *g = first_diag(n, C_QUEUE);
        CHECK(g && !strcmp(nmof(&g_tl, g->owner), "LOGGER") && !strcmp(nmof(&g_tl, g->other), "COMM") && !strcmp(nmof(&g_tl, g->obj), "logq"));
        CHECK(g && g->pct == 0 && g->blocked_us == 5400 && g->starve_us == 8270);
        if (g_verbose) { printf("-- S3 timeline (seed 0)\n"); tl_ascii(&g_tl, 4000, 14000, 250); }
    }
    /* A4 RM: S4 — BMS(4 ms) 가 IMU(0.5 ms) 창 전체를 먹는다 */
    {
        sys_t s = sys_make(SYS_RM);
        n = run_and_analyze(&s, 0, &r, &mask);
        const diag_t *g = first_diag(n, C_RM);
        CHECK(g && !strcmp(nmof(&g_tl, g->task), "IMU") && !strcmp(nmof(&g_tl, g->other), "BMS"));
        CHECK(g && g_tl.period[g->task] == 500 && g_tl.period[g->other] == 4000 && g->pct >= 80);
    }

    /* ---------- Part B: 함정 — wrap, seed, 오탐, 유실 ---------- */
    CHECK(unwrap32(0xFFFFFF00ull, 0x00000010u) == 0x100000010ull);
    CHECK(unwrap32(0x1FFFFFFF0ull, 0x00000005u) == 0x200000005ull);
    CHECK(unwrap32(1000, 1500) == 1500 && unwrap32(1000, 1000) == 1000);
    {   /* 같은 시스템을 t0 = 0 과 wrap 직전에서 → 진단 숫자가 같아야 한다 */
        int same = 1;
        for (int id = SYS_INV; id <= SYS_RM; id++) {
            sys_t s = sys_make(id);
            diag_t a[4], b[4];
            s.t0 = 0; n = run_and_analyze(&s, 0, &r, &mask);
            int na = n < 4 ? n : 4; memcpy(a, g_diag, sizeof a); unsigned ma = mask; uint64_t ba = g_tl.t_begin;
            s.t0 = 0xFFFFFF00u; n = run_and_analyze(&s, 0, &r, &mask);
            memcpy(b, g_diag, sizeof b); uint64_t bb = g_tl.t_begin;
            if (mask != ma || n < na) same = 0;
            for (int i = 0; i < na && same; i++)
                if (a[i].cause != b[i].cause || a[i].us != b[i].us || a[i].r - ba != b[i].r - bb || a[i].blocked_us != b[i].blocked_us) same = 0;
        }
        CHECK(same);
    }
    {   /* seed 1..8: 실행 시간이 흔들려도 원인은 같다, 대조군은 미스 0 */
        int ok = 1, healthy = 1;
        for (uint32_t seed = 1; seed <= 8; seed++) {
            for (int id = SYS_INV; id <= SYS_OK; id++) {
                sys_t s = sys_make(id);
                run_and_analyze(&s, seed, &r, &mask);
                if (mask != expect[id]) ok = 0;
                if (id == SYS_OK && r.misses) healthy = 0;
            }
        }
        CHECK(ok);
        CHECK(healthy);
    }
    {   /* 대조군의 "모든" 작업 창에 규칙을 강제로 돌려도 아무것도 안 걸린다 —
         * CTRL 은 실제로 spi 에서 막히고(유한 역전), ISR 도 돈다: 겉모습만 보면 걸릴 만한 재료가 있다 */
        sys_t s = sys_make(SYS_OK);
        run_and_analyze(&s, 3, &r, &mask);
        int ctrl = find_name("CTRL"), fired = 0, ctrl_blocked = 0;
        uint64_t isr_us = 0;
        for (int i = 0; i < g_tl.nblk; i++) if (g_tl.blk[i].task == ctrl && g_tl.blk[i].kind == BK_MUTEX && g_tl.blk[i].b > g_tl.blk[i].a) ctrl_blocked++;
        isr_us = run_in(&g_tl, find_name("UART_RX"), 0, ~0ull);
        for (int j = 0; j < g_tl.njob; j++) {
            miss_t fake = { g_tl.job[j].task, g_tl.job[j].r, g_tl.job[j].d, 0 };
            diag_t g;
            fired += rule_inversion(&g_tl, &fake, &g) + rule_isr(&g_tl, &fake, &g) + rule_queue(&g_tl, &fake, &g) + rule_rm(&g_tl, &fake, &g);
        }
        CHECK(g_tl.njob >= 35 && ctrl_blocked >= 3 && isr_us > 0);
        CHECK(fired == 0);
    }
    {   /* 유실: S1 미스 창 안에 DROP 줄을 끼워 넣으면 → 진단은 하되 "신뢰도 낮음" 표시 */
        sys_t s = sys_make(SYS_INV);
        sim_run(&s, 0);
        static char buf[1 << 20];
        char *p = line_at(g_tr, 500);                   /* 500 us 이후 첫 줄 앞에 끼운다 (첫 미스 창 200..1200) */
        CHECK(p != NULL);
        if (p) {
            size_t pre = (size_t)(p - g_tr);
            memcpy(buf, g_tr, pre);
            strcpy(buf + pre, "500 DROP TRACE 7\n");
            strcpy(buf + pre + strlen("500 DROP TRACE 7\n"), p);
            trace_parse(buf, &g_trace);
            tl_build(&g_trace, &g_tl);
            n = analyze(&g_tl, g_diag, 256, &mask);
            CHECK(g_tl.ngap == 1 && n >= 1 && g_diag[0].gap == 1 && g_diag[1].gap == 0);
        }
    }
    {   /* 트레이스가 중간부터 시작 (첫 출시를 놓침): 짝 없는 미스는 세고 버린다, 나머지는 진단 */
        sys_t s = sys_make(SYS_INV);
        sim_run(&s, 0);
        char *p = line_at(g_tr, 1100);                  /* 1100 us 부터만 남긴다: 첫 작업의 RELEASE(200 us)가 잘린다 */
        CHECK(p != NULL);
        if (p) {
            static char buf2[1 << 20];
            snprintf(buf2, sizeof buf2, "# rtos-trace v1 unit=us\n# task CTRL prio=3 period=1000\n# task COMM prio=2 period=5000\n# task LOG prio=1 period=10000\n%s", p);
            trace_parse(buf2, &g_trace);
            tl_build(&g_trace, &g_tl);
            analyze(&g_tl, g_diag, 256, &mask);
            CHECK(g_tl.unmatched == 1 && g_tl.nmiss == 2 && mask == C_INVERSION);
        }
    }

    /* ---------- Part C: 수정하고 다시 돌린다 ---------- */
    {
        int all_zero = 1, all_clean = 1;
        for (uint32_t seed = 0; seed <= 8; seed++)
            for (int id = SYS_INV; id <= SYS_RM; id++) {
                sys_t s = sys_make(id);
                n = run_and_analyze(&s, seed, &r, &mask);
                if (n == 0 || apply_fix(&s, &g_tl, &g_diag[0], 0) != 0) { all_zero = 0; continue; }
                simres_t r2;
                run_and_analyze(&s, seed, &r2, &mask);
                if (r2.misses || r2.lost) all_zero = 0;
                if (mask) all_clean = 0;
                if (g_verbose && seed == 0) printf("   fix %-50s misses %d -> %d\n", sys_make(id).title, r.misses, r2.misses);
            }
        CHECK(all_zero);
        CHECK(all_clean);
    }
    {   /* 수정 내용 자체 */
        sys_t s = sys_make(SYS_RM);
        CHECK(rm_assign(&s) == 2 && s.t[0].prio == 3 && s.t[1].prio == 2 && s.t[2].prio == 1);
        CHECK(rm_assign(&s) == 0);                                              /* 이미 RM 이면 그대로 */
        CHECK(queue_depth_for(8270, 1000) == 10 && queue_depth_for(1000, 1000) == 2 && queue_depth_for(1, 1000) == 2);
        s = sys_make(SYS_QUEUE);
        n = run_and_analyze(&s, 0, &r, &mask);
        CHECK(apply_fix(&s, &g_tl, first_diag(n, C_QUEUE), 0) == 0 && s.q[0].cap == 10);
        s = sys_make(SYS_INV);
        n = run_and_analyze(&s, 0, &r, &mask);
        CHECK(apply_fix(&s, &g_tl, first_diag(n, C_INVERSION), 0) == 0 && s.m[0].pi == 1);
        s = sys_make(SYS_ISR);
        n = run_and_analyze(&s, 0, &r, &mask);
        CHECK(apply_fix(&s, &g_tl, first_diag(n, C_ISR), 0) == 0 && s.isr[0].cost == 1 && s.nt == 3 &&
              s.t[2].prio == 2 && s.t[2].ops[1].n == 3 && s.isr[0].q == 0);
    }
    {   /* 대안 수정: 큐 → 소비자 우선순위 / ISR → 핸들러를 CTRL 위에 두면 여전히 미스 (우선순위는 비율이 아니라 마감으로) */
        int ok = 1;
        for (uint32_t seed = 0; seed <= 8; seed++) {
            sys_t s = sys_make(SYS_QUEUE);
            n = run_and_analyze(&s, seed, &r, &mask);
            if (apply_fix(&s, &g_tl, first_diag(n, C_QUEUE), 1) != 0 || s.t[2].prio != 3) ok = 0;
            run_and_analyze(&s, seed, &r, &mask);
            if (r.misses) ok = 0;
        }
        CHECK(ok);
        sys_t s = sys_make(SYS_ISR);
        n = run_and_analyze(&s, 0, &r, &mask);
        CHECK(apply_fix(&s, &g_tl, first_diag(n, C_ISR), 1) == 0 && s.t[2].prio == 4);
        n = run_and_analyze(&s, 0, &r, &mask);
        CHECK(r.misses > 0 && mask == C_UNKNOWN);       /* ISR 비율은 20 % 로 내려갔지만 PARSE(p4)가 CTRL 을 굶긴다 */
        if (g_verbose) report("S2 with handler ABOVE CTRL (wrong fix)", n);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
