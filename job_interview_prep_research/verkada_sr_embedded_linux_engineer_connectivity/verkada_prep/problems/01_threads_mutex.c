// 01_threads_mutex.c  —  PRACTICE STUB (직접 채워넣기)
// 스레드 & 뮤텍스 기초 (Threads & Mutex Fundamentals)  —  Q1~Q10
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=01_threads_mutex
//   또는:     cc -std=c11 -Wall -Wextra -O1 -g -pthread 01_threads_mutex.c -o /tmp/vk01p && /tmp/vk01p
//
// 각 함수의 '// TODO: implement' 를 채우고 다시 실행 → [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일·실행은 되며 대부분 FAIL 로 뜬다. 스레드 테스트는
//  유한 스핀 상한을 두어 절대 멈추지 않는다.)
//
// Verkada Connectivity 인터뷰 1번 주제인 "thread safety"의 바닥을 깐다. GC31-E
// 게이트웨이 펌웨어는 모뎀/링크 모니터/PoE 제어/업로드 스레드가 같은 상태를
// 건드린다. race를 눈으로 보기 → mutex로 불변식 보호 → 임계구역 최소화 →
// 중첩 락/TOCTOU 회피 → once/rwlock/샤딩/깔끔한 종료.
// ---------------------------------------------------------------------------
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ---------------------------------------------------------------------------
// ThreadSanitizer 빌드 감지 (건드리지 말 것).
// Q1은 "보호 없는 공유 카운터"를 일부러 보여주는 문제라 TSan이 반드시 경고한다.
// TSan 빌드에서만 Q1 데모를 '직렬 실행'으로 바꿔 나머지 9문제의 TSan 결과를
// 가리지 않게 한다. 일반 빌드에서는 진짜로 동시에 돈다.
//   → race를 TSan으로 직접 보고 싶으면: cc ... -fsanitize=thread -DVK_FORCE_RACE=1
// ---------------------------------------------------------------------------
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define VK_TSAN_BUILD 1
#  endif
#endif
#if defined(__SANITIZE_THREAD__)
#  define VK_TSAN_BUILD 1
#endif
#ifndef VK_TSAN_BUILD
#  define VK_TSAN_BUILD 0
#endif
#ifndef VK_FORCE_RACE
#  define VK_FORCE_RACE 0
#endif
#define VK_RACE_CONCURRENT (!VK_TSAN_BUILD || VK_FORCE_RACE)

#define MAX_WORKERS 8

// ===========================================================================
// Q1. 스레드 생성/조인 + 보호 없는 공유 카운터 (race 재현)
// ===========================================================================
static volatile long g_unsafe_counter = 0;   // volatile은 최적화 억제일 뿐, 원자성 없음

typedef struct {
    long iters;
    long local_done;     // 스레드마다 다른 슬롯 → 여기에는 race 없음
} RaceArg;

/* ---------------------------------------------------------------------------
 * Q1a.  race 워커
 *   KO: a->iters 번 반복하면서 (1) 전역 g_unsafe_counter 를 **보호 없이**
 *       `g_unsafe_counter = g_unsafe_counter + 1` 로 증가시키고,
 *       (2) 자기 슬롯 a->local_done 도 1 증가시킨다.
 *       local_done 은 스레드마다 다른 메모리라 경합이 없다.
 *   EN: Increment the unprotected global counter and this thread's own
 *       local_done slot, a->iters times.
 *   ex: iters=20000 -> local_done==20000, 전역은 그보다 작을 수 있다
 * ------------------------------------------------------------------------- */
