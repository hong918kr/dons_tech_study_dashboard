// 02_condvar_queues.c  —  REFERENCE SOLUTION
// 조건 변수 & 블로킹 큐 (Condition Variables & Blocking Queues)  —  Q11~Q20
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -O1 -g -pthread solutions/02_condvar_queues.c -o /tmp/vk02 && /tmp/vk02
// TSan: cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread solutions/02_condvar_queues.c -o /tmp/vk02t && /tmp/vk02t
//
// 이 세트가 Verkada 인터뷰의 1순위다. 리크루터 메일이 "thread safety"와
// "synchronization (mutex, condition variable)"을 못박았고, 그걸 한 문제로 확인하는
// 표준 문제가 bounded blocking queue다. GC31-E 게이트웨이에서 도어/센서/모뎀 I/O
// 스레드가 이벤트를 만들고 업로더 스레드가 LTE로 올리는 구조 그대로다.
// 여기서 평가되는 건 "돌아가는 코드"가 아니라 while-wait, broadcast, 종료 프로토콜,
// drop 카운터 같은 edge case를 스스로 짚어내는가다.
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
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공용 시간 유틸 (문제 아님 — 테스트와 Q16 구현이 함께 쓴다)
// ---------------------------------------------------------------------------
// CLOCK_MONOTONIC은 시스템 시계가 바뀌어도(NTP 보정, 사용자가 시각 변경) 뒤로
// 가지 않는다. 타임아웃/주기 계산은 반드시 monotonic으로. CLOCK_REALTIME은
// pthread_cond_timedwait의 기본 기준 시계라 어쩔 수 없이 쓸 때만 쓴다.
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

// flag가 want가 될 때까지 최대 timeout_ms 대기. 상한이 있으므로 미구현 상태에서도
// 영원히 멈추지 않는다(테스트가 hang 대신 FAIL 로 끝나게 하는 장치).
static bool wait_flag(_Atomic int *flag, int want, int timeout_ms) {
    long deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (atomic_load(flag) == want) return true;
        sleep_ms(2);
    }
    return atomic_load(flag) == want;
}

// ===========================================================================
// Q11. 블로킹 큐 init / destroy — 저장소 소유권과 필드 불변식
// ---------------------------------------------------------------------------
// 게이트웨이 이벤트 큐의 뼈대. 임베디드 관례상 저장소는 "외부 정적 배열"을 받을 수
// 있어야 하고(heap 회피), 편의를 위해 NULL이면 내부에서 calloc한다. 누가 free할지
// (owns_buf)를 구조체에 남기는 게 소유권 설계의 핵심.
// 불변식: 0 <= count <= cap, head < cap, 원소 i는 buf[(head+i) % cap].
// head + count 표현을 쓰면 full/empty 모호성(head==tail 문제)이 아예 사라진다.
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
    bool    closed;   // 종료 프로토콜 플래그(Q14)

    unsigned long pushed, popped;   // 관측성(observability) 카운터

    pthread_mutex_t m;              // 위 모든 필드를 보호
    pthread_cond_t  not_full;       // "슬롯이 생겼다" — 생산자가 기다림
    pthread_cond_t  not_empty;      // "원소가 생겼다" — 소비자가 기다림
} bq_t;

// storage != NULL이면 그 배열을 그대로 쓴다(정적 할당, 소유권 외부).
// storage == NULL이면 cap개를 calloc한다(소유권 내부).
int bq_init(bq_t *q, Event *storage, size_t cap) {
    if (!q || cap == 0) return -1;          // cap 0이면 이후 % cap이 0 나누기
    memset(q, 0, sizeof *q);
    if (storage) {
        q->buf = storage;
        q->owns_buf = false;
    } else {
        q->buf = calloc(cap, sizeof *q->buf);
        if (!q->buf) return -1;
        q->owns_buf = true;
    }
    q->cap = cap;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return 0;
}

