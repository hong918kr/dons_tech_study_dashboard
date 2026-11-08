/* OS-B06 조건 변수: bounded buffer 생산자-소비자
 *
 *   Part A  bounded buffer: mutex 1개 + 조건 변수 2개(empty / fill) + while 재검사
 *   Part B  OSTEP 30장의 깨진 버전(v1: if + CV 1개, v2: while + CV 1개)을 "강제 스케줄" 로
 *           재현하고 내 버전과 나란히 돌린다. 그리고 bb_close(): 종료 알림은 broadcast.
 *   Part C  센서 샘플 → 로거: bb_get_batch() 로 한 번에 여러 개 꺼내기 (여러 칸이 비면 broadcast)
 *
 * 체크는 "올바른 구현이면 매 실행 항상 성립" 하는 것만 본다: 모든 항목이 정확히 한 번,
 * 생산자별 FIFO, 강제 스케줄의 결과. 기다리는 곳은 전부 제한 시간(TMO_MS)이 있어서
 * 틀린 코드는 멈추지 않고 FAIL 로 끝난다. 시간 측정값은 체크하지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- 테스트용 조건 변수 scv (깨진 버전 v1/v2 전용, 하네스 제공) ----------
 * POSIX 는 signal 이 "기다리는 스레드 중 적어도 하나" 를 깨운다고만 하고, 누구를 깨울지,
 * 깨어난 스레드가 언제 다시 락을 잡을지는 정하지 않는다. scv 는 그 자유 중 하나를 고정한다.
 *   - signal 은 가장 오래 기다린 스레드를 깨운다 (FIFO)
 *   - held 모드: 깨어난 스레드도 하네스가 scv_run_one() 할 때까지 못 돈다 (= 스케줄러가 늦게 돌림)
 * 둘 다 진짜 pthread_cond 가 할 수 있는 동작이므로, 여기서 깨지는 코드는 실제로도 깨질 수 있다. */
typedef struct {
    pthread_cond_t cv;
    unsigned long next;       /* 다음 대기자 번호표 */
    unsigned long woken;      /* 번호 < woken : signal 받음 */
    unsigned long runnable;   /* 번호 < runnable : 하네스가 실행 허락 (held 일 때만 의미) */
    bool held;
} scv_t;

static void scv_wait(scv_t *s, pthread_mutex_t *m) {
    unsigned long my = s->next++;
    while (my >= s->woken || (s->held && my >= s->runnable))
        pthread_cond_wait(&s->cv, m);
}
static void scv_signal(scv_t *s)    { if (s->woken < s->next) s->woken++; pthread_cond_broadcast(&s->cv); }
static void scv_broadcast(scv_t *s) { s->woken = s->next; pthread_cond_broadcast(&s->cv); }
static void scv_run_one(scv_t *s)   { if (s->runnable < s->woken) s->runnable++; pthread_cond_broadcast(&s->cv); }

/* ---------- bounded buffer ---------- */
#define BB_MAX     64
#define EMPTY_READ (-999)     /* 빈 버퍼에서 꺼냈을 때 돌려주는 쓰레기 값 (OSTEP 은 assert) */

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  empty;    /* "빈 칸이 생겼다" — 생산자가 여기서 기다린다 */
    pthread_cond_t  fill;     /* "항목이 생겼다" — 소비자가 여기서 기다린다 */
    int  buf[BB_MAX];
    int  cap, head, count;    /* head: 다음에 꺼낼 칸, count: 들어 있는 개수 */
    bool closed;              /* bb_close() 이후 true (Part B) */
    scv_t one;                /* 깨진 버전(v1/v2) 이 쓰는 "하나뿐인" 조건 변수 */
    /* 테스트 계측 — 전부 m 으로 보호 */
    int  waiting;             /* 지금 대기 중인 스레드 수 */
    long waits;               /* 대기를 시작한 총 횟수 */
    long underflow, overflow; /* 빈 버퍼에서 get / 꽉 찬 버퍼에 put (깨진 버전만 > 0) */
} bb_t;

static void bb_init(bb_t *b, int cap) {
    memset(b, 0, sizeof *b);
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->empty, NULL);
    pthread_cond_init(&b->fill, NULL);
    pthread_cond_init(&b->one.cv, NULL);
    b->cap = cap;
}
static void bb_destroy(bb_t *b) {
    pthread_cond_destroy(&b->one.cv);
    pthread_cond_destroy(&b->fill);
    pthread_cond_destroy(&b->empty);
    pthread_mutex_destroy(&b->m);
}

