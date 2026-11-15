/* BUILD: -pthread */
/* OS-M09 reader-writer lock 구현과 writer starvation
 *
 * 호스트 모델: pthread 하나 = RTOS 태스크 하나. 보호 대상 = 여러 태스크가 읽고 가끔 고치는 설정 테이블.
 *   rwl_t = mutex 1개 + 조건 변수 2개(readers_ok / writer_ok) + 카운터들 (OSTEP 30장 · 31.5).
 *   Part A  reader-preferring(RW_READER_PREF) 와 writer-preferring(RW_WRITER_PREF) — TODO A1..A4
 *           강제 스케줄: reader 가 끊임없이 겹치면 reader-pref 에서는 writer 가 K 번의 교대 동안 못 들어가고,
 *           writer-pref 에서는 바로 다음 차례에 들어간다.
 *   Part B  phase-fair(RW_PHASE_FAIR): writer 가 놓을 때 기다리던 reader 를 "한 묶음" 으로 들여보낸다 — TODO B1..B4
 *           writer-pref 의 반대 문제(writer 가 줄줄이 오면 reader 굶주림)를 막는다.
 *   Part C  펌웨어 설정 테이블 래퍼: 중첩 읽기 허용 + R→W 승격 거부(EDEADLK) — TODO C1..C3
 *
 * 체크는 강제 스케줄로 결정적이다: actor 스레드에게 명령(rdlock/wrlock/...)을 하나씩 주고,
 *   rwl 내부 카운터(waiting_readers / waiting_writers)와 actor 상태가 원하는 값이 될 때까지 기다린다.
 *   시간으로 판정하지 않는다. 처리량 · writer 대기 시간은 demo_bench.c.
 * 워치독 스레드: 한 단계가 WD_MS 를 넘으면 FAIL 을 찍고 _exit(1) — 틀린 구현도 영원히 멈추지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static atomic_int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define WAIT_MS 1000   /* 하네스가 "원하는 상태" 를 기다리는 상한. 넘으면 그 CHECK 가 FAIL */
#define WD_MS   8000   /* 한 테스트 단계 전체의 상한. 넘으면 워치독이 _exit(1) */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}
static void sleep_us(unsigned us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ===== 워치독: 테스트가 교착에 빠져도 FAIL 로 끝낸다 ===== */
static _Atomic double g_wd_deadline;            /* 0 = 꺼짐 */
static _Atomic(const char *) g_wd_what;
static void wd_arm(const char *what) { atomic_store(&g_wd_what, what); atomic_store(&g_wd_deadline, now_ms() + WD_MS); }
static void wd_disarm(void) { atomic_store(&g_wd_deadline, 0.0); }
static void *wd_main(void *arg) {
    (void)arg;
    for (;;) {
        sleep_us(10000);
        double d = atomic_load(&g_wd_deadline);
        if (d > 0 && now_ms() > d) {
            printf("FAIL watchdog: '%s' did not finish in %d ms (deadlock or lost wakeup?)\n",
                   atomic_load(&g_wd_what), WD_MS);
            printf("%d/%d passed\n", atomic_load(&g_pass), atomic_load(&g_pass) + atomic_load(&g_fail) + 1);
            fflush(stdout);
            _exit(1);
        }
    }
    return NULL;
}

/* ===== reader-writer lock ===== */
enum { RW_READER_PREF, RW_WRITER_PREF, RW_PHASE_FAIR, RW_NPOLICY };
static const char *const policy_name[RW_NPOLICY] = { "reader-pref", "writer-pref", "phase-fair" };

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t readers_ok;   /* reader 가 잠드는 곳 */
    pthread_cond_t writer_ok;    /* writer 가 잠드는 곳 */
    int policy;
    int active_readers;          /* 임계 구역 안의 reader 수 */
    bool writer_active;          /* 임계 구역 안에 writer 가 있나 */
    int waiting_readers;         /* rdlock 안에서 아직 못 들어간 reader 수 — 모든 정책이 유지 (하네스가 본다) */
    int waiting_writers;         /* wrlock 안에서 아직 못 들어간 writer 수 — 모든 정책이 유지 */
    unsigned long read_gen;      /* phase-fair: writer 가 reader 묶음을 들여보낼 때마다 +1 */
} rwl_t;

static void rwl_init(rwl_t *rw, int policy) {
    pthread_mutex_init(&rw->m, NULL);
    pthread_cond_init(&rw->readers_ok, NULL);
    pthread_cond_init(&rw->writer_ok, NULL);
    rw->policy = policy;
    rw->active_readers = 0; rw->writer_active = false;
    rw->waiting_readers = 0; rw->waiting_writers = 0;
    rw->read_gen = 0;
}
static void rwl_destroy(rwl_t *rw) {
    pthread_mutex_destroy(&rw->m);
    pthread_cond_destroy(&rw->readers_ok);
    pthread_cond_destroy(&rw->writer_ok);
}
typedef struct { int active_readers, waiting_readers, waiting_writers; bool writer_active; } rwl_snap_t;
static rwl_snap_t rwl_snapshot(rwl_t *rw) {     /* 하네스 전용: 내부 mutex 를 쥐고 카운터를 복사 */
    pthread_mutex_lock(&rw->m);
    rwl_snap_t s = { rw->active_readers, rw->waiting_readers, rw->waiting_writers, rw->writer_active };
    pthread_mutex_unlock(&rw->m);
    return s;
}

