/* BUILD: -pthread */
/* OS-M09 추가 연습 — 한 번에 검증
 *   연습 1  rwl_tryrdlock / rwl_trywrlock (정책을 따르는 try: writer-pref 는 writer 가 기다리면 tryrdlock = EBUSY)
 *   연습 2  rwl_wrlock_timed — writer 가 시간 초과로 포기할 때 "자기 때문에" 줄 선 reader 를 깨워야 한다
 *   연습 3  rwl_downgrade — W 를 쥔 채 R 로 내려가기 (다른 writer 가 끼어들 틈 없이)
 *   연습 4  OSTEP 그림 31.13 의 세마포어 rwlock (맥은 sem_init 이 없어 mutex+cond 세마포어로) = reader 선호 확인
 * 본문과 같은 원칙: 판정은 카운터 · 상태로만, 시간은 안전장치(WAIT_MS)와 연습 2 의 타임아웃 값에만.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define WAIT_MS 1000

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}
static void sleep_us(unsigned us) { struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L }; nanosleep(&ts, NULL); }
static void deadline_after(struct timespec *ts, int ms) {          /* cond_timedwait 는 CLOCK_REALTIME 절대 시각 */
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* ===== 본문 Part A 의 rwl_t 축약판 (reader-pref / writer-pref) ===== */
enum { RW_READER_PREF, RW_WRITER_PREF };
typedef struct {
    pthread_mutex_t m; pthread_cond_t readers_ok, writer_ok;
    int policy, active_readers, waiting_readers, waiting_writers;
    bool writer_active;
} rwl_t;
static void rwl_init(rwl_t *rw, int policy) {
    pthread_mutex_init(&rw->m, NULL); pthread_cond_init(&rw->readers_ok, NULL); pthread_cond_init(&rw->writer_ok, NULL);
    rw->policy = policy; rw->active_readers = rw->waiting_readers = rw->waiting_writers = 0; rw->writer_active = false;
}
static void rwl_destroy(rwl_t *rw) { pthread_mutex_destroy(&rw->m); pthread_cond_destroy(&rw->readers_ok); pthread_cond_destroy(&rw->writer_ok); }
static bool reader_blocked(const rwl_t *rw) { return rw->writer_active || (rw->policy == RW_WRITER_PREF && rw->waiting_writers > 0); }
static void rwl_rdlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    rw->waiting_readers++;
    while (reader_blocked(rw)) pthread_cond_wait(&rw->readers_ok, &rw->m);
    rw->waiting_readers--; rw->active_readers++;
    pthread_mutex_unlock(&rw->m);
}
static void rwl_rdunlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    if (--rw->active_readers == 0 && rw->waiting_writers > 0) pthread_cond_signal(&rw->writer_ok);
    pthread_mutex_unlock(&rw->m);
}
static void rwl_wrlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    rw->waiting_writers++;
    while (rw->writer_active || rw->active_readers > 0) pthread_cond_wait(&rw->writer_ok, &rw->m);
    rw->waiting_writers--; rw->writer_active = true;
    pthread_mutex_unlock(&rw->m);
}
static void wake_after_writer_leaves(rwl_t *rw) {        /* 본문 A4 의 깨우기 규칙 (m 보유 상태에서) */
    bool readers_first = (rw->policy == RW_READER_PREF) ? rw->waiting_readers > 0 : rw->waiting_writers == 0;
    if (readers_first && rw->waiting_readers > 0) pthread_cond_broadcast(&rw->readers_ok);
    else if (rw->waiting_writers > 0) pthread_cond_signal(&rw->writer_ok);
}
static void rwl_wrunlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    rw->writer_active = false;
    wake_after_writer_leaves(rw);
    pthread_mutex_unlock(&rw->m);
}
typedef struct { int ar, wr, ww; bool w; } snap_t;
static snap_t snap(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    snap_t s = { rw->active_readers, rw->waiting_readers, rw->waiting_writers, rw->writer_active };
    pthread_mutex_unlock(&rw->m);
    return s;
}

/* ===== 연습 1: try ===== */
static int rwl_tryrdlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    int r = reader_blocked(rw) ? EBUSY : 0;          /* 정책 그대로: writer-pref 면 기다리는 writer 가 있어도 EBUSY */
    if (r == 0) rw->active_readers++;
    pthread_mutex_unlock(&rw->m);
    return r;
}
static int rwl_trywrlock(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    int r = (rw->writer_active || rw->active_readers > 0) ? EBUSY : 0;
    if (r == 0) rw->writer_active = true;
    pthread_mutex_unlock(&rw->m);
    return r;
}

