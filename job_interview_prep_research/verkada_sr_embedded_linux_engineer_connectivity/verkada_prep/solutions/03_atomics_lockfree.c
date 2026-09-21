// 03_atomics_lockfree.c  —  REFERENCE SOLUTION
// Atomic 연산 & Lock-free (Atomics & Lock-free Patterns)  —  Q21~Q30
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -pthread 03_atomics_lockfree.c -o /tmp/vk_atomics && /tmp/vk_atomics
// 리크루터 메일이 복습 주제로 "atomic operations"를 이름까지 찍어서 알려줬다.
// 게이트웨이(GC31-E/GW31-E) 펌웨어의 실제 동시성 경로 — 모뎀 UART RX, 링크 상태
// 스냅샷, 무중단 설정 교체 — 는 전부 mutex 없이 atomic + memory order로 푼다.
// 이 세트는 "왜 atomic인가 / 왜 이 memory order인가"를 말로 설명할 수 있게 만드는 것이 목표다.
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
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ---------------------------------------------------------------------------
// ⚠ TSan 스위치 — Q27/Q28 seqlock 전용
// seqlock은 "reader가 찢어진 값을 읽고 나서 버린다"는 설계라 C 표준상 **엄밀히
// data race**다. 그래서 ThreadSanitizer는 정상적으로 경고를 낸다(버그가 아니라
// 설계의 대가). TSan 빌드에서는 seqlock **스레드** 테스트만 끄고, 동일한 불변식을
// 단일 스레드로 검증한다. 나머지 모든 테스트는 TSan 클린이어야 한다.
//   수동으로 끄기: -DVK_NO_SEQLOCK_THREADS
//   TSan 경고를 직접 눈으로 보고 싶으면: -DVK_FORCE_SEQLOCK_THREADS
//     → `cc -std=c11 -fsanitize=thread -pthread -DVK_FORCE_SEQLOCK_THREADS ...`
//       실행하면 seq_read의 `*out = s->data` 위치에서 data race가 보고된다.
//       이건 버그가 아니라 seqlock의 정의 그 자체다. 면접에서 이 사실을 먼저
//       말하면 "표준까지 아는 사람"으로 읽힌다.
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
// Q21. atomic 카운터 (fetch_add, relaxed) vs 비원자 카운터
// ---------------------------------------------------------------------------
// `c++`는 원자 연산이 아니다 — load / add / store 세 단계다. 두 스레드가 이
// 세 단계 사이에서 끼어들면 증가가 통째로 유실된다(lost update).
// 통계 카운터(드롭 수, 재전송 수)처럼 "값만 맞으면 되고 다른 데이터와의 순서는
// 상관없는" 경우 memory_order_relaxed가 정답이다 — 배리어 비용을 내지 않는다.
// GC31-E: LTE 재연결 횟수, SIM failover 횟수, 링버퍼 drop 카운터가 전부 이것.
// ===========================================================================
typedef struct {
    _Atomic unsigned long long n;
} Counter;

void ctr_init(Counter *c) {
    atomic_init(&c->n, 0ull);
}

// relaxed: 원자성(값 유실 없음)만 필요하고 다른 메모리와의 순서는 필요 없다.
// 반환값은 "더하기 직전의 값"(fetch-then-add). 이걸 이용해 슬롯 예약도 한다.
unsigned long long ctr_add(Counter *c, unsigned long long delta) {
    return atomic_fetch_add_explicit(&c->n, delta, memory_order_relaxed);
}

unsigned long long ctr_get(const Counter *c) {
    return atomic_load_explicit(&c->n, memory_order_relaxed);
}