/* ---- Part A: reader-pref / writer-pref (같은 함수, policy 로 갈림) ---- */
/* A1: reader 입장. reader-pref: writer 가 "안에 있을 때만" 기다린다.
 *     writer-pref: writer 가 안에 있거나 "기다리고 있으면" 기다린다 (새 reader 가 줄을 서게 됨).
 *     기다리는 동안 waiting_readers 를 올려 두고, 들어갈 때 내리고 active_readers 를 올린다. */
static void rw_basic_rdlock(rwl_t *rw) {
    (void)rw; /* TODO A1 */
}
/* A2: reader 퇴장. 마지막 reader 가 나가면서 기다리는 writer 하나를 깨운다. */
static void rw_basic_rdunlock(rwl_t *rw) {
    (void)rw; /* TODO A2 */
}
/* A3: writer 입장. 아무도(reader 도 writer 도) 안에 없을 때까지 기다린다 (두 정책 공통). */
static void rw_basic_wrlock(rwl_t *rw) {
    (void)rw; /* TODO A3 */
}
/* A4: writer 퇴장. 누구를 먼저 깨울지가 정책이다.
 *     reader-pref: 기다리는 reader 가 있으면 전부(broadcast), 없으면 writer 하나.
 *     writer-pref: 기다리는 writer 가 있으면 writer 하나, 없으면 reader 전부. */
static void rw_basic_wrunlock(rwl_t *rw) {
    (void)rw; /* TODO A4 */
}

/* ---- Part B: phase-fair (reader 묶음과 writer 가 번갈아) ---- */
/* B1: writer 가 안에 있거나 기다리면 줄을 선다 (writer-pref 와 같음). 단, 깨어날 조건은 "내 묶음이 허락됨"
 *     = read_gen 이 바뀜. 허락한 writer 가 이미 active_readers 에 나를 넣어 주었다 (직접 전달, barging 없음). */
static void pf_rdlock(rwl_t *rw) {
    (void)rw; /* TODO B1 */
}
/* B2: reader 퇴장 — 마지막 reader 가 writer 하나를 깨운다. */
static void pf_rdunlock(rwl_t *rw) {
    (void)rw; /* TODO B2 */
}
/* B3: writer 입장 — A3 과 같다. */
static void pf_wrlock(rwl_t *rw) {
    (void)rw; /* TODO B3 */
}
/* B4: writer 퇴장 — 기다리는 reader 가 있으면 그 전원을 한 묶음으로 입장시킨다
 *     (active_readers += waiting_readers, waiting_readers = 0, read_gen++, broadcast).
 *     없을 때만 다음 writer 를 깨운다. → writer 사이에 reader 묶음이 반드시 한 번 낀다. */
static void pf_wrunlock(rwl_t *rw) {
    (void)rw; /* TODO B4 */
}

static void rwl_rdlock(rwl_t *rw)   { if (rw->policy == RW_PHASE_FAIR) pf_rdlock(rw);   else rw_basic_rdlock(rw); }
static void rwl_rdunlock(rwl_t *rw) { if (rw->policy == RW_PHASE_FAIR) pf_rdunlock(rw); else rw_basic_rdunlock(rw); }
static void rwl_wrlock(rwl_t *rw)   { if (rw->policy == RW_PHASE_FAIR) pf_wrlock(rw);   else rw_basic_wrlock(rw); }
static void rwl_wrunlock(rwl_t *rw) { if (rw->policy == RW_PHASE_FAIR) pf_wrunlock(rw); else rw_basic_wrunlock(rw); }

/* ===== Part C: 펌웨어 설정 테이블 =====
 * 여러 태스크(제어 루프, 텔레메트리, 통신)가 읽고, CLI/OTA 태스크가 가끔 통째로 고친다.
 * 래퍼 규칙: (1) 같은 태스크의 중첩 읽기는 rwlock 을 다시 잡지 않는다 — writer-pref/phase-fair 에서
 *            "R 쥔 채 R 다시" 는 writer 가 기다리는 순간 자기 교착이다.
 *            (2) R 을 쥔 채 W 를 잡으면(승격) 기다리지 않고 EDEADLK. (3) W 안에서 읽기는 허용(쓰기 ⊃ 읽기). */
#define CFG_N 16
typedef struct {
    uint32_t v[CFG_N];
    uint32_t sum;              /* v 의 검사합 — 찢어진 스냅샷이면 안 맞는다 */
    uint64_t version;
} cfg_table_t;
typedef struct { rwl_t lock; cfg_table_t tab; } cfg_store_t;

static uint32_t cfg_sum(const cfg_table_t *t) {
    uint32_t s = 0x5A5A5A5Au;
    for (int i = 0; i < CFG_N; i++) s = (s ^ t->v[i]) * 16777619u;
    return s;
}
static void cfg_fill(cfg_table_t *t, uint64_t version) {
    for (int i = 0; i < CFG_N; i++) t->v[i] = (uint32_t)(version * 2654435761u) ^ (uint32_t)(i * 40503u);
    t->sum = cfg_sum(t);
    t->version = version;
}

/* 스레드(태스크)별 보유 기록 — 하네스 제공. lock 하나당 칸 하나, 최대 4개. */
typedef struct { const rwl_t *lock; int rdepth; bool wr; } tl_hold_t;
static _Thread_local tl_hold_t tl_holds[4];
static tl_hold_t *tl_slot(const rwl_t *rw, bool create) {
    for (int i = 0; i < 4; i++) if (tl_holds[i].lock == rw) return &tl_holds[i];
    if (!create) return NULL;
    for (int i = 0; i < 4; i++) if (tl_holds[i].lock == NULL) { tl_holds[i].lock = rw; return &tl_holds[i]; }
    return NULL;
}
static void tl_maybe_free(tl_hold_t *h) { if (h->rdepth == 0 && !h->wr) h->lock = NULL; }