// 주의: destroy는 "모든 사용자 스레드가 끝난 뒤"에만 부른다. 대기 중인 스레드가
// 있는 상태에서 mutex/cond를 파괴하면 UB. 그래서 실무 API는 close() → join() →
// destroy() 순서를 강제한다.
void bq_destroy(bq_t *q) {
    if (!q) return;
    if (q->owns_buf) free(q->buf);
    q->buf = NULL;
    q->cap = q->head = q->count = 0;
    pthread_mutex_destroy(&q->m);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

// 스냅샷. 반환 즉시 낡을 수 있으므로 "제어 판단"에 쓰면 안 되고 관측용이다.
size_t bq_count(bq_t *q) {
    size_t n;
    pthread_mutex_lock(&q->m);
    n = q->count;
    pthread_mutex_unlock(&q->m);
    return n;
}

// ===========================================================================
// Q12. 블로킹 push — 가득 차면 대기
// ---------------------------------------------------------------------------
// 핵심 3가지:
//  ① 조건 검사는 if가 아니라 while (spurious wakeup + 깨어난 뒤 다른 생산자가
//     먼저 슬롯을 채가는 경우).
//  ② closed면 대기를 멈추고 false (종료 중 영구 대기 금지).
//  ③ signal은 unlock 뒤에 — 깨어난 스레드가 곧장 mutex를 잡을 수 있게.
//     (정확성에는 무관하고 성능 최적화다. "hurry-up-and-wait" 회피)
// ===========================================================================
bool bq_push(bq_t *q, Event ev) {
    pthread_mutex_lock(&q->m);
    while (q->count == q->cap && !q->closed)
        pthread_cond_wait(&q->not_full, &q->m);

    if (q->closed) {                     // close 후 push는 거부
        pthread_mutex_unlock(&q->m);
        return false;
    }
    q->buf[(q->head + q->count) % q->cap] = ev;
    q->count++;
    q->pushed++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);  // 소비자 하나만 깨우면 충분
    return true;
}