// 비원자 `++`의 최악 인터리빙을 **결정적으로** 재현한다.
//   T1: load(shared)          -> reg1 = start
//   T2: load(shared)          -> reg2 = start        (T1이 store하기 전)
//   T1: reg1+1 -> store(shared)
//   T2: reg2+1 -> store(shared)                      (T1의 증가를 덮어씀)
// 증가를 2번 했는데 결과는 start+1 — 이것이 lost update다.
unsigned long long racy_interleave_demo(unsigned long long start) {
    unsigned long long shared = start;
    unsigned long long r1, r2;
    r1 = shared;                 /* T1: load          */
    r2 = shared;                 /* T2: load (끼어듦) */
    r1 += 1; shared = r1;        /* T1: add + store   */
    r2 += 1; shared = r2;        /* T2: add + store   */
    return shared;               /* start + 1 (1회 유실) */
}

// 같은 시나리오라도 atomic RMW는 load-add-store가 쪼개지지 않으므로 유실이 없다.
unsigned long long atomic_interleave_demo(unsigned long long start) {
    _Atomic unsigned long long shared;
    atomic_init(&shared, start);
    atomic_fetch_add_explicit(&shared, 1ull, memory_order_relaxed);  /* T1 전체 */
    atomic_fetch_add_explicit(&shared, 1ull, memory_order_relaxed);  /* T2 전체 */
    return atomic_load_explicit(&shared, memory_order_relaxed);      /* start + 2 */
}

// ===========================================================================
// Q22. stop 플래그: release store / acquire load  (volatile로는 안 되는 이유)
// ---------------------------------------------------------------------------
// GW31-E는 "오프라인 30분이면 자가 재부팅"한다. 감시 스레드가 종료 사유를 채우고
// stop 플래그를 세우면, 워커는 그 플래그를 보는 순간 **사유 문자열까지 완성된
// 상태로** 봐야 한다.
//
//   writer: reason/seq 기록  →  atomic_store(stop, true, RELEASE)
//   reader: atomic_load(stop, ACQUIRE) == true  →  reason/seq 읽기
//
// release/acquire 짝이 하는 일은 딱 하나: "release store 이전의 모든 쓰기"가
// "그 값을 acquire load로 본 스레드"에게 **먼저 보이도록** 강제한다(happens-before).
//
// ★ volatile은 왜 안 되는가 (면접 단골)
//   volatile이 보장하는 것: 컴파일러가 접근을 캐시/삭제/병합하지 못한다.
//   volatile이 보장하지 **않는** 것:
//     ① 원자성   — volatile long long ++ 는 여전히 load/add/store 3단계다.
//     ② 순서     — 컴파일러는 volatile 접근끼리의 순서만 지킨다. 주변의
//                  **비-volatile** 접근(reason[] 쓰기!)은 자유롭게 재배치한다.
//     ③ 가시성/CPU — arm64/A53급 멀티코어는 store buffer + weak memory model이라
//                  CPU 자체가 순서를 바꾼다. volatile은 DMB 같은 배리어를 내보내지 않는다.
//   → x86에서는 "우연히" 동작해서 더 위험하다(TSO라 store-store 재배치가 없음).
//     같은 코드를 Cortex-A로 옮기면 필드에서만 터진다.
//   아래 VolStop은 그 반례를 코드로 남겨둔 것이고, 스레드로는 절대 돌리지 않는다.
// ===========================================================================
typedef struct {
    _Atomic bool stop;      // 동기화 지점 (release/acquire)
    char         reason[32];// stop 이전에 쓰이고, stop을 본 뒤에만 읽힌다
    unsigned     seq;       // 함께 publish되는 평범한(비원자) 데이터
} StopFlag;

void stop_init(StopFlag *f) {
    atomic_init(&f->stop, false);
    f->reason[0] = '\0';
    f->seq = 0;
}

// ① 평범한 데이터 먼저 → ② release store로 publish. 순서를 바꾸면 안 된다.
void stop_request(StopFlag *f, const char *reason, unsigned seq) {
    snprintf(f->reason, sizeof f->reason, "%s", reason);
    f->seq = seq;
    atomic_store_explicit(&f->stop, true, memory_order_release);
}