/* C1: 읽기 시작. 이미 이 lock 을 (읽기든 쓰기든) 쥐고 있으면 rdepth 만 올린다. 아니면 rwl_rdlock.
 *     칸이 없으면 ENOMEM. 성공 0. */
static int cfg_read_lock(cfg_store_t *s) {
    (void)s; (void)tl_slot; return 0; /* TODO C1 */
}
/* C1: 읽기 끝. 안 쥐었으면 EPERM. 가장 바깥 읽기이고 쓰기 중이 아니면 rwl_rdunlock. */
static int cfg_read_unlock(cfg_store_t *s) {
    (void)s; (void)tl_maybe_free; return 0; /* TODO C1 */
}
/* C2: 쓰기 시작. 이 태스크가 이미 읽기나 쓰기를 쥐었으면 기다리지 않고 EDEADLK (승격 · 재귀 금지).
 *     아니면 rwl_wrlock 후 wr = true. */
static int cfg_write_lock(cfg_store_t *s) {
    (void)s; return 0; /* TODO C2 */
}
/* C3: 쓰기 끝. 쓰기를 안 쥐었거나, 안에서 연 읽기가 아직 열려 있으면 EPERM. */
static int cfg_write_unlock(cfg_store_t *s) {
    (void)s; return 0; /* TODO C3 */
}

/* ===== 임계 구역 감시: 안에서 불변식을 잰다 (readers > 0 ⇒ writer 없음, writer ≤ 1) ===== */
static atomic_int g_in_r, g_in_w, g_viol;
static pthread_mutex_t g_log_m = PTHREAD_MUTEX_INITIALIZER;
static char g_log[512];
static void log_reset(void) { pthread_mutex_lock(&g_log_m); g_log[0] = 0; pthread_mutex_unlock(&g_log_m); }
static void log_add(const char *name) {
    pthread_mutex_lock(&g_log_m);
    size_t n = strlen(g_log);
    snprintf(g_log + n, sizeof g_log - n, "%s%s", n ? " " : "", name);
    pthread_mutex_unlock(&g_log_m);
}
/* seq_cst 증가 후 상대 카운터 확인 = Dekker 모양: 둘이 겹치면 적어도 한쪽은 반드시 상대를 본다. */
static void cs_enter_read(void)  { atomic_fetch_add(&g_in_r, 1); if (atomic_load(&g_in_w) != 0) g_viol++; }
static void cs_exit_read(void)   { atomic_fetch_sub(&g_in_r, 1); }
static void cs_enter_write(void) { if (atomic_fetch_add(&g_in_w, 1) != 0 || atomic_load(&g_in_r) != 0) g_viol++; }
static void cs_exit_write(void)  { atomic_fetch_sub(&g_in_w, 1); }

/* ===== actor: 하네스가 명령을 하나씩 주는 스레드 (강제 스케줄) ===== */
enum { CMD_NONE, CMD_RD, CMD_RDUN, CMD_WR, CMD_WRUN, CMD_CRD, CMD_CRDUN, CMD_CWR, CMD_CWRUN, CMD_EXIT };
enum { A_IDLE, A_BUSY, A_IN_R, A_IN_W };
typedef struct {
    pthread_t th;
    const char *name;
    rwl_t *rw;
    cfg_store_t *cs;
    pthread_mutex_t m;
    pthread_cond_t cv;
    int cmd;                   /* m 보호 */
    atomic_int state;          /* A_* */
    atomic_int ret;            /* 마지막 cfg_* 반환값 */
    atomic_int done;           /* 끝낸 명령 수 */
} actor_t;

static void *actor_main(void *arg) {
    actor_t *a = arg;
    for (;;) {
        pthread_mutex_lock(&a->m);
        while (a->cmd == CMD_NONE) pthread_cond_wait(&a->cv, &a->m);
        int c = a->cmd;
        a->cmd = CMD_NONE;
        pthread_cond_broadcast(&a->cv);
        pthread_mutex_unlock(&a->m);
        if (c == CMD_EXIT) break;
        switch (c) {
        case CMD_RD:   a->state = A_BUSY; rwl_rdlock(a->rw); cs_enter_read(); log_add(a->name); a->state = A_IN_R; break;
        case CMD_RDUN: cs_exit_read(); rwl_rdunlock(a->rw); a->state = A_IDLE; break;
        case CMD_WR:   a->state = A_BUSY; rwl_wrlock(a->rw); cs_enter_write(); log_add(a->name); a->state = A_IN_W; break;
        case CMD_WRUN: cs_exit_write(); rwl_wrunlock(a->rw); a->state = A_IDLE; break;
        case CMD_CRD: {
            int prev = a->state;
            a->state = A_BUSY;
            int r = cfg_read_lock(a->cs);
            a->ret = r;
            a->state = (r == 0 && prev != A_IN_W) ? A_IN_R : prev;
            break;
        }
        case CMD_CRDUN: a->ret = cfg_read_unlock(a->cs); break;
        case CMD_CWR: {
            int prev = a->state;
            a->state = A_BUSY;
            int r = cfg_write_lock(a->cs);
            a->ret = r;
            a->state = (r == 0) ? A_IN_W : prev;
            break;
        }
        case CMD_CWRUN: { int r = cfg_write_unlock(a->cs); a->ret = r; if (r == 0) a->state = A_IDLE; break; }
        }
        a->done++;
    }
    return NULL;
}
static void actor_start(actor_t *a, const char *name, rwl_t *rw, cfg_store_t *cs) {
    memset(a, 0, sizeof *a);
    a->name = name; a->rw = rw; a->cs = cs;
    pthread_mutex_init(&a->m, NULL);
    pthread_cond_init(&a->cv, NULL);
    pthread_create(&a->th, NULL, actor_main, a);
}
static void act(actor_t *a, int cmd) {              /* 명령만 넣고 돌아온다 (막힐 수 있는 명령용) */
    pthread_mutex_lock(&a->m);
    while (a->cmd != CMD_NONE) pthread_cond_wait(&a->cv, &a->m);
    a->cmd = cmd;
    pthread_cond_broadcast(&a->cv);
    pthread_mutex_unlock(&a->m);
}
static bool act_sync(actor_t *a, int cmd) {         /* 명령을 넣고 끝날 때까지 기다린다 (막히면 안 되는 명령용) */
    int before = a->done;
    act(a, cmd);
    double t0 = now_ms();
    while (a->done == before) { if (now_ms() - t0 > WAIT_MS) return false; sleep_us(200); }
    return true;
}
static void actor_release(actor_t *a) {     /* 틀린 구현이 남긴 보유를 풀어 다음 테스트를 오염시키지 않는다 */
    if (a->state == A_IN_R) act_sync(a, CMD_RDUN);
    else if (a->state == A_IN_W) act_sync(a, CMD_WRUN);
}
static void actor_stop(actor_t *a) {
    act(a, CMD_EXIT);
    pthread_join(a->th, NULL);
    pthread_mutex_destroy(&a->m);
    pthread_cond_destroy(&a->cv);
}

