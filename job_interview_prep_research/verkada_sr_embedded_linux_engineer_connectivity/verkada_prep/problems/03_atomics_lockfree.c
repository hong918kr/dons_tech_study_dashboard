// 03_atomics_lockfree.c  —  PRACTICE STUB (직접 채워넣기)
// Atomic 연산 & Lock-free (Atomics & Lock-free Patterns)  —  Q21~Q30
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=03_atomics_lockfree
//   또는:     cc -std=c11 -Wall -Wextra -pthread 03_atomics_lockfree.c -o /tmp/vk_atomics && /tmp/vk_atomics
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 → [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일·실행되며 대부분 FAIL 로 뜬다. 스레드 테스트는 전부
//  스핀 상한이 있어 몇 초 안에 FAIL 로 빠져나온다 — 절대 멈추지 않는다.)
//
// 리크루터 메일이 복습 주제로 "atomic operations"를 이름까지 찍어서 알려줬다.
// 게이트웨이(GC31-E/GW31-E) 펌웨어의 실제 동시성 경로 — 모뎀 UART RX, 링크 상태
// 스냅샷, 무중단 설정 교체 — 는 전부 mutex 없이 atomic + memory order로 푼다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

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
// ⚠ TSan 스위치 — Q27/Q28 seqlock 전용 (건드리지 말 것)
// seqlock은 C 표준상 **의도된 data race**라서 ThreadSanitizer가 정상적으로 경고한다.
// TSan 빌드에서는 seqlock **스레드** 테스트만 끄고 단일 스레드로 같은 불변식을 본다.
//   수동으로 끄기: -DVK_NO_SEQLOCK_THREADS
//   TSan 경고를 직접 보고 싶으면: -DVK_FORCE_SEQLOCK_THREADS
// ---------------------------------------------------------------------------
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define VK_TSAN 1
#  endif
#endif
#if defined(__SANITIZE_THREAD__) && !defined(VK_TSAN)
#  define VK_TSAN 1
#endif
#ifndef VK_TSAN
#  define VK_TSAN 0
#endif
#if defined(VK_FORCE_SEQLOCK_THREADS)
#  define SEQLOCK_THREADED 1
#elif VK_TSAN || defined(VK_NO_SEQLOCK_THREADS)
#  define SEQLOCK_THREADED 0
#else
#  define SEQLOCK_THREADED 1
#endif

// ===========================================================================
// Q21. atomic 카운터 vs 비원자 카운터
// ===========================================================================
typedef struct {
    _Atomic unsigned long long n;
} Counter;

/* ---------------------------------------------------------------------------
 * Q21a.  카운터 초기화 / 증가 / 읽기
 *   KO: ctr_add 는 memory_order_relaxed 로 fetch_add 하고 **더하기 직전 값**을
 *       반환한다. 통계 카운터(드롭 수, 재연결 수)는 다른 메모리와의 순서가
 *       필요 없으므로 relaxed 가 정답이다. atomic_init / atomic_load_explicit 사용.
 *   EN: Implement a relaxed atomic counter; fetch_add returns the value BEFORE
 *       the addition. Relaxed is correct for pure statistics counters.
 *   ex: ctr_add(c,5) -> 0 ; ctr_add(c,3) -> 5 ; ctr_get(c) -> 8
 * ------------------------------------------------------------------------- */
void ctr_init(Counter *c) {
    (void)c;
    // TODO: implement
}

unsigned long long ctr_add(Counter *c, unsigned long long delta) {
    (void)c; (void)delta;
    // TODO: implement
    return 0ull;   // placeholder
}

unsigned long long ctr_get(const Counter *c) {
    (void)c;
    // TODO: implement
    return 0ull;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q21b.  비원자 `++` 의 lost update 를 결정적으로 재현
 *   KO: `shared++` 는 load / add / store 3단계다. 두 스레드가 이 사이에서
 *       끼어드는 최악 인터리빙을 지역 변수(r1, r2)로 그대로 흉내 내고 최종
 *       shared 값을 반환하라. 증가 2회인데 결과는 start+1 이어야 한다.
 *       atomic_interleave_demo 는 같은 시나리오를 atomic RMW 로 → start+2.
 *   EN: Simulate the worst interleaving of a non-atomic ++ (load/add/store) and
 *       return the final value (start+1). The atomic version must give start+2.
 *   ex: racy_interleave_demo(10) -> 11 ; atomic_interleave_demo(10) -> 12
 * ------------------------------------------------------------------------- */
unsigned long long racy_interleave_demo(unsigned long long start) {
    (void)start;
    // TODO: implement  (T1 load -> T2 load -> T1 add/store -> T2 add/store)
    return 0ull;   // placeholder
}

unsigned long long atomic_interleave_demo(unsigned long long start) {
    (void)start;
    // TODO: implement  (_Atomic 지역변수 + atomic_fetch_add 2회)
    return 0ull;   // placeholder
}

// ===========================================================================
// Q22. stop 플래그: release store / acquire load  (volatile 반례 포함)
// ===========================================================================
typedef struct {
    _Atomic bool stop;      // 동기화 지점 (release/acquire)
    char         reason[32];// stop 이전에 쓰이고, stop 을 본 뒤에만 읽힌다
    unsigned     seq;       // 함께 publish 되는 평범한(비원자) 데이터
} StopFlag;

/* ---------------------------------------------------------------------------
 * Q22.  종료 플래그 publish / observe
 *   KO: GW31-E 는 "오프라인 30분이면 자가 재부팅"한다. 감시 스레드가 종료 사유를
 *       채우고 stop 을 세우면, 워커는 stop 을 보는 순간 **사유까지 완성된 상태로**
 *       봐야 한다. stop_request 는 ①reason/seq 기록 → ②atomic_store(RELEASE).
 *       stop_requested 는 atomic_load(ACQUIRE). 순서를 바꾸면 안 된다.
 *       "왜 volatile 로는 안 되는가"를 말로 설명할 수 있어야 한다(노트 3장).
 *   EN: Publish shutdown reason then release-store the flag; observe with an
 *       acquire-load. The acquire/release pair gives happens-before, so a reader
 *       that sees true also sees reason/seq. volatile gives neither atomicity
 *       nor ordering.
 *   ex: stop_request(f,"link-down",4242) -> 워커가 stop==true 를 보면 reason=="link-down"
 * ------------------------------------------------------------------------- */
void stop_init(StopFlag *f) {
    (void)f;
    // TODO: implement
}

void stop_request(StopFlag *f, const char *reason, unsigned seq) {
    (void)f; (void)reason; (void)seq;
    // TODO: implement  (데이터 먼저, 그 다음 release store)
}

bool stop_requested(const StopFlag *f) {
    (void)f;
    // TODO: implement  (acquire load)
    return false;  // placeholder
}

/* ---- 반례: volatile 버전 (절대 스레드로 쓰지 말 것) -----------------------
 * volatile 이 보장하는 것: 컴파일러가 접근을 캐시/삭제/병합하지 못한다.
 * volatile 이 보장하지 **않는** 것: ①원자성 ②주변 비-volatile 접근과의 순서
 * ③CPU 레벨 가시성(arm64 weak memory model 에서는 DMB 가 필요하다).
 * x86 은 TSO 라서 "우연히" 동작한다 — 그래서 더 위험하다.
 * 아래는 그 반례를 코드로 남긴 것이고, 단일 스레드에서만 호출한다.
 * ------------------------------------------------------------------------- */
typedef struct {
    volatile int stop;
    char         reason[32];
    unsigned     seq;
} VolStop;

void vol_stop_init(VolStop *f) {
    (void)f;
    // TODO: implement  (stop=0, reason 비우기, seq=0)
}

void vol_stop_request(VolStop *f, const char *reason, unsigned seq) {
    (void)f; (void)reason; (void)seq;
    // TODO: implement  (snprintf reason, seq 대입, 마지막에 stop=1)
}

bool vol_stop_requested(const VolStop *f) {
    (void)f;
    // TODO: implement
    return false;  // placeholder
}

// ===========================================================================
// Q23. CAS 루프로 최대값 갱신
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q23.  atomic high-water mark (compare_exchange_weak 루프)
 *   KO: C11 에는 fetch_max 가 없다. "최대 신호세기 / 최대 큐 깊이" 같은
 *       high-water mark 는 CAS 루프로 만든다.
 *         cur = load(relaxed);
 *         while (candidate > cur) { if (CAS_weak(slot,&cur,candidate,...)) return true; }
 *         return false;
 *       ★ CAS 가 실패하면 expected(cur)에 **현재 값이 자동으로 들어온다** →
 *         루프 안에서 다시 load 하지 말 것. weak 는 가짜 실패(spurious failure)가
 *         있지만 루프 안이므로 문제없고 ARM LL/SC 에서 더 싸다.
 *       성공 order 는 release, 실패 order 는 relaxed.
 *   EN: Build fetch_max out of a compare_exchange_weak loop. On failure the
 *       expected operand is updated with the current value — never re-load.
 *   ex: 0에 5,3,9,9 -> true,false,true,false, 최종 9
 * ------------------------------------------------------------------------- */
bool atomic_max_update(_Atomic unsigned *slot, unsigned candidate) {
    (void)slot; (void)candidate;
    // TODO: implement
    return false;  // placeholder
}

bool atomic_min_update(_Atomic unsigned *slot, unsigned candidate) {
    (void)slot; (void)candidate;
    // TODO: implement  (부등호만 뒤집은 같은 패턴)
    return false;  // placeholder
}

// ===========================================================================
// Q24. CAS 스핀락 (atomic_flag)
// ===========================================================================
typedef struct {
    atomic_flag held;
} SpinLock;

/* ---------------------------------------------------------------------------
 * Q24.  atomic_flag 스핀락
 *   KO: atomic_flag 는 C11 에서 **항상 lock-free 가 보장되는 유일한 타입**이다.
 *         lock  : atomic_flag_test_and_set_explicit(..., ACQUIRE) 가 false 를
 *                 돌려줄 때까지 회전 (false = 내가 잡았다)
 *         unlock: atomic_flag_clear_explicit(..., RELEASE)
 *       이 acquire/release 짝 덕분에 임계구역 안의 **평범한(비원자) 데이터**가
 *       안전해진다. trylock 은 test_and_set 결과의 부정.
 *       면접 포인트 — 스핀락을 쓰면 안 되는 경우를 말할 수 있어야 한다:
 *         ①임계구역이 길거나 그 안에서 블록/시스템콜 ②단일 코어 + 선점
 *         (우선순위 역전) ③유저스페이스 Linux 의 경합 상황(mutex 가 보통 더 빠름)
 *   EN: Implement a spinlock on atomic_flag with acquire/release ordering, plus
 *       trylock. Be ready to explain when a spinlock is the wrong tool.
 *   ex: trylock -> true ; 다시 trylock -> false ; unlock 후 trylock -> true
 * ------------------------------------------------------------------------- */
void spin_init(SpinLock *l) {
    (void)l;
    // TODO: implement
}

void spin_lock(SpinLock *l) {
    (void)l;
    // TODO: implement  (test_and_set 이 false 를 줄 때까지 회전, ACQUIRE)
}

bool spin_trylock(SpinLock *l) {
    (void)l;
    // TODO: implement
    return false;  // placeholder
}

void spin_unlock(SpinLock *l) {
    (void)l;
    // TODO: implement  (clear, RELEASE)
}

// ===========================================================================
// Q25. SPSC 링버퍼 push/pop (lock-free)
// ===========================================================================
#define RING_CAP 64u                      /* 반드시 2의 거듭제곱 */

typedef struct {
    uint8_t          buf[RING_CAP];
    _Atomic unsigned head;                /* 소비자만 store */
    _Atomic unsigned tail;                /* 생산자만 store */
} Ring;

/* ---------------------------------------------------------------------------
 * Q25.  SPSC 링버퍼 push / pop
 *   KO: 모뎀 UART RX ISR(생산자 1) → 파서 태스크(소비자 1). ISR 안에서는 mutex 금지.
 *       성립 근거: 생산자만 tail 을 쓰고 소비자만 head 를 쓴다 → write-write 경합
 *       자체가 없다. 남는 문제는 '순서'뿐이고 release/acquire 한 쌍이 푼다.
 *         push: buf[t] = b  →  store(tail, RELEASE)      (데이터 먼저, publish 나중)
 *         pop : load(tail, ACQUIRE)  →  b = buf[h]
 *       자기 인덱스는 relaxed 로 읽어도 된다(나만 쓰니까).
 *       **한 칸을 비워둔다** → head==tail 이면 empty, next==head 면 full. 용량은 CAP-1.
 *   EN: Lock-free single-producer/single-consumer ring. Data write precedes the
 *       release-store of the index; the consumer acquire-loads it. One slot is
 *       left empty so full and empty are distinguishable (capacity CAP-1).
 *   ex: 63개 push 성공 -> 64번째 false ; pop 은 FIFO 순서 보존
 * ------------------------------------------------------------------------- */
void ring_init(Ring *r) {
    (void)r;
    // TODO: implement
}

bool ring_push(Ring *r, uint8_t b) {
    (void)r; (void)b;
    // TODO: implement  (생산자 전용)
    return false;  // placeholder
}

bool ring_pop(Ring *r, uint8_t *out) {
    (void)r; (void)out;
    // TODO: implement  (소비자 전용)
    return false;  // placeholder
}

// ===========================================================================
// Q26. count / full / empty + power-of-two mask (& vs %)
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q26.  2의 거듭제곱 마스킹과 count/full/empty
 *   KO: cap 이 2의 거듭제곱이면 `idx % cap` 은 `idx & (cap-1)` 과 **완전히 동일**
 *       하고, 나눗셈 명령이 없는 MCU 에서 수십 배 싸다(ISR 안에서 중요).
 *       count 는 `(tail - head) & (CAP-1)` — 부호 없는 뺄셈이라 인덱스가
 *       랩되어도 정확하다. 한 칸을 비워두므로 full 은 count == CAP-1.
 *       is_pow2(0) 은 false 여야 한다(0 & (0-1) == 0 함정).
 *   EN: With a power-of-two capacity, `& (cap-1)` equals `% cap` but costs one
 *       cycle. count = (tail-head) & (CAP-1) is exact even across wrap-around.
 *   ex: is_pow2(64)=true, is_pow2(63)=false, is_pow2(0)=false
 *       ring_mask_index(65,64)=1 ; 3개 push 후 ring_count()=3
 * ------------------------------------------------------------------------- */
bool is_pow2(unsigned x) {
    (void)x;
    // TODO: implement
    return false;  // placeholder
}

unsigned ring_mask_index(unsigned idx, unsigned cap) {
    (void)idx; (void)cap;
    // TODO: implement
    return 0u;     // placeholder
}

unsigned ring_count(const Ring *r) {
    (void)r;
    // TODO: implement
    return 0u;     // placeholder
}

bool ring_is_empty(const Ring *r) {
    (void)r;
    // TODO: implement
    return true;   // placeholder
}

bool ring_is_full(const Ring *r) {
    (void)r;
    // TODO: implement
    return false;  // placeholder
}

// ===========================================================================
// Q27/Q28. seqlock (latest-value 스냅샷)
// ===========================================================================
typedef struct {
    int32_t  rsrp_dbm;     /* 테스트 불변식: rsrq_db == -rsrp_dbm */
    int32_t  rsrq_db;
    uint32_t uptime_s;
    uint8_t  sim_slot;
} LinkStatus;

typedef struct {
    _Atomic uint32_t seq;  /* 짝수 = 안정, 홀수 = 쓰기 중 */
    LinkStatus       data; /* seq 로 보호되는 평범한 구조체 (원자 타입이 아니다) */
} SeqLock;

/* ---------------------------------------------------------------------------
 * Q27.  seqlock write (홀수/짝수 시퀀스 + release fence)
 *   KO: GC31-E 링크 상태(RSRP/RSRQ/uptime/SIM)를 1초마다 쓰는 writer 1개와
 *       수시로 읽는 reader 여러 개. reader 때문에 writer 가 절대 지연되면 안 된다.
 *         v = load(seq, relaxed);
 *         store(seq, v+1, relaxed);              // 홀수 = 쓰는 중
 *         atomic_thread_fence(RELEASE);          // seq 증가가 데이터보다 먼저 보이게
 *         s->data = *in;
 *         store(seq, v+2, RELEASE);              // 짝수 = 완료
 *       첫 fence 를 빼면 reader 가 '짝수 seq + 반쯤 쓰인 데이터'를 보고도 통과한다.
 *       seq_writing() 은 seq 가 홀수인지 (진단/테스트용).
 *   EN: Bump seq to odd, release-fence, write the payload, then release-store an
 *       even seq. Writers are never blocked by readers.
 *   ex: init 후 seq=0(짝수) ; write 1회 후 seq=2
 * ------------------------------------------------------------------------- */
void seq_init(SeqLock *s) {
    (void)s;
    // TODO: implement
}

void seq_write(SeqLock *s, const LinkStatus *in) {
    (void)s; (void)in;
    // TODO: implement
}

bool seq_writing(const SeqLock *s) {
    (void)s;
    // TODO: implement
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q28.  seqlock read (재시도 루프)
 *   KO:   for (;;) {
 *           before = load(seq, ACQUIRE);
 *           if (before & 1) continue;            // 쓰는 중 → 처음부터 다시
 *           *out = s->data;                      // 찢어진 값일 수 있다(정상)
 *           atomic_thread_fence(ACQUIRE);
 *           after = load(seq, relaxed);
 *           if (before == after) return;         // 유효한 스냅샷
 *         }
 *       ★ do/while 로 쓰면 안 된다 — `continue` 가 while 조건으로 점프해서 아직
 *         대입된 적 없는 `after`(쓰레기값)와 비교한다. 흔한 예제 코드의 실제 버그.
 *       ★ `*out = s->data` 는 C 표준상 **data race** 다 → TSan 이 정확히 여기를
 *         지적한다(버그가 아니라 seqlock 의 정의). 면접에서 이 한계를 먼저 말할 것.
 *       seq_try_read 는 재시도 상한이 있는 버전 — 실시간 경로용, 실패 시 false.
 *   EN: Retry loop: acquire-load seq (retry if odd), copy, acquire-fence,
 *       re-load seq; the snapshot is valid only if the two reads match.
 *   ex: write{-95,95,1,0} 직후 read -> 정확히 같은 값
 * ------------------------------------------------------------------------- */
void seq_read(const SeqLock *s, LinkStatus *out) {
    (void)s;
    memset(out, 0, sizeof *out);
    // TODO: implement
}

bool seq_try_read(const SeqLock *s, LinkStatus *out, unsigned max_tries) {
    (void)s; (void)max_tries;
    memset(out, 0, sizeof *out);
    // TODO: implement
    return false;  // placeholder
}

// ===========================================================================
// Q29. atomic refcount
// ===========================================================================
typedef struct {
    _Atomic int refs;
    uint32_t    version;
    uint32_t    upload_period_ms;
    char        apn[24];
} RcConfig;

/* ---------------------------------------------------------------------------
 * Q29.  refcount 로 무중단 설정 교체
 *   KO: Command(클라우드)에서 새 설정이 내려와도 읽는 중인 설정을 free 하면 안 된다.
 *       설정을 **불변 객체**로 만들고 refcount 로 수명을 관리한다.
 *         rc_new : calloc + refs=1 + version/upload_period_ms(=version*10)/apn
 *         rc_get : fetch_add(+1, RELAXED)   — 이미 유효한 참조가 있을 때만 호출
 *         rc_put : fetch_sub(-1, RELEASE) 가 1 을 반환하면 내가 마지막 →
 *                  atomic_thread_fence(ACQUIRE) 후 free, freed 카운터++ , true 반환
 *       ★ 왜 release 만으로는 부족한가: release 는 "내 접근이 남에게 보인다"뿐이다.
 *         free 하는 쪽은 반대로 "남들의 접근이 나에게 보인다"가 필요 → acquire.
 *       ★ 왜 증가는 relaxed 인가: 살아있는 객체에 대한 증가라 publish 할 게 없다.
 *   EN: Immutable config object with an atomic refcount. Increment relaxed,
 *       decrement release; on the last reference insert an acquire fence before
 *       free so every other thread's accesses are visible.
 *   ex: new -> refs=1 ; get -> 2 ; put -> false ; put -> true(해제)
 * ------------------------------------------------------------------------- */
RcConfig *rc_new(uint32_t version, const char *apn) {
    (void)version; (void)apn;
    // TODO: implement
    return calloc(1, sizeof(RcConfig));   // placeholder (크래시 방지용 할당만)
}

void rc_get(RcConfig *c) {
    (void)c;
    // TODO: implement
}

bool rc_put(RcConfig *c, Counter *freed) {
    (void)c; (void)freed;
    // TODO: implement
    return false;  // placeholder
}

int rc_count(const RcConfig *c) {
    (void)c;
    // TODO: implement
    return 0;      // placeholder
}

// ===========================================================================
// Q30. ABA 문제와 tagged pointer
// ===========================================================================
#define POOL_N 8u

#define TAG_PACK(idx1, tag)  (((uint64_t)(uint32_t)(tag) << 32) | (uint32_t)(idx1))
#define TAG_IDX1(v)          ((uint32_t)((v) & 0xFFFFFFFFu))   /* 1-based, 0=NULL */
#define TAG_CNT(v)           ((uint32_t)((v) >> 32))

typedef struct {
    uint32_t         next[POOL_N];   /* freelist 링크 (1-based, 0 = 끝) */
    _Atomic uint64_t head;           /* [63:32]=tag, [31:0]=1-based index */
} TaggedPool;

/* ---------------------------------------------------------------------------
 * Q30.  ABA 와 tagged pointer (단일 스레드로 검증 가능)
 *   KO: ABA — T1 이 top==A 를 읽고 선점된 사이 T2 가 A pop, B pop, A push 한다.
 *       top 은 다시 A 지만 A->next 는 완전히 다른 값이다. T1 의 CAS 는 "값이
 *       같은가"만 보므로 **성공해버리고** 리스트가 깨진다.
 *       해법 ①tagged pointer(버전 카운터) ②hazard pointer/RCU ③그냥 mutex.
 *       여기서는 배열 freelist 에 (32bit index | 32bit tag)를 한 워드로 packing 해
 *       64비트 CAS 로 다룬다. pop/push 마다 tag 를 +1 하면 stale CAS 가 실패한다.
 *         pool_init : 0..POOL_N-1 을 전부 freelist 에 올린다(역순 push → top=0)
 *         pool_pop  : CAS 루프. head 의 index(1-based)가 0이면 false.
 *                     새 head = TAG_PACK(next[i-1], TAG_CNT(old)+1)
 *         pool_push : CAS 루프. next[idx]=TAG_IDX1(old);
 *                     새 head = TAG_PACK(idx+1, TAG_CNT(old)+1)
 *         pool_stale_cas : 옛 head 값으로 시도하는 CAS (strong) — 실패해야 정상
 *   EN: Pack a 32-bit index with a 32-bit tag into one 64-bit word and bump the
 *       tag on every push/pop, so a stale CAS from a preempted thread fails even
 *       when the index is identical. Demonstrable single-threaded.
 *   ex: snap=head ; pop,pop,push → index 같지만 tag 다름 → stale CAS false
 * ------------------------------------------------------------------------- */
void pool_init(TaggedPool *p) {
    (void)p;
    // TODO: implement
}

bool pool_pop(TaggedPool *p, uint32_t *out_idx) {
    (void)p; (void)out_idx;
    // TODO: implement
    return false;  // placeholder
}

void pool_push(TaggedPool *p, uint32_t idx) {
    (void)p; (void)idx;
    // TODO: implement
}

uint64_t pool_head_raw(const TaggedPool *p) {
    (void)p;
    // TODO: implement
    return 0ull;   // placeholder
}

bool pool_stale_cas(TaggedPool *p, uint64_t stale_head, uint64_t desired) {
    (void)p; (void)stale_head; (void)desired;
    // TODO: implement  (compare_exchange_strong)
    return false;  // placeholder
}


// 스레드 테스트 보조 + main (건드리지 말 것 — 전부 상한이 있어 절대 멈추지 않는다)
// 스레드 테스트 보조 (전부 상한이 있어 절대 멈추지 않는다)
// ===========================================================================

// ---- Q21 -------------------------------------------------------------------
#define Q21_THREADS 4
#define Q21_PER     50000ull
static Counter g_ctr;

static void *q21_worker(void *arg) {
    (void)arg;
    for (unsigned long long i = 0; i < Q21_PER; i++) ctr_add(&g_ctr, 1ull);
    return NULL;
}

// ---- Q22 -------------------------------------------------------------------
#define Q22_ROUNDS   20
#define Q22_SPIN_CAP 1000000L   /* 상한 — 미구현 stub에서도 절대 멈추지 않도록 */
typedef struct { StopFlag *f; bool saw_stop; bool payload_ok; } Q22Arg;

static void *q22_worker(void *arg) {
    Q22Arg *a = (Q22Arg *)arg;
    for (long i = 0; i < Q22_SPIN_CAP; i++) {
        if (stop_requested(a->f)) {
            a->saw_stop = true;
            /* acquire로 true를 봤으므로 reason/seq는 완성된 상태여야 한다 */
            a->payload_ok = (strcmp(a->f->reason, "link-down") == 0) &&
                            (a->f->seq == 4242u);
            return NULL;
        }
    }
    return NULL;   /* 상한 초과 → 미구현 stub도 여기서 빠져나온다 */
}

// ---- Q23 -------------------------------------------------------------------
#define Q23_THREADS 4
#define Q23_PER     5000u
static _Atomic unsigned g_hwm;
typedef struct { unsigned lane; } Q23Arg;

static void *q23_worker(void *arg) {
    Q23Arg *a = (Q23Arg *)arg;
    for (unsigned i = 0; i < Q23_PER; i++)
        atomic_max_update(&g_hwm, i * Q23_THREADS + a->lane + 1u);
    return NULL;
}

// ---- Q24 -------------------------------------------------------------------
#define Q24_THREADS 4
#define Q24_PER     100000
static SpinLock     g_spin;
static unsigned long g_guarded;    /* 일부러 비원자 — 스핀락이 지켜준다 */

static void *q24_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < Q24_PER; i++) {
        spin_lock(&g_spin);
        g_guarded++;                /* 임계구역: 딱 이만큼만 */
        /* 하네스 전용 컴파일러 배리어 — 락이 no-op일 때 컴파일러가 증가를
         * 루프 밖으로 빼내(g_guarded += N) 경합을 숨기는 것을 막는다.
         * 런타임 비용 0이고, 제대로 구현된 락에서는 아무 영향도 없다. */
        atomic_signal_fence(memory_order_seq_cst);
        spin_unlock(&g_spin);
    }
    return NULL;
}

// ---- Q25 -------------------------------------------------------------------
#define Q25_BYTES      50000
#define Q25_RETRY_CAP  20000000L
static Ring g_ring;
typedef struct { bool ok; unsigned long long sum; } Q25Arg;

static void *q25_producer(void *arg) {
    Q25Arg *a = (Q25Arg *)arg;
    for (int i = 0; i < Q25_BYTES; i++) {
        uint8_t b = (uint8_t)(i & 0xFF);
        long tries = 0;
        while (!ring_push(&g_ring, b)) {
            if (++tries > Q25_RETRY_CAP) { a->ok = false; return NULL; }
        }
        a->sum += b;
    }
    return NULL;
}

static void *q25_consumer(void *arg) {
    Q25Arg *a = (Q25Arg *)arg;
    int got = 0;
    long idle = 0;
    while (got < Q25_BYTES) {
        uint8_t b;
        if (ring_pop(&g_ring, &b)) {
            if (b != (uint8_t)(got & 0xFF)) { a->ok = false; return NULL; } /* FIFO 검증 */
            a->sum += b;
            got++;
            idle = 0;
        } else if (++idle > Q25_RETRY_CAP) {
            a->ok = false; return NULL;      /* 미구현 stub 탈출구 */
        }
    }
    return NULL;
}

// ---- Q27/Q28 (SEQLOCK_THREADED 일 때만) -------------------------------------
#if SEQLOCK_THREADED
#define Q28_READERS 3
#define Q28_WRITES  120000u
static SeqLock             g_seq;
static _Atomic bool        g_seq_stop;
static _Atomic unsigned long g_seq_torn, g_seq_seen;

static void *q28_writer(void *arg) {
    (void)arg;
    for (uint32_t i = 1; i <= Q28_WRITES; i++) {
        LinkStatus s = {
            .rsrp_dbm = -(int32_t)(i % 120u),
            .rsrq_db  =  (int32_t)(i % 120u),    /* 불변식: rsrq == -rsrp */
            .uptime_s = i,
            .sim_slot = (uint8_t)(i & 1u),
        };
        seq_write(&g_seq, &s);
    }
    atomic_store_explicit(&g_seq_stop, true, memory_order_release);
    return NULL;
}

static void *q28_reader(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&g_seq_stop, memory_order_acquire)) {
        LinkStatus s;
        seq_read(&g_seq, &s);
        if (s.uptime_s != 0u) {
            atomic_fetch_add_explicit(&g_seq_seen, 1ul, memory_order_relaxed);
            if (s.rsrq_db != -s.rsrp_dbm)
                atomic_fetch_add_explicit(&g_seq_torn, 1ul, memory_order_relaxed);
        }
    }
    return NULL;
}
#endif /* SEQLOCK_THREADED */