static void *race_worker(void *p) {
    RaceArg *a = (RaceArg *)p;
    (void)a; (void)g_unsafe_counter;
    // TODO: implement
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Q1b.  스레드 생성/조인 + race 데모
 *   KO: nthreads(1~MAX_WORKERS) 개의 스레드를 race_worker 로 띄우고 전부 join 한다.
 *       g_unsafe_counter 를 0으로 초기화한 뒤 시작하고, join 후 각 스레드의
 *       local_done 합을 *out_attempts(NULL 가능)에 넣고 g_unsafe_counter 를 반환.
 *       nthreads 가 범위를 벗어나거나 생성 실패면 -1.
 *       ※ TSan 빌드에서는 #if !VK_RACE_CONCURRENT 자리에서 즉시 join 한다(아래 골격 참고).
 *   EN: Spawn nthreads race workers, join them all, report attempts and the
 *       observed shared counter; return -1 on bad argument or create failure.
 *   ex: race_demo(4, 20000, &att) -> att==80000, 반환값 <= 80000
 * ------------------------------------------------------------------------- */
long race_demo(int nthreads, long iters, long *out_attempts) {
    (void)nthreads; (void)iters; (void)out_attempts;
    (void)race_worker;   // 구현 시 pthread_create 의 start_routine 으로 쓴다
    // TODO: implement
    //   pthread_t th[MAX_WORKERS]; RaceArg args[MAX_WORKERS];
    //   생성 루프 안:
    //     #if !VK_RACE_CONCURRENT
    //         pthread_join(th[i], NULL);   // TSan 빌드에서는 직렬 실행
    //     #endif
    //   생성 루프 뒤:
    //     #if VK_RACE_CONCURRENT
    //         for (...) pthread_join(th[i], NULL);
    //     #endif
    return -1;
}

// ===========================================================================
// Q2. mutex로 보호한 카운터 — 정확히 N이 나온다
// ===========================================================================
typedef struct {
    long            value;
    pthread_mutex_t m;
} SafeCounter;

/* ---------------------------------------------------------------------------
 * Q2.  뮤텍스로 보호한 카운터
 *   KO: sc_init/sc_destroy 는 value 와 mutex 를 초기화/파괴한다.
 *       sc_inc 는 락 안에서 value 를 1 증가시키고, sc_get 은 **읽기도 락으로**
 *       보호해 현재 값을 반환한다(NULL 이면 -1). 모든 함수는 NULL 방어.
 *       safe_counter_run 은 nthreads 개 스레드가 각각 iters 번 sc_inc 한 뒤
 *       최종값을 반환한다 (nthreads 범위 밖이면 -1).
 *   EN: A mutex-protected counter (init/destroy/inc/get) plus a helper that
 *       runs N threads x iters increments and returns the final value.
 *   ex: safe_counter_run(4, 20000) -> 정확히 80000
 * ------------------------------------------------------------------------- */
void sc_init(SafeCounter *c) {
    (void)c;
    // TODO: implement
}

void sc_destroy(SafeCounter *c) {
    (void)c;
    // TODO: implement
}

void sc_inc(SafeCounter *c) {
    (void)c;
    // TODO: implement
}

long sc_get(SafeCounter *c) {
    (void)c;
    // TODO: implement
    return 0;   // placeholder
}

typedef struct { SafeCounter *c; long iters; } SafeArg;

static void *safe_worker(void *p) {
    SafeArg *a = (SafeArg *)p;
    for (long i = 0; i < a->iters; ++i) sc_inc(a->c);
    return NULL;
}

long safe_counter_run(int nthreads, long iters) {
    (void)nthreads; (void)iters;
    (void)safe_worker;   // 구현 시 pthread_create 의 start_routine 으로 쓴다
    // TODO: implement
    return -1;
}

// ===========================================================================
// Q3. 구조체 불변식 보호 — 링크 통계를 한 임계구역에서 일관되게 갱신
// ===========================================================================
typedef struct {
    long            count;
    long            sum_ms;
    int             min_ms;
    int             max_ms;
    pthread_mutex_t m;
} LinkStats;

typedef struct {          // 락 밖으로 들고 나가는 값 복사본
    long count;
    long sum_ms;
    int  min_ms;
    int  max_ms;
} LinkStatsView;

/* ---------------------------------------------------------------------------
 * Q3.  구조체 불변식 보호 (WAN 링크 RTT 통계)
 *   KO: ls_init 은 count=0, sum=0, min=INT_MAX, max=INT_MIN 으로 두고 mutex 초기화.
 *       ls_add(s, rtt) 는 **한 번의 lock 안에서** count/sum/min/max 네 필드를 함께
 *       갱신한다. ls_snapshot 은 **한 번의 lock 안에서** 네 필드를 out 으로 통째로
 *       복사한다(표본이 0건이면 min=max=0 으로 정규화).
 *       필드별로 락을 나눠 잡으면 찢어진(torn) 스냅샷이 나온다.
 *   EN: Update and copy the four stat fields inside a single critical section
 *       so a reader never sees a half-updated struct.
 *   ex: add 20,10,30 -> snapshot {count=3, sum=60, min=10, max=30}
 * ------------------------------------------------------------------------- */
void ls_init(LinkStats *s) {
    (void)s;
    // TODO: implement
}

void ls_destroy(LinkStats *s) {
    (void)s;
    // TODO: implement
}

void ls_add(LinkStats *s, int rtt_ms) {
    (void)s; (void)rtt_ms;
    // TODO: implement
}

void ls_snapshot(LinkStats *s, LinkStatsView *out) {
    (void)s; (void)out;
    // TODO: implement
}

typedef struct { LinkStats *s; int lo, hi; } LsArg;

static void *ls_writer(void *p) {
    LsArg *a = (LsArg *)p;
    for (int v = a->lo; v <= a->hi; ++v) ls_add(a->s, v);
    return NULL;
}

static _Atomic long g_ls_bad_snapshots = 0;
static _Atomic bool g_ls_reader_stop   = false;

static void *ls_reader(void *p) {
    LinkStats *s = (LinkStats *)p;
    while (!atomic_load(&g_ls_reader_stop)) {
        LinkStatsView v = {0, 0, 0, 0};
        ls_snapshot(s, &v);
        // 불변식: min<=max, 그리고 sum은 count*min ~ count*max 사이에 있어야 한다.
        if (v.count > 0) {
            if (v.min_ms > v.max_ms ||
                v.sum_ms < (long)v.min_ms * v.count ||
                v.sum_ms > (long)v.max_ms * v.count) {
                atomic_fetch_add(&g_ls_bad_snapshots, 1L);
            }
        }
    }
    return NULL;
}

// ===========================================================================
// Q4. 임계구역 최소화 — 느린 I/O를 락 밖으로
// ===========================================================================
typedef struct {
    char            last[64];
    long            queued;
    _Atomic long    io_calls;
    pthread_mutex_t m;
} LogSink;

static bool         g_lockcheck_on     = false;
static _Atomic long g_io_inside_lock   = 0;

// --- 아래 4개는 이미 구현되어 있다 (참고용 / 측정 장치). 건드리지 말 것 ---
void sink_init(LogSink *s) {
    if (!s) return;
    s->last[0] = '\0';
    s->queued  = 0;
    atomic_store(&s->io_calls, 0L);
    pthread_mutex_init(&s->m, NULL);
}

void sink_destroy(LogSink *s) {
    if (!s) return;
    pthread_mutex_destroy(&s->m);
}

// 느린 업로드 흉내 (모뎀으로 blocking write 한다고 치자).
// g_lockcheck_on 이 켜져 있으면 "호출 시점에 락을 쥐고 있었는가"를 trylock 으로 측정한다.
static void slow_upload(LogSink *s, const char *msg) {
    (void)msg;
    if (g_lockcheck_on) {
        if (pthread_mutex_trylock(&s->m) == 0) pthread_mutex_unlock(&s->m);  // 락 안 쥠 = 정답
        else                                   atomic_fetch_add(&g_io_inside_lock, 1L);
    }
    for (volatile int i = 0; i < 200; ++i) { }   // 지연 흉내
    atomic_fetch_add(&s->io_calls, 1L);
}

// ★ 나쁜 예 (참고용): 락을 쥔 채 업로드한다. 모뎀이 막히면 전원이 막힌다.
bool sink_write_bad(LogSink *s, const char *msg) {
    if (!s || !msg) return false;
    pthread_mutex_lock(&s->m);
    snprintf(s->last, sizeof s->last, "%s", msg);
    s->queued++;
    slow_upload(s, s->last);          // ← 임계구역 안에서 I/O
    pthread_mutex_unlock(&s->m);
    return true;
}

/* ---------------------------------------------------------------------------
 * Q4.  임계구역 최소화 — I/O를 락 밖으로
 *   KO: sink_write 는 위 sink_write_bad 와 같은 일을 하되 **slow_upload 를 락 밖에서**
 *       부른다. 락 안에서는 s->last 갱신 + s->queued++ + 지역 배열로 복사(snapshot)
 *       까지만 하고 unlock 한 뒤 slow_upload(s, copy) 를 호출한다.
 *       sink_queued 는 락을 잡고 queued 를 반환(NULL 이면 -1).
 *       s 나 msg 가 NULL 이면 false.
 *   EN: Copy what you need under the lock, unlock, then do the slow I/O.
 *       Never call blocking I/O or a user callback inside a critical section.
 *   ex: 3회 write -> g_io_inside_lock==0, io_calls==3, queued==3
 * ------------------------------------------------------------------------- */
bool sink_write(LogSink *s, const char *msg) {
    (void)s; (void)msg;
    (void)slow_upload;   // 구현 시 락을 **푼 뒤** 이 함수를 호출한다
    // TODO: implement
    return false;
}

long sink_queued(LogSink *s) {
    (void)s;
    // TODO: implement
    return -1;
}

typedef struct { LogSink *s; long iters; } SinkArg;

static void *sink_worker(void *p) {
    SinkArg *a = (SinkArg *)p;
    for (long i = 0; i < a->iters; ++i) sink_write(a->s, "link up");
    return NULL;
}

// ===========================================================================
// Q5. 중첩 락 방지 — `_locked` 접미사 규약
// ===========================================================================
#define POE_PORTS 2

typedef struct {
    int             port_mw[POE_PORTS];
    int             total_mw;        // 파생값 — 항상 port_mw 합과 같아야 한다 (불변식)
    pthread_mutex_t m;               // PTHREAD_MUTEX_ERRORCHECK
} PoeTable;

typedef struct { int port_mw[POE_PORTS]; int total_mw; } PoeView;

// --- 이미 구현됨: ERRORCHECK 뮤텍스로 초기화한다 (중첩 락이 행 대신 EDEADLK 가 되도록) ---
void poe_init(PoeTable *t) {
    if (!t) return;
    for (int i = 0; i < POE_PORTS; ++i) t->port_mw[i] = 0;
    t->total_mw = 0;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&t->m, &attr);
    pthread_mutexattr_destroy(&attr);
}