/* ===== 연습 2: 시간 제한 writer ===== */
static int rwl_wrlock_timed(rwl_t *rw, int max_ms) {
    struct timespec ts; deadline_after(&ts, max_ms);
    pthread_mutex_lock(&rw->m);
    rw->waiting_writers++;
    while (rw->writer_active || rw->active_readers > 0) {
        if (pthread_cond_timedwait(&rw->writer_ok, &rw->m, &ts) == ETIMEDOUT &&
            (rw->writer_active || rw->active_readers > 0)) {
            rw->waiting_writers--;
            /* 핵심: writer-pref 에서 reader 들은 "writer 가 기다린다" 는 이유만으로 잠들어 있을 수 있다.
             * 그 이유가 사라졌으니 깨워야 한다. 안 깨우면 다음 broadcast 까지 영원히 잔다 (lost wakeup). */
            if (rw->waiting_writers == 0 && !rw->writer_active && rw->waiting_readers > 0)
                pthread_cond_broadcast(&rw->readers_ok);
            pthread_mutex_unlock(&rw->m);
            return ETIMEDOUT;
        }
    }
    rw->waiting_writers--; rw->writer_active = true;
    pthread_mutex_unlock(&rw->m);
    return 0;
}

/* ===== 연습 3: downgrade (Linux 커널 downgrade_write 와 같은 생각) ===== */
static void rwl_downgrade(rwl_t *rw) {
    pthread_mutex_lock(&rw->m);
    rw->writer_active = false;
    rw->active_readers++;                               /* 놓기 전에 reader 로 등록 → writer 가 끼어들 틈이 없다 */
    if (rw->waiting_readers > 0 && !reader_blocked(rw)) pthread_cond_broadcast(&rw->readers_ok);
    pthread_mutex_unlock(&rw->m);
}

/* ===== 연습 4: OSTEP 그림 31.13 — 세마포어 2개로 만든 rwlock ===== */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; int value, waiters; } csem_t;
static void csem_init(csem_t *s, int v) { pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->cv, NULL); s->value = v; s->waiters = 0; }
static void csem_destroy(csem_t *s) { pthread_mutex_destroy(&s->m); pthread_cond_destroy(&s->cv); }
static void csem_wait(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->waiters++;
    while (s->value <= 0) pthread_cond_wait(&s->cv, &s->m);
    s->waiters--; s->value--;
    pthread_mutex_unlock(&s->m);
}
static void csem_post(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->value++;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->m);
}
static int csem_waiters(csem_t *s) { pthread_mutex_lock(&s->m); int w = s->waiters; pthread_mutex_unlock(&s->m); return w; }
typedef struct { csem_t lock, writelock; int readers; } ostep_rw_t;
static void ostep_init(ostep_rw_t *rw) { csem_init(&rw->lock, 1); csem_init(&rw->writelock, 1); rw->readers = 0; }
static void ostep_destroy(ostep_rw_t *rw) { csem_destroy(&rw->lock); csem_destroy(&rw->writelock); }
static void ostep_rdlock(ostep_rw_t *rw) {
    csem_wait(&rw->lock);
    if (++rw->readers == 1) csem_wait(&rw->writelock);  /* 첫 reader 가 writer 를 막는다 */
    csem_post(&rw->lock);
}
static void ostep_rdunlock(ostep_rw_t *rw) {
    csem_wait(&rw->lock);
    if (--rw->readers == 0) csem_post(&rw->writelock);  /* 마지막 reader 가 푼다 — 잡은 스레드와 다를 수 있다 (세마포어라 가능) */
    csem_post(&rw->lock);
}
static void ostep_wrlock(ostep_rw_t *rw) { csem_wait(&rw->writelock); }
static void ostep_wrunlock(ostep_rw_t *rw) { csem_post(&rw->writelock); }
static int ostep_readers(ostep_rw_t *rw) { csem_wait(&rw->lock); int r = rw->readers; csem_post(&rw->lock); return r; }