/* 조건이 참이 될 때까지 (최대 WAIT_MS) 기다린다. 판정은 조건으로만, 시간은 안전장치. */
typedef bool (*pred_fn)(void *);
static bool wait_until(pred_fn p, void *ctx) {
    double t0 = now_ms();
    while (!p(ctx)) { if (now_ms() - t0 > WAIT_MS) return false; sleep_us(200); }
    return true;
}
typedef struct { rwl_t *rw; actor_t *a, *b; int wr, ww, ar; } wctx_t;
/* a 가 막혀서 줄을 섰거나(카운터가 기대값) 이미 들어갔으면(틀린 구현) 참 */
static bool p_reader_queued_or_in(void *c) {
    wctx_t *w = c; rwl_snap_t s = rwl_snapshot(w->rw);
    return s.waiting_readers >= w->wr || w->a->state == A_IN_R;
}
static bool p_writer_queued_or_in(void *c) {
    wctx_t *w = c; rwl_snap_t s = rwl_snapshot(w->rw);
    return s.waiting_writers >= w->ww || w->a->state == A_IN_W;
}
static bool p_a_in(void *c) { wctx_t *w = c; return w->a->state == A_IN_R || w->a->state == A_IN_W; }
static bool p_a_or_b_in(void *c) {
    wctx_t *w = c;
    return w->a->state == A_IN_R || w->a->state == A_IN_W || w->b->state == A_IN_R || w->b->state == A_IN_W;
}

/* ===== 테스트 ===== */
static bool snap_is(rwl_t *rw, int ar, bool w, int wr, int ww) {
    rwl_snap_t s = rwl_snapshot(rw);
    return s.active_readers == ar && s.writer_active == w && s.waiting_readers == wr && s.waiting_writers == ww;
}

/* 1 스레드: 기본 카운터 동작 (읽기 2중 = writer 가 없으니 어느 정책이든 허용) */
static bool test_single(int pol) {
    rwl_t rw; rwl_init(&rw, pol);
    bool ok = true;
    rwl_rdlock(&rw); rwl_rdlock(&rw);
    ok &= snap_is(&rw, 2, false, 0, 0);
    rwl_rdunlock(&rw); rwl_rdunlock(&rw);
    ok &= snap_is(&rw, 0, false, 0, 0);
    rwl_wrlock(&rw);
    ok &= snap_is(&rw, 0, true, 0, 0);
    rwl_wrunlock(&rw);
    ok &= snap_is(&rw, 0, false, 0, 0);
    rwl_destroy(&rw);
    return ok;
}

/* 두 reader 겹침 + writer 가 reader 를 막음 + reader 가 writer 를 막음 */
static bool test_basic_sched(int pol) {
    rwl_t rw; rwl_init(&rw, pol);
    actor_t A, B, W;
    actor_start(&A, "A", &rw, NULL); actor_start(&B, "B", &rw, NULL); actor_start(&W, "W", &rw, NULL);
    bool ok = true;
    g_viol = 0;
    wctx_t c = { .rw = &rw };
    /* 1) A, B 둘 다 읽기 → 동시에 안에 있어야 한다 */
    act(&A, CMD_RD); c.a = &A; ok &= wait_until(p_a_in, &c);
    act(&B, CMD_RD); c.a = &B; ok &= wait_until(p_a_in, &c);
    ok &= snap_is(&rw, 2, false, 0, 0);
    /* 2) W 는 reader 가 있는 동안 기다린다 */
    act(&W, CMD_WR); c.a = &W; c.ww = 1; ok &= wait_until(p_writer_queued_or_in, &c);
    ok &= W.state == A_BUSY;
    ok &= act_sync(&A, CMD_RDUN);
    ok &= W.state == A_BUSY;                      /* B 가 아직 안에 있다 */
    ok &= act_sync(&B, CMD_RDUN);
    ok &= wait_until(p_a_in, &c) && W.state == A_IN_W;
    /* 3) W 가 쥔 동안 reader A 는 줄을 선다 */
    act(&A, CMD_RD); c.a = &A; c.wr = 1; ok &= wait_until(p_reader_queued_or_in, &c);
    ok &= A.state == A_BUSY && snap_is(&rw, 0, true, 1, 0);
    ok &= act_sync(&W, CMD_WRUN);
    ok &= wait_until(p_a_in, &c) && A.state == A_IN_R;
    ok &= act_sync(&A, CMD_RDUN);
    ok &= snap_is(&rw, 0, false, 0, 0) && g_viol == 0;
    wd_arm("test_basic_sched: stopping actors");
    actor_release(&A); actor_release(&B); actor_release(&W);
    actor_stop(&A); actor_stop(&B); actor_stop(&W);
    rwl_destroy(&rw);
    return ok;
}