void poe_destroy(PoeTable *t) {
    if (!t) return;
    pthread_mutex_destroy(&t->m);
}

/* ---------------------------------------------------------------------------
 * Q5.  중첩 락 방지 — `_locked` 접미사 규약 (PoE 포트 전력 테이블)
 *   KO: 내부 헬퍼는 "호출자가 이미 t->m 을 쥐고 있다"를 전제로 락을 잡지 않는다.
 *       - poe_recompute_locked : port_mw 합을 total_mw 에 다시 계산 (락 없음)
 *       - poe_set_port_locked  : port_mw[idx]=mw 후 recompute (락 없음)
 *       공개 API 는 여기서만 락을 잡는다.
 *       - poe_set_port  : 인자 검증(idx 범위, mw>=0) 후 lock → _locked → unlock
 *       - poe_set_both  : **lock 한 번**으로 두 포트를 _locked 헬퍼로 원자 갱신
 *                         (여기서 poe_set_port 를 부르면 중첩 락!)
 *       - poe_snapshot  : lock 한 번으로 port_mw[] 와 total_mw 를 통째 복사
 *       - poe_relock_errno : 교육용. lock 을 잡은 상태에서 한 번 더 lock 을 시도하고
 *                         그 반환코드를 돌려준다(ERRORCHECK → EDEADLK). 반환 전에
 *                         정리(성공했다면 unlock)하고 원래 락도 unlock. t=NULL 이면 -1.
 *   EN: Public functions take the lock; internal helpers suffixed `_locked`
 *       assume it is already held, so compound operations need only one lock.
 *   ex: poe_set_both(&t, 20000, 10000) -> {20000,10000}, total==30000
 * ------------------------------------------------------------------------- */
static void poe_recompute_locked(PoeTable *t) {
    (void)t;
    // TODO: implement
}

static void poe_set_port_locked(PoeTable *t, int idx, int mw) {
    (void)t; (void)idx; (void)mw;
    (void)poe_recompute_locked;
    // TODO: implement
}

bool poe_set_port(PoeTable *t, int idx, int mw) {
    (void)t; (void)idx; (void)mw;
    (void)poe_set_port_locked;
    // TODO: implement
    return false;
}

bool poe_set_both(PoeTable *t, int a_mw, int b_mw) {
    (void)t; (void)a_mw; (void)b_mw;
    // TODO: implement  (lock 1회 + poe_set_port_locked 2회)
    return false;
}

void poe_snapshot(PoeTable *t, PoeView *out) {
    (void)t; (void)out;
    // TODO: implement
}

int poe_relock_errno(PoeTable *t) {
    (void)t;
    // TODO: implement
    return 0;   // placeholder
}

static _Atomic long g_poe_bad_views = 0;
static _Atomic bool g_poe_stop      = false;

