// 01_threads_mutex.c  —  REFERENCE SOLUTION
// 스레드 & 뮤텍스 기초 (Threads & Mutex Fundamentals)  —  Q1~Q10
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -O1 -g -pthread 01_threads_mutex.c -o /tmp/vk01 && /tmp/vk01
// Verkada Connectivity 인터뷰 1번 주제인 "thread safety"의 바닥을 깐다. GC31-E
// 게이트웨이 펌웨어는 모뎀/링크 모니터/PoE 제어/업로드 스레드가 같은 상태를
// 건드린다. 여기서 다루는 것: race를 눈으로 보기 → mutex로 불변식 보호 →
// 임계구역 최소화 → 중첩 락/TOCTOU 회피 → once/rwlock/샤딩/깔끔한 종료.
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
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ---------------------------------------------------------------------------
// ThreadSanitizer 빌드 감지.
// Q1은 "보호 없는 공유 카운터"를 일부러 보여주는 문제라 TSan이 반드시 경고한다.
// 그 경고는 정답이지만, 나머지 9문제의 TSan 클린 여부를 가리면 곤란하다.
// 그래서 TSan 빌드에서만 Q1 데모를 '직렬 실행'(create→join 즉시)으로 바꿔
// 전체 실행을 TSan 경고 0으로 유지한다. 일반 빌드에서는 진짜로 동시에 돈다.
//   → race를 직접 보고 싶으면: cc ... -fsanitize=thread -DVK_FORCE_RACE=1
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
// ---------------------------------------------------------------------------
// pthread_create / pthread_join 의 기본기와, 보호 없는 `counter = counter + 1`
// 이 왜 깨지는지를 동시에 본다. `++` 는 load-modify-store 3단계라 원자적이지
// 않다. 스레드별 슬롯(local_done)은 경합이 없으므로 "몇 번 시도했는가"는
// 정확히 셀 수 있고, 공유 카운터는 그보다 작아질 수 있다.
// 테스트는 결정적으로: attempts == N*iters, 1 <= observed <= attempts.
// ===========================================================================
static volatile long g_unsafe_counter = 0;   // volatile은 최적화 억제일 뿐, 원자성 없음

typedef struct {
    long iters;
    long local_done;     // 스레드마다 다른 슬롯 → 여기에는 race 없음
} RaceArg;

static void *race_worker(void *p) {
    RaceArg *a = (RaceArg *)p;
    for (long i = 0; i < a->iters; ++i) {
        g_unsafe_counter = g_unsafe_counter + 1;   // ★ 보호 없음 — 의도된 race
        a->local_done++;
    }
    return NULL;
}

// 반환: 관측된 최종 공유 카운터. *out_attempts: 스레드별로 센 총 증가 시도 횟수.
// 실패 시 -1.
long race_demo(int nthreads, long iters, long *out_attempts) {
    pthread_t th[MAX_WORKERS];
    RaceArg   args[MAX_WORKERS];
    if (nthreads < 1 || nthreads > MAX_WORKERS) return -1;

    g_unsafe_counter = 0;
    for (int i = 0; i < nthreads; ++i) { args[i].iters = iters; args[i].local_done = 0; }

    int created = 0;
    for (int i = 0; i < nthreads; ++i) {
        if (pthread_create(&th[i], NULL, race_worker, &args[i]) != 0) break;
        created++;
#if !VK_RACE_CONCURRENT
        pthread_join(th[i], NULL);   // TSan 빌드: 직렬 실행 → happens-before 성립
#endif
    }
#if VK_RACE_CONCURRENT
    for (int i = 0; i < created; ++i) pthread_join(th[i], NULL);
#endif
    if (created != nthreads) return -1;

    long attempts = 0;
    for (int i = 0; i < nthreads; ++i) attempts += args[i].local_done;
    if (out_attempts) *out_attempts = attempts;
    return g_unsafe_counter;
}

// ===========================================================================
// Q2. mutex로 보호한 카운터 — 정확히 N이 나온다
// ---------------------------------------------------------------------------
// 같은 루프를 mutex로 감싸면 결과가 항상 nthreads*iters 로 결정적이다.
// 임계구역은 "증가 한 줄"뿐 — 짧을수록 좋다.
// ===========================================================================
typedef struct {
    long            value;
    pthread_mutex_t m;
} SafeCounter;

void sc_init(SafeCounter *c) {
    if (!c) return;
    c->value = 0;
    pthread_mutex_init(&c->m, NULL);
}