// ===========================================================================
// Q13. 블로킹 pop — 비면 대기
// ---------------------------------------------------------------------------
// push의 거울상. 종료 판정만 다르다: closed만으로는 부족하고 "closed && empty"여야
// false를 준다 — 그래야 close 시점에 남아 있던 이벤트가 버려지지 않는다(drain).
// ===========================================================================
bool bq_pop(bq_t *q, Event *out) {
    if (!out) return false;
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    if (q->count == 0) {                 // 여기 도달 == closed && empty
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    q->popped++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

// ===========================================================================
// Q14. close / 종료 프로토콜
// ---------------------------------------------------------------------------
// 인터뷰에서 가장 많이 파는 지점. 규칙 4개:
//  ① closed 플래그는 mutex 안에서 세팅 (condvar 조건의 일부다)
//  ② 깨우기는 signal이 아니라 broadcast — 대기자가 몇 명인지 모른다
//  ③ "closed && empty"일 때만 pop이 false (남은 것은 drain)
//  ④ 이중 close는 멱등 — 재시작/watchdog 경로에서 두 번 불릴 수 있다
// ===========================================================================
void bq_close(bq_t *q) {
    pthread_mutex_lock(&q->m);
    if (q->closed) {                     // 멱등: 두 번째 호출은 조용히 무시
        pthread_mutex_unlock(&q->m);
        return;
    }
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    // 두 condvar 모두 broadcast. not_full 쪽을 빠뜨리면 가득 찬 큐에서 대기하던
    // 생산자가 영원히 잠든다 — 가장 흔한 "종료가 안 돼요" 버그.
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}

bool bq_is_closed(bq_t *q) {
    bool c;
    pthread_mutex_lock(&q->m);
    c = q->closed;
    pthread_mutex_unlock(&q->m);
    return c;
}

// ===========================================================================
// Q15. try_push / try_pop — 논블로킹 변형
// ---------------------------------------------------------------------------
// 실시간 경로(제어 루프, 오디오 콜백, 인터럽트 하위 스레드)는 절대 블록하면 안
// 된다. 그래서 같은 자료구조에 "대기 없음" 진입점을 하나 더 둔다.
// 주의: 논블로킹(non-blocking)이지 무락(lock-free)이 아니다. mutex는 여전히 잡는다
// — 다만 조건이 안 맞으면 기다리지 않고 즉시 실패를 돌려준다.
// ===========================================================================
bool bq_try_push(bq_t *q, Event ev) {
    pthread_mutex_lock(&q->m);
    if (q->closed || q->count == q->cap) {
        pthread_mutex_unlock(&q->m);
        return false;                    // 대기하지 않고 즉시 실패
    }
    q->buf[(q->head + q->count) % q->cap] = ev;
    q->count++;
    q->pushed++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);
    return true;
}

bool bq_try_pop(bq_t *q, Event *out) {
    if (!out) return false;
    pthread_mutex_lock(&q->m);
    if (q->count == 0) {                 // 비었으면 closed 여부와 무관하게 실패
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    q->popped++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

// ===========================================================================
// Q16. timed pop — pthread_cond_timedwait + 이식성 헬퍼
// ---------------------------------------------------------------------------
// "링크가 끊겼으면 5초마다 재연결을 시도하고, 그 사이에 이벤트가 오면 즉시 처리"
// 같은 루프에 필요하다. 이식성 함정이 둘 있다:
//  ① POSIX pthread_cond_timedwait는 '절대시각'을 받고 기준 시계는 기본
//     CLOCK_REALTIME이다 → 시스템 시각이 뒤로 점프하면 타임아웃이 늘어난다.
//  ② Linux는 pthread_condattr_setclock(CLOCK_MONOTONIC)으로 고칠 수 있지만
//     macOS에는 그 API가 없다. 대신 상대시간 확장 API가 있다:
//     pthread_cond_timedwait_relative_np().
// 그래서 "ms 만큼 상대 대기" 헬퍼 하나로 감싸고 플랫폼 차이를 여기서만 흡수한다.
// 그리고 재대기(spurious wakeup) 때 남은 시간을 monotonic 기준으로 다시 계산한다.
// ===========================================================================
#if defined(__APPLE__)
static int cond_wait_ms(pthread_cond_t *cv, pthread_mutex_t *m, long ms) {
    struct timespec rel;
    rel.tv_sec  = ms / 1000;
    rel.tv_nsec = (ms % 1000) * 1000000L;
    return pthread_cond_timedwait_relative_np(cv, m, &rel);   // macOS 전용 확장
}
#else
static int cond_wait_ms(pthread_cond_t *cv, pthread_mutex_t *m, long ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);      // 기본 condattr의 기준 시계
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
    return pthread_cond_timedwait(cv, m, &ts);
}
#endif

// true = 원소를 받음 / false = 타임아웃이거나 (closed && empty)
bool bq_pop_timed(bq_t *q, Event *out, long timeout_ms) {
    if (!out) return false;
    long deadline = now_ms() + timeout_ms;   // 절대 마감(monotonic)을 먼저 고정

    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed) {
        long remain = deadline - now_ms();   // 깨어날 때마다 남은 예산 재계산
        if (remain <= 0) {
            pthread_mutex_unlock(&q->m);
            return false;                    // 타임아웃
        }
        int rc = cond_wait_ms(&q->not_empty, &q->m, remain);
        if (rc == ETIMEDOUT && q->count == 0 && !q->closed) {
            pthread_mutex_unlock(&q->m);
            return false;
        }
        // rc == 0 이어도 조건은 while이 다시 검사한다 (spurious wakeup 방어)
    }
    if (q->count == 0) {                     // closed && empty
        pthread_mutex_unlock(&q->m);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    q->popped++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

// ===========================================================================
// Q17. batch pop — 한 번에 최대 n개
// ---------------------------------------------------------------------------
// LTE 업로드는 패킷/HTTP 요청 하나당 고정 오버헤드가 크다. 이벤트 1개마다 요청을
// 보내면 전력과 데이터 요금이 전부 헤더로 나간다. 그래서 소비자는 "있는 만큼
// 한꺼번에" 꺼내 한 번에 올린다 — 락 획득 횟수도 n분의 1로 줄어든다.
// 반환값 0 == closed && empty (종료 신호). 그 외에는 최소 1개를 보장한다.
// 여러 슬롯이 한꺼번에 비므로 깨우기는 signal이 아니라 broadcast.
// ===========================================================================
size_t bq_pop_batch(bq_t *q, Event *out, size_t max) {
    if (!out || max == 0) return 0;
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    size_t n = q->count < max ? q->count : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = q->buf[q->head];
        q->head = (q->head + 1) % q->cap;
    }
    q->count -= n;
    q->popped += n;
    pthread_mutex_unlock(&q->m);
    if (n > 0) pthread_cond_broadcast(&q->not_full);  // n개 슬롯이 동시에 비었다
    return n;                                          // 0 == closed && empty
}

// ===========================================================================
// Q18. drop 정책 큐 — backpressure 설계
// ---------------------------------------------------------------------------
// 링크가 끊기면 업로더가 막히고 그 사이에도 센서 스레드는 계속 샘플을 만든다.
// 메모리는 유한하므로 "가득 찼을 때 무엇을 할지"를 정책으로 정한다:
//   TQ_BLOCK        생산자를 막는다        손실 0, 실시간 경로에는 금지
//   TQ_DROP_NEWEST  새 샘플을 버린다        과거 이력 보존 → 감사 로그/이벤트
//   TQ_DROP_OLDEST  가장 오래된 것을 버린다  최신 상태 보존 → 상태/알람/텔레메트리
// 정책은 '데이터의 의미'가 결정한다. 그리고 버린 개수는 반드시 카운트해서 함께
// 보고한다 — 조용한 손실(silent loss)이 제일 나쁘다.
// 불변식: produced == dequeued + dropped + queued
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

int tq_init(tq_t *q, size_t cap, tq_policy_t p) {
    if (!q || cap == 0) return -1;
    memset(q, 0, sizeof *q);
    q->buf = calloc(cap, sizeof *q->buf);
    if (!q->buf) return -1;
    q->cap = cap;
    q->policy = p;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return 0;
}

void tq_destroy(tq_t *q) {
    if (!q) return;
    free(q->buf);
    q->buf = NULL;
    pthread_mutex_destroy(&q->m);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

// false는 오직 "closed"일 때만. drop된 경우도 true를 주되 dropped를 올린다
// (생산자 입장에서는 "접수됨", 손실은 카운터로 보고).
bool tq_push(tq_t *q, Sample s) {
    pthread_mutex_lock(&q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return false; }

    if (q->count == q->cap) {
        switch (q->policy) {
        case TQ_BLOCK:
            while (q->count == q->cap && !q->closed)
                pthread_cond_wait(&q->not_full, &q->m);
            if (q->closed) { pthread_mutex_unlock(&q->m); return false; }
            break;
        case TQ_DROP_NEWEST:
            q->dropped++;                          // 방금 것을 버린다
            pthread_mutex_unlock(&q->m);
            return true;
        case TQ_DROP_OLDEST:
            q->head = (q->head + 1) % q->cap;      // 가장 오래된 것을 밀어낸다
            q->count--;
            q->dropped++;
            break;
        }
    }
    q->buf[(q->head + q->count) % q->cap] = s;
    q->count++;
    q->enqueued++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);
    return true;
}

bool tq_pop(tq_t *q, Sample *out) {
    if (!out) return false;
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);
    if (q->count == 0) { pthread_mutex_unlock(&q->m); return false; }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    q->dequeued++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_full);
    return true;
}