static void *poe_writer(void *p) {
    PoeTable *t = (PoeTable *)p;
    for (int i = 0; i < 20000; ++i) {
        if (i & 1) poe_set_both(t, 15000, 15000);
        else       poe_set_both(t, 20000, 10000);
    }
    return NULL;
}

static void *poe_checker(void *p) {
    PoeTable *t = (PoeTable *)p;
    while (!atomic_load(&g_poe_stop)) {
        PoeView v = {{0, 0}, 0};
        poe_snapshot(t, &v);
        // 불변식: total == 합, 그리고 (a,b)는 writer가 쓰는 쌍 중 하나(또는 초기값)
        bool sum_ok  = (v.total_mw == v.port_mw[0] + v.port_mw[1]);
        bool pair_ok = (v.port_mw[0] == 0     && v.port_mw[1] == 0)     ||
                       (v.port_mw[0] == 15000 && v.port_mw[1] == 15000) ||
                       (v.port_mw[0] == 20000 && v.port_mw[1] == 10000);
        if (!sum_ok || !pair_ok) atomic_fetch_add(&g_poe_bad_views, 1L);
    }
    return NULL;
}

// ===========================================================================
// Q6. TOCTOU — PoE 전력 예산 예약을 원자적으로
// ===========================================================================
typedef struct {
    int             total_mw;
    int             used_mw;
    pthread_mutex_t m;
} PoeBudget;

// --- 이미 구현됨 ---
void budget_init(PoeBudget *b, int total_mw) {
    if (!b) return;
    b->total_mw = total_mw;
    b->used_mw  = 0;
    pthread_mutex_init(&b->m, NULL);
}

void budget_destroy(PoeBudget *b) {
    if (!b) return;
    pthread_mutex_destroy(&b->m);
}

// ★ 나쁜 예 (참고용): 검사와 행동 사이에 락이 풀린다 → over-commit
bool budget_reserve_toctou(PoeBudget *b, int mw) {
    if (!b || mw <= 0) return false;
    pthread_mutex_lock(&b->m);
    bool fits = (b->used_mw + mw <= b->total_mw);   // check ...
    pthread_mutex_unlock(&b->m);                    // ← 여기서 창이 열린다

    if (!fits) return false;
    sched_yield();                                  // 창을 벌려 버그를 재현

    pthread_mutex_lock(&b->m);
    b->used_mw += mw;                               // ... act (조건 재확인 없음)
    pthread_mutex_unlock(&b->m);
    return true;
}

/* ---------------------------------------------------------------------------
 * Q6.  TOCTOU 없는 전력 예산 예약 (802.3bt, 총 60W)
 *   KO: budget_reserve(b, mw) 는 "used+mw <= total 인가?"(check)와 "used += mw"(act)를
 *       **같은 임계구역 안에서** 수행하고 성공 여부를 반환한다. mw<=0 또는 NULL 이면 false.
 *       budget_release(b, mw) 는 락 안에서 used 를 줄이되 0 밑으로는 내려가지 않는다.
 *       budget_used(b) 는 락을 잡고 used 를 반환(NULL 이면 -1).
 *       위 budget_reserve_toctou 와 비교해 보면 왜 쪼개면 안 되는지 보인다.
 *   EN: Do the check and the act in one critical section, so concurrent
 *       reservations can never over-commit the power budget.
 *   ex: total=60000, 8스레드가 각각 15000 예약 -> 정확히 4개만 성공, used==60000
 * ------------------------------------------------------------------------- */
bool budget_reserve(PoeBudget *b, int mw) {
    (void)b; (void)mw;
    // TODO: implement
    return false;
}

void budget_release(PoeBudget *b, int mw) {
    (void)b; (void)mw;
    // TODO: implement
}

int budget_used(PoeBudget *b) {
    (void)b;
    // TODO: implement
    return -1;
}

typedef struct { PoeBudget *b; int mw; bool use_toctou; bool granted; } ResArg;

static void *reserve_worker(void *p) {
    ResArg *a = (ResArg *)p;
    a->granted = a->use_toctou ? budget_reserve_toctou(a->b, a->mw)
                               : budget_reserve(a->b, a->mw);
    return NULL;
}

// nthreads개 스레드가 동시에 mw씩 예약 시도. 성공 개수를 반환. (테스트 헬퍼 — 이미 구현됨)
static int run_reservations(PoeBudget *b, int nthreads, int mw, bool use_toctou) {
    pthread_t th[MAX_WORKERS];
    ResArg    args[MAX_WORKERS];
    if (nthreads < 1 || nthreads > MAX_WORKERS) return -1;

    for (int i = 0; i < nthreads; ++i) {
        args[i].b = b; args[i].mw = mw; args[i].use_toctou = use_toctou; args[i].granted = false;
    }
    for (int i = 0; i < nthreads; ++i) pthread_create(&th[i], NULL, reserve_worker, &args[i]);
    for (int i = 0; i < nthreads; ++i) pthread_join(th[i], NULL);

    int granted = 0;
    for (int i = 0; i < nthreads; ++i) if (args[i].granted) granted++;
    return granted;
}

// ===========================================================================
// Q7. pthread_once 로 스레드 안전 lazy init
// ===========================================================================
typedef struct {
    int  fd;
    int  apn_id;
    char name[16];
} ModemHandle;

static pthread_once_t g_modem_once  = PTHREAD_ONCE_INIT;
static ModemHandle    g_modem;
static _Atomic int    g_modem_init_calls = 0;

// --- 이미 구현됨: 이 초기화 루틴이 정확히 1회만 불려야 한다 ---
static void modem_init_once(void) {
    atomic_fetch_add(&g_modem_init_calls, 1);
    g_modem.fd     = 42;
    g_modem.apn_id = 7;
    snprintf(g_modem.name, sizeof g_modem.name, "wwan0");
}

