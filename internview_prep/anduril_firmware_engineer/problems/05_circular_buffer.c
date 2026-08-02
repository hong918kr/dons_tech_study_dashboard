// 05_circular_buffer.c  —  PRACTICE STUB (직접 채워넣기)
// 원형 버퍼 / 링 버퍼 (Ring buffer)  —  Q56~Q65
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=05_circular_buffer
//   또는:     cc -std=c11 -Wall -Wextra 05_circular_buffer.c -o /tmp/andb_circular_buffer -lpthread && /tmp/andb_circular_buffer
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 스레드 테스트는
//  몇 초 스핀 후 FAIL 로 빠져나온다.)
//
// 임베디드 최다 빈출 자료구조. UART RX ISR -> main loop 파이프라인의 심장.
// 네 가지 스타일:
//   (A) count 필드 기반 cb_t   (B) power-of-two mask 기반 cbp2_t
//   (C) SPSC 무락 spsc_t        (D) MPMC mutex+condvar mpmc_t
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

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
// (A) count 필드 기반 원형 버퍼 cb_t
// ===========================================================================
typedef struct {
    uint8_t *buffer;  // 외부에서 준 정적 저장소
    size_t   size;    // 버퍼 용량 (원소 수)
    size_t   head;    // 다음에 쓸 인덱스 (생산자)
    size_t   tail;    // 다음에 읽을 인덱스 (소비자)
    size_t   count;   // 현재 저장된 원소 수
} cb_t;

/* ---------------------------------------------------------------------------
 * Q56.  원형 버퍼 초기화
 *   KO: cb 를 buffer/size 로 초기화하고 head=tail=count=0 으로 리셋한다.
 *       cb 가 NULL 이면 아무것도 하지 않는다.
 *   EN: Initialize the ring buffer with the given storage and capacity;
 *       reset head/tail/count. Do nothing if cb is NULL.
 *   ex: cb_init(&cb, buf, 8) -> count==0, is_empty==true
 * ------------------------------------------------------------------------- */
