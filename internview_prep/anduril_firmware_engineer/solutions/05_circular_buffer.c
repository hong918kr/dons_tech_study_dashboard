// 05_circular_buffer.c  —  REFERENCE SOLUTION
// 원형 버퍼 / 링 버퍼 (Ring buffer)  —  Q56~Q65
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 05_circular_buffer.c -o /tmp/andb_circular_buffer -lpthread
// 임베디드 최다 빈출 자료구조. UART RX ISR -> main loop 파이프라인의 심장.
// 세 가지 구현 스타일을 모두 다룬다:
//   (A) count 필드 기반 cb_t         : 가장 직관적, 단일 컨텍스트
//   (B) power-of-two mask 기반 cbp2_t: head/tail 자유 증가 + 마스킹, 전용량 사용
//   (C) SPSC 무락(lock-free) spsc_t  : ISR<->main 정석 패턴 (C11 atomics)
//   (D) MPMC mutex+condvar mpmc_t    : 다중 생산자/소비자 블로킹 큐
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

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
// (A) Q56~Q62 : count 필드 기반 원형 버퍼 cb_t
// ---------------------------------------------------------------------------
// Q56. 구조체 정의 + 초기화.  head=쓰기 위치(생산자), tail=읽기 위치(소비자),
//      count=현재 원소 수.  count 필드가 있으면 full/empty 판별이 O(1)로 간단.
// ===========================================================================
typedef struct {
    uint8_t *buffer;  // 외부에서 준 정적 저장소 (heap 사용 안 함)
    size_t   size;    // 버퍼 용량 (원소 수)
    size_t   head;    // 다음에 쓸 인덱스
    size_t   tail;    // 다음에 읽을 인덱스
    size_t   count;   // 현재 저장된 원소 수
} cb_t;

// Q56: 초기화
void cb_init(cb_t *cb, uint8_t *buffer, size_t size) {
    if (!cb) return;
    cb->buffer = buffer;
    cb->size   = size;
    cb->head   = 0;
    cb->tail   = 0;
    cb->count  = 0;
}

// Q59: 비었는가?  (Q57/Q58에서 사용하므로 먼저 정의)
bool cb_is_empty(const cb_t *cb) {
    if (!cb) return true;          // NULL은 "빈 것"으로 안전 취급
    return cb->count == 0;
}

// Q60: 가득 찼는가?
bool cb_is_full(const cb_t *cb) {
    if (!cb) return false;
    return cb->count == cb->size;
}

// Q57: push / enqueue.  가득 차면 false (덮어쓰기 안 함).
bool cb_push(cb_t *cb, uint8_t data) {
    if (!cb || cb_is_full(cb)) return false;
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->size;   // 랩어라운드
    cb->count++;
    return true;
}

// Q58: pop / dequeue.  비면 false.
bool cb_pop(cb_t *cb, uint8_t *out) {
    if (!cb || !out || cb_is_empty(cb)) return false;
    *out = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->size;
    cb->count--;
    return true;
}

// Q61: 현재 원소 수
size_t cb_count(const cb_t *cb) {
    return cb ? cb->count : 0;
}

// Q62: flush (논리적 비우기).  데이터는 그대로 두고 인덱스만 리셋.
void cb_flush(cb_t *cb) {
    if (!cb) return;
    cb->head = cb->tail = cb->count = 0;
}

// ===========================================================================
// (B) Q63 : power-of-two 크기 + 마스킹 변형 cbp2_t
// ---------------------------------------------------------------------------
// head/tail 을 자유 증가(free-running) uint32_t 로 두고 접근 시 & mask 로 인덱스
// 계산.  count = head - tail (부호없는 뺄셈이라 랩되어도 정확).  나머지 연산(%)
// 대신 AND 한 번 -> ISR 에서 빠르다.  count 필드 없이도 full/empty 판별 가능,
// 그리고 slot 낭비 없이 전용량(size) 사용.
// ===========================================================================
typedef struct {
    uint8_t *buf;
    uint32_t size;   // 반드시 2의 거듭제곱
    uint32_t mask;   // size - 1
    uint32_t head;   // free-running
    uint32_t tail;   // free-running
} cbp2_t;

// size 가 2의 거듭제곱이 아니면 false
bool cbp2_init(cbp2_t *q, uint8_t *buf, uint32_t size) {
    if (!q || !buf) return false;
    if (size < 2 || (size & (size - 1)) != 0) return false;  // pow2 검사
    q->buf  = buf;
    q->size = size;
    q->mask = size - 1;
    q->head = 0;
    q->tail = 0;
    return true;
}

uint32_t cbp2_count(const cbp2_t *q) {
    if (!q) return 0;
    return q->head - q->tail;   // 부호없는 뺄셈: 랩어라운드 안전
}