void tq_close(tq_t *q) {
    pthread_mutex_lock(&q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return; }
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}

// ===========================================================================
// Q19. 워커 풀 + graceful shutdown
// ---------------------------------------------------------------------------
// 게이트웨이 백그라운드 작업(로그 업로드, 설정 동기화, 헬스체크)을 N개 워커가
// 처리한다. 종료 모드 두 가지:
//   POOL_DRAIN  큐에 남은 작업을 모두 처리하고 종료 (정상 재부팅)
//   POOL_STOP   진행 중 작업만 끝내고 큐는 버림 (watchdog/긴급)
// 설계 포인트:
//  - 작업 실행 t.fn(t.arg)은 반드시 락 밖에서. 안에서 하면 풀이 직렬화된다.
//  - 종료 시 broadcast(work/slot 둘 다). signal이면 하나만 깨어나 hang.
//  - shutdown(= 상태전환 + broadcast + join)과 destroy(= free)를 분리한다.
//    합쳐두면 shutdown 직후 submit이 해제된 메모리를 만져 UAF가 난다.
//  - executed / discarded / rejected 세 카운터로 "몇 개가 어디로 갔는지" 보고.
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

// 워커 하나가 작업 하나를 꺼낸다. false를 받으면 워커는 종료한다.
static bool pool_take(pool_t *p, Task *out) {
    pthread_mutex_lock(&p->m);
    while (p->count == 0 && p->state == POOL_RUNNING)
        pthread_cond_wait(&p->work, &p->m);

    if (p->state == POOL_STOP || p->count == 0) {
        if (p->state == POOL_STOP && p->count > 0) {
            p->discarded += p->count;      // 남은 작업은 버리되 반드시 센다
            p->count = 0;
        }
        pthread_mutex_unlock(&p->m);
        return false;                      // 워커 종료
    }
    *out = p->q[p->head];
    p->head = (p->head + 1) % p->cap;
    p->count--;
    p->executed++;
    pthread_mutex_unlock(&p->m);
    pthread_cond_signal(&p->slot);
    return true;
}