/* 링 연산 — 호출자가 m 을 잡고 있어야 한다. 잘못된 호출은 막지 않고 세기만 한다. */
static void ring_put(bb_t *b, int v) {
    if (b->count == b->cap) { b->overflow++; return; }
    b->buf[(b->head + b->count) % b->cap] = v;
    b->count++;
}
static int ring_get(bb_t *b) {
    if (b->count == 0) { b->underflow++; return EMPTY_READ; }
    int v = b->buf[b->head];
    b->head = (b->head + 1) % b->cap;
    b->count--;
    return v;
}
/* 기다릴 때는 이걸 부른다: 안에서 pthread_cond_wait 를 그대로 부르고, 하네스용으로 대기자 수를 센다. */
static void wait_on(bb_t *b, pthread_cond_t *cv) {
    b->waiting++; b->waits++;
    pthread_cond_wait(cv, &b->m);      /* 락을 풀고 잠듦 → 깨어나면 락을 다시 잡고 돌아옴 */
    b->waiting--;
}

/* ======================= Part A — bounded buffer (내 버전) ======================= */
/* 꽉 찼으면 빈 칸이 생길 때까지 기다렸다가 넣는다. 닫힌 버퍼면 넣지 않고 false. */
static bool bb_put(bb_t *b, int v) {
    pthread_mutex_lock(&b->m);
    while (b->count == b->cap && !b->closed)
        wait_on(b, &b->empty);
    if (b->closed) { pthread_mutex_unlock(&b->m); return false; }
    ring_put(b, v);
    pthread_cond_signal(&b->fill);
    pthread_mutex_unlock(&b->m);
    return true;
}
/* 비었으면 항목이 생길 때까지 기다렸다가 꺼낸다. 닫혔고 비었으면 false (닫혀도 남은 항목은 준다). */
static bool bb_get(bb_t *b, int *out) {
    pthread_mutex_lock(&b->m);
    while (b->count == 0 && !b->closed)
        wait_on(b, &b->fill);
    if (b->count == 0) { pthread_mutex_unlock(&b->m); return false; }
    *out = ring_get(b);
    pthread_cond_signal(&b->empty);
    pthread_mutex_unlock(&b->m);
    return true;
}

/* ======================= Part B — 종료 ======================= */
/* 닫기: 이후 put 은 false, get 은 남은 것을 다 준 뒤 false. 기다리던 스레드는 전부 깨어나야 한다. */
static void bb_close(bb_t *b) {
    pthread_mutex_lock(&b->m);
    b->closed = true;
    pthread_cond_broadcast(&b->fill);
    pthread_cond_broadcast(&b->empty);
    pthread_mutex_unlock(&b->m);
}

/* ======================= Part C — 한 번에 여러 개 꺼내기 ======================= */
/* 최소 1개가 생길 때까지 기다린 뒤 최대 max 개를 꺼낸다. 닫혔고 비었으면 0. */
static int bb_get_batch(bb_t *b, int *out, int max) {
    pthread_mutex_lock(&b->m);
    while (b->count == 0 && !b->closed)
        wait_on(b, &b->fill);
    int n = 0;
    while (n < max && b->count > 0)
        out[n++] = ring_get(b);
    if (n > 1) pthread_cond_broadcast(&b->empty);      /* 칸이 여러 개 비었다 → 생산자 여럿이 들어올 수 있다 */
    else if (n == 1) pthread_cond_signal(&b->empty);
    pthread_mutex_unlock(&b->m);
    return n;
}