bool cbp2_push(cbp2_t *q, uint8_t data) {
    if (!q) return false;
    if (cbp2_count(q) == q->size) return false;   // full
    q->buf[q->head & q->mask] = data;             // % 대신 & mask
    q->head++;                                    // 자유 증가
    return true;
}

bool cbp2_pop(cbp2_t *q, uint8_t *out) {
    if (!q || !out) return false;
    if (q->head == q->tail) return false;         // empty
    *out = q->buf[q->tail & q->mask];
    q->tail++;
    return true;
}

// ===========================================================================
// (C) Q64 : SPSC 무락(lock-free) 링 버퍼 spsc_t
// ---------------------------------------------------------------------------
// 정석 임베디드 패턴: 생산자(예: UART RX ISR)만 head 를 쓰고, 소비자(main loop)
// 만 tail 을 쓴다.  서로 상대 인덱스는 읽기만 하므로 공유 쓰기 경합이 없다 ->
// 락 불필요.  count 를 공유하면 양쪽이 쓰게 되어 경합 -> 그래서 count 대신
// head==tail(empty), (head+1)==tail(full) 로 판별하고 한 칸을 희생한다.
//
// 이식성: 멀티코어에서 재정렬을 막으려면 acquire/release 원자연산이 필요하다
// (여기서 C11 atomics 사용).  단일코어 베어메탈 ISR<->main 에서는 head/tail 을
// volatile 로 두고 "데이터 먼저 쓰고 인덱스 나중" 순서만 지키면 전통적으로 충분.
// ===========================================================================
typedef struct {
    uint8_t       *buf;
    uint32_t       size;  // pow2 (마스킹용)
    uint32_t       mask;
    _Atomic uint32_t head; // 생산자만 store
    _Atomic uint32_t tail; // 소비자만 store
} spsc_t;

bool spsc_init(spsc_t *q, uint8_t *buf, uint32_t size) {
    if (!q || !buf) return false;
    if (size < 2 || (size & (size - 1)) != 0) return false;
    q->buf  = buf;
    q->size = size;
    q->mask = size - 1;
    atomic_init(&q->head, 0u);
    atomic_init(&q->tail, 0u);
    return true;
}

// 생산자 컨텍스트에서만 호출.  사용 가능 용량 = size - 1 (한 칸 희생).
bool spsc_push(spsc_t *q, uint8_t data) {
    if (!q) return false;
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed); // 내 것
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire); // 소비자 것
    if (((h + 1) & q->mask) == (t & q->mask)) return false;            // full
    q->buf[h & q->mask] = data;                                        // 1) 데이터
    atomic_store_explicit(&q->head, h + 1, memory_order_release);      // 2) 인덱스 공개
    return true;
}

// 소비자 컨텍스트에서만 호출.
bool spsc_pop(spsc_t *q, uint8_t *out) {
    if (!q || !out) return false;
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed); // 내 것
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire); // 생산자 것
    if (h == t) return false;                                          // empty
    *out = q->buf[t & q->mask];                                        // 1) 데이터
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);      // 2) 인덱스 공개
    return true;
}

// ===========================================================================
// (D) Q65 : MPMC 블로킹 큐 mpmc_t (mutex + condition variable)
// ---------------------------------------------------------------------------
// 다중 생산자/소비자는 head/tail/count 를 모두 여러 스레드가 갱신 -> 경합.
// mutex 로 임계구역을 보호하고, 가득/빔 상태는 condition variable 로 대기/통지.
// while (조건) cond_wait : spurious wakeup 대비 반드시 while 루프.
// ===========================================================================
typedef struct {
    uint8_t        *buf;
    size_t          size;
    size_t          head, tail, count;
    pthread_mutex_t m;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} mpmc_t;

void mpmc_init(mpmc_t *q, uint8_t *buf, size_t size) {
    q->buf = buf; q->size = size;
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

void mpmc_destroy(mpmc_t *q) {
    pthread_mutex_destroy(&q->m);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

void mpmc_push(mpmc_t *q, uint8_t data) {   // 블로킹
    pthread_mutex_lock(&q->m);
    while (q->count == q->size)             // while: spurious wakeup 방어
        pthread_cond_wait(&q->not_full, &q->m);
    q->buf[q->head] = data;
    q->head = (q->head + 1) % q->size;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
}

uint8_t mpmc_pop(mpmc_t *q) {               // 블로킹
    pthread_mutex_lock(&q->m);
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->m);
    uint8_t data = q->buf[q->tail];
    q->tail = (q->tail + 1) % q->size;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);
    return data;
}

