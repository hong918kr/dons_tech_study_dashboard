// 02_condvar_queues.c  —  PRACTICE STUB (직접 채워넣기)
// 조건 변수 & 블로킹 큐 (Condition Variables & Blocking Queues)  —  Q11~Q20
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=02_condvar_queues
//   또는:     cc -std=c11 -Wall -Wextra -O1 -g -pthread problems/02_condvar_queues.c -o /tmp/vk02p && /tmp/vk02p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 블로킹/스레드 테스트는
//  전부 상한 타임아웃을 두어 hang 대신 FAIL 로 빠져나온다.)
//
// 이 세트가 Verkada 인터뷰의 1순위다. 리크루터 메일이 "thread safety"와
// "synchronization (mutex, condition variable)"을 못박았고, 그걸 한 문제로 확인하는
// 표준 문제가 bounded blocking queue다. GC31-E 게이트웨이에서 도어/센서/모뎀 I/O
// 스레드가 이벤트를 만들고 업로더 스레드가 LTE로 올리는 구조 그대로다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공용 시간 유틸 (제공됨 — 건드리지 말 것)
// ---------------------------------------------------------------------------
// CLOCK_MONOTONIC은 시스템 시계가 바뀌어도 뒤로 가지 않는다. 타임아웃/주기 계산은
// 반드시 monotonic으로.
// ===========================================================================
static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

// flag가 want가 될 때까지 최대 timeout_ms 대기 (상한이 있으므로 hang 없음).
static bool wait_flag(_Atomic int *flag, int want, int timeout_ms) {
    long deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (atomic_load(flag) == want) return true;
        sleep_ms(2);
    }
    return atomic_load(flag) == want;
}

// ===========================================================================
// 블로킹 큐 bq_t — Q11~Q17 이 모두 이 자료구조 위에서 돈다
// ---------------------------------------------------------------------------
// 불변식: 0 <= count <= cap, head < cap, 원소 i는 buf[(head + i) % cap].
// head + count 표현을 쓰면 head==tail 의 full/empty 모호성이 아예 없다.
// ===========================================================================
typedef struct {
    int src;   // 이벤트 발생원 (도어/센서/모뎀/링크모니터 …)
    int seq;   // 발생원별 시퀀스 번호
} Event;

typedef struct {
    Event  *buf;
    size_t  cap;      // 용량(원소 수)
    size_t  head;     // 다음에 읽을 인덱스
    size_t  count;    // 현재 원소 수
    bool    owns_buf; // true면 destroy에서 free한다
    bool    closed;   // 종료 프로토콜 플래그

    unsigned long pushed, popped;   // 관측성(observability) 카운터

    pthread_mutex_t m;              // 위 모든 필드를 보호
    pthread_cond_t  not_full;       // "슬롯이 생겼다" — 생산자가 기다림
    pthread_cond_t  not_empty;      // "원소가 생겼다" — 소비자가 기다림
} bq_t;

/* ---------------------------------------------------------------------------
 * Q11.  큐 init / destroy — 저장소 소유권과 필드 불변식
 *   KO: storage != NULL 이면 그 외부 배열을 그대로 쓰고(owns_buf=false),
 *       NULL 이면 cap개를 calloc 한다(owns_buf=true). cap==0 이나 q==NULL 이면 -1.
 *       mutex 1개 + condvar 2개(not_full/not_empty)를 초기화한다.
 *       destroy는 owns_buf일 때만 free하고 mutex/condvar를 파괴한다.
 *       bq_count는 락을 잡고 count 스냅샷을 돌려준다.
 *   EN: Init the queue over caller-provided storage, or calloc it when NULL;
 *       reject cap==0; init one mutex and two condvars. destroy frees only what
 *       it owns. bq_count returns a locked snapshot.
 *   ex: bq_init(&q, storage, 4) -> 0, cap==4, count==0, owns_buf==false
 *       bq_init(&q, NULL, 0)    -> -1
 * ------------------------------------------------------------------------- */
int bq_init(bq_t *q, Event *storage, size_t cap) {
    (void)q; (void)storage; (void)cap;
    // TODO: implement
    return 0;   // placeholder
}

void bq_destroy(bq_t *q) {
    (void)q;
    // TODO: implement
}