static void *pool_worker(void *arg) {
    pool_t *p = (pool_t *)arg;
    Task t;
    while (pool_take(p, &t))
        t.fn(t.arg);                       // ★ 실행은 락 밖에서
    return NULL;
}

int pool_init(pool_t *p, size_t nworkers, size_t qcap) {
    if (!p || nworkers == 0 || qcap == 0) return -1;
    memset(p, 0, sizeof *p);
    p->workers = calloc(nworkers, sizeof *p->workers);
    p->q       = calloc(qcap, sizeof *p->q);
    if (!p->workers || !p->q) { free(p->workers); free(p->q); return -1; }
    p->cap   = qcap;
    p->state = POOL_RUNNING;
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->work, NULL);
    pthread_cond_init(&p->slot, NULL);
    for (size_t i = 0; i < nworkers; i++) {
        if (pthread_create(&p->workers[i], NULL, pool_worker, p) != 0) break;
        p->nworkers++;                     // 실제로 뜬 개수만 센다(join 대상)
    }
    return 0;
}

// false = 종료 중이라 거부됨. 조용히 무시하지 않고 rejected를 올린다.
bool pool_submit(pool_t *p, task_fn fn, void *arg) {
    if (!p || !fn) return false;
    pthread_mutex_lock(&p->m);
    while (p->count == p->cap && p->state == POOL_RUNNING)
        pthread_cond_wait(&p->slot, &p->m);   // 큐가 차면 제출자가 기다린다
    if (p->state != POOL_RUNNING) {
        p->rejected++;
        pthread_mutex_unlock(&p->m);
        return false;
    }
    p->q[(p->head + p->count) % p->cap] = (Task){ fn, arg };
    p->count++;
    pthread_mutex_unlock(&p->m);
    pthread_cond_signal(&p->work);
    return true;
}