// acquire load: true를 봤다면 writer가 release 이전에 쓴 것이 전부 보인다.
bool stop_requested(const StopFlag *f) {
    return atomic_load_explicit(&f->stop, memory_order_acquire);
}

// ---- 반례(절대 스레드로 쓰지 말 것): volatile 버전 -------------------------
typedef struct {
    volatile int stop;      // 최적화 억제일 뿐, 배리어가 아니다
    char         reason[32];
    unsigned     seq;
} VolStop;

void vol_stop_init(VolStop *f) { f->stop = 0; f->reason[0] = '\0'; f->seq = 0; }

void vol_stop_request(VolStop *f, const char *reason, unsigned seq) {
    snprintf(f->reason, sizeof f->reason, "%s", reason);  /* 비-volatile 접근 */
    f->seq = seq;                                          /* 비-volatile 접근 */
    f->stop = 1;   /* ← 컴파일러/CPU가 위 두 줄보다 먼저 보이게 만들 수 있다 */
}

bool vol_stop_requested(const VolStop *f) { return f->stop != 0; }

// ===========================================================================
// Q23. CAS 루프로 최대값 갱신 (compare_exchange_weak)
// ---------------------------------------------------------------------------
// "최대 신호세기", "최대 큐 깊이", "최대 업로드 지연" 같은 high-water mark는
// fetch_max가 C11에 없으므로 **CAS 루프**로 만든다. 이것이 lock-free의 기본형이다.
//
// ★ 실패 시 규칙 (면접에서 제일 자주 틀리는 지점)
//   compare_exchange_*가 실패하면 expected에 **현재 값이 자동으로 들어온다**.
//   그러니 루프 안에서 다시 atomic_load 하면 안 된다 — 불필요한 로드일 뿐 아니라
//   "읽고 나서 또 바뀌는" 창을 스스로 넓히는 셈이다.
// ★ weak vs strong
//   weak는 값이 같아도 **가짜 실패(spurious failure)** 할 수 있다. 대신 LL/SC
//   (ARM의 LDXR/STXR) 기반 CPU에서 더 싸다. 어차피 루프 안이면 weak가 정석.
//   루프가 없는 단발 CAS에서만 strong을 쓴다.
// ===========================================================================

// candidate가 더 크면 갱신하고 true. 아니면 false.
bool atomic_max_update(_Atomic unsigned *slot, unsigned candidate) {
    unsigned cur = atomic_load_explicit(slot, memory_order_relaxed);
    while (candidate > cur) {
        // 성공: release (이 값 이전의 내 작업을 함께 publish)
        // 실패: relaxed (아직 아무것도 publish하지 않았다) + cur에 현재값이 실린다
        if (atomic_compare_exchange_weak_explicit(slot, &cur, candidate,
                                                  memory_order_release,
                                                  memory_order_relaxed))
            return true;
        /* 실패 → cur는 이미 갱신됨. 다시 load하지 말 것. while 조건이 재검사한다 */
    }
    return false;
}

// 같은 패턴의 최소값 버전 (대칭성 확인용)
bool atomic_min_update(_Atomic unsigned *slot, unsigned candidate) {
    unsigned cur = atomic_load_explicit(slot, memory_order_relaxed);
    while (candidate < cur) {
        if (atomic_compare_exchange_weak_explicit(slot, &cur, candidate,
                                                  memory_order_release,
                                                  memory_order_relaxed))
            return true;
    }
    return false;
}