/* 강제 스케줄 ①: reader 가 끊임없이 겹친다 → writer starvation.
 * RA 가 읽는 중, W 가 대기. 새 reader 를 넣고 → 그 reader 가 들어오면 옛 reader 를 내보낸다 (교대 1회).
 * 이 순서면 active_readers 가 0 이 되는 순간이 없다. K 번 교대 동안 W 가 들어갔는지 본다. */
typedef struct {
    int handoffs;              /* W 가 기다리는 동안 성공한 reader 교대 수 */
    bool writer_waited_all;    /* K 번 동안 W 가 한 번도 안 들어감 */
    bool writer_in_next;       /* 마지막 reader 가 나가자 W 가 들어감 */
    bool blocked_reader_after; /* 줄 선 reader 가 W 뒤에 들어감 (writer-pref/phase-fair) */
    char log[512];
} starve_t;
static starve_t starve_schedule(int pol, int K) {
    starve_t r; memset(&r, 0, sizeof r);
    rwl_t rw; rwl_init(&rw, pol);
    actor_t RA, RB, W;
    actor_start(&RA, "RA", &rw, NULL); actor_start(&RB, "RB", &rw, NULL); actor_start(&W, "W", &rw, NULL);
    log_reset();
    wctx_t c = { .rw = &rw };
    act(&RA, CMD_RD); c.a = &RA; wait_until(p_a_in, &c);
    act(&W, CMD_WR);  c.a = &W; c.ww = 1; wait_until(p_writer_queued_or_in, &c);
    actor_t *cur = &RA, *nxt = &RB;
    bool reader_blocked = false;
    r.writer_waited_all = true;
    for (int i = 0; i < K; i++) {
        act(nxt, CMD_RD); c.a = nxt; c.wr = 1; wait_until(p_reader_queued_or_in, &c);
        if (nxt->state != A_IN_R) { reader_blocked = true; break; }      /* 새 reader 가 줄을 섰다 */
        act_sync(cur, CMD_RDUN);                                          /* 옛 reader 퇴장: 그래도 1명이 남아 있다 */
        if (W.state == A_IN_W) { r.writer_waited_all = false; break; }
        r.handoffs++;
        actor_t *t = cur; cur = nxt; nxt = t;
    }
    if (W.state == A_IN_W) r.writer_waited_all = false;
    if (reader_blocked) r.writer_waited_all = false;
    /* 남은 reader(cur) 를 내보낸다 → W 차례 */
    if (cur->state == A_IN_R) act_sync(cur, CMD_RDUN);
    c.a = &W; r.writer_in_next = wait_until(p_a_in, &c) && W.state == A_IN_W;
    if (reader_blocked) {
        bool still_out = nxt->state == A_BUSY;                           /* W 가 안에 있는 동안 reader 는 밖 */
        act_sync(&W, CMD_WRUN);
        c.a = nxt; r.blocked_reader_after = still_out && wait_until(p_a_in, &c) && nxt->state == A_IN_R;
        act_sync(nxt, CMD_RDUN);
    } else if (W.state == A_IN_W) {
        act_sync(&W, CMD_WRUN);
    }
    pthread_mutex_lock(&g_log_m); memcpy(r.log, g_log, sizeof r.log); pthread_mutex_unlock(&g_log_m);
    wd_arm("starve_schedule: stopping actors");
    actor_release(&RA); actor_release(&RB); actor_release(&W);
    actor_stop(&RA); actor_stop(&RB); actor_stop(&W);
    rwl_destroy(&rw);
    return r;
}

/* 강제 스케줄 ②: writer 가 줄줄이 온다 → reader starvation (writer-pref 의 반대 문제).
 * R0 읽는 중 → Wa 대기 → R1, R2 대기 → R0 퇴장(Wa 입장). 그 뒤 매 판: 다음 writer 가 줄을 선 상태에서
 * 지금 writer 가 나간다. 그다음 들어가는 게 writer 면 reader 는 writer 한 판을 더 기다린 것이다. */