/* ---------------------------------------------------------------------------
 * Q7.  pthread_once 로 모뎀 핸들 1회 초기화
 *   KO: modem_get() 은 여러 스레드가 동시에 불러도 modem_init_once 가 **정확히 1회**만
 *       실행되도록 하고, 초기화가 끝난 &g_modem 을 반환한다.
 *       `if (!inited) init();` 은 두 스레드가 동시에 통과할 수 있으므로 금지.
 *       modem_init_calls() 는 지금까지의 초기화 호출 횟수를 반환한다.
 *   EN: Use pthread_once so lazy initialization runs exactly once and late
 *       callers see the fully-initialized handle.
 *   ex: 8스레드가 modem_get() -> 모두 같은 포인터, init_calls==1
 * ------------------------------------------------------------------------- */
ModemHandle *modem_get(void) {
    (void)modem_init_once; (void)g_modem_once;
    // TODO: implement  (힌트: pthread_once(&g_modem_once, modem_init_once))
    return NULL;
}

int modem_init_calls(void) {
    // TODO: implement
    return -1;
}

static void *modem_worker(void *p) {
    ModemHandle **slot = (ModemHandle **)p;
    *slot = modem_get();
    return NULL;
}

// ===========================================================================
// Q8. 스레드별 로컬 집계 후 병합 — 경합 줄이기
// ===========================================================================
static long            g_merged_total = 0;
static long            g_merge_locks  = 0;
static pthread_mutex_t g_merge_m      = PTHREAD_MUTEX_INITIALIZER;

typedef struct { long iters; } ShardArg;

/* ---------------------------------------------------------------------------
 * Q8.  스레드별 로컬 집계 후 한 번만 병합
 *   KO: shard_worker 는 **지역 변수**에 a->iters 번 누적한 뒤, 루프가 끝난 다음에
 *       g_merge_m 을 딱 한 번 잡아 g_merged_total 에 더하고 g_merge_locks 를 1 증가시킨다.
 *       (루프 안에서 락을 잡으면 Q2와 똑같아진다 — 락 획득이 iters 배로 늘어난다.)
 *       sharded_sum_run 은 g_merged_total/g_merge_locks 를 0으로 리셋하고 nthreads 개
 *       스레드를 띄워 join 한 뒤, 총합을 반환하고 *out_locks(NULL 가능)에 락 횟수를 넣는다.
 *       nthreads 범위 밖이면 -1.
 *   EN: Accumulate per-thread in a local, then merge once under the lock;
 *       lock acquisitions drop from N*iters to N.
 *   ex: sharded_sum_run(4, 50000, &locks) -> 200000, locks==4
 * ------------------------------------------------------------------------- */
static void *shard_worker(void *p) {
    ShardArg *a = (ShardArg *)p;
    (void)a; (void)g_merged_total; (void)g_merge_locks; (void)g_merge_m;
    // TODO: implement
    return NULL;
}

long sharded_sum_run(int nthreads, long iters, long *out_locks) {
    (void)nthreads; (void)iters; (void)out_locks;
    (void)shard_worker;
    // TODO: implement
    return -1;
}

// ===========================================================================
// Q9. pthread_rwlock — 읽기 다수 / 쓰기 소수 설정 테이블
// ===========================================================================
typedef struct {
    int              apn_id;
    int              mtu;
    char             apn[32];
    pthread_rwlock_t rw;
} WanConfig;

/* ---------------------------------------------------------------------------
 * Q9.  pthread_rwlock 으로 WAN 설정 테이블 보호
 *   KO: 불변식은 mtu == 1000 + apn_id 이고 apn 문자열 == "apn-<apn_id>" 다.
 *       wc_init  : rwlock 초기화 + apn_id=1, mtu=1001, apn="apn-1"
 *       wc_set_apn(c, id) : **wrlock** 안에서 세 필드를 함께 갱신
 *       wc_read  : **rdlock** 안에서 세 필드를 호출자 버퍼로 복사(문자열은 snprintf 로
 *                  n 바이트 한도). 인자 중 NULL 이거나 n==0 이면 false.
 *       wc_destroy : rwlock 파괴.
 *       rdlock 은 reader 끼리 병렬 진입을 허용하고 writer 만 배타로 만든다.
 *   EN: Guard a rarely-written, often-read config table with a rwlock; readers
 *       take rdlock and copy out, the writer takes wrlock and updates together.
 *   ex: wc_set_apn(&c,4) 뒤 wc_read -> id=4, mtu=1004, "apn-4"
 * ------------------------------------------------------------------------- */
void wc_init(WanConfig *c) {
    (void)c;
    // TODO: implement
}

void wc_destroy(WanConfig *c) {
    (void)c;
    // TODO: implement
}

void wc_set_apn(WanConfig *c, int apn_id) {
    (void)c; (void)apn_id;
    // TODO: implement
}

bool wc_read(WanConfig *c, int *apn_id, int *mtu, char *buf, size_t n) {
    (void)c; (void)apn_id; (void)mtu; (void)buf; (void)n;
    // TODO: implement
    return false;
}

static _Atomic long g_wc_reads      = 0;
static _Atomic long g_wc_torn       = 0;
static _Atomic bool g_wc_stop       = false;

static void *wc_reader(void *p) {
    WanConfig *c = (WanConfig *)p;
    while (!atomic_load(&g_wc_stop)) {
        int id = 0, mtu = 0;
        char buf[32] = {0}, expect[32] = {0};
        wc_read(c, &id, &mtu, buf, sizeof buf);
        snprintf(expect, sizeof expect, "apn-%d", id);
        if (mtu != 1000 + id || strcmp(buf, expect) != 0)
            atomic_fetch_add(&g_wc_torn, 1L);
        atomic_fetch_add(&g_wc_reads, 1L);
    }
    return NULL;
}