// ===========================================================================
// 스레드 테스트용 워커
// ===========================================================================
// --- SPSC: 생산자 1 / 소비자 1, 순서 보존 검증 ---
#define SPSC_N 5000
#define SPIN_LIMIT 5000000    // 연속 실패 상한: 정상 구현이면 절대 안 걸림(미구현 stub 는 여기서 탈출)
typedef struct { spsc_t *q; bool ok; } spsc_arg_t;

static void *spsc_prod(void *arg) {
    spsc_arg_t *a = (spsc_arg_t *)arg;
    for (int i = 0; i < SPSC_N; ++i) {
        long spins = 0;
        while (!spsc_push(a->q, (uint8_t)(i & 0xFF))) {
            if (++spins > SPIN_LIMIT) { a->ok = false; return NULL; }  // 진행 없음 -> 중단
            sched_yield();
        }
    }
    return NULL;
}
static void *spsc_cons(void *arg) {
    spsc_arg_t *a = (spsc_arg_t *)arg;
    for (int i = 0; i < SPSC_N; ++i) {
        uint8_t v;
        long spins = 0;
        while (!spsc_pop(a->q, &v)) {
            if (++spins > SPIN_LIMIT) { a->ok = false; return NULL; }
            sched_yield();
        }
        if (v != (uint8_t)(i & 0xFF)) a->ok = false;   // FIFO 순서 위반
    }
    return NULL;
}

// --- MPMC: 생산자 2 / 소비자 2, 합계 보존 검증 ---
#define MPMC_PER 1000   // 각 생산자가 미는 개수
typedef struct { mpmc_t *q; _Atomic uint64_t *sum; _Atomic uint32_t *popped; int total_to_pop; } mpmc_arg_t;