typedef struct { int writer_phases_waited; bool batch_together; bool final_writer_in; char log[512]; } rstarve_t;
static rstarve_t reader_starve_schedule(int pol, int K) {
    rstarve_t r; memset(&r, 0, sizeof r);
    rwl_t rw; rwl_init(&rw, pol);
    actor_t R0, R1, R2, WA, WB;
    actor_start(&R0, "R0", &rw, NULL); actor_start(&R1, "R1", &rw, NULL); actor_start(&R2, "R2", &rw, NULL);
    actor_start(&WA, "Wa", &rw, NULL); actor_start(&WB, "Wb", &rw, NULL);
    log_reset();
    wctx_t c = { .rw = &rw };
    act(&R0, CMD_RD); c.a = &R0; wait_until(p_a_in, &c);
    act(&WA, CMD_WR); c.a = &WA; c.ww = 1; wait_until(p_writer_queued_or_in, &c);
    act(&R1, CMD_RD); c.a = &R1; c.wr = 1; wait_until(p_reader_queued_or_in, &c);
    act(&R2, CMD_RD); c.a = &R2; c.wr = 2; wait_until(p_reader_queued_or_in, &c);
    act_sync(&R0, CMD_RDUN);
    c.a = &WA; wait_until(p_a_in, &c);
    actor_t *cur = &WA, *nxt = &WB;
    bool readers_in = false;
    for (int i = 0; i < K && cur->state == A_IN_W; i++) {
        act(nxt, CMD_WR); c.a = nxt; c.ww = 1; wait_until(p_writer_queued_or_in, &c);
        act_sync(cur, CMD_WRUN);
        c.a = nxt; c.b = &R1; wait_until(p_a_or_b_in, &c);
        if (R1.state == A_IN_R) { readers_in = true; break; }
        if (nxt->state != A_IN_W) break;
        r.writer_phases_waited++;
        actor_t *t = cur; cur = nxt; nxt = t;
    }
    if (readers_in) {
        /* phase-fair: R1 과 R2 가 한 묶음으로 같이 들어왔어야 한다, 줄 선 writer(nxt) 는 그다음 */
        c.a = &R2; wait_until(p_a_in, &c);
        r.batch_together = R2.state == A_IN_R && nxt->state == A_BUSY && snap_is(&rw, 2, false, 0, 1);
        act_sync(&R1, CMD_RDUN); act_sync(&R2, CMD_RDUN);
        c.a = nxt; r.final_writer_in = wait_until(p_a_in, &c) && nxt->state == A_IN_W;
        act_sync(nxt, CMD_WRUN);
    } else {
        if (cur->state == A_IN_W) act_sync(cur, CMD_WRUN);
        c.a = &R1; wait_until(p_a_in, &c);
        c.a = &R2; wait_until(p_a_in, &c);
        if (R1.state == A_IN_R) act_sync(&R1, CMD_RDUN);
        if (R2.state == A_IN_R) act_sync(&R2, CMD_RDUN);
        if (nxt->state == A_IN_W) act_sync(nxt, CMD_WRUN);
    }
    pthread_mutex_lock(&g_log_m); memcpy(r.log, g_log, sizeof r.log); pthread_mutex_unlock(&g_log_m);
    wd_arm("reader_starve_schedule: stopping actors");
    actor_release(&R0); actor_release(&R1); actor_release(&R2); actor_release(&WA); actor_release(&WB);
    actor_stop(&R0); actor_stop(&R1); actor_stop(&R2); actor_stop(&WA); actor_stop(&WB);
    rwl_destroy(&rw);
    return r;
}

/* 스트레스: 8 스레드, 읽기 95 % / 쓰기 5 %. 테이블 필드는 일반 변수 — 락이 틀리면 검사합이 깨지고 TSan 이 본다. */
typedef struct { rwl_t *rw; cfg_table_t *tab; int ops; uint32_t seed; long reads, writes, bad, nonmono; } stress_arg_t;
static void *stress_main(void *arg) {
    stress_arg_t *a = arg;
    uint64_t last = 0;
    for (int i = 0; i < a->ops; i++) {
        if (xorshift32(&a->seed) % 100 < 5) {
            rwl_wrlock(a->rw); cs_enter_write();
            cfg_fill(a->tab, a->tab->version + 1);
            cs_exit_write(); rwl_wrunlock(a->rw);
            a->writes++;
        } else {
            rwl_rdlock(a->rw); cs_enter_read();
            /* 제자리에서 필드별로 읽는다 (구조체 복사 = memcpy 경로는 이 맥의 TSan 이 놓쳤다 — 본문 §7) */
            bool sum_ok = a->tab->sum == cfg_sum(a->tab);
            uint64_t ver = a->tab->version;
            cs_exit_read(); rwl_rdunlock(a->rw);
            if (!sum_ok) a->bad++;
            if (ver < last) a->nonmono++;
            last = ver;
            a->reads++;
        }
    }
    return NULL;
}
static bool stress(int pol, int nthreads, int ops) {
    rwl_t rw; rwl_init(&rw, pol);
    cfg_table_t tab; cfg_fill(&tab, 0);
    stress_arg_t args[8]; pthread_t th[8];
    g_viol = 0;
    for (int t = 0; t < nthreads; t++) {
        args[t] = (stress_arg_t){ .rw = &rw, .tab = &tab, .ops = ops, .seed = 0x9E3779B9u + (uint32_t)t * 7919u };
        pthread_create(&th[t], NULL, stress_main, &args[t]);
    }
    long reads = 0, writes = 0, bad = 0, nonmono = 0;
    for (int t = 0; t < nthreads; t++) {
        pthread_join(th[t], NULL);
        reads += args[t].reads; writes += args[t].writes; bad += args[t].bad; nonmono += args[t].nonmono;
    }
    printf("  [stress %-11s] %d threads x %d ops: reads=%ld writes=%ld violations=%d bad_sum=%ld nonmono=%ld version=%llu\n",
           policy_name[pol], nthreads, ops, reads, writes, atomic_load(&g_viol), bad, nonmono,
           (unsigned long long)tab.version);
    bool ok = g_viol == 0 && bad == 0 && nonmono == 0 && (long)tab.version == writes && reads + writes == (long)nthreads * ops
              && snap_is(&rw, 0, false, 0, 0);
    rwl_destroy(&rw);
    return ok;
}