/* ======================= OSTEP 30장의 깨진 버전 (하네스 제공, 고치지 말 것) ======================= */
static void wait_one(bb_t *b) {                 /* 하나뿐인 CV 에서 대기 (FIFO scv) */
    b->waiting++; b->waits++;
    scv_wait(&b->one, &b->m);
    b->waiting--;
}
/* v1 — 그림 30.6: CV 1개 + if */
static bool v1_put(bb_t *b, int v) {
    pthread_mutex_lock(&b->m);
    if (b->count == b->cap && !b->closed) wait_one(b);
    if (b->closed) { pthread_mutex_unlock(&b->m); return false; }
    ring_put(b, v);                             /* 깨어났을 때 다시 꽉 차 있으면 overflow */
    scv_signal(&b->one);
    pthread_mutex_unlock(&b->m);
    return true;
}
static bool v1_get(bb_t *b, int *out) {
    pthread_mutex_lock(&b->m);
    if (b->count == 0 && !b->closed) wait_one(b);
    if (b->count == 0 && b->closed) { pthread_mutex_unlock(&b->m); return false; }
    *out = ring_get(b);                         /* 깨어났을 때 비어 있으면 underflow */
    scv_signal(&b->one);
    pthread_mutex_unlock(&b->m);
    return true;
}
/* v2 — 그림 30.8: CV 1개 + while. wake_all=true 면 signal 대신 broadcast (30.3 covering condition) */
static bool v2_put_impl(bb_t *b, int v, bool wake_all) {
    pthread_mutex_lock(&b->m);
    while (b->count == b->cap && !b->closed) wait_one(b);
    if (b->closed) { pthread_mutex_unlock(&b->m); return false; }
    ring_put(b, v);
    if (wake_all) scv_broadcast(&b->one); else scv_signal(&b->one);
    pthread_mutex_unlock(&b->m);
    return true;
}
static bool v2_get_impl(bb_t *b, int *out, bool wake_all) {
    pthread_mutex_lock(&b->m);
    while (b->count == 0 && !b->closed) wait_one(b);
    if (b->count == 0) { pthread_mutex_unlock(&b->m); return false; }
    *out = ring_get(b);
    if (wake_all) scv_broadcast(&b->one); else scv_signal(&b->one);
    pthread_mutex_unlock(&b->m);
    return true;
}
static bool v2_put(bb_t *b, int v)     { return v2_put_impl(b, v, false); }
static bool v2_get(bb_t *b, int *out)  { return v2_get_impl(b, out, false); }
static bool v2b_put(bb_t *b, int v)    { return v2_put_impl(b, v, true); }
static bool v2b_get(bb_t *b, int *out) { return v2_get_impl(b, out, true); }

typedef struct {
    const char *name;
    bool (*put)(bb_t *, int);
    bool (*get)(bb_t *, int *);
    void (*wake_consumers)(bb_t *, bool all);   /* 하네스가 "생산자" 역할로 소비자를 깨울 때 (m 보유) */
} variant_t;
static void wake_fill(bb_t *b, bool all) { if (all) pthread_cond_broadcast(&b->fill); else pthread_cond_signal(&b->fill); }
static void wake_one_cv(bb_t *b, bool all) { if (all) scv_broadcast(&b->one); else scv_signal(&b->one); }
static void wake_one_cv_all(bb_t *b, bool all) { (void)all; scv_broadcast(&b->one); }
static const variant_t V1   = { "v1 (if, CV 1)",          v1_put,  v1_get,  wake_one_cv };
static const variant_t V2   = { "v2 (while, CV 1)",       v2_put,  v2_get,  wake_one_cv };
static const variant_t V2B  = { "v2b (while, CV 1, bcast)", v2b_put, v2b_get, wake_one_cv_all };
static const variant_t MINE = { "mine (while, CV 2)",     bb_put,  bb_get,  wake_fill };

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static void skip(int n, const char *what) {        /* 단위 테스트가 실패하면 스트레스는 건너뛴다(=실패로 셈) */
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

#define TMO_MS 2000
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
/* cond 가 참이 될 때까지(또는 ms 밀리초) 1 ms 간격으로 b->m 을 잡고 확인한다. */
#define WAIT_FOR(bp, ms, cond) do {                                          \
        for (int t_ = 0; ; t_++) {                                           \
            pthread_mutex_lock(&(bp)->m); bool ok_ = (cond);                 \
            pthread_mutex_unlock(&(bp)->m);                                  \
            if (ok_ || t_ >= (ms)) break;                                    \
            sleep_ms(1);                                                     \
        }                                                                    \
    } while (0)

/* 시나리오에 등장하는 스레드 ("배우"). done/got/ngot 는 b->m 으로 보호. */
typedef struct {
    bb_t *b; const variant_t *v;
    const int *items; int nitems;   /* 생산자: 넣을 값 */
    int got[8]; int ngot;           /* 소비자: 받은 값 */
    bool done;
    pthread_t th;
} actor_t;