// ---- Q29 -------------------------------------------------------------------
#define Q29_THREADS 4
#define Q29_PER     20000
static RcConfig *g_cfg;
static Counter   g_freed;

static void *q29_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < Q29_PER; i++) {
        rc_get(g_cfg);
        /* 불변식 확인: 살아있는 동안 내용은 불변 */
        if (g_cfg->upload_period_ms != g_cfg->version * 10u) { /* never */ }
        rc_put(g_cfg, &g_freed);
    }
    return NULL;
}

// ===========================================================================
// main
// ===========================================================================
int main(void) {
    // ---------------------------------------------------------------- Q21 --
    printf("== Q21~Q22  atomic 기본: 카운터 & stop 플래그 ==\n");
    T("Q21 비원자 ++ 인터리빙 → 증가 1회 유실 (10+1+1 == 11)",
      racy_interleave_demo(10ull) == 11ull);
    T("Q21 atomic RMW는 같은 시나리오에서도 유실 없음 (== 12)",
      atomic_interleave_demo(10ull) == 12ull);

    Counter c1; ctr_init(&c1);
    T("Q21 fetch_add는 '더하기 직전 값'을 반환",
      ctr_add(&c1, 5ull) == 0ull && ctr_add(&c1, 3ull) == 5ull && ctr_get(&c1) == 8ull);

    ctr_init(&g_ctr);
    {
        pthread_t th[Q21_THREADS];
        for (int i = 0; i < Q21_THREADS; i++) pthread_create(&th[i], NULL, q21_worker, NULL);
        for (int i = 0; i < Q21_THREADS; i++) pthread_join(th[i], NULL);
    }
    T("Q21 relaxed fetch_add: 4스레드 × 50000 == 200000 (유실 0)",
      ctr_get(&g_ctr) == (unsigned long long)Q21_THREADS * Q21_PER);

    // ---------------------------------------------------------------- Q22 --
    {
        StopFlag f; stop_init(&f);
        T("Q22 초기 상태는 stop 아님", stop_requested(&f) == false);
    }
    {
        int rounds_ok = 0;
        for (int r = 0; r < Q22_ROUNDS; r++) {
            StopFlag f; stop_init(&f);
            Q22Arg a = { &f, false, false };
            pthread_t th;
            pthread_create(&th, NULL, q22_worker, &a);
            stop_request(&f, "link-down", 4242u);
            pthread_join(th, NULL);
            if (a.saw_stop && a.payload_ok) rounds_ok++;
        }
        T("Q22 acquire로 stop을 본 워커는 reason/seq도 완성된 상태로 본다 (20/20)",
          rounds_ok == Q22_ROUNDS);
    }
    {
        VolStop v; vol_stop_init(&v);
        bool before = vol_stop_requested(&v);
        vol_stop_request(&v, "link-down", 7u);
        /* 단일 스레드에서는 통과한다 — 그래서 위험하다.
         * volatile은 원자성도 순서도 보장하지 않으므로 멀티코어에서는 payload가
         * 늦게 보일 수 있다. 그래서 이 반례는 절대 스레드로 돌리지 않는다. */
        T("Q22 volatile 반례: 단일 스레드에서는 '통과'한다 (그래서 더 위험하다)",
          before == false && vol_stop_requested(&v) == true && v.seq == 7u);
    }

    // ---------------------------------------------------------------- Q23 --
    printf("\n== Q23~Q24  CAS: 최대값 갱신 & 스핀락 ==\n");
    {
        _Atomic unsigned hwm; atomic_init(&hwm, 0u);
        T("Q23 5 → 갱신됨(true)",            atomic_max_update(&hwm, 5u) == true);
        T("Q23 3 → 더 작으므로 무시(false)", atomic_max_update(&hwm, 3u) == false);
        T("Q23 9 → 갱신됨",                  atomic_max_update(&hwm, 9u) == true);
        T("Q23 9 → 같은 값도 갱신 안 함",    atomic_max_update(&hwm, 9u) == false);
        T("Q23 최종 high-water mark == 9",   atomic_load(&hwm) == 9u);

        _Atomic unsigned lwm; atomic_init(&lwm, 1000u);
        T("Q23 min 버전도 같은 CAS 루프로 동작",
          atomic_min_update(&lwm, 42u) == true && atomic_min_update(&lwm, 99u) == false
          && atomic_load(&lwm) == 42u);
    }
    atomic_init(&g_hwm, 0u);
    {
        pthread_t th[Q23_THREADS];
        Q23Arg    ar[Q23_THREADS];
        for (int i = 0; i < Q23_THREADS; i++) {
            ar[i].lane = (unsigned)i;
            pthread_create(&th[i], NULL, q23_worker, &ar[i]);
        }
        for (int i = 0; i < Q23_THREADS; i++) pthread_join(th[i], NULL);
    }
    T("Q23 4스레드 동시 갱신 후 전역 최대값 정확 (== 20000)",
      atomic_load(&g_hwm) == Q23_PER * Q23_THREADS);

    // ---------------------------------------------------------------- Q24 --
    {
        SpinLock l; spin_init(&l);
        T("Q24 초기 상태에서 trylock 성공", spin_trylock(&l) == true);
        T("Q24 이미 잡힌 락의 trylock 실패", spin_trylock(&l) == false);
        spin_unlock(&l);
        T("Q24 unlock 후 trylock 다시 성공", spin_trylock(&l) == true);
        spin_unlock(&l);
        spin_lock(&l);
        T("Q24 lock 후 trylock 실패",       spin_trylock(&l) == false);
        spin_unlock(&l);
    }
    spin_init(&g_spin);
    g_guarded = 0;
    {
        pthread_t th[Q24_THREADS];
        for (int i = 0; i < Q24_THREADS; i++) pthread_create(&th[i], NULL, q24_worker, NULL);
        for (int i = 0; i < Q24_THREADS; i++) pthread_join(th[i], NULL);
    }
    T("Q24 스핀락이 비원자 카운터를 보호 (4×100000 == 400000)",
      g_guarded == (unsigned long)Q24_THREADS * Q24_PER);

    // ---------------------------------------------------------------- Q25 --
    printf("\n== Q25~Q26  SPSC 링버퍼 (lock-free) ==\n");
    {
        Ring r; ring_init(&r);
        uint8_t v = 0;
        T("Q25 빈 버퍼 pop → false", ring_pop(&r, &v) == false);
        T("Q25 push/pop 왕복",       ring_push(&r, 0xA5) && ring_pop(&r, &v) && v == 0xA5);

        bool fill_ok = true;
        for (unsigned i = 0; i < RING_CAP - 1u; i++) fill_ok &= ring_push(&r, (uint8_t)i);
        T("Q25 용량은 CAP-1 (63개 push 성공)", fill_ok);
        T("Q25 한 칸 비워두므로 64번째 push는 실패", ring_push(&r, 0xFF) == false);

        bool fifo_ok = true;
        for (unsigned i = 0; i < RING_CAP - 1u; i++) {
            if (!ring_pop(&r, &v) || v != (uint8_t)i) { fifo_ok = false; break; }
        }
        T("Q25 FIFO 순서 + 랩어라운드 보존", fifo_ok);
        T("Q25 모두 빼면 다시 empty", ring_pop(&r, &v) == false);
    }
    ring_init(&g_ring);
    {
        Q25Arg pa = { true, 0ull }, ca = { true, 0ull };
        pthread_t pt, ct;
        pthread_create(&ct, NULL, q25_consumer, &ca);
        pthread_create(&pt, NULL, q25_producer, &pa);
        pthread_join(pt, NULL);
        pthread_join(ct, NULL);
        T("Q25 스레드 SPSC: 50000바이트 무손실·순서보존·체크섬 일치",
          pa.ok && ca.ok && pa.sum == ca.sum && pa.sum > 0ull);
    }

    // ---------------------------------------------------------------- Q26 --
    {
        T("Q26 is_pow2(64) true / is_pow2(63) false / is_pow2(0) false",
          is_pow2(64u) && !is_pow2(63u) && !is_pow2(0u));
        T("Q26 is_pow2(1)==true, is_pow2(2)==true", is_pow2(1u) && is_pow2(2u));

        bool mask_eq = true;
        for (unsigned i = 0; i < 256u; i++)
            if (ring_mask_index(i, 64u) != i % 64u) { mask_eq = false; break; }
        T("Q26 & (cap-1) 가 % cap 과 완전히 동일 (0..255, cap=64)", mask_eq);
        T("Q26 마스킹 랩: 64→0, 65→1, 63→63",
          ring_mask_index(64u,64u)==0u && ring_mask_index(65u,64u)==1u
          && ring_mask_index(63u,64u)==63u);

        Ring r; ring_init(&r);
        T("Q26 init 직후 empty, count 0, full 아님",
          ring_is_empty(&r) && ring_count(&r) == 0u && !ring_is_full(&r));
        ring_push(&r, 1); ring_push(&r, 2); ring_push(&r, 3);
        T("Q26 3개 push 후 count == 3", ring_count(&r) == 3u);
        uint8_t v; ring_pop(&r, &v);
        T("Q26 1개 pop 후 count == 2, empty 아님",
          ring_count(&r) == 2u && !ring_is_empty(&r));
        while (ring_push(&r, 0xEE)) { }
        T("Q26 가득 차면 is_full true, count == CAP-1",
          ring_is_full(&r) && ring_count(&r) == RING_CAP - 1u);
    }

    // ------------------------------------------------------------ Q27/Q28 --
    printf("\n== Q27~Q28  seqlock (latest-value 스냅샷) ==\n");
    {
        SeqLock s; seq_init(&s);
        LinkStatus out;
        T("Q27 init: seq == 0 (짝수 = 안정)",
          !seq_writing(&s));

        LinkStatus in1 = { -95, 95, 1u, 0u };
        seq_write(&s, &in1);
        T("Q27 write 1회 후 seq == 2 (홀수→짝수 2단계)", !seq_writing(&s));

        seq_read(&s, &out);
        T("Q28 read가 기록한 스냅샷을 그대로 반환",
          out.rsrp_dbm == -95 && out.rsrq_db == 95 && out.uptime_s == 1u && out.sim_slot == 0u);

        bool seq_ok = true;
        for (uint32_t i = 2; i <= 1000u; i++) {
            LinkStatus in = { -(int32_t)(i % 120u), (int32_t)(i % 120u), i, (uint8_t)(i & 1u) };
            seq_write(&s, &in);
            LinkStatus got;
            seq_read(&s, &got);
            if (got.uptime_s != i || got.rsrq_db != -got.rsrp_dbm) { seq_ok = false; break; }
        }
        T("Q27/Q28 1000회 write→read 왕복: 항상 일관된 최신 스냅샷", seq_ok);
        T("Q27 시퀀스는 write 1회당 정확히 2 증가 (write 1000회 → 짝수 유지)",
          !seq_writing(&s));

        LinkStatus tr;
        T("Q28 seq_try_read: 경합 없으면 첫 시도에 성공",
          seq_try_read(&s, &tr, 1u) == true && tr.uptime_s == 1000u);

        SeqLock empty; seq_init(&empty);
        LinkStatus z;
        seq_read(&empty, &z);
        T("Q28 아무도 안 썼으면 0으로 초기화된 스냅샷", z.uptime_s == 0u && z.rsrp_dbm == 0);
    }
#if SEQLOCK_THREADED
    {
        seq_init(&g_seq);
        atomic_init(&g_seq_stop, false);
        atomic_init(&g_seq_torn, 0ul);
        atomic_init(&g_seq_seen, 0ul);
        pthread_t w, rd[Q28_READERS];
        for (int i = 0; i < Q28_READERS; i++) pthread_create(&rd[i], NULL, q28_reader, NULL);
        pthread_create(&w, NULL, q28_writer, NULL);
        pthread_join(w, NULL);
        for (int i = 0; i < Q28_READERS; i++) pthread_join(rd[i], NULL);
        T("Q28 스레드: writer 120000회 + reader 3개, 찢어진 스냅샷 0",
          atomic_load(&g_seq_torn) == 0ul && atomic_load(&g_seq_seen) > 0ul);
    }
#else
    printf("  [SKIP] Q28 스레드 테스트 — seqlock은 의도된 data race라 TSan 빌드에서 제외\n");
    printf("         (위의 단일 스레드 검증이 동일한 불변식을 확인한다)\n");
#endif

    // ------------------------------------------------------------ Q29/Q30 --
    printf("\n== Q29~Q30  refcount & ABA / tagged pointer ==\n");
    {
        Counter freed; ctr_init(&freed);
        RcConfig *c = rc_new(7u, "apn.one");
        T("Q29 rc_new: refs == 1, 내용 정확",
          c != NULL && rc_count(c) == 1 && c->upload_period_ms == 70u
          && strcmp(c->apn, "apn.one") == 0);
        rc_get(c);
        T("Q29 rc_get 후 refs == 2", rc_count(c) == 2);
        T("Q29 마지막이 아닌 put은 free 안 함(false)", rc_put(c, &freed) == false);
        T("Q29 put 후 refs == 1, 아직 해제 안 됨", rc_count(c) == 1 && ctr_get(&freed) == 0ull);
        T("Q29 마지막 put에서만 free (true)", rc_put(c, &freed) == true);
        T("Q29 free 카운터 == 1", ctr_get(&freed) == 1ull);
    }
    ctr_init(&g_freed);
    g_cfg = rc_new(11u, "apn.two");     /* main이 참조 1개를 계속 들고 있는다 */
    {
        pthread_t th[Q29_THREADS];
        for (int i = 0; i < Q29_THREADS; i++) pthread_create(&th[i], NULL, q29_worker, NULL);
        for (int i = 0; i < Q29_THREADS; i++) pthread_join(th[i], NULL);
    }
    T("Q29 4스레드 × 20000 get/put 후 refs == 1 (조기 해제 없음)",
      rc_count(g_cfg) == 1 && ctr_get(&g_freed) == 0ull);
    T("Q29 main이 마지막 참조를 놓으면 그때 해제",
      rc_put(g_cfg, &g_freed) == true && ctr_get(&g_freed) == 1ull);
    g_cfg = NULL;

    // ---------------------------------------------------------------- Q30 --
    {
        TaggedPool p;
        pool_init(&p);
        uint32_t a = 99u, b = 99u, x = 99u;

        T("Q30 init 후 top은 인덱스 0", TAG_IDX1(pool_head_raw(&p)) == 1u);

        uint64_t snap_A = pool_head_raw(&p);   /* ← T1이 읽고 선점되었다고 가정 */
        uint32_t next_of_A = p.next[0];        /*    T1이 기억한 A->next (= 1) */

        /* T2가 하는 일: pop A, pop B, push A  → top은 다시 A (=ABA) */
        T("Q30 pop A → 0", pool_pop(&p, &a) && a == 0u);
        T("Q30 pop B → 1", pool_pop(&p, &b) && b == 1u);
        pool_push(&p, a);

        uint64_t now = pool_head_raw(&p);
        T("Q30 ABA 상황 재현: 인덱스는 같은데(0) 태그가 달라 값 자체가 다르다",
          TAG_IDX1(snap_A) == TAG_IDX1(now) && snap_A != now
          && TAG_CNT(now) > TAG_CNT(snap_A));

        /* 태그가 없었다면 이 CAS가 성공해서 리스트가 깨진다 */
        T("Q30 stale CAS는 태그 덕분에 실패한다 (리스트 보호)",
          pool_stale_cas(&p, snap_A, TAG_PACK(next_of_A, 999u)) == false);

        /* 태그까지 일치하는 최신 값이면 당연히 성공 */
        uint64_t fresh = pool_head_raw(&p);
        T("Q30 최신 값(태그 포함)으로는 CAS 성공",
          pool_stale_cas(&p, fresh, TAG_PACK(TAG_IDX1(fresh), TAG_CNT(fresh) + 1u)) == true);

        /* freelist 자체의 무결성: 남은 것을 전부 꺼내고 다시 채운다 */
        pool_init(&p);
        unsigned n = 0;
        while (pool_pop(&p, &x)) n++;
        T("Q30 POOL_N개 전부 pop 가능, 그 다음은 false",
          n == POOL_N && pool_pop(&p, &x) == false);

        uint32_t t0 = TAG_CNT(pool_head_raw(&p));
        pool_push(&p, 3u);
        pool_pop(&p, &x);
        T("Q30 태그는 push/pop마다 단조 증가 (ABA 탐지의 근거)",
          x == 3u && TAG_CNT(pool_head_raw(&p)) == t0 + 2u);
    }

    // ---------------------------------------------------------------- 결과 --
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