void sc_destroy(SafeCounter *c) {
    if (!c) return;
    pthread_mutex_destroy(&c->m);
}

void sc_inc(SafeCounter *c) {
    if (!c) return;
    pthread_mutex_lock(&c->m);
    c->value++;                      // 임계구역: 딱 이 한 줄
    pthread_mutex_unlock(&c->m);
}

long sc_get(SafeCounter *c) {
    if (!c) return -1;
    pthread_mutex_lock(&c->m);        // 읽기도 보호해야 한다 (torn/stale 방지)
    long v = c->value;
    pthread_mutex_unlock(&c->m);
    return v;
}

typedef struct { SafeCounter *c; long iters; } SafeArg;

static void *safe_worker(void *p) {
    SafeArg *a = (SafeArg *)p;
    for (long i = 0; i < a->iters; ++i) sc_inc(a->c);
    return NULL;
}

// nthreads개 스레드가 각각 iters번 증가시킨 뒤 최종값을 반환. 실패 시 -1.
long safe_counter_run(int nthreads, long iters) {
    pthread_t th[MAX_WORKERS];
    SafeArg   args[MAX_WORKERS];
    SafeCounter c;
    if (nthreads < 1 || nthreads > MAX_WORKERS) return -1;

    sc_init(&c);
    for (int i = 0; i < nthreads; ++i) { args[i].c = &c; args[i].iters = iters; }
    for (int i = 0; i < nthreads; ++i) pthread_create(&th[i], NULL, safe_worker, &args[i]);
    for (int i = 0; i < nthreads; ++i) pthread_join(th[i], NULL);

    long v = sc_get(&c);
    sc_destroy(&c);
    return v;
}

// ===========================================================================
// Q3. 구조체 불변식 보호 — 링크 통계를 한 임계구역에서 일관되게 갱신
// ---------------------------------------------------------------------------
// GC31-E의 WAN 링크 RTT 통계(count/sum/min/max). 필드 4개가 "같은 순간의
// 값"이어야 한다는 것이 불변식이다. 필드마다 락을 따로 잡거나 snapshot을
// 여러 번 나눠 읽으면 count는 새 값, min은 옛 값인 찢어진(torn) 상태가 나온다.
// 규칙: 한 번의 lock 안에서 관련 필드를 전부 갱신 / 전부 복사한다.
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

void ls_init(LinkStats *s) {
    if (!s) return;
    s->count = 0;
    s->sum_ms = 0;
    s->min_ms = INT_MAX;
    s->max_ms = INT_MIN;
    pthread_mutex_init(&s->m, NULL);
}

void ls_destroy(LinkStats *s) {
    if (!s) return;
    pthread_mutex_destroy(&s->m);
}

void ls_add(LinkStats *s, int rtt_ms) {
    if (!s) return;
    pthread_mutex_lock(&s->m);
    s->count++;
    s->sum_ms += rtt_ms;
    if (rtt_ms < s->min_ms) s->min_ms = rtt_ms;
    if (rtt_ms > s->max_ms) s->max_ms = rtt_ms;
    pthread_mutex_unlock(&s->m);      // 네 필드가 한 임계구역에서 함께 갱신됨
}