/* Part C: 중첩 읽기 + 승격 거부 */
static bool test_cfg_nested(int pol) {
    cfg_store_t s; rwl_init(&s.lock, pol); cfg_fill(&s.tab, 1);
    actor_t T, W;
    actor_start(&T, "T", &s.lock, &s); actor_start(&W, "W", &s.lock, &s);
    bool ok = true;
    wctx_t c = { .rw = &s.lock };
    ok &= act_sync(&T, CMD_CRD) && T.ret == 0 && T.state == A_IN_R;
    act(&W, CMD_WR); c.a = &W; c.ww = 1; ok &= wait_until(p_writer_queued_or_in, &c) && W.state == A_BUSY;
    /* writer 가 기다리는 중에 같은 태스크가 다시 읽는다 — 래퍼가 rwlock 을 다시 잡지 않아야 안 막힌다 */
    ok &= act_sync(&T, CMD_CRD) && T.ret == 0;
    ok &= snap_is(&s.lock, 1, false, 0, 1);       /* rwlock 입장에서는 reader 1명뿐 */
    ok &= act_sync(&T, CMD_CRDUN) && T.ret == 0 && W.state == A_BUSY;   /* 안쪽 해제: 아직 쥐고 있다 */
    ok &= act_sync(&T, CMD_CRDUN) && T.ret == 0;
    c.a = &W; ok &= wait_until(p_a_in, &c) && W.state == A_IN_W;
    ok &= act_sync(&W, CMD_WRUN);
    ok &= act_sync(&T, CMD_CRDUN) && T.ret == EPERM;                     /* 안 쥐었는데 해제 */
    wd_arm("test_cfg_nested: stopping actors");
    actor_stop(&T); actor_stop(&W);
    ok &= snap_is(&s.lock, 0, false, 0, 0);
    rwl_destroy(&s.lock);
    return ok;
}
static bool test_cfg_upgrade(int pol) {
    cfg_store_t s; rwl_init(&s.lock, pol); cfg_fill(&s.tab, 1);
    actor_t T;
    actor_start(&T, "T", &s.lock, &s);
    bool ok = true;
    ok &= act_sync(&T, CMD_CRD) && T.ret == 0;
    ok &= act_sync(&T, CMD_CWR) && T.ret == EDEADLK && T.state == A_IN_R;   /* 승격: 기다리지 않고 거부 */
    ok &= act_sync(&T, CMD_CRDUN) && T.ret == 0;
    ok &= act_sync(&T, CMD_CWR) && T.ret == 0 && T.state == A_IN_W;
    ok &= act_sync(&T, CMD_CWR) && T.ret == EDEADLK;                        /* 재귀 쓰기도 거부 */
    ok &= act_sync(&T, CMD_CRD) && T.ret == 0;                              /* 쓰기 안에서 읽기 = 허용 */
    ok &= act_sync(&T, CMD_CWRUN) && T.ret == EPERM;                        /* 안쪽 읽기가 열린 채 쓰기 해제 */
    ok &= act_sync(&T, CMD_CRDUN) && T.ret == 0;
    ok &= snap_is(&s.lock, 0, true, 0, 0);                                  /* 쓰기는 아직 쥐고 있다 */
    ok &= act_sync(&T, CMD_CWRUN) && T.ret == 0;
    ok &= snap_is(&s.lock, 0, false, 0, 0);
    wd_arm("test_cfg_upgrade: stopping actors");
    actor_stop(&T);
    rwl_destroy(&s.lock);
    return ok;
}

/* Part C 스트레스: 제어 태스크 7개가 중첩 읽기(테이블 읽기 안에서 항목 조회 함수 호출), 1개가 OTA 처럼 갱신 */
typedef struct { cfg_store_t *s; int ops; bool writer; long bad, nonmono, err, done; } cfg_arg_t;
static int cfg_get_entry(cfg_store_t *s, int i, uint32_t *out) {      /* 다른 모듈의 도우미 = 자기도 읽기 락을 잡는다 */
    int r = cfg_read_lock(s);
    if (r) return r;
    *out = s->tab.v[i];
    return cfg_read_unlock(s);
}
static void *cfg_task(void *arg) {
    cfg_arg_t *a = arg;
    uint64_t last = 0;
    for (int i = 0; i < a->ops; i++) {
        if (a->writer) {
            if (cfg_write_lock(a->s)) { a->err++; continue; }
            cfg_table_t nt; cfg_fill(&nt, a->s->tab.version + 1);
            a->s->tab = nt;
            if (cfg_write_unlock(a->s)) a->err++;
            sleep_us(20);
        } else {
            if (cfg_read_lock(a->s)) { a->err++; continue; }
            cfg_table_t snap = a->s->tab;
            uint32_t e = 0;
            if (cfg_get_entry(a->s, i % CFG_N, &e) || e != snap.v[i % CFG_N]) a->err++;  /* 중첩 읽기: 같은 판 */
            if (cfg_read_unlock(a->s)) a->err++;
            if (snap.sum != cfg_sum(&snap)) a->bad++;
            if (snap.version < last) a->nonmono++;
            last = snap.version;
        }
        a->done++;
    }
    return NULL;
}
static bool cfg_stress(int pol) {
    cfg_store_t s; rwl_init(&s.lock, pol); cfg_fill(&s.tab, 0);
    cfg_arg_t args[8]; pthread_t th[8];
    for (int t = 0; t < 8; t++) {
        args[t] = (cfg_arg_t){ .s = &s, .ops = (t == 7) ? 300 : 6000, .writer = (t == 7) };
        pthread_create(&th[t], NULL, cfg_task, &args[t]);
    }
    long bad = 0, nonmono = 0, err = 0, done = 0;
    for (int t = 0; t < 8; t++) {
        pthread_join(th[t], NULL);
        bad += args[t].bad; nonmono += args[t].nonmono; err += args[t].err; done += args[t].done;
    }
    printf("  [cfg %-11s] 7 readers x 6000 (nested) + 1 writer x 300: done=%ld bad_sum=%ld nonmono=%ld err=%ld version=%llu\n",
           policy_name[pol], done, bad, nonmono, err, (unsigned long long)s.tab.version);
    bool ok = bad == 0 && nonmono == 0 && err == 0 && done == 7 * 6000 + 300 && s.tab.version == 300
              && snap_is(&s.lock, 0, false, 0, 0);
    rwl_destroy(&s.lock);
    return ok;
}