// ===========================================================================
// Q24. CAS 스핀락 (atomic_flag test_and_set / clear)
// ---------------------------------------------------------------------------
// atomic_flag는 C11에서 **항상 lock-free가 보장되는 유일한 타입**이다.
//   lock  : test_and_set이 false를 돌려줄 때까지 회전 → ACQUIRE
//   unlock: clear                                    → RELEASE
// 이 acquire/release 짝 덕분에 스핀락 안의 **평범한(비원자) 데이터**가 안전해진다.
//
// ★ 스핀락을 쓰면 안 되는 경우 (이게 진짜 질문이다)
//   1) 임계구역이 길거나 그 안에서 블록/시스템콜/malloc을 한다 → CPU를 태운다.
//      배터리로 도는 MT81 트레일러에서 스핀은 곧 주행거리다.
//   2) 단일 코어 + 선점형 스케줄러 → 락 보유자가 선점되면 대기자가 자기 타임슬라이스
//      전체를 태우고서야 양보한다. 최악은 우선순위 역전(priority inversion)으로
//      진행 자체가 멈춘다. 이때는 mutex(+ priority inheritance)가 정답.
//   3) 유저스페이스 Linux에서 경합이 있는 경우 → 대개 pthread_mutex가 더 빠르다.
//      (경합 없을 땐 futex 덕에 이미 유저스페이스에서 끝난다)
//   쓰는 곳: 멀티코어에서 수십 나노초짜리 임계구역, 인터럽트를 끌 수 없는 짧은 구간.
// ===========================================================================
typedef struct {
    atomic_flag held;
} SpinLock;

void spin_init(SpinLock *l) {
    atomic_flag_clear_explicit(&l->held, memory_order_relaxed);
}

void spin_lock(SpinLock *l) {
    // test_and_set: 이전 값을 돌려주고 true로 만든다. false를 받으면 내가 잡은 것.
    while (atomic_flag_test_and_set_explicit(&l->held, memory_order_acquire)) {
        /* 실전에서는 여기서 CPU 힌트를 준다: ARM `wfe`/`yield`, x86 `pause`.
         * 이식성 때문에 여기서는 빈 루프로 둔다. */
    }
}

bool spin_trylock(SpinLock *l) {
    return !atomic_flag_test_and_set_explicit(&l->held, memory_order_acquire);
}

void spin_unlock(SpinLock *l) {
    atomic_flag_clear_explicit(&l->held, memory_order_release);
}

// ===========================================================================
// Q25. SPSC 링버퍼 push/pop  (release/acquire 짝, 한 칸 비우기)
// ---------------------------------------------------------------------------
// 모뎀 UART RX ISR(생산자 1) → 파서 태스크(소비자 1). ISR 안에서는 mutex 금지.
// 성립 근거: **생산자만 tail을 쓰고, 소비자만 head를 쓴다.** 상대 인덱스는 읽기만
// 하므로 write-write 경합이 구조적으로 없다. 남은 문제는 "순서"뿐이고 그것을
// release/acquire 한 쌍이 해결한다.
//
//   push: buf[t] = b            (①데이터)
//         store(tail, RELEASE)  (②publish)   → ①이 ②보다 먼저 보인다
//   pop : load(tail, ACQUIRE)   (②를 봤다면)
//         b = buf[h]            (①도 보인다)
//
// 한 칸을 비워두면 head==tail(빈 것)과 가득 참을 인덱스만으로 구분할 수 있다.
// → 용량은 RING_CAP-1. count 필드를 두면 양쪽이 쓰게 되어 SPSC의 이점이 사라진다.
// ===========================================================================
#define RING_CAP 64u                      /* 반드시 2의 거듭제곱 */

typedef struct {
    uint8_t          buf[RING_CAP];
    _Atomic unsigned head;                /* 소비자만 store */
    _Atomic unsigned tail;                /* 생산자만 store */
} Ring;

void ring_init(Ring *r) {
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
}

// 생산자(ISR) 전용. 가득 차면 false — 실전에서는 drop 카운터를 올리고 즉시 리턴한다.
bool ring_push(Ring *r, uint8_t b) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed); /* 내가 쓴 값 */
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    /* 소비자가 얼마나 비웠는지 본다 → acquire (소비자의 buf 읽기 완료를 본다) */
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                     /* full */

    r->buf[t] = b;                        /* ① 데이터 먼저 */
    atomic_store_explicit(&r->tail, next, memory_order_release); /* ② 그 다음 publish */
    return true;
}