size_t bq_count(bq_t *q) {
    (void)q;
    // TODO: implement
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q12.  블로킹 push — 가득 차면 대기
 *   KO: 큐가 가득 차 있으면 not_full 에서 대기한다. 조건 검사는 반드시 while
 *       (spurious wakeup + 깨어난 뒤 다른 생산자가 먼저 슬롯을 채감).
 *       closed면 대기를 그만두고 false. 성공하면 원소를 넣고 not_empty를
 *       signal 하되 mutex를 unlock 한 뒤에 한다.
 *   EN: Block on not_full while the queue is full and not closed (while, not if).
 *       Return false once closed; otherwise enqueue and signal not_empty after
 *       unlocking.
 *   ex: cap=2, push a,b -> true,true / push c -> pop 이 일어날 때까지 블록
 * ------------------------------------------------------------------------- */
bool bq_push(bq_t *q, Event ev) {
    (void)q; (void)ev;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q13.  블로킹 pop — 비면 대기
 *   KO: 비어 있으면 not_empty 에서 while 로 대기한다. 깨어난 뒤 count==0 이면
 *       (= closed && empty) false 를 반환한다. 성공하면 head 에서 꺼내고
 *       head 를 랩어라운드시킨 뒤 not_full 을 signal 한다.
 *   EN: Block on not_empty while empty and open; after waking, count==0 means
 *       closed-and-empty -> false. Otherwise dequeue from head and signal not_full.
 *   ex: 빈 큐에서 pop -> push 가 올 때까지 블록, 그 값을 그대로 반환
 * ------------------------------------------------------------------------- */
bool bq_pop(bq_t *q, Event *out) {
    (void)q; (void)out;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q14.  close / 종료 프로토콜
 *   KO: closed 플래그를 mutex 안에서 세우고, 두 condvar를 모두 broadcast 한다
 *       (signal이면 대기자 하나만 깨어나 나머지가 영원히 잠든다).
 *       이미 closed면 아무 것도 하지 않는다(멱등). bq_is_closed는 락 잡고 조회.
 *       규칙: close 후 push는 거부, pop은 "closed && empty"일 때만 false.
 *   EN: Set closed under the mutex, then broadcast BOTH condvars; second close
 *       is a no-op (idempotent). After close, push is rejected and pop returns
 *       false only once the queue is also empty.
 *   ex: push a,b -> close -> pop a, pop b, pop false / push -> false
 * ------------------------------------------------------------------------- */
void bq_close(bq_t *q) {
    (void)q;
    // TODO: implement
}

bool bq_is_closed(bq_t *q) {
    (void)q;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q15.  try_push / try_pop — 논블로킹 변형
 *   KO: 조건이 안 맞으면 절대 기다리지 않고 즉시 false. 실시간 경로(제어 루프,
 *       오디오 콜백)는 블록하면 안 되기 때문에 같은 큐에 진입점을 하나 더 둔다.
 *       실패해도 큐 상태와 *out 을 바꾸면 안 된다.
 *       주의: 논블로킹(non-blocking)이지 무락(lock-free)이 아니다 — mutex는 잡는다.
 *   EN: Never wait: return false immediately when full (try_push) or empty
 *       (try_pop), leaving queue state and *out untouched.
 *   ex: 가득 찬 큐에 try_push -> false, count 그대로 / 빈 큐에 try_pop -> false
 * ------------------------------------------------------------------------- */
bool bq_try_push(bq_t *q, Event ev) {
    (void)q; (void)ev;
    // TODO: implement
    return false;   // placeholder
}

bool bq_try_pop(bq_t *q, Event *out) {
    (void)q; (void)out;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q16.  timed pop — pthread_cond_timedwait + 이식성 헬퍼
 *   KO: ① 먼저 "ms 만큼 상대 대기" 헬퍼 cond_wait_ms() 를 만든다.
 *          macOS   : pthread_cond_timedwait_relative_np(cv, m, &rel)
 *          그 외    : clock_gettime(CLOCK_REALTIME) + ms 로 '절대시각'을 만들어
 *                    pthread_cond_timedwait (tv_nsec 캐리 처리 잊지 말 것)
 *       ② bq_pop_timed: monotonic 절대 마감(deadline)을 먼저 고정하고, while
 *          루프에서 깨어날 때마다 남은 시간을 다시 계산해 넘긴다. 남은 시간이
 *          0 이하거나 ETIMEDOUT이면 false. closed && empty도 false.
 *   EN: Write a portable "wait at most N ms" wrapper (relative_np on macOS,
 *       absolute CLOCK_REALTIME elsewhere), then implement a timed pop that
 *       recomputes the remaining budget from a monotonic deadline on each wake.
 *   ex: 빈 큐 + 80ms -> 약 80ms 후 false / 40ms 뒤 push -> true 로 즉시 복귀
 * ------------------------------------------------------------------------- */
#if defined(__APPLE__)
static int cond_wait_ms(pthread_cond_t *cv, pthread_mutex_t *m, long ms) {
    (void)cv; (void)m; (void)ms;
    // TODO: implement (pthread_cond_timedwait_relative_np)
    return 0;   // placeholder
}
#else
static int cond_wait_ms(pthread_cond_t *cv, pthread_mutex_t *m, long ms) {
    (void)cv; (void)m; (void)ms;
    // TODO: implement (clock_gettime(CLOCK_REALTIME) + pthread_cond_timedwait)
    return 0;   // placeholder
}
#endif

bool bq_pop_timed(bq_t *q, Event *out, long timeout_ms) {
    (void)q; (void)out; (void)timeout_ms;
    (void)cond_wait_ms;   // (미구현 경고 억제용 — 구현하면서 지울 것)
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q17.  batch pop — 한 번에 최대 n개
 *   KO: 비어 있으면 (blocking pop 처럼) not_empty 에서 대기하고, 깨어나면
 *       min(count, max) 개를 out[0..n-1] 에 FIFO 순서로 옮긴 뒤 n 을 반환한다.
 *       반환 0 == closed && empty. 여러 슬롯이 한꺼번에 비므로 깨우기는
 *       signal 이 아니라 broadcast.
 *       왜: LTE 업로드는 요청 하나당 고정 오버헤드가 크다 — 모아서 한 번에 올린다.
 *   EN: Block until non-empty (or closed), then move min(count,max) items out in
 *       FIFO order and return that count; 0 means closed-and-empty. Broadcast
 *       not_full because several slots freed at once.
 *   ex: 큐에 0..4, max=3 -> n=3, out = {0,1,2} / 다음 호출 -> n=2, out={3,4}
 * ------------------------------------------------------------------------- */
size_t bq_pop_batch(bq_t *q, Event *out, size_t max) {
    (void)q; (void)out; (void)max;
    // TODO: implement
    return 0;   // placeholder
}

// ===========================================================================
// drop 정책 큐 tq_t — Q18
// ===========================================================================
typedef enum { TQ_BLOCK, TQ_DROP_NEWEST, TQ_DROP_OLDEST } tq_policy_t;

typedef struct { uint32_t ts_ms; int32_t value; } Sample;

typedef struct {
    Sample     *buf;
    size_t      cap, head, count;
    tq_policy_t policy;
    bool        closed;
    unsigned long enqueued, dropped, dequeued;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} tq_t;

/* ---------------------------------------------------------------------------
 * Q18.  drop 정책 큐 (backpressure 설계)
 *   KO: 링크가 끊기면 업로더가 막히고 그 사이에도 센서는 계속 샘플을 만든다.
 *       가득 찼을 때의 동작을 정책으로 고른다:
 *         TQ_BLOCK       생산자를 막는다        (손실 0, 실시간 경로엔 금지)
 *         TQ_DROP_NEWEST 방금 것을 버린다        (과거 이력 보존 — 감사 로그)
 *         TQ_DROP_OLDEST 가장 오래된 것을 밀어낸다(최신 상태 보존 — 텔레메트리)
 *       drop 해도 push는 true를 반환하되 dropped 카운터를 반드시 올린다.
 *       false는 오직 closed 일 때만. tq_pop/tq_close 는 Q13/Q14 와 동일 패턴.
 *       불변식: produced == dequeued + dropped + queued
 *   EN: Implement a bounded queue whose full-behaviour is a policy (block /
 *       drop newest / drop oldest). Dropping still returns true but must bump a
 *       drop counter — silent loss is the worst failure mode.
 *   ex: cap=4, DROP_NEWEST 로 0..5 push -> 큐={0,1,2,3}, dropped=2
 *       cap=4, DROP_OLDEST 로 0..5 push -> 큐={2,3,4,5}, dropped=2
 * ------------------------------------------------------------------------- */
int tq_init(tq_t *q, size_t cap, tq_policy_t p) {
    (void)q; (void)cap; (void)p;
    // TODO: implement
    return 0;   // placeholder
}

void tq_destroy(tq_t *q) {
    (void)q;
    // TODO: implement
}

bool tq_push(tq_t *q, Sample s) {
    (void)q; (void)s;
    // TODO: implement
    return false;   // placeholder
}

bool tq_pop(tq_t *q, Sample *out) {
    (void)q; (void)out;
    // TODO: implement
    return false;   // placeholder
}

void tq_close(tq_t *q) {
    (void)q;
    // TODO: implement
}

// ===========================================================================
// 워커 풀 pool_t — Q19
// ===========================================================================
typedef void (*task_fn)(void *arg);
typedef enum { POOL_RUNNING, POOL_DRAIN, POOL_STOP } pool_state_t;

typedef struct { task_fn fn; void *arg; } Task;

typedef struct {
    pthread_t   *workers;
    size_t       nworkers;
    Task        *q;
    size_t       cap, head, count;
    pool_state_t state;
    unsigned long executed, rejected, discarded;
    pthread_mutex_t m;
    pthread_cond_t  work;   // "작업이 생겼다" — 워커가 대기
    pthread_cond_t  slot;   // "자리가 생겼다" — 제출자가 대기
} pool_t;

/* ---------------------------------------------------------------------------
 * Q19.  워커 풀 + graceful shutdown (DRAIN vs STOP)
 *   KO: N개 워커가 큐의 작업을 꺼내 실행한다. 구현할 조각 4개:
 *       · pool_take : count==0 && state==RUNNING 인 동안 work 에서 대기.
 *                     state==STOP 이면 남은 count 를 discarded 에 더하고 버린 뒤
 *                     false(워커 종료). DRAIN 이면 count 가 0 이 될 때까지 계속.
 *                     꺼낼 때 executed++ 하고 slot 을 signal.
 *       · pool_init : 큐/워커 배열 할당, state=RUNNING, 워커 nworkers개 생성.
 *       · pool_submit: 큐가 차면 slot 에서 대기. RUNNING 이 아니면 rejected++ 후
 *                     false (조용히 무시하지 말 것).
 *       · pool_shutdown: 상태 전환 -> work/slot 둘 다 broadcast -> 전원 join.
 *                     free 는 pool_destroy 에서 — 합치면 shutdown 직후 submit 이
 *                     해제된 메모리를 만져 UAF.
 *       ★ 작업 실행 t.fn(t.arg) 은 반드시 락 밖에서 (pool_worker 는 제공됨).
 *   EN: A fixed worker pool with two shutdown modes: DRAIN runs the backlog to
 *       completion, STOP discards it (counted). Broadcast both condvars, join
 *       every worker, and keep shutdown separate from destroy.
 *   ex: 2000개 submit 후 shutdown(drain=true) -> executed==2000, discarded==0
 *       shutdown(drain=false) -> executed + discarded == submitted
 * ------------------------------------------------------------------------- */
static bool pool_take(pool_t *p, Task *out) {
    (void)p; (void)out;
    // TODO: implement
    return false;   // placeholder
}

// 제공됨 (건드리지 말 것): 워커 루프. 실행은 락 밖에서 한다는 점에 주목.
static void *pool_worker(void *arg) {
    pool_t *p = (pool_t *)arg;
    Task t;
    while (pool_take(p, &t))
        t.fn(t.arg);                       // ★ 실행은 락 밖에서
    return NULL;
}

int pool_init(pool_t *p, size_t nworkers, size_t qcap) {
    (void)p; (void)nworkers; (void)qcap;
    (void)pool_worker;   // (미구현 경고 억제용 — 구현하면서 지울 것)
    // TODO: implement
    return 0;   // placeholder
}

bool pool_submit(pool_t *p, task_fn fn, void *arg) {
    (void)p; (void)fn; (void)arg;
    // TODO: implement
    return false;   // placeholder
}

void pool_shutdown(pool_t *p, bool drain) {
    (void)p; (void)drain;
    // TODO: implement
}

void pool_destroy(pool_t *p) {
    (void)p;
    // TODO: implement
}

// ===========================================================================
// counting semaphore csem_t — Q20
// ===========================================================================
typedef struct {
    int value;                 // 남은 허가(permit) 수
    pthread_mutex_t m;
    pthread_cond_t  cv;
} csem_t;

/* ---------------------------------------------------------------------------
 * Q20.  counting semaphore 를 mutex + condvar 로 구현
 *   KO: "동시 업로드는 최대 N개" 같은 자원 제한용. macOS는 POSIX 무명 세마포어
 *       (sem_init)가 미구현이라 직접 만드는 게 이식성 정답이다.
 *       wait : while (value == 0) cond_wait; 그 다음 value--
 *       post : value++ 후 signal (자리가 하나 생겼으니 broadcast 불필요 —
 *              broadcast 는 thundering herd)
 *       trywait: value==0 이면 즉시 false, 아니면 value-- 후 true
 *       불변식: value >= 0, 동시 사용자 수 <= 초기 value
 *   EN: Build a counting semaphore from one mutex + one condvar: wait blocks in
 *       a while loop until value>0 then decrements; post increments and signals
 *       exactly one waiter; trywait never blocks.
 *   ex: init(2) 로 6개 스레드가 acquire/release -> 동시 실행 최대 2
 * ------------------------------------------------------------------------- */
int csem_init(csem_t *s, int initial) {
    (void)s; (void)initial;
    // TODO: implement
    return 0;   // placeholder
}

void csem_destroy(csem_t *s) {
    (void)s;
    // TODO: implement
}

void csem_wait(csem_t *s) {
    (void)s;
    // TODO: implement
}

bool csem_trywait(csem_t *s) {
    (void)s;
    // TODO: implement
    return false;   // placeholder
}

void csem_post(csem_t *s) {
    (void)s;
    // TODO: implement
}

int csem_value(csem_t *s) {
    (void)s;
    // TODO: implement
    return 0;   // placeholder
}

// ===========================================================================
// 테스트용 전역과 스레드 함수 (건드리지 말 것)
// (정적 저장기간 = 0 초기화 보장 → 미구현 상태에서도 안전하게 동작)
// ===========================================================================
static bq_t   s_q;
static Event  s_storage[4];

static _Atomic int s_push_done, s_push_ok;
static void *th_push_one(void *arg) {
    Event ev = { .src = 9, .seq = (int)(intptr_t)arg };
    bool ok = bq_push(&s_q, ev);
    atomic_store(&s_push_ok, ok ? 1 : 0);
    atomic_store(&s_push_done, 1);
    return NULL;
}

static _Atomic int s_pop_done, s_pop_ok;
static Event s_pop_ev;                       // join 이후에만 읽는다
static void *th_pop_one(void *arg) {
    (void)arg;
    Event ev = { -1, -1 };
    bool ok = bq_pop(&s_q, &ev);
    s_pop_ev = ev;
    atomic_store(&s_pop_ok, ok ? 1 : 0);
    atomic_store(&s_pop_done, 1);
    return NULL;
}

static _Atomic int s_woke, s_false_ret;
static void *th_pop_until_false(void *arg) {
    (void)arg;
    Event ev;
    while (bq_pop(&s_q, &ev)) { /* drain */ }
    atomic_fetch_add(&s_false_ret, 1);
    atomic_fetch_add(&s_woke, 1);
    return NULL;
}

static void *th_delayed_push(void *arg) {
    sleep_ms((int)(intptr_t)arg);
    Event ev = { .src = 7, .seq = 77 };
    (void)bq_push(&s_q, ev);
    return NULL;
}

static Event s_batch_out[8];
static _Atomic int s_batch_n, s_batch_done;
static void *th_batch_pop(void *arg) {
    (void)arg;
    size_t n = bq_pop_batch(&s_q, s_batch_out, 8);
    atomic_store(&s_batch_n, (int)n);
    atomic_store(&s_batch_done, 1);
    return NULL;
}

#define TQ_NPROD 2
#define TQ_NPER  2000
static tq_t s_tq;
static _Atomic unsigned long s_tq_consumed;
static _Atomic int s_tq_push_reject;
static void *th_tq_prod(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < TQ_NPER; i++) {
        Sample s = { (uint32_t)i, (int32_t)id };
        if (!tq_push(&s_tq, s)) { atomic_fetch_add(&s_tq_push_reject, 1); return NULL; }
    }
    return NULL;
}
static void *th_tq_cons(void *arg) {
    (void)arg;
    Sample s;
    unsigned long n = 0;
    while (tq_pop(&s_tq, &s)) n++;
    atomic_fetch_add(&s_tq_consumed, n);
    return NULL;
}

static pool_t s_pool;
static _Atomic unsigned long s_task_count;
static void task_inc(void *arg) { (void)arg; atomic_fetch_add(&s_task_count, 1UL); }

static csem_t s_sem;
static pthread_mutex_t s_peak_m = PTHREAD_MUTEX_INITIALIZER;
static int s_cur, s_peak;
static _Atomic int s_sem_done;
static void *th_sem_worker(void *arg) {
    (void)arg;
    csem_wait(&s_sem);
    pthread_mutex_lock(&s_peak_m);
    s_cur++;
    if (s_cur > s_peak) s_peak = s_cur;
    pthread_mutex_unlock(&s_peak_m);
    sleep_ms(20);                            // 자원을 잡고 있는 구간
    pthread_mutex_lock(&s_peak_m);
    s_cur--;
    pthread_mutex_unlock(&s_peak_m);
    csem_post(&s_sem);
    atomic_fetch_add(&s_sem_done, 1);
    return NULL;
}

// ===========================================================================
// main  (건드리지 말 것 — 정답 파일과 동일)
// ===========================================================================
int main(void) {
    Event ev = { -1, -1 };

    // ---------------------------------------------------------------- Q11
    printf("== Q11 큐 init/destroy ==\n");
    T("bq_init(외부 저장소) returns 0",      bq_init(&s_q, s_storage, 4) == 0);
    T("init 불변식: cap=4, count=0, head=0", s_q.cap == 4 && s_q.count == 0 && s_q.head == 0);
    T("init: closed=false, owns_buf=false",  s_q.closed == false && s_q.owns_buf == false);
    T("bq_count == 0 after init",            bq_count(&s_q) == 0);
    bq_destroy(&s_q);
    T("bq_init(cap=0) rejected",             bq_init(&s_q, s_storage, 0) == -1);
    T("bq_init(NULL storage) 내부 calloc",   bq_init(&s_q, NULL, 4) == 0 && s_q.buf != NULL && s_q.owns_buf == true);
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q12
    printf("\n== Q12 블로킹 push ==\n");
    bq_init(&s_q, s_storage, 2);                 // 일부러 작게 → 블로킹 경로 강제
    ev.src = 1; ev.seq = 1;
    T("push into empty queue succeeds", bq_push(&s_q, ev) == true);
    ev.seq = 2;
    T("두 번째 push 후 count==2(가득)",  bq_push(&s_q, ev) == true && bq_count(&s_q) == 2);
    {
        pthread_t th;
        atomic_store(&s_push_done, 0);
        atomic_store(&s_push_ok, -1);
        pthread_create(&th, NULL, th_push_one, (void *)(intptr_t)3);
        sleep_ms(60);
        T("가득 찬 큐에서 push는 블록한다", atomic_load(&s_push_done) == 0);
        T("블로킹 중 pop 하나 성공",        bq_pop(&s_q, &ev) == true && ev.seq == 1);
        T("pop 이후 블록된 push가 완료됨",  wait_flag(&s_push_done, 1, 2000));
        pthread_join(th, NULL);
        T("블록됐던 push의 반환값 true",    atomic_load(&s_push_ok) == 1);
    }
    T("wrap-around 후에도 FIFO 순서 유지", bq_pop(&s_q, &ev) && ev.seq == 2 &&
                                            bq_pop(&s_q, &ev) && ev.seq == 3);
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q13
    printf("\n== Q13 블로킹 pop ==\n");
    bq_init(&s_q, s_storage, 4);
    {
        pthread_t th;
        atomic_store(&s_pop_done, 0);
        atomic_store(&s_pop_ok, -1);
        pthread_create(&th, NULL, th_pop_one, NULL);
        sleep_ms(60);
        T("빈 큐에서 pop은 블록한다", atomic_load(&s_pop_done) == 0);
        ev.src = 5; ev.seq = 42;
        bq_push(&s_q, ev);
        T("push 후 블록된 pop이 깨어남", wait_flag(&s_pop_done, 1, 2000));
        pthread_join(th, NULL);
        T("pop이 true를 반환",           atomic_load(&s_pop_ok) == 1);
        T("pop된 값이 push한 값과 동일", s_pop_ev.src == 5 && s_pop_ev.seq == 42);
    }
    T("pop 후 count 0 복귀", bq_count(&s_q) == 0);
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q14
    printf("\n== Q14 close / 종료 프로토콜 ==\n");
    bq_init(&s_q, s_storage, 4);
    ev.src = 2; ev.seq = 100; bq_push(&s_q, ev);
    ev.seq = 101;             bq_push(&s_q, ev);
    bq_close(&s_q);
    T("close 후에도 남은 항목은 drain (1/2)", bq_pop(&s_q, &ev) == true && ev.seq == 100);
    T("close 후에도 남은 항목은 drain (2/2)", bq_pop(&s_q, &ev) == true && ev.seq == 101);
    T("closed && empty -> pop false",         bq_pop(&s_q, &ev) == false);
    ev.seq = 999;
    T("close 후 push는 거부(false)",          bq_push(&s_q, ev) == false);
    T("close 후 try_push도 거부",             bq_try_push(&s_q, ev) == false);
    bq_close(&s_q);
    T("이중 close는 멱등(크래시/상태변화 없음)", bq_is_closed(&s_q) == true && bq_count(&s_q) == 0);
    bq_destroy(&s_q);

    bq_init(&s_q, s_storage, 4);
    {
        pthread_t th[3];
        atomic_store(&s_woke, 0);
        atomic_store(&s_false_ret, 0);
        for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, th_pop_until_false, NULL);
        sleep_ms(50);
        T("close 전에는 3개 소비자가 모두 대기 중", atomic_load(&s_woke) == 0);
        bq_close(&s_q);                       // broadcast가 없으면 여기서 hang
        bool all = wait_flag(&s_woke, 3, 3000);
        for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
        T("close가 대기 중인 소비자 3개를 모두 깨움", all);
        T("깨어난 소비자는 모두 false를 받음",        atomic_load(&s_false_ret) == 3);
    }
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q15
    printf("\n== Q15 try_push / try_pop (논블로킹) ==\n");
    bq_init(&s_q, s_storage, 2);
    ev.src = 3; ev.seq = 10;
    T("try_push 성공(여유 있음)",       bq_try_push(&s_q, ev) == true);
    ev.seq = 11;
    T("try_push 두 번째도 성공",        bq_try_push(&s_q, ev) == true);
    ev.seq = 12;
    {
        long t0 = now_ms();
        bool r = bq_try_push(&s_q, ev);
        long dt = now_ms() - t0;
        T("가득 찬 큐에서 try_push는 블록 없이 false", r == false && dt < 50);
    }
    T("실패한 try_push는 count를 바꾸지 않음", bq_count(&s_q) == 2);
    T("try_pop이 FIFO로 값을 반환",            bq_try_pop(&s_q, &ev) == true && ev.seq == 10);
    (void)bq_try_pop(&s_q, &ev);
    {
        Event sentinel = { -7, -7 };
        long t0 = now_ms();
        bool r = bq_try_pop(&s_q, &sentinel);
        long dt = now_ms() - t0;
        T("빈 큐에서 try_pop은 블록 없이 false", r == false && dt < 50);
        T("실패한 try_pop은 out을 건드리지 않음", sentinel.src == -7 && sentinel.seq == -7);
    }
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q16
    printf("\n== Q16 timed pop (pthread_cond_timedwait) ==\n");
    bq_init(&s_q, s_storage, 4);
    {
        long t0 = now_ms();
        bool r = bq_pop_timed(&s_q, &ev, 80);
        long dt = now_ms() - t0;
        T("빈 큐 + 타임아웃 -> false",        r == false);
        T("실제로 대기했다 (40ms~1500ms)",    dt >= 40 && dt <= 1500);
    }
    ev.src = 4; ev.seq = 55; bq_push(&s_q, ev);
    {
        long t0 = now_ms();
        bool r = bq_pop_timed(&s_q, &ev, 1000);
        long dt = now_ms() - t0;
        T("항목이 있으면 즉시 true",          r == true && ev.seq == 55);
        T("즉시 반환은 40ms 미만",            dt < 40);
    }
    {
        pthread_t th;
        pthread_create(&th, NULL, th_delayed_push, (void *)(intptr_t)40);
        long t0 = now_ms();
        bool r = bq_pop_timed(&s_q, &ev, 2000);
        long dt = now_ms() - t0;
        pthread_join(th, NULL);
        T("타임아웃 전에 들어온 push로 깨어남", r == true && ev.seq == 77);
        T("깨어난 시각이 타임아웃보다 훨씬 빠름", dt < 1500);
    }
    bq_close(&s_q);
    T("closed && empty -> timed pop 즉시 false", bq_pop_timed(&s_q, &ev, 2000) == false);
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q17
    printf("\n== Q17 batch pop ==\n");
    bq_init(&s_q, NULL, 8);
    for (int i = 0; i < 5; i++) { ev.src = 6; ev.seq = i; bq_push(&s_q, ev); }
    {
        Event out[8];
        size_t n = bq_pop_batch(&s_q, out, 3);
        T("batch pop은 min(count,max)=3개 반환", n == 3);
        T("배치 안의 순서는 FIFO (0,1,2)",       n == 3 && out[0].seq == 0 && out[1].seq == 1 && out[2].seq == 2);
        n = bq_pop_batch(&s_q, out, 10);
        T("두 번째 배치는 남은 2개만 반환",      n == 2 && out[0].seq == 3 && out[1].seq == 4);
    }
    {
        pthread_t th;
        atomic_store(&s_batch_done, 0);
        atomic_store(&s_batch_n, -1);
        pthread_create(&th, NULL, th_batch_pop, NULL);
        sleep_ms(60);
        T("빈 큐에서 batch pop은 블록한다", atomic_load(&s_batch_done) == 0);
        ev.src = 6; ev.seq = 90; bq_push(&s_q, ev);
        ev.seq = 91;             bq_push(&s_q, ev);
        bool done = wait_flag(&s_batch_done, 1, 2000);
        pthread_join(th, NULL);
        int n = atomic_load(&s_batch_n);
        T("push 후 batch pop이 깨어나 1~2개 반환", done && n >= 1 && n <= 2 && s_batch_out[0].seq == 90);
    }
    bq_close(&s_q);
    while (bq_try_pop(&s_q, &ev)) { }
    {
        Event out[8];
        T("closed && empty -> batch pop은 0 반환", bq_pop_batch(&s_q, out, 8) == 0);
    }
    bq_destroy(&s_q);

    // ---------------------------------------------------------------- Q18
    printf("\n== Q18 drop 정책 큐 ==\n");
    {
        Sample s;
        tq_init(&s_tq, 4, TQ_DROP_NEWEST);
        for (int i = 0; i < 6; i++) { s.ts_ms = (uint32_t)i; s.value = i; tq_push(&s_tq, s); }
        T("DROP_NEWEST: 큐에는 cap(4)개만 남음", s_tq.count == 4);
        T("DROP_NEWEST: dropped == 2",           s_tq.dropped == 2);
        tq_close(&s_tq);
        T("DROP_NEWEST: 오래된 쪽(0,1,2,3)이 보존됨",
          tq_pop(&s_tq, &s) && s.value == 0 && tq_pop(&s_tq, &s) && s.value == 1);
        tq_destroy(&s_tq);

        tq_init(&s_tq, 4, TQ_DROP_OLDEST);
        for (int i = 0; i < 6; i++) { s.ts_ms = (uint32_t)i; s.value = i; tq_push(&s_tq, s); }
        T("DROP_OLDEST: dropped == 2", s_tq.dropped == 2 && s_tq.count == 4);
        tq_close(&s_tq);
        T("DROP_OLDEST: 최신 쪽(2,3,4,5)이 보존됨",
          tq_pop(&s_tq, &s) && s.value == 2 && tq_pop(&s_tq, &s) && s.value == 3);
        tq_destroy(&s_tq);
    }
    {
        // BLOCK 정책 스트레스: 손실이 0이어야 한다 (결정적)
        pthread_t prod[TQ_NPROD], cons;
        atomic_store(&s_tq_consumed, 0UL);
        atomic_store(&s_tq_push_reject, 0);
        tq_init(&s_tq, 16, TQ_BLOCK);
        pthread_create(&cons, NULL, th_tq_cons, NULL);
        for (int i = 0; i < TQ_NPROD; i++) pthread_create(&prod[i], NULL, th_tq_prod, (void *)(intptr_t)i);
        for (int i = 0; i < TQ_NPROD; i++) pthread_join(prod[i], NULL);
        tq_close(&s_tq);
        pthread_join(cons, NULL);
        unsigned long produced = (unsigned long)TQ_NPROD * TQ_NPER;
        T("BLOCK 스트레스: 열려 있는 동안 push는 전부 수락", atomic_load(&s_tq_push_reject) == 0);
        T("BLOCK 스트레스: 손실 0 (dropped==0)",            s_tq.dropped == 0);
        T("BLOCK 스트레스: consumed == produced",           atomic_load(&s_tq_consumed) == produced);
        tq_destroy(&s_tq);
    }
    {
        // DROP_OLDEST 스트레스: 불변식 produced == consumed + dropped + queued
        pthread_t prod[TQ_NPROD], cons;
        atomic_store(&s_tq_consumed, 0UL);
        atomic_store(&s_tq_push_reject, 0);
        tq_init(&s_tq, 16, TQ_DROP_OLDEST);
        pthread_create(&cons, NULL, th_tq_cons, NULL);
        for (int i = 0; i < TQ_NPROD; i++) pthread_create(&prod[i], NULL, th_tq_prod, (void *)(intptr_t)i);
        for (int i = 0; i < TQ_NPROD; i++) pthread_join(prod[i], NULL);
        tq_close(&s_tq);
        pthread_join(cons, NULL);
        unsigned long produced = (unsigned long)TQ_NPROD * TQ_NPER;
        unsigned long left     = (unsigned long)s_tq.count;
        printf("     (produced=%lu consumed=%lu dropped=%lu left=%lu)\n",
               produced, atomic_load(&s_tq_consumed), s_tq.dropped, left);
        T("DROP_OLDEST 스트레스: 불변식 produced == consumed + dropped + queued",
          atomic_load(&s_tq_consumed) + s_tq.dropped + left == produced);
        // DROP_OLDEST는 '밀어내고 나서 반드시 넣는다' → 모든 push가 enqueue된다.
        T("DROP_OLDEST 스트레스: 모든 push가 enqueue됨 (enqueued == produced)",
          s_tq.enqueued == produced);
        tq_destroy(&s_tq);
    }

    // ---------------------------------------------------------------- Q19
    printf("\n== Q19 워커 풀 + graceful shutdown ==\n");
    {
        atomic_store(&s_task_count, 0UL);
        T("pool_init(4 workers, qcap 16)", pool_init(&s_pool, 4, 16) == 0);
        for (int i = 0; i < 2000; i++) (void)pool_submit(&s_pool, task_inc, NULL);
        pool_shutdown(&s_pool, true);                 // DRAIN
        T("DRAIN: 제출한 2000개가 전부 실행됨", atomic_load(&s_task_count) == 2000UL);
        T("DRAIN: discarded == 0",              s_pool.discarded == 0);
        T("DRAIN: executed 카운터 == 2000",     s_pool.executed == 2000UL);
        T("shutdown 후 submit은 거부됨",        pool_submit(&s_pool, task_inc, NULL) == false);
        T("거부는 rejected 카운터로 보고됨",    s_pool.rejected >= 1);
        pool_destroy(&s_pool);
    }
    {
        atomic_store(&s_task_count, 0UL);
        pool_init(&s_pool, 2, 64);
        unsigned long submitted = 0;
        for (int i = 0; i < 2000; i++) if (pool_submit(&s_pool, task_inc, NULL)) submitted++;
        pool_shutdown(&s_pool, false);                // STOP
        printf("     (submitted=%lu executed=%lu discarded=%lu)\n",
               submitted, s_pool.executed, s_pool.discarded);
        T("STOP: executed + discarded == submitted", s_pool.executed + s_pool.discarded == submitted);
        T("STOP: 실제 실행 횟수 == executed 카운터", atomic_load(&s_task_count) == s_pool.executed);
        pool_destroy(&s_pool);
    }

    // ---------------------------------------------------------------- Q20
    printf("\n== Q20 counting semaphore (mutex + condvar) ==\n");
    T("csem_init(3) -> value 3", csem_init(&s_sem, 3) == 0 && csem_value(&s_sem) == 3);
    csem_wait(&s_sem); csem_wait(&s_sem); csem_wait(&s_sem);
    T("wait 3회 -> value 0",     csem_value(&s_sem) == 0);
    {
        long t0 = now_ms();
        bool r = csem_trywait(&s_sem);
        T("value 0에서 trywait은 블록 없이 false", r == false && now_ms() - t0 < 50);
    }
    csem_post(&s_sem);
    T("post -> value 1",         csem_value(&s_sem) == 1);
    T("post 후 trywait 성공",    csem_trywait(&s_sem) == true && csem_value(&s_sem) == 0);
    csem_destroy(&s_sem);

    {
        pthread_t th[6];
        s_cur = 0; s_peak = 0;
        atomic_store(&s_sem_done, 0);
        csem_init(&s_sem, 2);                          // 동시 업로드 2개로 제한
        for (int i = 0; i < 6; i++) pthread_create(&th[i], NULL, th_sem_worker, NULL);
        for (int i = 0; i < 6; i++) pthread_join(th[i], NULL);
        printf("     (peak concurrency = %d, limit = 2)\n", s_peak);
        T("6개 워커가 모두 완료",              atomic_load(&s_sem_done) == 6);
        T("동시 실행 수가 세마포어 한도(2) 이하", s_peak >= 1 && s_peak <= 2);
        T("모두 끝난 뒤 value가 2로 복구",     csem_value(&s_sem) == 2 && s_cur == 0);
        csem_destroy(&s_sem);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