static void *consumer_once(void *arg) {         /* get 한 번 */
    actor_t *a = arg; int v = 0;
    bool r = a->v->get(a->b, &v);
    pthread_mutex_lock(&a->b->m);
    if (r) a->got[a->ngot++] = v;
    a->done = true;
    pthread_mutex_unlock(&a->b->m);
    return NULL;
}
static void *consumer_loop(void *arg) {         /* get 이 false 일 때까지 */
    actor_t *a = arg; int v = 0;
    while (a->v->get(a->b, &v)) {
        pthread_mutex_lock(&a->b->m);
        if (a->ngot < 8) a->got[a->ngot++] = v;
        pthread_mutex_unlock(&a->b->m);
    }
    pthread_mutex_lock(&a->b->m); a->done = true; pthread_mutex_unlock(&a->b->m);
    return NULL;
}
static void *producer(void *arg) {
    actor_t *a = arg;
    for (int i = 0; i < a->nitems; i++)
        if (!a->v->put(a->b, a->items[i])) break;
    pthread_mutex_lock(&a->b->m); a->done = true; pthread_mutex_unlock(&a->b->m);
    return NULL;
}
static void start(actor_t *a, bb_t *b, const variant_t *v, void *(*fn)(void *)) {
    a->b = b; a->v = v;
    pthread_create(&a->th, NULL, fn, a);
}
/* 하네스 구조대: 닫고 모든 CV 를 broadcast. 그래도 안 끝나는 스레드는 detach (프로세스 종료 시 정리). */
static int g_stuck;
static void finish(bb_t *b, actor_t **as, int n) {
    pthread_mutex_lock(&b->m);
    b->closed = true;
    b->one.held = false; scv_broadcast(&b->one);
    pthread_cond_broadcast(&b->fill); pthread_cond_broadcast(&b->empty);
    pthread_mutex_unlock(&b->m);
    for (int i = 0; i < n; i++) {
        WAIT_FOR(b, TMO_MS, as[i]->done);
        pthread_mutex_lock(&b->m); bool d = as[i]->done; pthread_mutex_unlock(&b->m);
        if (d) pthread_join(as[i]->th, NULL); else { pthread_detach(as[i]->th); g_stuck++; }
    }
    if (g_stuck == 0) bb_destroy(b);
}

/* ---------- Part B 시나리오: 결과 분류 ---------- */
enum { OUT_OK, OUT_UNDERFLOW, OUT_ALL_ASLEEP, OUT_WRONG };
static const char *const OUT_NAME[] = { "OK", "UNDERFLOW", "ALL_ASLEEP", "WRONG" };

/* S1 "가로채기" (OSTEP 그림 30.7): C1 이 잠든 사이 P 가 넣고 signal, C1 이 돌기 전에 C2 가 가져간다.
 * S2 "가짜 깨움": C1 이 잠든 사이 아무 상태 변화 없이 깨운다 (spurious wakeup 과 같은 상황). */
static int scen_wake_empty(const variant_t *v, bool spurious) {
    bb_t b; bb_init(&b, 1);
    actor_t c1 = {0};
    start(&c1, &b, v, consumer_once);
    WAIT_FOR(&b, TMO_MS, b.waiting == 1 || c1.done);         /* ① C1: 빈 버퍼 → 잠듦 */
    pthread_mutex_lock(&b.m);
    bool slept = (b.waiting == 1);
    if (slept && !spurious) {
        ring_put(&b, 42); v->wake_consumers(&b, false);       /* ② P: 넣고 signal → C1 은 Ready */
        (void)ring_get(&b);                                   /* ③ C2: C1 보다 먼저 락을 잡아 42 를 가져감 */
    } else if (slept) {
        v->wake_consumers(&b, true);                          /* ② 상태 변화 없이 깨움 */
    }
    pthread_mutex_unlock(&b.m);
    WAIT_FOR(&b, TMO_MS, c1.done || b.waits >= 2);           /* ④ C1 실행: 다시 잠들거나, 빈 버퍼에서 꺼내거나 */
    if (slept) (void)v->put(&b, 43);                          /* ⑤ 진짜 항목 */
    WAIT_FOR(&b, TMO_MS, c1.done);
    pthread_mutex_lock(&b.m);
    int out = b.underflow ? OUT_UNDERFLOW
            : (slept && c1.done && c1.ngot == 1 && c1.got[0] == 43) ? OUT_OK : OUT_WRONG;
    pthread_mutex_unlock(&b.m);
    actor_t *as[] = { &c1 };
    finish(&b, as, 1);
    return out;
}

/* S3 "엉뚱한 깨움" (OSTEP 그림 30.9): 용량 1, 소비자 C1·C2 가 먼저 잠들고 생산자 P 가 1, 2 를 넣는다.
 * forced: scv(FIFO + held) 로 C1 의 signal 이 P 대신 C2 에게 가게 만든다. CV 2개 버전은 강제할 필요가 없다. */