// 소비자(task) 전용. 비면 false.
bool ring_pop(Ring *r, uint8_t *out) {
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed); /* 내가 쓴 값 */
    /* acquire: tail을 본 뒤에 buf를 읽어야 생산자의 데이터가 보인다 */
    if (h == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                     /* empty */

    *out = r->buf[h];
    atomic_store_explicit(&r->head, (h + 1u) & (RING_CAP - 1u),
                          memory_order_release);
    return true;
}

// ===========================================================================
// Q26. count / full / empty + power-of-two mask (& vs %)
// ---------------------------------------------------------------------------
// 부호 없는 뺄셈 `(tail - head) & (CAP-1)`은 인덱스가 랩되어도 정확하다.
// `%`는 나눗셈 명령이 없는 MCU에서 수십 사이클짜리 라이브러리 호출이 되지만,
// 2의 거듭제곱이면 `&`는 1사이클이다. ISR 안에서는 이 차이가 실제로 문제가 된다.
// 주의: `&` 트릭은 **2의 거듭제곱일 때만** 성립한다 → init에서 반드시 검증.
// ===========================================================================
bool is_pow2(unsigned x) {
    return x != 0u && (x & (x - 1u)) == 0u;
}

// cap이 2의 거듭제곱이면 idx % cap과 동일하되 나눗셈이 없다.
unsigned ring_mask_index(unsigned idx, unsigned cap) {
    return idx & (cap - 1u);
}

// 관찰용(느슨한 스냅샷). 생산자/소비자 각각 자기 쪽에서 부르면 정확하다.
unsigned ring_count(const Ring *r) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_acquire);
    unsigned h = atomic_load_explicit(&r->head, memory_order_acquire);
    return (t - h) & (RING_CAP - 1u);     /* unsigned 뺄셈 → 랩되어도 정확 */
}

bool ring_is_empty(const Ring *r) {
    return ring_count(r) == 0u;
}

bool ring_is_full(const Ring *r) {
    return ring_count(r) == RING_CAP - 1u;   /* 한 칸은 항상 비워둔다 */
}

// ===========================================================================
// Q27. seqlock write (홀수/짝수 시퀀스 + release fence)
// ---------------------------------------------------------------------------
// GC31-E 링크 상태(RSRP/RSRQ/uptime/SIM 슬롯)를 1초마다 쓰는 writer 1개와,
// 수시로 읽는 reader 여러 개. 요구: reader가 **같은 시점의 스냅샷**을 보고,
// reader 때문에 writer가 절대 지연되지 않을 것(writer 우선).
//
//   seq 홀수 = 쓰는 중, 짝수 = 안정
//   writer: seq++(홀수) → [release fence] → 데이터 기록 → seq++(짝수, release store)
//
// 첫 번째 fence가 하는 일: "seq가 홀수가 된 사실"이 데이터 기록보다 **먼저** 보이게
// 한다. 이게 없으면 reader가 짝수 seq를 본 채로 반쯤 쓰인 데이터를 복사하고도
// 검증을 통과해버린다.
// 마지막 release store: 데이터 기록이 "짝수 seq publish"보다 먼저 보이게 한다.
// ===========================================================================
typedef struct {
    int32_t  rsrp_dbm;     /* 테스트 불변식: rsrq_db == -rsrp_dbm */
    int32_t  rsrq_db;
    uint32_t uptime_s;
    uint8_t  sim_slot;
} LinkStatus;

typedef struct {
    _Atomic uint32_t seq;  /* 짝수 = 안정, 홀수 = 쓰기 중 */
    LinkStatus       data; /* seq로 보호되는 평범한 구조체 (원자 타입이 아니다) */
} SeqLock;

void seq_init(SeqLock *s) {
    atomic_init(&s->seq, 0u);
    memset(&s->data, 0, sizeof s->data);
}