static void *mpmc_prod(void *arg) {
    mpmc_t *q = ((mpmc_arg_t *)arg)->q;
    for (int i = 0; i < MPMC_PER; ++i)
        mpmc_push(q, (uint8_t)(i & 0xFF));
    return NULL;
}
static void *mpmc_cons(void *arg) {
    mpmc_arg_t *a = (mpmc_arg_t *)arg;
    for (;;) {
        // 남은 pop 슬롯을 원자적으로 하나 확보 (없으면 종료)
        uint32_t idx = atomic_fetch_add(a->popped, 1);
        if ((int)idx >= a->total_to_pop) { atomic_fetch_sub(a->popped, 1); break; }
        uint8_t v = mpmc_pop(a->q);
        atomic_fetch_add(a->sum, (uint64_t)v);
    }
    return NULL;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- (A) cb_t : count 기반 --------
    printf("== (A) cb_t (count-based) ==\n");
    uint8_t sto[8];
    cb_t cb;
    cb_init(&cb, sto, 8);
    T("cb_init -> empty",            cb_is_empty(&cb) && cb_count(&cb) == 0);
    T("cb_is_full false on empty",   !cb_is_full(&cb));
    T("cb_push(NULL) -> false",      cb_push(NULL, 1) == false);
    T("cb_pop(NULL) -> false",       cb_pop(NULL, NULL) == false);
    T("cb_is_empty(NULL) -> true",   cb_is_empty(NULL) == true);

    bool push_ok = true;
    for (uint8_t i = 1; i <= 8; ++i) push_ok &= cb_push(&cb, i);
    T("push 1..8 all succeed",       push_ok);
    T("count == 8 after 8 pushes",   cb_count(&cb) == 8);
    T("cb_is_full true",             cb_is_full(&cb));
    T("push on full -> false",       cb_push(&cb, 99) == false);

    uint8_t v = 0;
    bool order_ok = true;
    for (uint8_t i = 1; i <= 3; ++i) { cb_pop(&cb, &v); if (v != i) order_ok = false; }
    T("pop 3 -> FIFO 1,2,3",         order_ok);
    T("count == 5 after 3 pops",     cb_count(&cb) == 5);

    // 랩어라운드: head 가 끝을 넘어 되돌아온다
    T("push wrap 9,10,11 succeed",   cb_push(&cb,9) && cb_push(&cb,10) && cb_push(&cb,11));
    T("count == 8 (full again)",     cb_count(&cb) == 8);
    uint8_t expect[8] = {4,5,6,7,8,9,10,11};
    bool wrap_ok = true;
    for (int i = 0; i < 8; ++i) { cb_pop(&cb, &v); if (v != expect[i]) wrap_ok = false; }
    T("pop all -> 4..8,9,10,11 order", wrap_ok);
    T("empty after draining",        cb_is_empty(&cb));
    T("pop on empty -> false",       cb_pop(&cb, &v) == false);

    cb_push(&cb, 42); cb_push(&cb, 43);
    cb_flush(&cb);
    T("cb_flush -> empty",           cb_is_empty(&cb) && cb_count(&cb) == 0);

    // -------- (B) cbp2_t : pow2 masking --------
    printf("== (B) cbp2_t (power-of-two masking) ==\n");
    uint8_t p2sto[4];
    cbp2_t p2;
    T("cbp2_init(size=3) rejects non-pow2", cbp2_init(&p2, p2sto, 3) == false);
    T("cbp2_init(size=4) accepts pow2",     cbp2_init(&p2, p2sto, 4) == true);
    T("cbp2 empty count 0",          cbp2_count(&p2) == 0);
    T("cbp2 fill 4 (full capacity)", cbp2_push(&p2,10)&&cbp2_push(&p2,20)&&cbp2_push(&p2,30)&&cbp2_push(&p2,40));
    T("cbp2 count == 4 (no wasted slot)", cbp2_count(&p2) == 4);
    T("cbp2 push on full -> false",  cbp2_push(&p2, 50) == false);
    cbp2_pop(&p2, &v); T("cbp2 pop -> 10 (FIFO)", v == 10);
    cbp2_pop(&p2, &v); T("cbp2 pop -> 20",        v == 20);
    // 랩: head/tail 이 4를 넘어 마스킹으로 되돌아온다
    T("cbp2 push wrap 50,60 succeed", cbp2_push(&p2,50) && cbp2_push(&p2,60));
    uint8_t p2e[4] = {30,40,50,60};
    bool p2ok = true;
    for (int i = 0; i < 4; ++i) { cbp2_pop(&p2, &v); if (v != p2e[i]) p2ok = false; }
    T("cbp2 drain -> 30,40,50,60",   p2ok);
    T("cbp2 pop on empty -> false",  cbp2_pop(&p2, &v) == false);

    // -------- (C) spsc_t : lock-free, single thread + threaded --------
    printf("== (C) spsc_t (SPSC lock-free) ==\n");
    uint8_t ssto[4];
    spsc_t sp;
    spsc_init(&sp, ssto, 4);
    T("spsc capacity = size-1 (3): fill 3", spsc_push(&sp,1)&&spsc_push(&sp,2)&&spsc_push(&sp,3));
    T("spsc push 4th -> false (1 slot sacrificed)", spsc_push(&sp, 4) == false);
    spsc_pop(&sp, &v); T("spsc pop -> 1", v == 1);
    spsc_pop(&sp, &v); T("spsc pop -> 2", v == 2);
    spsc_pop(&sp, &v); T("spsc pop -> 3", v == 3);
    T("spsc pop on empty -> false",  spsc_pop(&sp, &v) == false);

    // 스레드: 생산자1/소비자1, 5000개 순서 보존 무손실
    uint8_t tsto[8];
    spsc_t tq;
    spsc_init(&tq, tsto, 8);
    spsc_arg_t sarg = { &tq, true };   // 에러 발생 시에만 false 로 내려감
    pthread_t pt, ct;
    pthread_create(&pt, NULL, spsc_prod, &sarg);
    pthread_create(&ct, NULL, spsc_cons, &sarg);
    pthread_join(pt, NULL);
    pthread_join(ct, NULL);
    T("spsc threaded: 5000 items, FIFO, no loss", sarg.ok);

    // -------- (D) mpmc_t : threaded 2P/2C, 합계 보존 --------
    printf("== (D) mpmc_t (MPMC mutex+condvar) ==\n");
    uint8_t msto[8];
    mpmc_t mq;
    mpmc_init(&mq, msto, 8);
    _Atomic uint64_t sum = 0;
    _Atomic uint32_t popped = 0;
    int total = 2 * MPMC_PER;                 // 생산자 2명 × MPMC_PER
    mpmc_arg_t marg = { &mq, &sum, &popped, total };
    pthread_t mp[2], mc[2];
    pthread_create(&mp[0], NULL, mpmc_prod, &marg);
    pthread_create(&mp[1], NULL, mpmc_prod, &marg);
    pthread_create(&mc[0], NULL, mpmc_cons, &marg);
    pthread_create(&mc[1], NULL, mpmc_cons, &marg);
    pthread_join(mp[0], NULL); pthread_join(mp[1], NULL);
    pthread_join(mc[0], NULL); pthread_join(mc[1], NULL);
    // 기대 합계: 각 생산자가 (0..MPMC_PER-1 & 0xFF) 를 밈 -> 2배
    uint64_t one = 0;
    for (int i = 0; i < MPMC_PER; ++i) one += (uint8_t)(i & 0xFF);
    T("mpmc threaded: 2P/2C sum preserved", sum == 2 * one);
    mpmc_destroy(&mq);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