static int scen_wrong_wakeup(const variant_t *v, bool forced) {
    static const int items[2] = { 1, 2 };
    bb_t b; bb_init(&b, 1);
    b.one.held = forced;
    actor_t c1 = {0}, c2 = {0}, p = {0};
    p.items = items; p.nitems = 2;
    start(&c1, &b, v, consumer_loop);
    WAIT_FOR(&b, TMO_MS, b.waiting == 1 || c1.done);         /* C1 잠듦 (번호표 0) */
    start(&c2, &b, v, consumer_loop);
    WAIT_FOR(&b, TMO_MS, b.waiting == 2 || c2.done);         /* C2 잠듦 (번호표 1) */
    start(&p, &b, v, producer);
    int out = OUT_OK;
    if (forced) {
        WAIT_FOR(&b, TMO_MS, b.waits >= 3);                   /* P: 1 넣고 signal(C1 Ready) → 2 는 꽉 차서 잠듦 */
        pthread_mutex_lock(&b.m); scv_run_one(&b.one); pthread_mutex_unlock(&b.m);
        WAIT_FOR(&b, TMO_MS, b.waits >= 4);                   /* C1: 1 꺼내고 signal → FIFO 맨 앞 = C2. 다시 잠듦 */
        pthread_mutex_lock(&b.m); scv_run_one(&b.one); pthread_mutex_unlock(&b.m);
        WAIT_FOR(&b, TMO_MS, b.waits >= 5);                   /* C2: 실행 */
        pthread_mutex_lock(&b.m);
        bool nobody_ready = b.one.woken <= b.one.runnable;
        if (b.underflow) out = OUT_UNDERFLOW;
        else if (b.waiting == 3 && nobody_ready && b.count == 0 && !p.done) out = OUT_ALL_ASLEEP;
        b.one.held = false; pthread_cond_broadcast(&b.one.cv);   /* 아니면 보류를 풀고 끝까지 돌려 본다 */
        pthread_mutex_unlock(&b.m);
    }
    if (out == OUT_OK) {
        WAIT_FOR(&b, TMO_MS, p.done && (c1.ngot + c2.ngot >= 2 || (c1.done && c2.done)));
        WAIT_FOR(&b, TMO_MS, b.waiting == 2 || (c1.done && c2.done));   /* 받은 뒤 다시 잠들 때까지 */
        pthread_mutex_lock(&b.m);
        int sum = 0;
        for (int i = 0; i < c1.ngot; i++) sum += c1.got[i];
        for (int i = 0; i < c2.ngot; i++) sum += c2.got[i];
        if (b.underflow) out = OUT_UNDERFLOW;
        else if (!(p.done && c1.ngot + c2.ngot == 2 && sum == 3 && b.overflow == 0)) out = OUT_WRONG;
        pthread_mutex_unlock(&b.m);
    }
    actor_t *as[] = { &c1, &c2, &p };
    finish(&b, as, 3);
    return out;
}