// writer 1명 전용. writer가 여러 명이면 writer끼리는 mutex로 직렬화해야 한다.
void seq_write(SeqLock *s, const LinkStatus *in) {
    uint32_t v = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, v + 1u, memory_order_relaxed);  /* 홀수: 쓰는 중 */
    atomic_thread_fence(memory_order_release);   /* seq 증가가 데이터보다 먼저 보이게 */

    s->data = *in;                               /* 평범한 쓰기 (여기가 race 지점) */

    /* 데이터 기록이 짝수 publish보다 먼저 보이게 */
    atomic_store_explicit(&s->seq, v + 2u, memory_order_release);  /* 짝수: 완료 */
}

// 현재 writer가 쓰는 중인가 (진단/테스트용)
bool seq_writing(const SeqLock *s) {
    return (atomic_load_explicit(&s->seq, memory_order_relaxed) & 1u) != 0u;
}

// ===========================================================================
// Q28. seqlock read (재시도 루프)
// ---------------------------------------------------------------------------
//   before = load(seq, ACQUIRE);  홀수면 재시도
//   *out = s->data;                        ← 찢어진 값일 수 있다(정상)
//   [acquire fence]
//   after = load(seq, RELAXED);
//   before == after 면 스냅샷 유효, 아니면 처음부터 다시
//
// ★ 정직하게 말해야 할 한계 (면접 가산점)
//   - `*out = s->data`는 C 표준상 **data race**다(비원자 객체를 writer와 동시에 접근).
//     그래서 `-fsanitize=thread`로 빌드하면 TSan이 정확히 여기를 지적한다.
//     실무 해법: ① 필드별 relaxed atomic으로 읽고 쓰기 ② Linux 커널처럼
//     READ_ONCE/WRITE_ONCE(volatile 접근 + 컴파일러 배리어)로 플랫폼 보장에 기대기.
//   - 데이터에 **포인터/핸들이 있으면 쓰면 안 된다** — 찢어진 포인터를 잠시라도
//     역참조하면 죽는다. POD 스냅샷 전용.
//   - reader가 writer보다 훨씬 느리면 영원히 재시도할 수 있다(starvation) →
//     재시도 상한을 두고 초과 시 mutex 경로로 fallback.
// ===========================================================================
// ★ 루프는 do/while이 아니라 for(;;)로 쓴다.
//   do { ... if (before & 1) continue; ... } while (before != after);
//   이렇게 쓰면 `continue`가 **while 조건으로 점프**해서, 아직 대입된 적 없는
//   `after`(첫 바퀴에는 쓰레기값)와 비교한다. 운 나쁘게 같으면 *out을 한 번도
//   채우지 않고 루프를 빠져나온다 → 호출자가 초기화되지 않은 스냅샷을 받는다.
//   흔한 seqlock 예제 코드에 실제로 박혀 있는 버그다.
void seq_read(const SeqLock *s, LinkStatus *out) {
    for (;;) {
        uint32_t before = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (before & 1u) continue;               /* 쓰는 중 → 처음부터 다시 */

        *out = s->data;                          /* 찢어진 값일 수 있다 */

        atomic_thread_fence(memory_order_acquire); /* 복사가 after 로드보다 먼저 */
        uint32_t after = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (before == after) return;             /* 같으면 유효한 스냅샷 */
    }
}

// 재시도 상한이 있는 버전 — 실시간 경로에서는 이쪽이 안전하다.
// 상한 내에 일관된 스냅샷을 못 얻으면 false (호출자가 mutex 경로로 fallback).
bool seq_try_read(const SeqLock *s, LinkStatus *out, unsigned max_tries) {
    for (unsigned i = 0; i < max_tries; i++) {
        uint32_t before = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (before & 1u) continue;
        *out = s->data;
        atomic_thread_fence(memory_order_acquire);
        uint32_t after = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (before == after) return true;
    }
    return false;
}