void cb_init(cb_t *cb, uint8_t *buffer, size_t size) {
    (void)cb; (void)buffer; (void)size;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q59.  비었는가?  (Q57/Q58 이 내부적으로 쓰므로 먼저 구현 권장)
 *   KO: 원소가 하나도 없으면 true. cb 가 NULL 이면 안전하게 true.
 *   EN: Return true if empty (or cb is NULL).
 *   ex: 방금 init 한 버퍼 -> true
 * ------------------------------------------------------------------------- */
bool cb_is_empty(const cb_t *cb) {
    (void)cb;
    // TODO: implement
    return true;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q60.  가득 찼는가?
 *   KO: count == size 이면 true. cb 가 NULL 이면 false.
 *   EN: Return true if full (count == size); false if cb is NULL.
 *   ex: size 8 에 8개 push 후 -> true
 * ------------------------------------------------------------------------- */
bool cb_is_full(const cb_t *cb) {
    (void)cb;
    // TODO: implement
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q57.  push / enqueue
 *   KO: head 위치에 data 를 쓰고 head 를 랩어라운드 증가, count++.
 *       가득 차 있으면(또는 cb NULL) 덮어쓰지 말고 false 반환.
 *   EN: Enqueue one byte; advance head with wrap-around; return false if full.
 *   ex: 빈 버퍼에 push(5) -> true, count==1
 *       가득 찬 버퍼에 push -> false
 * ------------------------------------------------------------------------- */
bool cb_push(cb_t *cb, uint8_t data) {
    (void)cb; (void)data;
    // TODO: implement
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q58.  pop / dequeue
 *   KO: tail 위치의 값을 *out 에 담고 tail 랩어라운드 증가, count--.
 *       비어 있으면(또는 cb/out NULL) false.
 *   EN: Dequeue one byte into *out (FIFO); return false if empty.
 *   ex: [1,2,3] pop -> *out==1, true
 *       빈 버퍼 pop -> false
 * ------------------------------------------------------------------------- */
bool cb_pop(cb_t *cb, uint8_t *out) {
    (void)cb; (void)out;
    // TODO: implement
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q61.  현재 원소 수
 *   KO: 저장된 원소 수를 반환. cb NULL 이면 0.
 *   EN: Return the number of stored elements (0 if cb is NULL).
 * ------------------------------------------------------------------------- */
size_t cb_count(const cb_t *cb) {
    (void)cb;
    // TODO: implement
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q62.  flush (논리적 비우기)
 *   KO: 데이터는 지우지 않고 head/tail/count 만 0 으로. cb NULL 이면 무시.
 *   EN: Logically empty the buffer by resetting indices/count.
 * ------------------------------------------------------------------------- */
void cb_flush(cb_t *cb) {
    (void)cb;
    // TODO: implement
}

// ===========================================================================
// (B) power-of-two 크기 + 마스킹 변형 cbp2_t
//   head/tail 을 자유 증가 uint32_t 로 두고 접근 시 & mask.  % 대신 AND.
//   count = head - tail (부호없는 뺄셈).  slot 낭비 없이 전용량 사용.
// ===========================================================================
typedef struct {
    uint8_t *buf;
    uint32_t size;   // 반드시 2의 거듭제곱
    uint32_t mask;   // size - 1
    uint32_t head;   // free-running
    uint32_t tail;   // free-running
} cbp2_t;

/* ---------------------------------------------------------------------------
 * Q63.  power-of-two 마스킹 링 버퍼  (init / count / push / pop)
 *   KO: size 가 2의 거듭제곱이 아니면 init 은 false. 인덱스는 자유 증가시키고
 *       버퍼 접근 시에만 & mask 로 감싼다. full = (count==size), empty=(head==tail).
 *   EN: Ring buffer whose capacity is a power of two; use bit-mask instead of
 *       modulo. init returns false for non-pow2 size.
 *   ex: cbp2_init(&q, buf, 4) -> true ; cbp2_init(&q, buf, 3) -> false
 *       size 4 에 4개 push 성공(전용량), 5번째 push -> false
 *   hint: (size & (size-1)) == 0  <=> size 는 2의 거듭제곱
 * ------------------------------------------------------------------------- */
bool cbp2_init(cbp2_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement
    return false;  // placeholder
}
uint32_t cbp2_count(const cbp2_t *q) {
    (void)q;
    // TODO: implement (head - tail)
    return 0;  // placeholder
}
bool cbp2_push(cbp2_t *q, uint8_t data) {
    (void)q; (void)data;
    // TODO: implement
    return false;  // placeholder
}
bool cbp2_pop(cbp2_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: implement
    return false;  // placeholder
}

// ===========================================================================
// (C) SPSC 무락(lock-free) 링 버퍼 spsc_t
//   생산자(ISR)만 head store, 소비자(main)만 tail store -> 공유 쓰기 경합 없음.
//   count 대신 head==tail(empty), (head+1)==tail(full) 판별, 한 칸 희생.
//   멀티코어 재정렬 방지: C11 acquire/release atomics 사용.
// ===========================================================================
typedef struct {
    uint8_t          *buf;
    uint32_t          size;  // pow2
    uint32_t          mask;
    _Atomic uint32_t  head;  // 생산자만 store
    _Atomic uint32_t  tail;  // 소비자만 store
} spsc_t;

/* ---------------------------------------------------------------------------
 * Q64.  SPSC lock-free 링 버퍼  (init / push / pop)
 *   KO: 단일 생산자/단일 소비자 전용. 락 없이 안전. 사용 가능 용량 = size-1.
 *       push: head(relaxed 로 내 것) 읽고 tail(acquire 로 상대 것) 읽어 full 검사
 *             -> 데이터 먼저 쓰고 -> head 를 release store 로 공개.
 *       pop:  tail(relaxed) / head(acquire) 로 empty 검사 -> 데이터 읽고
 *             -> tail 을 release store.
 *   EN: Lock-free single-producer/single-consumer ring. Producer only stores
 *       head, consumer only stores tail; publish data before advancing the
 *       index (release), read the peer index with acquire. Usable = size-1.
 *   ex: size 4 -> 최대 3개 저장, 4번째 push false
 *   hint: full = ((head+1) & mask) == (tail & mask) ; empty = head == tail
 *         왜 무락인가? 각 인덱스를 한 스레드만 '쓴다' -> write-write 경합 없음.
 * ------------------------------------------------------------------------- */
bool spsc_init(spsc_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement
    return false;  // placeholder
}
bool spsc_push(spsc_t *q, uint8_t data) {
    (void)q; (void)data;
    // TODO: implement (relaxed load head, acquire load tail, release store head)
    return false;  // placeholder
}
bool spsc_pop(spsc_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: implement (relaxed load tail, acquire load head, release store tail)
    return false;  // placeholder
}

// ===========================================================================
// (D) MPMC 블로킹 큐 mpmc_t (mutex + condition variable)
//   다중 생산자/소비자 -> head/tail/count 를 여럿이 갱신 -> mutex 로 보호.
//   가득/빔 대기는 condition variable. while(조건) cond_wait 로 spurious wakeup 방어.
// ===========================================================================
typedef struct {
    uint8_t        *buf;
    size_t          size;
    size_t          head, tail, count;
    pthread_mutex_t m;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} mpmc_t;

/* ---------------------------------------------------------------------------
 * Q65.  MPMC 블로킹 큐  (init / push / pop / destroy)
 *   KO: push/pop 은 블로킹. 가득 차면 not_full 에서 대기, 비면 not_empty 에서 대기.
 *       임계구역 전체를 mutex 로 감싸고, 성공 후 반대편 cond 를 signal.
 *   EN: Blocking multi-producer/multi-consumer queue. Guard the whole
 *       critical section with a mutex; wait on a condvar while full/empty
 *       (use a while loop!), signal the other condvar after mutating.
 *   ex: 버퍼가 가득 차면 push 하는 스레드는 소비자가 pop 할 때까지 잠든다.
 *   hint: while (count == size)  pthread_cond_wait(&not_full, &m);
 *         while (count == 0)     pthread_cond_wait(&not_empty, &m);
 * ------------------------------------------------------------------------- */
void mpmc_init(mpmc_t *q, uint8_t *buf, size_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement (init mutex + 2 condvars, reset indices)
}
void mpmc_push(mpmc_t *q, uint8_t data) {
    (void)q; (void)data;
    // TODO: implement (blocking)
}
uint8_t mpmc_pop(mpmc_t *q) {
    (void)q;
    // TODO: implement (blocking)
    return 0;  // placeholder
}
void mpmc_destroy(mpmc_t *q) {
    (void)q;
    // TODO: implement (destroy mutex + condvars)
}

// ===========================================================================
// 스레드 테스트용 워커 (건드리지 말 것)
// ===========================================================================
#define SPSC_N 5000
#define SPIN_LIMIT 5000000   // 연속 실패 상한: 정상 구현이면 안 걸림(미구현 stub 는 여기서 탈출)
typedef struct { spsc_t *q; bool ok; } spsc_arg_t;

static void *spsc_prod(void *arg) {
    spsc_arg_t *a = (spsc_arg_t *)arg;
    for (int i = 0; i < SPSC_N; ++i) {
        long spins = 0;
        while (!spsc_push(a->q, (uint8_t)(i & 0xFF))) {
            if (++spins > SPIN_LIMIT) { a->ok = false; return NULL; }
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

#define MPMC_PER 1000
typedef struct { mpmc_t *q; _Atomic uint64_t *sum; _Atomic uint32_t *popped; int total_to_pop; } mpmc_arg_t;

static void *mpmc_prod(void *arg) {
    mpmc_t *q = ((mpmc_arg_t *)arg)->q;
    for (int i = 0; i < MPMC_PER; ++i) mpmc_push(q, (uint8_t)(i & 0xFF));
    return NULL;
}
static void *mpmc_cons(void *arg) {
    mpmc_arg_t *a = (mpmc_arg_t *)arg;
    for (;;) {
        uint32_t idx = atomic_fetch_add(a->popped, 1);
        if ((int)idx >= a->total_to_pop) { atomic_fetch_sub(a->popped, 1); break; }
        uint8_t v = mpmc_pop(a->q);
        atomic_fetch_add(a->sum, (uint64_t)v);
    }
    return NULL;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
int main(void) {
    // -------- (A) cb_t --------
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

    // -------- (B) cbp2_t --------
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
    T("cbp2 push wrap 50,60 succeed", cbp2_push(&p2,50) && cbp2_push(&p2,60));
    uint8_t p2e[4] = {30,40,50,60};
    bool p2ok = true;
    for (int i = 0; i < 4; ++i) { cbp2_pop(&p2, &v); if (v != p2e[i]) p2ok = false; }
    T("cbp2 drain -> 30,40,50,60",   p2ok);
    T("cbp2 pop on empty -> false",  cbp2_pop(&p2, &v) == false);

    // -------- (C) spsc_t --------
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

    uint8_t tsto[8];
    spsc_t tq;
    spsc_init(&tq, tsto, 8);
    spsc_arg_t sarg = { &tq, true };
    pthread_t pt, ct;
    pthread_create(&pt, NULL, spsc_prod, &sarg);
    pthread_create(&ct, NULL, spsc_cons, &sarg);
    pthread_join(pt, NULL);
    pthread_join(ct, NULL);
    T("spsc threaded: 5000 items, FIFO, no loss", sarg.ok);

    // -------- (D) mpmc_t --------
    printf("== (D) mpmc_t (MPMC mutex+condvar) ==\n");
    uint8_t msto[8];
    mpmc_t mq;
    mpmc_init(&mq, msto, 8);
    _Atomic uint64_t sum = 0;
    _Atomic uint32_t popped = 0;
    int total = 2 * MPMC_PER;
    mpmc_arg_t marg = { &mq, &sum, &popped, total };
    pthread_t mp[2], mc[2];
    pthread_create(&mp[0], NULL, mpmc_prod, &marg);
    pthread_create(&mp[1], NULL, mpmc_prod, &marg);
    pthread_create(&mc[0], NULL, mpmc_cons, &marg);
    pthread_create(&mc[1], NULL, mpmc_cons, &marg);
    pthread_join(mp[0], NULL); pthread_join(mp[1], NULL);
    pthread_join(mc[0], NULL); pthread_join(mc[1], NULL);
    uint64_t one = 0;
    for (int i = 0; i < MPMC_PER; ++i) one += (uint8_t)(i & 0xFF);
    T("mpmc threaded: 2P/2C sum preserved", sum == 2 * one);
    mpmc_destroy(&mq);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