/* ---------- Part A/C 스트레스 ---------- */
#define SEQ_MUL   1000000
#define STRESS_MS 30000
typedef struct {
    bb_t *b; int id, nprod, per, batch;
    uint8_t *seen;                  /* [nprod*per], 서로 다른 칸만 쓰므로 레이스 아님 */
    long n, bad, batches;
    bool done;                      /* b->m 으로 보호 */
} sargs_t;
static void mark_done(sargs_t *s) { pthread_mutex_lock(&s->b->m); s->done = true; pthread_mutex_unlock(&s->b->m); }
static void *stress_prod(void *arg) {
    sargs_t *s = arg;
    for (int i = 0; i < s->per; i++) (void)bb_put(s->b, s->id * SEQ_MUL + i);
    mark_done(s);
    return NULL;
}
/* 하네스 쪽 닫기 (Part A 가 Part B 의 bb_close 에 기대지 않도록): closed + 모든 대기자 깨우기 */
static void hclose(bb_t *b) {
    pthread_mutex_lock(&b->m);
    b->closed = true;
    pthread_cond_broadcast(&b->fill); pthread_cond_broadcast(&b->empty);
    pthread_mutex_unlock(&b->m);
}
static int ndone(const sargs_t *s, int n) { int k = 0; for (int i = 0; i < n; i++) k += s[i].done; return k; }
/* 제한 시간 안에 끝나면 join, 아니면 hclose 로 구조 → 그래도 안 끝나면 detach. 모두 제때 끝났으면 true. */
static bool join_timed(bb_t *b, pthread_t *th, sargs_t *s, int n) {
    WAIT_FOR(b, STRESS_MS, ndone(s, n) == n);
    pthread_mutex_lock(&b->m); bool ok = ndone(s, n) == n; pthread_mutex_unlock(&b->m);
    if (!ok) { printf("  (stress timeout: rescuing with close)\n"); hclose(b); WAIT_FOR(b, TMO_MS, ndone(s, n) == n); }
    for (int i = 0; i < n; i++) {
        pthread_mutex_lock(&b->m); bool d = s[i].done; pthread_mutex_unlock(&b->m);
        if (d) pthread_join(th[i], NULL); else { pthread_detach(th[i]); g_stuck++; }
    }
    return ok;
}
static void record(sargs_t *s, int v, int *last) {
    int p = v / SEQ_MUL, q = v % SEQ_MUL;
    if (v < 0 || p >= s->nprod || q >= s->per || q <= last[p]) { s->bad++; return; }   /* 생산자별 순서 역행 */
    last[p] = q; s->seen[p * s->per + q]++; s->n++;
}
static void *stress_cons(void *arg) {
    sargs_t *s = arg; int last[8], v = 0;
    for (int i = 0; i < 8; i++) last[i] = -1;
    while (bb_get(s->b, &v)) record(s, v, last);
    mark_done(s);
    return NULL;
}
static void *logger(void *arg) {                /* Part C: 배치로 꺼내 "플래시 페이지" 에 쓴다 */
    sargs_t *s = arg; int last[8], page[16];
    for (int i = 0; i < 8; i++) last[i] = -1;
    int n;
    while ((n = bb_get_batch(s->b, page, s->batch)) > 0) {
        s->batches++;
        for (int i = 0; i < n; i++) record(s, page[i], last);
    }
    mark_done(s);
    return NULL;
}
/* seen[] 이 전부 1 인가 (누락 0, 중복 0) */
static bool all_once(const uint8_t *seen, int n) {
    for (int i = 0; i < n; i++) if (seen[i] != 1) return false;
    return true;
}
static void stress_pc(int np, int nc, int cap, int per) {
    bb_t b; bb_init(&b, cap);
    uint8_t *seen = calloc((size_t)(np * per), 1);
    pthread_t tp[8], tc[8]; sargs_t sp[8], sc[8];
    for (int i = 0; i < nc; i++) {
        sc[i] = (sargs_t){ &b, i, np, per, 0, seen, 0, 0, 0, false };
        pthread_create(&tc[i], NULL, stress_cons, &sc[i]);
    }
    for (int i = 0; i < np; i++) {
        sp[i] = (sargs_t){ &b, i, np, per, 0, seen, 0, 0, 0, false };
        pthread_create(&tp[i], NULL, stress_prod, &sp[i]);
    }
    bool ok = join_timed(&b, tp, sp, np);
    hclose(&b);                                              /* 다 넣었다 → 소비자는 남은 걸 비우고 false */
    ok = join_timed(&b, tc, sc, nc) && ok;
    long total = 0, bad = 0;
    for (int i = 0; i < nc; i++) { total += sc[i].n; bad += sc[i].bad; }
    printf("Part A stress P=%d C=%d cap=%d: items=%ld waits=%ld\n", np, nc, cap, total, b.waits);
    CHECK(total == (long)np * per && bad == 0 && all_once(seen, np * per));   /* 정확히 한 번 + 생산자별 FIFO */
    CHECK(ok && b.underflow == 0 && b.overflow == 0 && b.count == 0);   /* 제한 시간 안에 끝남 */
    free(seen);
    if (g_stuck == 0) bb_destroy(&b);
}