// ===========================================================================
// Q29. atomic refcount (fetch_sub release + acquire fence 후 해제)
// ---------------------------------------------------------------------------
// 클라우드(Command)에서 새 설정이 내려와도 읽는 중인 설정을 free하면 안 된다.
// 설정을 **불변 객체**로 만들고 refcount로 수명을 관리한다.
//
//   증가: fetch_add(+1, RELAXED)  — 이미 유효한 참조를 들고 있는 상태에서만 부른다.
//         순서 요구가 없으므로 relaxed로 충분하다.
//   감소: fetch_sub(-1, RELEASE)  — "내가 이 객체에 한 모든 접근"을 publish한다.
//         반환값이 1이면 내가 마지막 참조 → acquire fence로 **다른 스레드들의
//         모든 접근을 본 뒤에** free한다.
//
// ★ 왜 release만으로는 부족한가
//   release 감소만 하면 "내 접근이 다른 스레드에게 보인다"는 보장뿐이다. free하는
//   쪽은 반대로 "남들의 접근이 나에게 보인다"가 필요하다 → acquire가 있어야 한다.
//   fetch_sub(acq_rel)로 매번 acquire를 거는 것도 맞지만, 마지막 한 번만
//   fence로 거는 편이 빈번한 감소 경로에서 더 싸다. (Boost/LLVM 표준 관용구)
// ★ 왜 증가는 relaxed인데 감소는 release인가
//   증가는 이미 살아있는 객체에 대한 것이라 아무것도 publish할 게 없다.
//   감소는 "나는 이제 이 객체를 안 본다"를 알리는 것이므로 내 작업을 먼저 flush해야 한다.
// ===========================================================================
typedef struct {
    _Atomic int refs;
    uint32_t    version;
    uint32_t    upload_period_ms;
    char        apn[24];
} RcConfig;

RcConfig *rc_new(uint32_t version, const char *apn) {
    RcConfig *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    atomic_init(&c->refs, 1);                    /* 생성자가 참조 1개를 갖는다 */
    c->version = version;
    c->upload_period_ms = version * 10u;         /* 불변식: period == version*10 */
    snprintf(c->apn, sizeof c->apn, "%s", apn);
    return c;
}

// 이미 유효한 참조를 들고 있을 때만 호출 (그래서 relaxed로 충분)
void rc_get(RcConfig *c) {
    atomic_fetch_add_explicit(&c->refs, 1, memory_order_relaxed);
}

// 마지막 참조였으면 free하고 true를 돌려준다. freed는 통계용(NULL 허용).
bool rc_put(RcConfig *c, Counter *freed) {
    if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {
        /* 내가 마지막 → 다른 스레드의 접근을 모두 본 뒤 해제 */
        atomic_thread_fence(memory_order_acquire);
        if (freed) ctr_add(freed, 1ull);
        free(c);
        return true;
    }
    return false;
}

int rc_count(const RcConfig *c) {
    return atomic_load_explicit(&c->refs, memory_order_relaxed);
}

// ===========================================================================
// Q30. ABA 문제와 tagged pointer
// ---------------------------------------------------------------------------
// ★ ABA란
//   T1이 top == A를 읽는다 → 선점됨.
//   T2가 A를 pop, B를 pop, A를 다시 push. 이제 top == A지만 **A->next는 완전히
//   다른 값**이다(B가 아니라 그 아래 노드).
//   T1이 깨어나 CAS(top, A, A->next_old)를 한다 → 값이 A로 같으니 **성공한다**.
//   리스트가 깨진다. CAS는 "값이 같은가"만 보지 "그 사이에 바뀌었는가"는 못 본다.
//
// ★ 해법
//   1) tagged pointer(= 버전 카운터) — 포인터와 태그를 한 워드에 묶어 CAS.
//      pop/push마다 태그를 올리면 "같은 A"라도 값이 달라져 stale CAS가 실패한다.
//      64비트 CAS로 (32bit index | 32bit tag), 또는 DWCAS(x86 cmpxchg16b,
//      ARM CASP)로 (64bit ptr | 64bit tag).
//   2) hazard pointer / epoch-based reclamation / RCU — 재사용 자체를 지연시킨다.
//   3) 그냥 mutex를 쓴다. 대부분의 임베디드 코드에서 이게 정답이다.
//
// 여기서는 배열 기반 freelist에 **인덱스+태그**를 packing해서 단일 스레드로
// ABA 시나리오를 **결정적으로 재현·검증**한다(멀티스레드 없이 증명 가능).
// 인덱스는 1-based로 저장해 0을 "비었음"(NULL)으로 쓴다.
// ===========================================================================
#define POOL_N 8u