// 한 번의 lock으로 4개 필드를 통째로 복사한다. 표본이 없으면 min/max = 0.
void ls_snapshot(LinkStats *s, LinkStatsView *out) {
    if (!s || !out) return;
    pthread_mutex_lock(&s->m);
    out->count  = s->count;
    out->sum_ms = s->sum_ms;
    out->min_ms = (s->count == 0) ? 0 : s->min_ms;
    out->max_ms = (s->count == 0) ? 0 : s->max_ms;
    pthread_mutex_unlock(&s->m);
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
// ---------------------------------------------------------------------------
// "락 안에서 I/O도 콜백도 부르지 않는다"는 임베디드 동시성의 1번 규칙.
// 락 안에서는 상태만 바꾸고 필요한 데이터를 지역 변수로 복사한 뒤, 락을 풀고
// 업로드한다. 락 안에서 blocking write를 하면 모뎀이 느려질 때 전체 파이프라인이
// 멈추고, 콜백을 부르면 그 콜백이 다시 락을 잡아 데드락이 난다.
// 검증 트릭: 가짜 I/O 안에서 같은 mutex를 trylock 해본다. 성공하면 "락을 안 쥔
// 상태"(정답), EBUSY면 "락을 쥔 채 I/O"(오답). 단일 스레드에서만 유효한 측정이라
// g_lockcheck_on 플래그로 측정 구간을 한정한다.
// ===========================================================================
typedef struct {
    char            last[64];
    long            queued;
    _Atomic long    io_calls;
    pthread_mutex_t m;
} LogSink;

static bool         g_lockcheck_on     = false;
static _Atomic long g_io_inside_lock   = 0;

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

// 느린 업로드 흉내 (모뎀으로 blocking write 한다고 치자). 건드리지 말 것.
static void slow_upload(LogSink *s, const char *msg) {
    (void)msg;
    if (g_lockcheck_on) {
        if (pthread_mutex_trylock(&s->m) == 0) pthread_mutex_unlock(&s->m);  // 락 안 쥠 = 정답
        else                                   atomic_fetch_add(&g_io_inside_lock, 1L);
    }
    for (volatile int i = 0; i < 200; ++i) { }   // 지연 흉내
    atomic_fetch_add(&s->io_calls, 1L);
}

// ★ 나쁜 예 (참고용, 이미 구현됨): 락을 쥔 채 업로드한다.
bool sink_write_bad(LogSink *s, const char *msg) {
    if (!s || !msg) return false;
    pthread_mutex_lock(&s->m);
    snprintf(s->last, sizeof s->last, "%s", msg);
    s->queued++;
    slow_upload(s, s->last);          // ← 임계구역 안에서 I/O. 모뎀이 막히면 전원이 막힌다.
    pthread_mutex_unlock(&s->m);
    return true;
}

// ★ 좋은 예: 락 안에서는 상태 갱신 + 지역 복사만, I/O는 락 밖에서.
bool sink_write(LogSink *s, const char *msg) {
    if (!s || !msg) return false;
    char copy[64];
    pthread_mutex_lock(&s->m);
    snprintf(s->last, sizeof s->last, "%s", msg);
    s->queued++;
    memcpy(copy, s->last, sizeof copy);   // 락 안에서 스냅샷
    pthread_mutex_unlock(&s->m);

    slow_upload(s, copy);                 // 락 밖에서 느린 작업
    return true;
}

long sink_queued(LogSink *s) {
    if (!s) return -1;
    pthread_mutex_lock(&s->m);
    long q = s->queued;
    pthread_mutex_unlock(&s->m);
    return q;
}

typedef struct { LogSink *s; long iters; } SinkArg;

static void *sink_worker(void *p) {
    SinkArg *a = (SinkArg *)p;
    for (long i = 0; i < a->iters; ++i) sink_write(a->s, "link up");
    return NULL;
}

// ===========================================================================
// Q5. 중첩 락 방지 — `_locked` 접미사 규약
// ---------------------------------------------------------------------------
// 공개 함수는 락을 잡고, 내부 헬퍼는 "호출자가 이미 락을 쥐고 있다"를 전제로
// `_locked` 접미사를 붙여 락 없는 버전으로 둔다. 그러면 여러 연산을 한 임계
// 구역으로 묶을 때 공개 함수를 재호출하지 않아도 된다.
// 재귀 뮤텍스(PTHREAD_MUTEX_RECURSIVE)를 쓰지 않는 이유: 잠긴 채 함수를 빠져나가도
// 컴파일이 되고, "이 함수가 불변식을 깬 중간 상태에서 불릴 수 있다"는 사실이
// 감춰지며, condvar와 섞이면 wait 시 카운트가 1만 내려가 버그가 난다.
// 여기서는 진단용으로 ERRORCHECK 뮤텍스를 써서 중첩 락이 EDEADLK 로 즉시 잡히게 한다.
// ===========================================================================
#define POE_PORTS 2

typedef struct {
    int             port_mw[POE_PORTS];
    int             total_mw;        // 파생값 — 항상 port_mw 합과 같아야 한다 (불변식)
    pthread_mutex_t m;               // PTHREAD_MUTEX_ERRORCHECK
} PoeTable;

typedef struct { int port_mw[POE_PORTS]; int total_mw; } PoeView;

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

// --- 락 없는 내부 헬퍼들: 호출자가 t->m 을 쥐고 있어야 한다 ---
static void poe_recompute_locked(PoeTable *t) {
    int sum = 0;
    for (int i = 0; i < POE_PORTS; ++i) sum += t->port_mw[i];
    t->total_mw = sum;
}

static void poe_set_port_locked(PoeTable *t, int idx, int mw) {
    t->port_mw[idx] = mw;
    poe_recompute_locked(t);
}

// --- 공개 API: 여기서만 락을 잡는다 ---
bool poe_set_port(PoeTable *t, int idx, int mw) {
    if (!t || idx < 0 || idx >= POE_PORTS || mw < 0) return false;
    pthread_mutex_lock(&t->m);
    poe_set_port_locked(t, idx, mw);
    pthread_mutex_unlock(&t->m);
    return true;
}

// 두 포트를 한 임계구역에서 원자적으로 바꾼다.
// ★ 여기서 poe_set_port() 를 부르면 중첩 락 → 데드락(또는 EDEADLK).
//   `_locked` 버전을 부르기 때문에 안전하다.
bool poe_set_both(PoeTable *t, int a_mw, int b_mw) {
    if (!t || a_mw < 0 || b_mw < 0) return false;
    pthread_mutex_lock(&t->m);
    poe_set_port_locked(t, 0, a_mw);
    poe_set_port_locked(t, 1, b_mw);
    pthread_mutex_unlock(&t->m);
    return true;
}

void poe_snapshot(PoeTable *t, PoeView *out) {
    if (!t || !out) return;
    pthread_mutex_lock(&t->m);
    for (int i = 0; i < POE_PORTS; ++i) out->port_mw[i] = t->port_mw[i];
    out->total_mw = t->total_mw;
    pthread_mutex_unlock(&t->m);
}

// 교육용: 같은 스레드에서 두 번 lock 하면 ERRORCHECK 뮤텍스는 EDEADLK 를 준다.
// (기본 뮤텍스였다면 여기서 그냥 멈춘다 = 프로덕션에서 찾기 힘든 행)
int poe_relock_errno(PoeTable *t) {
    if (!t) return -1;
    pthread_mutex_lock(&t->m);
    int rc = pthread_mutex_lock(&t->m);       // 중첩 락 시도
    if (rc == 0) pthread_mutex_unlock(&t->m); // 만약 성공했다면(재귀 뮤텍스) 정리
    pthread_mutex_unlock(&t->m);
    return rc;
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
// ---------------------------------------------------------------------------
// GC31-E는 802.3bt 출력 2포트에 총 60W 예산을 나눠 준다. "남았나 확인 → 예약"이
// 두 개의 임계구역으로 쪼개지면 Time-Of-Check-To-Time-Of-Use 버그가 나서 예산을
// 초과 배정(over-commit)한다. 검사와 행동은 반드시 같은 lock 안에 있어야 한다.
// ===========================================================================
typedef struct {
    int             total_mw;
    int             used_mw;
    pthread_mutex_t m;
} PoeBudget;

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

// ★ 좋은 예: 검사와 행동이 한 임계구역
bool budget_reserve(PoeBudget *b, int mw) {
    if (!b || mw <= 0) return false;
    bool ok = false;
    pthread_mutex_lock(&b->m);
    if (b->used_mw + mw <= b->total_mw) {   // check
        b->used_mw += mw;                   // ...and act — 같은 락 안
        ok = true;
    }
    pthread_mutex_unlock(&b->m);
    return ok;
}

// ★ 나쁜 예 (참고용, 이미 구현됨): 검사와 행동 사이에 락이 풀린다 → over-commit
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

void budget_release(PoeBudget *b, int mw) {
    if (!b || mw <= 0) return;
    pthread_mutex_lock(&b->m);
    b->used_mw -= mw;
    if (b->used_mw < 0) b->used_mw = 0;
    pthread_mutex_unlock(&b->m);
}

int budget_used(PoeBudget *b) {
    if (!b) return -1;
    pthread_mutex_lock(&b->m);
    int u = b->used_mw;
    pthread_mutex_unlock(&b->m);
    return u;
}

typedef struct { PoeBudget *b; int mw; bool use_toctou; bool granted; } ResArg;

static void *reserve_worker(void *p) {
    ResArg *a = (ResArg *)p;
    a->granted = a->use_toctou ? budget_reserve_toctou(a->b, a->mw)
                               : budget_reserve(a->b, a->mw);
    return NULL;
}

// nthreads개 스레드가 동시에 mw씩 예약 시도. 성공 개수를 반환.
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
// ---------------------------------------------------------------------------
// 모뎀 핸들처럼 "처음 쓸 때 한 번만" 열어야 하는 자원. `if (!inited) init();`
// 은 여러 스레드가 동시에 통과해 두 번 초기화된다(포트 두 번 열기 = 실패).
// pthread_once 는 초기화가 정확히 1회 실행되고, 늦게 온 스레드는 초기화가
// 끝날 때까지 기다린 뒤 완성된 상태를 본다(메모리 순서까지 보장).
// ===========================================================================
typedef struct {
    int  fd;
    int  apn_id;
    char name[16];
} ModemHandle;

static pthread_once_t g_modem_once  = PTHREAD_ONCE_INIT;
static ModemHandle    g_modem;
static _Atomic int    g_modem_init_calls = 0;

static void modem_init_once(void) {
    atomic_fetch_add(&g_modem_init_calls, 1);
    g_modem.fd     = 42;
    g_modem.apn_id = 7;
    snprintf(g_modem.name, sizeof g_modem.name, "wwan0");
}

ModemHandle *modem_get(void) {
    pthread_once(&g_modem_once, modem_init_once);
    return &g_modem;
}

int modem_init_calls(void) { return atomic_load(&g_modem_init_calls); }

static void *modem_worker(void *p) {
    ModemHandle **slot = (ModemHandle **)p;
    *slot = modem_get();
    return NULL;
}

// ===========================================================================
// Q8. 스레드별 로컬 집계 후 병합 — 경합 줄이기
// ---------------------------------------------------------------------------
// Q2는 증가 1회마다 락을 잡는다(= nthreads*iters 번). 각 스레드가 지역 변수에
// 모았다가 끝에 한 번만 병합하면 락 획득이 nthreads 번으로 줄고, 캐시 라인
// 핑퐁도 사라진다. 통계/카운터처럼 "중간값이 필요 없는" 집계의 정석.
// 테스트는 결과값뿐 아니라 '락 획득 횟수'까지 결정적으로 검사한다.
// ===========================================================================
static long            g_merged_total = 0;
static long            g_merge_locks  = 0;
static pthread_mutex_t g_merge_m      = PTHREAD_MUTEX_INITIALIZER;

typedef struct { long iters; } ShardArg;

static void *shard_worker(void *p) {
    ShardArg *a = (ShardArg *)p;
    long local = 0;
    for (long i = 0; i < a->iters; ++i) local++;   // 공유 상태 접근 0회

    pthread_mutex_lock(&g_merge_m);                // 스레드당 락 1회
    g_merged_total += local;
    g_merge_locks++;
    pthread_mutex_unlock(&g_merge_m);
    return NULL;
}

// 반환: 병합된 총합. *out_locks: 총 락 획득 횟수(= nthreads 여야 한다).
long sharded_sum_run(int nthreads, long iters, long *out_locks) {
    pthread_t th[MAX_WORKERS];
    ShardArg  args[MAX_WORKERS];
    if (nthreads < 1 || nthreads > MAX_WORKERS) return -1;

    pthread_mutex_lock(&g_merge_m);
    g_merged_total = 0;
    g_merge_locks  = 0;
    pthread_mutex_unlock(&g_merge_m);

    for (int i = 0; i < nthreads; ++i) args[i].iters = iters;
    for (int i = 0; i < nthreads; ++i) pthread_create(&th[i], NULL, shard_worker, &args[i]);
    for (int i = 0; i < nthreads; ++i) pthread_join(th[i], NULL);

    pthread_mutex_lock(&g_merge_m);
    long total = g_merged_total;
    if (out_locks) *out_locks = g_merge_locks;
    pthread_mutex_unlock(&g_merge_m);
    return total;
}

// ===========================================================================
// Q9. pthread_rwlock — 읽기 다수 / 쓰기 소수 설정 테이블
// ---------------------------------------------------------------------------
// WAN 설정(APN/MTU)은 거의 안 바뀌는데 여러 스레드가 매 패킷마다 읽는다.
// rwlock 은 읽기끼리는 병렬을 허용하고 쓰기만 배타로 만든다.
// 쓸 때: 읽기가 압도적으로 많고 임계구역이 충분히 길 때.
// 쓰지 말아야 할 때: 임계구역이 몇 ns라 rwlock 자체 비용이 더 클 때, 쓰기가
// 잦을 때(writer starvation 위험), 또는 그냥 atomic/RCU/더블 버퍼가 맞을 때.
// 불변식: mtu == 1000 + apn_id 이고 apn 문자열도 apn_id 와 일치해야 한다.
// ===========================================================================
typedef struct {
    int              apn_id;
    int              mtu;
    char             apn[32];
    pthread_rwlock_t rw;
} WanConfig;

void wc_init(WanConfig *c) {
    if (!c) return;
    pthread_rwlock_init(&c->rw, NULL);
    c->apn_id = 1;
    c->mtu    = 1001;
    snprintf(c->apn, sizeof c->apn, "apn-1");
}

void wc_destroy(WanConfig *c) {
    if (!c) return;
    pthread_rwlock_destroy(&c->rw);
}

// 쓰기: 세 필드를 한 번의 wrlock 안에서 함께 바꾼다.
void wc_set_apn(WanConfig *c, int apn_id) {
    if (!c) return;
    pthread_rwlock_wrlock(&c->rw);
    c->apn_id = apn_id;
    c->mtu    = 1000 + apn_id;
    snprintf(c->apn, sizeof c->apn, "apn-%d", apn_id);
    pthread_rwlock_unlock(&c->rw);
}

// 읽기: rdlock — 다른 reader와 동시에 들어갈 수 있다. 복사해서 나간다.
bool wc_read(WanConfig *c, int *apn_id, int *mtu, char *buf, size_t n) {
    if (!c || !apn_id || !mtu || !buf || n == 0) return false;
    pthread_rwlock_rdlock(&c->rw);
    *apn_id = c->apn_id;
    *mtu    = c->mtu;
    snprintf(buf, n, "%s", c->apn);
    pthread_rwlock_unlock(&c->rw);
    return true;
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
// ---------------------------------------------------------------------------
// pthread_cancel 은 취소 지점이 어디인지 불분명하고 락/자원 누수를 남긴다.
// 임베디드에서는 항상 "협조적 종료": mutex로 보호되는 stop 플래그를 세우고
// (블로킹 중이면 condvar broadcast나 self-pipe로 깨우고) join 해서
// 스레드가 실제로 끝난 것을 확인한 뒤 자원을 해제한다.
// stop_and_join 은 두 번 불러도 안전해야 한다(idempotent).
// ===========================================================================
typedef struct {
    pthread_t       th;
    bool            running;      // m으로 보호
    bool            stop;         // m으로 보호
    _Atomic long    ticks;        // 외부에서 락 없이 관찰
    pthread_mutex_t m;
} Worker;

void worker_init(Worker *w) {
    if (!w) return;
    w->running = false;
    w->stop    = false;
    atomic_store(&w->ticks, 0L);
    pthread_mutex_init(&w->m, NULL);
}

void worker_destroy(Worker *w) {
    if (!w) return;
    pthread_mutex_destroy(&w->m);
}

bool worker_should_stop(Worker *w) {
    if (!w) return true;
    pthread_mutex_lock(&w->m);
    bool s = w->stop;
    pthread_mutex_unlock(&w->m);
    return s;
}

long worker_ticks(Worker *w) { return w ? atomic_load(&w->ticks) : -1; }

static void *worker_main(void *p) {
    Worker *w = (Worker *)p;
    while (!worker_should_stop(w)) {
        atomic_fetch_add(&w->ticks, 1L);     // 실제로는 한 프레임 처리
    }
    return NULL;
}

bool worker_start(Worker *w) {
    if (!w) return false;
    pthread_mutex_lock(&w->m);
    if (w->running) { pthread_mutex_unlock(&w->m); return false; }
    w->stop    = false;
    w->running = true;
    pthread_mutex_unlock(&w->m);

    if (pthread_create(&w->th, NULL, worker_main, w) != 0) {
        pthread_mutex_lock(&w->m);
        w->running = false;
        pthread_mutex_unlock(&w->m);
        return false;
    }
    return true;
}

// stop 플래그를 세우고 join. 이미 멈췄으면 아무것도 하지 않는다. 총 ticks 반환.
long worker_stop_and_join(Worker *w) {
    if (!w) return -1;
    pthread_mutex_lock(&w->m);
    bool need_join = w->running;
    w->stop    = true;
    w->running = false;
    pthread_mutex_unlock(&w->m);

    if (need_join) pthread_join(w->th, NULL);   // ★ join 해야 자원 해제가 안전
    return atomic_load(&w->ticks);
}

// 테스트 헬퍼: ticks가 target 이상이 될 때까지 유한 횟수만 스핀한다.
// (미구현 stub 에서도 절대 멈추지 않도록 상한을 둔다)
static long spin_until_ticks(Worker *w, long target, long max_spins) {
    for (long i = 0; i < max_spins; ++i) {
        long t = worker_ticks(w);
        if (t >= target) return t;
        if ((i & 0x3FF) == 0) sched_yield();
    }
    return worker_ticks(w);
}

// ===========================================================================
// main — 테스트
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