static void *wc_writer(void *p) {
    WanConfig *c = (WanConfig *)p;
    for (int i = 0; i < 3000; ++i) wc_set_apn(c, (i % 5) + 1);
    return NULL;
}

// ===========================================================================
// Q10. 취소 가능한 워커 — stop 플래그 + join 으로 깔끔한 종료
// ===========================================================================
typedef struct {
    pthread_t       th;
    bool            running;      // m으로 보호
    bool            stop;         // m으로 보호
    _Atomic long    ticks;        // 외부에서 락 없이 관찰
    pthread_mutex_t m;
} Worker;

/* ---------------------------------------------------------------------------
 * Q10. 협조적으로 취소 가능한 워커 (pthread_cancel 금지)
 *   KO: worker_init/worker_destroy : 필드와 mutex 초기화/파괴 (running=stop=false, ticks=0).
 *       worker_should_stop : 락을 잡고 stop 플래그를 읽어 반환 (w=NULL 이면 true).
 *       worker_ticks       : atomic_load 로 ticks 반환 (w=NULL 이면 -1).
 *       worker_main        : !worker_should_stop(w) 동안 ticks 를 1씩 증가.
 *       worker_start       : 이미 running 이면 false. stop=false/running=true 로 두고
 *                            pthread_create. 생성 실패면 running 을 되돌리고 false.
 *       worker_stop_and_join : 락 안에서 stop=true, running=false 로 바꾸고 "join 필요 여부"를
 *                            기억한 뒤 **락을 푼 상태에서** join. 총 ticks 를 반환.
 *                            이미 멈춘 워커에 다시 불러도 안전해야 한다(idempotent).
 *                            w=NULL 이면 -1.
 *   EN: Cooperative shutdown: set a mutex-protected stop flag, then join.
 *       stop_and_join must be idempotent and the worker restartable.
 *   ex: start -> tick 증가 -> stop_and_join -> ticks 고정, 두 번 불러도 같은 값
 * ------------------------------------------------------------------------- */
void worker_init(Worker *w) {
    (void)w;
    // TODO: implement
}

void worker_destroy(Worker *w) {
    (void)w;
    // TODO: implement
}

bool worker_should_stop(Worker *w) {
    (void)w;
    // TODO: implement
    return true;   // placeholder (미구현 시 워커가 즉시 멈추도록)
}

long worker_ticks(Worker *w) {
    (void)w;
    // TODO: implement
    return 0;   // placeholder
}

static void *worker_main(void *p) {
    Worker *w = (Worker *)p;
    (void)w;
    // TODO: implement  (while (!worker_should_stop(w)) ticks++)
    return NULL;
}

bool worker_start(Worker *w) {
    (void)w;
    (void)worker_main;   // 구현 시 pthread_create 의 start_routine 으로 쓴다
    // TODO: implement
    return false;
}

long worker_stop_and_join(Worker *w) {
    (void)w;
    // TODO: implement
    return -1;
}

// 테스트 헬퍼: ticks가 target 이상이 될 때까지 유한 횟수만 스핀한다.
// (미구현 stub 에서도 절대 멈추지 않도록 상한을 둔다 — 건드리지 말 것)
static long spin_until_ticks(Worker *w, long target, long max_spins) {
    for (long i = 0; i < max_spins; ++i) {
        long t = worker_ticks(w);
        if (t >= target) return t;
        if ((i & 0x3FF) == 0) sched_yield();
    }
    return worker_ticks(w);
}