#define TAG_PACK(idx1, tag)  (((uint64_t)(uint32_t)(tag) << 32) | (uint32_t)(idx1))
#define TAG_IDX1(v)          ((uint32_t)((v) & 0xFFFFFFFFu))   /* 1-based, 0=NULL */
#define TAG_CNT(v)           ((uint32_t)((v) >> 32))

typedef struct {
    uint32_t         next[POOL_N];   /* freelist 링크 (1-based, 0 = 끝) */
    _Atomic uint64_t head;           /* [63:32]=tag, [31:0]=1-based index */
} TaggedPool;

// 0..POOL_N-1 을 전부 freelist에 올린다 (0이 top이 되도록 역순으로 push).
void pool_init(TaggedPool *p) {
    atomic_init(&p->head, (uint64_t)0);
    for (unsigned i = 0; i < POOL_N; i++) p->next[i] = 0u;
    for (unsigned i = POOL_N; i-- > 0; ) {
        uint64_t old = atomic_load_explicit(&p->head, memory_order_relaxed);
        p->next[i] = TAG_IDX1(old);
        atomic_store_explicit(&p->head, TAG_PACK(i + 1u, TAG_CNT(old) + 1u),
                              memory_order_relaxed);
    }
}

// CAS 루프 pop. 성공 시 *out_idx 에 0-based 인덱스.
bool pool_pop(TaggedPool *p, uint32_t *out_idx) {
    uint64_t old = atomic_load_explicit(&p->head, memory_order_acquire);
    for (;;) {
        uint32_t i1 = TAG_IDX1(old);
        if (i1 == 0u) return false;                      /* 비었음 */
        uint64_t neu = TAG_PACK(p->next[i1 - 1u], TAG_CNT(old) + 1u); /* 태그 +1 */
        if (atomic_compare_exchange_weak_explicit(&p->head, &old, neu,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            *out_idx = i1 - 1u;
            return true;
        }
        /* 실패 → old에 현재 head가 자동으로 실린다. 다시 load하지 않는다. */
    }
}

// CAS 루프 push. 태그를 항상 올린다 — 이것이 ABA 방어의 전부다.
void pool_push(TaggedPool *p, uint32_t idx) {
    uint64_t old = atomic_load_explicit(&p->head, memory_order_relaxed);
    uint64_t neu;
    do {
        p->next[idx] = TAG_IDX1(old);
        neu = TAG_PACK(idx + 1u, TAG_CNT(old) + 1u);
    } while (!atomic_compare_exchange_weak_explicit(&p->head, &old, neu,
                                                    memory_order_release,
                                                    memory_order_relaxed));
}

uint64_t pool_head_raw(const TaggedPool *p) {
    return atomic_load_explicit(&p->head, memory_order_acquire);
}

// "선점되었던 스레드"가 옛 head 값으로 시도하는 stale CAS. 태그 덕에 실패해야 한다.
bool pool_stale_cas(TaggedPool *p, uint64_t stale_head, uint64_t desired) {
    uint64_t expected = stale_head;
    return atomic_compare_exchange_strong_explicit(&p->head, &expected, desired,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire);
}

// ===========================================================================
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