/* ===== 작은 스레드 도우미 ===== */
typedef struct { void (*fn)(void *); void *arg; atomic_int done; int ret; pthread_t th; } job_t;
static void *job_main(void *p) { job_t *j = p; j->fn(j->arg); atomic_store(&j->done, 1); return NULL; }
static void job_start(job_t *j, void (*fn)(void *), void *arg) { j->fn = fn; j->arg = arg; atomic_store(&j->done, 0); pthread_create(&j->th, NULL, job_main, j); }
static bool job_wait_done(job_t *j) {
    double t0 = now_ms();
    while (!atomic_load(&j->done)) { if (now_ms() - t0 > WAIT_MS) return false; sleep_us(200); }
    pthread_join(j->th, NULL);
    return true;
}
static bool wait_snap(rwl_t *rw, int min_wr, int min_ww) {      /* 줄 선 reader/writer 수가 최소 이만큼 될 때까지 */
    double t0 = now_ms();
    for (;;) {
        snap_t s = snap(rw);
        if (s.wr >= min_wr && s.ww >= min_ww) return true;
        if (now_ms() - t0 > WAIT_MS) return false;
        sleep_us(200);
    }
}
static rwl_t *g_rw;
static ostep_rw_t *g_ost;
static atomic_int g_tryrd_ret, g_timed_ret, g_rd_in;
static void j_wrlock_then_unlock(void *a) { (void)a; rwl_wrlock(g_rw); rwl_wrunlock(g_rw); }
static void j_tryrd(void *a) { (void)a; int r = rwl_tryrdlock(g_rw); atomic_store(&g_tryrd_ret, r); if (r == 0) rwl_rdunlock(g_rw); }
static void j_timed_wr(void *a) { int r = rwl_wrlock_timed(g_rw, *(int *)a); atomic_store(&g_timed_ret, r); if (r == 0) rwl_wrunlock(g_rw); }
static void j_rd_hold(void *a) { (void)a; rwl_rdlock(g_rw); atomic_store(&g_rd_in, 1); }
static void j_ost_wr(void *a) { (void)a; ostep_wrlock(g_ost); ostep_wrunlock(g_ost); }
static void j_ost_rd(void *a) { (void)a; ostep_rdlock(g_ost); }

static void ex1_try(void) {
    printf("== 연습 1: tryrdlock / trywrlock ==\n");
    for (int pol = 0; pol < 2; pol++) {
        rwl_t rw; rwl_init(&rw, pol); g_rw = &rw;
        CHECK(rwl_tryrdlock(&rw) == 0 && rwl_tryrdlock(&rw) == 0 && rwl_trywrlock(&rw) == EBUSY);
        rwl_rdunlock(&rw); rwl_rdunlock(&rw);
        CHECK(rwl_trywrlock(&rw) == 0 && rwl_tryrdlock(&rw) == EBUSY && rwl_trywrlock(&rw) == EBUSY);
        rwl_wrunlock(&rw);
        /* main 이 R 을 쥐고, writer 가 줄을 선 상태에서 다른 스레드가 tryrdlock */
        rwl_rdlock(&rw);
        job_t w; job_start(&w, j_wrlock_then_unlock, NULL);
        bool queued = wait_snap(&rw, 0, 1);
        job_t t; job_start(&t, j_tryrd, NULL); bool tdone = job_wait_done(&t);
        int expect = pol == RW_WRITER_PREF ? EBUSY : 0;
        printf("  [%s] tryrdlock while a writer waits -> %s\n", pol ? "writer-pref" : "reader-pref",
               atomic_load(&g_tryrd_ret) == EBUSY ? "EBUSY" : "0");
        CHECK(queued && tdone && atomic_load(&g_tryrd_ret) == expect);
        rwl_rdunlock(&rw);
        CHECK(job_wait_done(&w));
        rwl_destroy(&rw);
    }
}

static void ex2_timed(void) {
    printf("== 연습 2: timed writer gives up -> queued reader must wake ==\n");
    rwl_t rw; rwl_init(&rw, RW_WRITER_PREF); g_rw = &rw;
    atomic_store(&g_rd_in, 0);
    rwl_rdlock(&rw);                                    /* main = 오래 읽는 reader */
    int ms = 100;
    job_t w; job_start(&w, j_timed_wr, &ms);
    bool wq = wait_snap(&rw, 0, 1);
    job_t r; job_start(&r, j_rd_hold, NULL);            /* writer 때문에 줄을 선다 */
    bool rq = wait_snap(&rw, 1, 1);
    bool wdone = job_wait_done(&w);                     /* 100 ms 뒤 포기 */
    bool rdone = job_wait_done(&r);                     /* 깨워 주지 않으면 여기서 WAIT_MS 초과 */
    printf("  timed wrlock -> %s, queued reader admitted=%d\n",
           atomic_load(&g_timed_ret) == ETIMEDOUT ? "ETIMEDOUT" : "?", atomic_load(&g_rd_in));
    CHECK(wq && rq && wdone && atomic_load(&g_timed_ret) == ETIMEDOUT);
    CHECK(rdone && atomic_load(&g_rd_in) == 1);
    snap_t s = snap(&rw);
    CHECK(s.ar == 2 && s.ww == 0 && s.wr == 0 && !s.w);
    rwl_rdunlock(&rw); rwl_rdunlock(&rw);               /* reader 스레드의 R 은 main 이 대신 푼다 (소유자 없는 카운터) */
    ms = 1000;
    job_start(&w, j_timed_wr, &ms);
    CHECK(job_wait_done(&w) && atomic_load(&g_timed_ret) == 0);   /* 비어 있으면 바로 획득 */
    rwl_destroy(&rw);
}