// ===========================================================================
// main — 테스트 (건드리지 말 것)
// ===========================================================================
int main(void) {
    // ---------------- Q1 ----------------
    printf("== Q1. 스레드 생성/조인 + 보호 없는 카운터 (race 데모) ==\n");
    long attempts = 0;
    long observed = race_demo(4, 20000, &attempts);
    T("Q1 race_demo: 4개 스레드 생성/조인 성공",          observed >= 0);
    T("Q1 스레드별 슬롯 합계 == 4*20000 (경합 없음)",      attempts == 80000);
    T("Q1 공유 카운터 <= 시도 횟수 (lost update 가능)",    observed <= attempts);
    T("Q1 공유 카운터 >= 1 (최소 한 번은 반영)",            observed >= 1);
    T("Q1 잘못된 스레드 수는 -1",                          race_demo(0, 10, NULL) == -1 &&
                                                           race_demo(MAX_WORKERS + 1, 10, NULL) == -1);
    printf("     (관측: counter=%ld / attempts=%ld, 손실=%ld%s)\n",
           observed, attempts, attempts - observed,
           VK_RACE_CONCURRENT ? "" : " — TSan 빌드라 직렬 실행됨");

    // ---------------- Q2 ----------------
    printf("== Q2. mutex로 보호한 카운터 ==\n");
    T("Q2 4스레드 x 20000 == 80000 (정확)",  safe_counter_run(4, 20000) == 80000);
    T("Q2 1스레드 x 1000 == 1000",           safe_counter_run(1, 1000) == 1000);
    T("Q2 8스레드 x 5000 == 40000",          safe_counter_run(8, 5000) == 40000);
    {
        SafeCounter c;
        sc_init(&c);
        T("Q2 sc_init 후 값 0", sc_get(&c) == 0);
        sc_inc(&c); sc_inc(&c); sc_inc(&c);
        T("Q2 sc_inc 3회 후 3", sc_get(&c) == 3);
        sc_destroy(&c);
    }
    T("Q2 NULL 방어: sc_get(NULL) == -1", sc_get(NULL) == -1);

    // ---------------- Q3 ----------------
    printf("== Q3. 구조체 불변식 보호 (링크 통계) ==\n");
    {
        LinkStats s;
        LinkStatsView v = {0, 0, 0, 0};
        ls_init(&s);
        ls_snapshot(&s, &v);
        T("Q3 빈 통계: count=0, min=max=0", v.count == 0 && v.min_ms == 0 && v.max_ms == 0);

        ls_add(&s, 20); ls_add(&s, 10); ls_add(&s, 30);
        ls_snapshot(&s, &v);
        T("Q3 count/sum 정확",      v.count == 3 && v.sum_ms == 60);
        T("Q3 min/max 정확",        v.min_ms == 10 && v.max_ms == 30);
        ls_destroy(&s);
    }
    {
        LinkStats s;
        LinkStatsView v = {0, 0, 0, 0};
        pthread_t wth[4], rth;
        LsArg args[4];
        ls_init(&s);
        atomic_store(&g_ls_bad_snapshots, 0L);
        atomic_store(&g_ls_reader_stop, false);
        pthread_create(&rth, NULL, ls_reader, &s);
        for (int i = 0; i < 4; ++i) { args[i].s = &s; args[i].lo = 1; args[i].hi = 5000;
                                      pthread_create(&wth[i], NULL, ls_writer, &args[i]); }
        for (int i = 0; i < 4; ++i) pthread_join(wth[i], NULL);
        atomic_store(&g_ls_reader_stop, true);
        pthread_join(rth, NULL);

        ls_snapshot(&s, &v);
        long expect_sum = 4L * (5000L * 5001L / 2L);
        T("Q3 동시 갱신 후 count == 20000",  v.count == 20000);
        T("Q3 동시 갱신 후 sum 정확",        v.sum_ms == expect_sum);
        T("Q3 min=1, max=5000",              v.min_ms == 1 && v.max_ms == 5000);
        T("Q3 찢어진 snapshot 0건",          atomic_load(&g_ls_bad_snapshots) == 0);
        ls_destroy(&s);
    }

    // ---------------- Q4 ----------------
    printf("== Q4. 임계구역 최소화 (I/O를 락 밖으로) ==\n");
    {
        LogSink s;
        sink_init(&s);
        g_lockcheck_on = true;
        atomic_store(&g_io_inside_lock, 0L);
        sink_write(&s, "wan up"); sink_write(&s, "sim1"); sink_write(&s, "poe on");
        T("Q4 sink_write: I/O가 락 밖에서 실행됨", atomic_load(&g_io_inside_lock) == 0);
        T("Q4 sink_write: I/O 3회, queued 3",      atomic_load(&s.io_calls) == 3 && sink_queued(&s) == 3);

        atomic_store(&g_io_inside_lock, 0L);
        sink_write_bad(&s, "a"); sink_write_bad(&s, "b"); sink_write_bad(&s, "c");
        T("Q4 나쁜 예: I/O 3회 모두 락 안에서",     atomic_load(&g_io_inside_lock) == 3);
        g_lockcheck_on = false;
        T("Q4 NULL 방어",                           sink_write(&s, NULL) == false &&
                                                    sink_write(NULL, "x") == false);
        sink_destroy(&s);
    }
    {
        LogSink s;
        pthread_t th[4];
        SinkArg args[4];
        sink_init(&s);
        for (int i = 0; i < 4; ++i) { args[i].s = &s; args[i].iters = 500;
                                      pthread_create(&th[i], NULL, sink_worker, &args[i]); }
        for (int i = 0; i < 4; ++i) pthread_join(th[i], NULL);
        T("Q4 동시 write 2000건 손실 없음", sink_queued(&s) == 2000 &&
                                            atomic_load(&s.io_calls) == 2000);
        sink_destroy(&s);
    }

    // ---------------- Q5 ----------------
    printf("== Q5. 중첩 락 방지 (_locked 규약) ==\n");
    {
        PoeTable t;
        PoeView  v = {{0, 0}, 0};
        poe_init(&t);
        T("Q5 init: 모두 0",                 (poe_snapshot(&t, &v), v.total_mw == 0));
        T("Q5 poe_set_port(0, 25000)",       poe_set_port(&t, 0, 25000));
        poe_snapshot(&t, &v);
        T("Q5 total 재계산됨",               v.port_mw[0] == 25000 && v.total_mw == 25000);
        T("Q5 잘못된 인덱스 거부",           poe_set_port(&t, 5, 1000) == false &&
                                             poe_set_port(&t, -1, 1000) == false);
        T("Q5 poe_set_both 원자적 갱신",     poe_set_both(&t, 20000, 10000));
        poe_snapshot(&t, &v);
        T("Q5 두 포트 + total 일관",         v.port_mw[0] == 20000 && v.port_mw[1] == 10000 &&
                                             v.total_mw == 30000);
        T("Q5 중첩 락은 EDEADLK (재귀 아님)", poe_relock_errno(&t) == EDEADLK);
        poe_destroy(&t);
    }
    {
        PoeTable t;
        pthread_t wth[2], cth;
        poe_init(&t);
        atomic_store(&g_poe_bad_views, 0L);
        atomic_store(&g_poe_stop, false);
        pthread_create(&cth, NULL, poe_checker, &t);
        for (int i = 0; i < 2; ++i) pthread_create(&wth[i], NULL, poe_writer, &t);
        for (int i = 0; i < 2; ++i) pthread_join(wth[i], NULL);
        atomic_store(&g_poe_stop, true);
        pthread_join(cth, NULL);
        T("Q5 동시 갱신 중 불변식 위반 0건", atomic_load(&g_poe_bad_views) == 0);
        poe_destroy(&t);
    }

    // ---------------- Q6 ----------------
    printf("== Q6. TOCTOU — PoE 전력 예산 예약 ==\n");
    {
        PoeBudget b;
        budget_init(&b, 60000);                 // 60W
        T("Q6 단일: 15W 예약 성공",       budget_reserve(&b, 15000) && budget_used(&b) == 15000);
        T("Q6 단일: 50W 예약 거부",       budget_reserve(&b, 50000) == false && budget_used(&b) == 15000);
        budget_release(&b, 15000);
        T("Q6 release 후 used 0",         budget_used(&b) == 0);

        int granted = run_reservations(&b, 8, 15000, false);
        T("Q6 8스레드 x 15W → 정확히 4개만 성공", granted == 4);
        T("Q6 used == 60000 (예산 초과 없음)",     budget_used(&b) == 60000);
        T("Q6 예산 소진 후 추가 예약 거부",        budget_reserve(&b, 1000) == false);
        budget_destroy(&b);
    }
    {
        PoeBudget b;
        budget_init(&b, 60000);
        int granted = run_reservations(&b, 8, 15000, true);   // 나쁜 예
        T("Q6 TOCTOU 버전: 성공 개수 >= 4 (over-commit 가능)", granted >= 4);
        T("Q6 TOCTOU 버전: used == granted*15000",             budget_used(&b) == granted * 15000);
        printf("     (관측: TOCTOU granted=%d, used=%dmW / 예산 60000mW%s)\n",
               granted, budget_used(&b), granted > 4 ? "  ← over-commit 발생!" : "");
        budget_destroy(&b);
    }

    // ---------------- Q7 ----------------
    printf("== Q7. pthread_once 로 lazy init ==\n");
    {
        pthread_t th[8];
        ModemHandle *slots[8] = {0};
        for (int i = 0; i < 8; ++i) pthread_create(&th[i], NULL, modem_worker, &slots[i]);
        for (int i = 0; i < 8; ++i) pthread_join(th[i], NULL);

        bool all_same = true;
        for (int i = 0; i < 8; ++i) if (slots[i] != slots[0] || slots[i] == NULL) all_same = false;
        T("Q7 8스레드가 동일한 핸들을 받음", all_same);
        T("Q7 초기화 함수는 정확히 1회 실행", modem_init_calls() == 1);
        T("Q7 핸들 내용이 완전히 초기화됨",
          slots[0] && slots[0]->fd == 42 && slots[0]->apn_id == 7 &&
          strcmp(slots[0]->name, "wwan0") == 0);
        T("Q7 이후 재호출도 초기화 1회 유지", modem_get() == slots[0] && modem_init_calls() == 1);
    }

    // ---------------- Q8 ----------------
    printf("== Q8. 스레드별 로컬 집계 후 병합 ==\n");
    {
        long locks = -1;
        long total = sharded_sum_run(4, 50000, &locks);
        T("Q8 4스레드 x 50000 == 200000",      total == 200000);
        T("Q8 락 획득은 스레드당 1회 (=4회)",  locks == 4);

        locks = -1;
        total = sharded_sum_run(8, 10000, &locks);
        T("Q8 8스레드 x 10000 == 80000",       total == 80000);
        T("Q8 락 획득 8회 (Q2라면 80000회)",   locks == 8);
        T("Q8 잘못된 스레드 수는 -1",          sharded_sum_run(0, 10, NULL) == -1);
    }

    // ---------------- Q9 ----------------
    printf("== Q9. pthread_rwlock — 설정 테이블 ==\n");
    {
        WanConfig c;
        int id = 0, mtu = 0;
        char buf[32] = {0};
        wc_init(&c);
        T("Q9 init 기본값 읽기", wc_read(&c, &id, &mtu, buf, sizeof buf) &&
                                 id == 1 && mtu == 1001 && strcmp(buf, "apn-1") == 0);
        wc_set_apn(&c, 4);
        T("Q9 wrlock 후 세 필드 일관", wc_read(&c, &id, &mtu, buf, sizeof buf) &&
                                       id == 4 && mtu == 1004 && strcmp(buf, "apn-4") == 0);
        T("Q9 NULL 방어", wc_read(NULL, &id, &mtu, buf, sizeof buf) == false &&
                          wc_read(&c, &id, &mtu, buf, 0) == false);
        wc_destroy(&c);
    }
    {
        WanConfig c;
        pthread_t rth[4], wth;
        wc_init(&c);
        atomic_store(&g_wc_reads, 0L);
        atomic_store(&g_wc_torn, 0L);
        atomic_store(&g_wc_stop, false);
        for (int i = 0; i < 4; ++i) pthread_create(&rth[i], NULL, wc_reader, &c);
        pthread_create(&wth, NULL, wc_writer, &c);
        pthread_join(wth, NULL);
        atomic_store(&g_wc_stop, true);
        for (int i = 0; i < 4; ++i) pthread_join(rth[i], NULL);

        T("Q9 reader 4개가 실제로 읽음", atomic_load(&g_wc_reads) > 0);
        T("Q9 찢어진 읽기 0건 (mtu/apn 일관)", atomic_load(&g_wc_torn) == 0);
        wc_destroy(&c);
    }

    // ---------------- Q10 ----------------
    printf("== Q10. 취소 가능한 워커 (stop 플래그 + join) ==\n");
    {
        Worker w;
        worker_init(&w);
        T("Q10 worker_start 성공",        worker_start(&w));
        T("Q10 이중 start 거부",          worker_start(&w) == false);

        long t1 = spin_until_ticks(&w, 100, 2000000);
        T("Q10 워커가 실제로 동작 (>=100 tick)", t1 >= 100);

        long t2 = worker_stop_and_join(&w);
        long t3 = worker_ticks(&w);
        T("Q10 join 후 tick이 더 늘지 않음", t2 == t3 && t2 >= t1);
        T("Q10 stop_and_join 재호출 안전",   worker_stop_and_join(&w) == t2);
        T("Q10 종료 후 재시작 가능",         worker_start(&w));
        long t4 = worker_stop_and_join(&w);
        T("Q10 재시작 후에도 깔끔히 종료",   t4 >= t2);
        worker_destroy(&w);
    }
    T("Q10 NULL 방어", worker_stop_and_join(NULL) == -1 && worker_start(NULL) == false);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