#ifndef RWL_NO_MAIN
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    pthread_t wd;
    pthread_create(&wd, NULL, wd_main, NULL);
    pthread_detach(wd);
    bool unit_ok[RW_NPOLICY];
    const int K = 20;

    printf("== Part A/B: single-thread counters, basic schedules ==\n");
    for (int p = 0; p < RW_NPOLICY; p++) {
        wd_arm("test_single"); bool a = test_single(p);
        wd_arm("test_basic_sched"); bool b = test_basic_sched(p);
        printf("  [%s] single=%d basic_sched=%d\n", policy_name[p], a, b);
        unit_ok[p] = a && b;
    }
    CHECK(unit_ok[RW_READER_PREF]);
    CHECK(unit_ok[RW_WRITER_PREF]);
    CHECK(unit_ok[RW_PHASE_FAIR]);

    printf("== Part A: forced schedule 1 — readers overlap continuously, writer waits (K = %d) ==\n", K);
    starve_t s[RW_NPOLICY];
    for (int p = 0; p < RW_NPOLICY; p++) {
        wd_arm("starve_schedule");
        s[p] = starve_schedule(p, K);
        printf("  [%-11s] handoffs while W waited=%d waited_all=%d W_in_next=%d blocked_reader_after_W=%d\n"
               "                order: %s\n",
               policy_name[p], s[p].handoffs, s[p].writer_waited_all, s[p].writer_in_next,
               s[p].blocked_reader_after, s[p].log);
    }
    CHECK(s[RW_READER_PREF].handoffs == K && s[RW_READER_PREF].writer_waited_all);   /* writer starvation 재현 */
    CHECK(s[RW_READER_PREF].writer_in_next);                                          /* reader 가 끊기면 그제야 */
    CHECK(s[RW_WRITER_PREF].handoffs == 0 && s[RW_WRITER_PREF].writer_in_next && s[RW_WRITER_PREF].blocked_reader_after);
    CHECK(s[RW_WRITER_PREF].blocked_reader_after && strcmp(s[RW_WRITER_PREF].log, "RA W RB") == 0);
    CHECK(s[RW_PHASE_FAIR].handoffs == 0 && s[RW_PHASE_FAIR].writer_in_next && s[RW_PHASE_FAIR].blocked_reader_after);

    printf("== Part B: forced schedule 2 — writers arrive back-to-back, readers wait (K = %d) ==\n", K);
    rstarve_t rs[RW_NPOLICY];
    for (int p = RW_WRITER_PREF; p < RW_NPOLICY; p++) {
        wd_arm("reader_starve_schedule");
        rs[p] = reader_starve_schedule(p, K);
        printf("  [%-11s] writer phases R1/R2 waited=%d batch_together=%d next_writer_after_batch=%d\n"
               "                order: %s\n",
               policy_name[p], rs[p].writer_phases_waited, rs[p].batch_together, rs[p].final_writer_in, rs[p].log);
    }
    CHECK(rs[RW_WRITER_PREF].writer_phases_waited == K);                  /* writer-pref: reader starvation */
    CHECK(rs[RW_PHASE_FAIR].writer_phases_waited == 0);                   /* phase-fair: writer 한 판 뒤 바로 */
    CHECK(rs[RW_PHASE_FAIR].batch_together && rs[RW_PHASE_FAIR].final_writer_in);
    CHECK(rs[RW_PHASE_FAIR].batch_together &&
          (strcmp(rs[RW_PHASE_FAIR].log, "R0 Wa R1 R2 Wb") == 0 || strcmp(rs[RW_PHASE_FAIR].log, "R0 Wa R2 R1 Wb") == 0));

    printf("== Part A/B: stress 95/5, invariants checked inside critical sections ==\n");
    for (int p = 0; p < RW_NPOLICY; p++) {
        if (!unit_ok[p]) { printf("SKIP stress %s (unit tests failed)\n", policy_name[p]); g_fail++; continue; }
        wd_arm("stress");
        CHECK(stress(p, 8, 20000));
    }

    printf("== Part C: firmware config table wrapper ==\n");
    wd_arm("test_cfg_nested");
    CHECK(test_cfg_nested(RW_WRITER_PREF));
    CHECK(test_cfg_nested(RW_PHASE_FAIR));
    wd_arm("test_cfg_upgrade");
    CHECK(test_cfg_upgrade(RW_WRITER_PREF));
    CHECK(test_cfg_upgrade(RW_READER_PREF));
    if (unit_ok[RW_WRITER_PREF] && unit_ok[RW_PHASE_FAIR]) {
        wd_arm("cfg_stress");
        CHECK(cfg_stress(RW_WRITER_PREF));
        CHECK(cfg_stress(RW_PHASE_FAIR));
    } else {
        printf("SKIP cfg_stress (unit tests failed)\n"); g_fail += 2;
    }
    wd_disarm();

    int pass = atomic_load(&g_pass), fail = atomic_load(&g_fail);
    printf("%d/%d passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
#endif