static void ex3_downgrade(void) {
    printf("== 연습 3: downgrade W -> R ==\n");
    for (int pol = 0; pol < 2; pol++) {
        rwl_t rw; rwl_init(&rw, pol); g_rw = &rw;
        atomic_store(&g_rd_in, 0);
        rwl_wrlock(&rw);
        job_t r; job_start(&r, j_rd_hold, NULL);
        bool rq = wait_snap(&rw, 1, 0);
        job_t w; job_start(&w, j_wrlock_then_unlock, NULL);
        bool wq = wait_snap(&rw, 1, 1);
        rwl_downgrade(&rw);                              /* 이제 main 은 reader */
        snap_t s;
        bool r_done;
        if (pol == RW_READER_PREF) {
            r_done = job_wait_done(&r);                  /* reader-pref: 줄 선 reader 도 같이 들어온다 */
            s = snap(&rw);
            CHECK(rq && wq && r_done && s.ar == 2 && !s.w && s.ww == 1);
        } else {
            sleep_us(20000);                             /* 틀린 구현이라면 reader 가 들어올 시간. 맞으면 시간과 무관하게 줄에 남는다 */
            s = snap(&rw);
            CHECK(rq && wq && s.ar == 1 && !s.w && s.ww == 1 && s.wr == 1);   /* writer 가 먼저 */
        }
        printf("  [%s] after downgrade: active_readers=%d waiting_writers=%d waiting_readers=%d\n",
               pol ? "writer-pref" : "reader-pref", s.ar, s.ww, s.wr);
        rwl_rdunlock(&rw);                               /* main 의 R */
        if (pol == RW_READER_PREF) rwl_rdunlock(&rw);    /* reader 스레드의 R (끝난 스레드 대신) */
        CHECK(job_wait_done(&w));                        /* writer 가 들어갔다 나감 */
        if (pol == RW_WRITER_PREF) { CHECK(job_wait_done(&r)); rwl_rdunlock(&rw); }
        CHECK(snap(&rw).ar == 0 && !snap(&rw).w);
        rwl_destroy(&rw);
    }
}

static void ex4_ostep(void) {
    printf("== 연습 4: OSTEP Fig 31.13 semaphore rwlock is reader-preferring ==\n");
    ostep_rw_t rw; ostep_init(&rw); g_ost = &rw;
    ostep_rdlock(&rw);                                   /* main = R1 */
    job_t w; job_start(&w, j_ost_wr, NULL);
    double t0 = now_ms();
    while (csem_waiters(&rw.writelock) < 1 && now_ms() - t0 < WAIT_MS) sleep_us(200);
    bool wq = csem_waiters(&rw.writelock) == 1;
    job_t r2; job_start(&r2, j_ost_rd, NULL);            /* R2: writer 가 기다리는데도 들어온다 */
    bool r2in = job_wait_done(&r2);
    printf("  writer queued=%d, new reader admitted while writer waits=%d, readers=%d\n", wq, r2in, ostep_readers(&rw));
    CHECK(wq && r2in && ostep_readers(&rw) == 2);
    ostep_rdunlock(&rw);                                 /* R1 퇴장: 아직 R2 가 있다 */
    CHECK(atomic_load(&w.done) == 0 && csem_waiters(&rw.writelock) == 1);
    ostep_rdunlock(&rw);                                 /* R2 퇴장 (R2 스레드 대신 main 이) → writer */
    CHECK(job_wait_done(&w));
    ostep_destroy(&rw);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    ex1_try();
    ex2_timed();
    ex3_downgrade();
    ex4_ostep();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