// 상태 전환 + 전원 깨우기 + join. 반환 시점에 워커는 전부 죽어 있다.
void pool_shutdown(pool_t *p, bool drain) {
    if (!p) return;
    pthread_mutex_lock(&p->m);
    if (p->state != POOL_RUNNING) { pthread_mutex_unlock(&p->m); return; } // 멱등
    p->state = drain ? POOL_DRAIN : POOL_STOP;
    pthread_mutex_unlock(&p->m);
    pthread_cond_broadcast(&p->work);      // 잠든 워커 전부
    pthread_cond_broadcast(&p->slot);      // 큐가 차서 대기 중인 제출자 전부
    for (size_t i = 0; i < p->nworkers; i++)
        pthread_join(p->workers[i], NULL);
}

// join이 끝난 뒤에만 호출해야 한다. shutdown과 분리했기 때문에 그 사이에
// 카운터를 읽거나 submit을 시도해 봐도 안전하다.
void pool_destroy(pool_t *p) {
    if (!p) return;
    free(p->workers); p->workers = NULL;
    free(p->q);       p->q = NULL;
    p->nworkers = 0;
    pthread_mutex_destroy(&p->m);
    pthread_cond_destroy(&p->work);
    pthread_cond_destroy(&p->slot);
}

// ===========================================================================
// Q20. counting semaphore를 mutex + condvar로 구현
// ---------------------------------------------------------------------------
// "동시 업로드는 최대 2개" 같은 자원 제한이 필요할 때. macOS는 POSIX 무명
// 세마포어(sem_init)가 미구현이고 sem_open은 이름/정리가 번거롭다 — 그래서
// mutex+condvar로 직접 만드는 게 이식성 정답이자 단골 면접 문제다.
// 불변식: value >= 0, 그리고 "동시에 임계 자원을 쓰는 스레드 수 <= 초기 value".
// 깨우기는 post 한 번에 자리 하나가 나므로 signal로 충분하다(broadcast는
// thundering herd — N명이 깨어나 1명 빼고 다시 자는 낭비).
// ===========================================================================
typedef struct {
    int value;                 // 남은 허가(permit) 수
    pthread_mutex_t m;
    pthread_cond_t  cv;
} csem_t;

int csem_init(csem_t *s, int initial) {
    if (!s || initial < 0) return -1;
    s->value = initial;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->cv, NULL);
    return 0;
}

void csem_destroy(csem_t *s) {
    if (!s) return;
    pthread_mutex_destroy(&s->m);
    pthread_cond_destroy(&s->cv);
}

void csem_wait(csem_t *s) {
    pthread_mutex_lock(&s->m);
    while (s->value == 0)                  // if가 아니라 while
        pthread_cond_wait(&s->cv, &s->m);
    s->value--;
    pthread_mutex_unlock(&s->m);
}

bool csem_trywait(csem_t *s) {
    pthread_mutex_lock(&s->m);
    if (s->value == 0) { pthread_mutex_unlock(&s->m); return false; }
    s->value--;
    pthread_mutex_unlock(&s->m);
    return true;
}

void csem_post(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->value++;
    pthread_mutex_unlock(&s->m);
    pthread_cond_signal(&s->cv);           // 자리 하나 → 한 명만 깨우면 된다
}

int csem_value(csem_t *s) {
    int v;
    pthread_mutex_lock(&s->m);
    v = s->value;
    pthread_mutex_unlock(&s->m);
    return v;
}

// ===========================================================================
// 테스트용 전역과 스레드 함수
// (정적 저장기간 = 0 초기화 보장 → 미구현 stub에서도 안전하게 동작)
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
// main
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
        // (DROP_NEWEST였다면 enqueued < produced 가 된다 — 정책 차이가 여기서 드러남)
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