int main(void) {
    bb_t b; int x = 0, y = 0, z = 0;
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---------------- Part A: 단위 ---------------- */
    bb_init(&b, 3);
    CHECK(bb_put(&b, 1) && bb_put(&b, 2) && bb_put(&b, 3));
    CHECK(b.count == 3);
    CHECK(bb_get(&b, &x) && bb_get(&b, &y) && bb_get(&b, &z) && x == 1 && y == 2 && z == 3);   /* FIFO */
    CHECK(bb_put(&b, 4) && bb_get(&b, &x) && x == 4);
    CHECK(bb_put(&b, 5) && bb_put(&b, 6) && bb_put(&b, 7) &&
          bb_get(&b, &x) && bb_get(&b, &y) && bb_get(&b, &z) && x == 5 && y == 6 && z == 7);  /* wrap-around */
    CHECK(b.waits == 0 && b.count == 0);                      /* 필요 없을 때는 잠들지 않는다 */
    bb_destroy(&b);
    int unit_a = g_total - g_pass;

    /* 꽉 차면 생산자가 잠들고, 하나 꺼내면 깨어나서 넣는다 */
    {
        static const int it[1] = { 11 };
        bb_init(&b, 1); (void)bb_put(&b, 10);
        actor_t p = {0}; p.items = it; p.nitems = 1;
        start(&p, &b, &MINE, producer);
        WAIT_FOR(&b, TMO_MS, b.waiting == 1 || p.done);
        pthread_mutex_lock(&b.m); bool blocked = b.waiting == 1 && !p.done; pthread_mutex_unlock(&b.m);
        CHECK(blocked);
        x = y = 0;
        bool g1 = bb_get(&b, &x);
        WAIT_FOR(&b, TMO_MS, p.done);
        bool g2 = bb_get(&b, &y);
        CHECK(g1 && g2 && x == 10 && y == 11);
        actor_t *as[] = { &p }; finish(&b, as, 1);
    }
    /* 비면 소비자가 잠들고, 넣으면 깨어나서 꺼낸다 */
    {
        bb_init(&b, 2);
        actor_t c = {0};
        start(&c, &b, &MINE, consumer_once);
        WAIT_FOR(&b, TMO_MS, b.waiting == 1 || c.done);
        pthread_mutex_lock(&b.m); bool blocked = b.waiting == 1 && !c.done; pthread_mutex_unlock(&b.m);
        CHECK(blocked);
        (void)bb_put(&b, 20);
        WAIT_FOR(&b, TMO_MS, c.done);
        pthread_mutex_lock(&b.m); bool ok = c.done && c.ngot == 1 && c.got[0] == 20; pthread_mutex_unlock(&b.m);
        CHECK(ok);
        actor_t *as[] = { &c }; finish(&b, as, 1);
    }

    /* ---------------- Part A: 스트레스 ---------------- */
    if (unit_a == 0) {
        stress_pc(1, 1, 1, 20000);
        stress_pc(4, 4, 4, 20000);
        stress_pc(8, 2, 8, 10000);
        stress_pc(2, 8, 2, 20000);
    } else skip(8, "Part A");

    /* ---------------- Part B: 강제 스케줄 3종 × 버전 4개 ---------------- */
    {
        const variant_t *vs[4] = { &V1, &V2, &V2B, &MINE };
        int r[3][4];
        for (int i = 0; i < 4; i++) {
            r[0][i] = scen_wake_empty(vs[i], false);
            r[1][i] = scen_wake_empty(vs[i], true);
            r[2][i] = scen_wrong_wakeup(vs[i], vs[i] != &MINE);
        }
        const char *sn[3] = { "S1 steal (30.7)", "S2 spurious", "S3 wrong wakeup (30.9)" };
        for (int s = 0; s < 3; s++)
            printf("%-24s v1=%-10s v2=%-10s v2b=%-10s mine=%s\n", sn[s],
                   OUT_NAME[r[s][0]], OUT_NAME[r[s][1]], OUT_NAME[r[s][2]], OUT_NAME[r[s][3]]);
        CHECK(r[0][0] == OUT_UNDERFLOW && r[1][0] == OUT_UNDERFLOW && r[2][0] == OUT_UNDERFLOW); /* if: 다 깨짐 */
        CHECK(r[0][1] == OUT_OK && r[1][1] == OUT_OK);                                         /* while 은 S1·S2 해결 */
        CHECK(r[2][1] == OUT_ALL_ASLEEP);                                                      /* CV 1개: 모두 잠듦 */
        CHECK(r[0][2] == OUT_OK && r[1][2] == OUT_OK && r[2][2] == OUT_OK);                    /* CV 1개 + broadcast */
        CHECK(r[0][3] == OUT_OK);                                                              /* 내 버전 */
        CHECK(r[1][3] == OUT_OK);
        CHECK(r[2][3] == OUT_OK);
    }

    /* ---------------- Part B: bb_close ---------------- */
    {
        bb_init(&b, 2);                                       /* 소비자 3명이 잠든 상태에서 close */
        actor_t c[3] = {{0}};
        for (int i = 0; i < 3; i++) start(&c[i], &b, &MINE, consumer_once);
        WAIT_FOR(&b, TMO_MS, b.waiting == 3 || (c[0].done && c[1].done && c[2].done));
        pthread_mutex_lock(&b.m); bool all_slept = b.waiting == 3; pthread_mutex_unlock(&b.m);
        CHECK(all_slept);
        bb_close(&b);
        WAIT_FOR(&b, TMO_MS, c[0].done && c[1].done && c[2].done);
        pthread_mutex_lock(&b.m);
        bool woke = c[0].done && c[1].done && c[2].done && c[0].ngot + c[1].ngot + c[2].ngot == 0;
        pthread_mutex_unlock(&b.m);
        CHECK(woke);                                          /* 셋 다 깨어나 false (signal 이면 하나만) */
        actor_t *as[] = { &c[0], &c[1], &c[2] }; finish(&b, as, 3);
    }
    {
        static const int it[2][1] = { { 100 }, { 101 } };
        bb_init(&b, 1); (void)bb_put(&b, 7);                  /* 생산자 2명이 꽉 찬 버퍼에서 잠든 상태에서 close */
        actor_t p[2] = {{0}};
        for (int i = 0; i < 2; i++) { p[i].items = it[i]; p[i].nitems = 1; start(&p[i], &b, &MINE, producer); }
        WAIT_FOR(&b, TMO_MS, b.waiting == 2 || (p[0].done && p[1].done));
        bb_close(&b);
        WAIT_FOR(&b, TMO_MS, p[0].done && p[1].done);
        pthread_mutex_lock(&b.m); bool ok = p[0].done && p[1].done && b.count == 1 && b.buf[b.head] == 7;
        pthread_mutex_unlock(&b.m);
        CHECK(ok);                                            /* 둘 다 깨어나 넣지 않고 돌아옴 */
        actor_t *as[] = { &p[0], &p[1] }; finish(&b, as, 2);
    }
    {
        bb_init(&b, 4);                                       /* 닫혀도 남은 항목은 다 준다 */
        (void)bb_put(&b, 1); (void)bb_put(&b, 2);
        bb_close(&b);
        x = y = z = 0;
        CHECK(!bb_put(&b, 3));
        CHECK(bb_get(&b, &x) && bb_get(&b, &y) && x == 1 && y == 2 && !bb_get(&b, &z));
        bb_destroy(&b);
    }

    /* ---------------- Part C: 배치 ---------------- */
    int out[16], fails_before_c = g_total - g_pass;
    bb_init(&b, 8);
    for (int i = 1; i <= 5; i++) (void)bb_put(&b, i);
    int n1 = bb_get_batch(&b, out, 3);
    CHECK(n1 == 3 && out[0] == 1 && out[1] == 2 && out[2] == 3);
    int n2 = bb_get_batch(&b, out, 16);
    CHECK(n2 == 2 && out[0] == 4 && out[1] == 5 && b.count == 0);
    bb_close(&b);
    CHECK(bb_get_batch(&b, out, 16) == 0);
    bb_destroy(&b);
    int unit_c = (g_total - g_pass) - fails_before_c;

    {   /* 꽉 찬 버퍼(용량 4)에 생산자 3명이 잠들어 있다. 한 번에 4개를 꺼내면 셋 다 들어와야 한다. */
        static const int it[3][1] = { { 100 }, { 101 }, { 102 } };
        bb_init(&b, 4);
        for (int i = 1; i <= 4; i++) (void)bb_put(&b, i);
        actor_t p[3] = {{0}};
        for (int i = 0; i < 3; i++) { p[i].items = it[i]; p[i].nitems = 1; start(&p[i], &b, &MINE, producer); }
        WAIT_FOR(&b, TMO_MS, b.waiting == 3 || (p[0].done && p[1].done && p[2].done));
        int n = bb_get_batch(&b, out, 4);
        WAIT_FOR(&b, TMO_MS, p[0].done && p[1].done && p[2].done);
        pthread_mutex_lock(&b.m); bool all_in = p[0].done && p[1].done && p[2].done && b.count == 3;
        pthread_mutex_unlock(&b.m);
        CHECK(n == 4 && out[0] == 1 && out[3] == 4);
        CHECK(all_in);                                        /* signal 한 번이면 1명만 들어온다 */
        actor_t *as[] = { &p[0], &p[1], &p[2] }; finish(&b, as, 3);
    }
    if (unit_c == 0) {   /* 센서 2개 → 로거 1개 */
        enum { NS = 2, PER = 30000 };
        bb_init(&b, 32);
        uint8_t *seen = calloc(NS * PER, 1);
        sargs_t sp[NS], lg = { &b, 0, NS, PER, 16, seen, 0, 0, 0, false };
        pthread_t ts[NS], tl;
        pthread_create(&tl, NULL, logger, &lg);
        for (int i = 0; i < NS; i++) {
            sp[i] = (sargs_t){ &b, i, NS, PER, 0, seen, 0, 0, 0, false };
            pthread_create(&ts[i], NULL, stress_prod, &sp[i]);
        }
        bool ok = join_timed(&b, ts, sp, NS);
        bb_close(&b);
        ok = join_timed(&b, &tl, &lg, 1) && ok;
        printf("Part C logger: samples=%ld batches=%ld (avg %.1f per batch)\n",
               lg.n, lg.batches, lg.batches ? (double)lg.n / (double)lg.batches : 0.0);
        CHECK(lg.n == NS * PER && lg.bad == 0 && all_once(seen, NS * PER));
        CHECK(ok && lg.batches >= (NS * PER + 15) / 16 && b.underflow == 0 && b.overflow == 0);
        free(seen);
        if (g_stuck == 0) bb_destroy(&b);
    } else skip(2, "Part C");

    if (g_stuck) printf("(%d thread(s) never returned — detached)\n", g_stuck);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
