/* CPP-A07 정적 할당 액티브 오브젝트 프레임워크
 *
 * QP 스타일 이벤트 드리븐 구조를 힙 없이 만든다 (QP 코드는 쓰지 않음 — 아이디어만, 구현은 직접).
 *   이벤트 풀(참조 카운트) + AO별 고정 큐 + 평면 상태 머신(멤버 함수 포인터 핸들러, entry/exit)
 *   + 우선순위 협조 스케줄러 + 틱 기반 타임 이벤트 + "ISR" 에서 post (호스트 임계 구역 모델)
 *   Part A: 풀 alloc/release, RTC 디스패치(entry/exit), publish/gc, 스케줄러
 *   Part B: 넘침 정책(post = 보장 → fault, try_post = 버림 + 카운터), ISR 선점, 타임 이벤트 disarm
 *   Part C: 버튼(디바운스, 낡은 타임아웃 거르기) → LED 블링크. 가상 시간 트레이스로 검증, RAM 계산, new 0회
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 *   -DAO_NO_HARNESS : 프레임워크 + 시나리오 AO 만 (demo_target.cpp 가 Cortex-M 용으로 이 파일을 include)
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#ifndef AO_NO_HARNESS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

/* ================= 포트: 임계 구역 =================
 * Cortex-M: PRIMASK 저장 → cpsid i → ... → 복원 (CPP-B06). 호스트: PRIMASK 는 전역 변수, 인터럽트는
 * "pending ISR 1개" (CPP-A04 와 같은 모델). 마스크가 풀리는 순간 pending ISR 이 실행된다. */
#if defined(__ARM_ARCH_PROFILE) && __ARM_ARCH_PROFILE == 'M'
static inline uint32_t irq_save() {
    uint32_t pm;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) : : "memory");
    return pm;
}
static inline void irq_restore(uint32_t pm) { __asm volatile("msr primask, %0" : : "r"(pm) : "memory"); }
static inline void isr_window() {}
#else
static uint32_t g_primask = 0;
static int g_isr_runs = 0;
static void (*g_pending_isr)() = nullptr;
static void (*g_armed_isr)() = nullptr;
static void dispatch_pending() {
    if (g_pending_isr != nullptr && (g_primask & 1u) == 0) {
        void (*isr)() = g_pending_isr;
        g_pending_isr = nullptr;
        ++g_isr_runs;
        isr();
    }
}
static uint32_t irq_save() { const uint32_t pm = g_primask; g_primask = 1; return pm; }
static void irq_restore(uint32_t pm) { g_primask = pm; dispatch_pending(); }
/* "여기서 인터럽트가 들어올 수 있다" 지점 (칩에서는 빈 함수). arm_preempt() 로 장전한 ISR 을 한 번 요청 */
[[maybe_unused]] static void arm_preempt(void (*isr)()) { g_armed_isr = isr; }
static void isr_window() {
    if (g_armed_isr != nullptr) {
        g_pending_isr = g_armed_isr;
        g_armed_isr = nullptr;
        dispatch_pending();                  // 마스크 중이면 pending 으로 남는다 (NVIC 처럼)
    }
}
#endif

class CritSec {                              // 저장/복원형 — 중첩돼도 바깥 마스크를 풀지 않는다
public:
    CritSec() : pm_(irq_save()) {}
    ~CritSec() { irq_restore(pm_); }
    CritSec(const CritSec&) = delete;
    CritSec& operator=(const CritSec&) = delete;
private:
    uint32_t pm_;
};

/* ================= fault 훅 · 가상 시간 · 트레이스 ================= */
enum : uint16_t { FAULT_QUEUE_FULL = 1, FAULT_TRAN_IN_ENTRY = 2, FAULT_BAD_PRIO = 3, FAULT_POOL_EMPTY = 4, FAULT_BAD_ARG = 5 };
struct FaultLog { uint32_t count; uint16_t last; };
static FaultLog g_fault{};
/* 설계 버그(보장 post 넘침, entry 에서 전이 ...) 는 여기로. 칩: 로그 + BKPT/리셋. 호스트: 세고 계속 (테스트가 확인) */
static void on_fault(uint16_t code) { ++g_fault.count; g_fault.last = code; }

static uint32_t g_now = 0;                   // 가상 시간 [ms] = 틱 수 (칩: SysTick 1 kHz)

#ifndef AO_TRACE_CAP
#define AO_TRACE_CAP 512
#endif
enum class Tk : uint8_t { Dispatch, Ignored, Led, Pub };
struct TraceRec { uint32_t t; Tk kind; uint8_t a; uint16_t b; };
static TraceRec g_trace[AO_TRACE_CAP > 0 ? AO_TRACE_CAP : 1];
static uint16_t g_trace_n = 0;
static uint16_t trace(Tk k, uint8_t a, uint16_t b) {      // QS 같은 이진 기록 (문자열은 호스트에서만 만든다)
    if (AO_TRACE_CAP > 0 && g_trace_n < AO_TRACE_CAP) { g_trace[g_trace_n] = {g_now, k, a, b}; return g_trace_n++; }
    return 0xFFFF;
}

/* ================= 이벤트 ================= */
using Signal = uint16_t;
enum : Signal {
    SIG_ENTRY = 1, SIG_EXIT, SIG_USER,
    SIG_BTN_EDGE = SIG_USER, SIG_DEBOUNCE_TO, SIG_PRESSED, SIG_RELEASED, SIG_BLINK_TO,
    SIG_A, SIG_B, SIG_C, SIG_TO,              // Part A/B 시험용
    SIG_MAX
};
struct Event {
    Signal sig;
    uint8_t pool;                             // 0 = 정적 이벤트 (재활용 안 함), 1 = 이벤트 풀
    mutable uint8_t ref;                      // 이 이벤트를 아직 들고 있는 큐 수 (+ publish 중 임시 1)
};
inline constexpr Event kEntryEvt{SIG_ENTRY, 0, 0};
inline constexpr Event kExitEvt{SIG_EXIT, 0, 0};
struct EdgeEvt : Event { uint8_t level; };                  // EXTI ISR → 버튼
struct PressEvt : Event { uint16_t seq; uint32_t t; };      // 버튼 → 구독자들 (publish)

inline constexpr std::size_t kBlock = 16;   // 풀 블록 크기 (가장 큰 이벤트가 들어가야 한다)
template <class T>
concept PoolEvent = std::is_base_of_v<Event, T> && std::is_trivially_destructible_v<T> &&
                    sizeof(T) <= kBlock && alignof(T) <= alignof(std::max_align_t);

/* ================= Part A — 고정 블록 이벤트 풀 (DS-M02 의 free list, 여기선 ISR 안전) ================= */
class EventPool {
public:
    constexpr EventPool() = default;
    EventPool(const EventPool&) = delete;
    EventPool& operator=(const EventPool&) = delete;
    void init(void* storage, uint16_t block_size, uint16_t n_blocks) {
        head_ = nullptr; block_ = block_size; total_ = n_blocks; free_ = 0; fail_ = 0;
        auto* base = static_cast<std::byte*>(storage);
        for (uint16_t i = n_blocks; i-- > 0;) release(base + std::size_t{i} * block_size);   // 0번 블록이 맨 앞
        min_ = free_;
    }
    /* free list 에서 한 블록. 비었으면 nullptr + n_fail++ (정책은 호출자: ISR 은 버림, 태스크는 fault) */
    void* alloc() {
        /* TODO(A1): CritSec 안에서 free list 머리 블록을 떼어 돌려준다. 비었으면 ++fail_ 후 nullptr.
         *           free_ 를 줄이고 최저 수위 min_ 갱신 */
        return nullptr;
    }
    void release(void* blk) {
        /* TODO(A1): CritSec 안에서 blk 자리에 FreeBlk{head_} 를 placement new 로 만들고 머리로. ++free_ */
        (void)blk;
    }
    uint16_t n_free() const { return free_; }
    uint16_t n_min() const { return min_; }
    uint16_t n_total() const { return total_; }
    uint32_t n_fail() const { return fail_; }
    uint16_t block_size() const { return block_; }
private:
    struct FreeBlk { FreeBlk* next; };
    FreeBlk* head_ = nullptr;
    uint16_t block_ = 0, total_ = 0, free_ = 0, min_ = 0;
    uint32_t fail_ = 0;
};
template <std::size_t BlockSize, std::size_t N>
class StaticPool : public EventPool {
    static_assert(BlockSize >= sizeof(void*) && BlockSize % alignof(void*) == 0, "block must hold the free-list link");
public:
    void init() { EventPool::init(buf_, BlockSize, N); }
private:
    alignas(std::max_align_t) std::byte buf_[BlockSize * N]{};
};

/* ================= 이벤트 큐 (DS-B03 링 버퍼 — 주어짐) =================
 * get() 의 n_ 읽기~쓰기 사이에 isr_window(): 컴파일된 "ldr n; ...; str n-1" 사이에 ISR 이 오는 상황. */
class EventQueue {
public:
    constexpr EventQueue(const Event** buf, uint16_t cap) : buf_(buf), cap_(cap) {}
    bool put(const Event* e) {
        if (n_ == cap_) return false;
        buf_[head_] = e;
        head_ = static_cast<uint16_t>(head_ + 1 == cap_ ? 0 : head_ + 1);
        ++n_;
        if (n_ > max_) max_ = n_;
        return true;
    }
    const Event* get() {
        const uint16_t n = n_;
        if (n == 0) return nullptr;
        const Event* e = buf_[tail_];
        tail_ = static_cast<uint16_t>(tail_ + 1 == cap_ ? 0 : tail_ + 1);
        isr_window();
        n_ = static_cast<uint16_t>(n - 1);
        return e;
    }
    bool empty() const { return n_ == 0; }
    uint16_t size() const { return n_; }
    uint16_t capacity() const { return cap_; }
    uint16_t max_used() const { return max_; }
private:
    const Event** buf_;
    uint16_t cap_, head_ = 0, tail_ = 0, n_ = 0, max_ = 0;
};

/* ================= 액티브 오브젝트 ================= */
enum class Status : uint8_t { Handled, Ignored, Tran };
class Framework;

class ActiveObject {                         // 템플릿 아닌 코어: 큐 + 우선순위 + 디스패치 썽크 (가상 함수 없음)
public:
    using Thunk = Status (*)(ActiveObject&, const Event&);
    constexpr ActiveObject(uint8_t prio, const Event** qbuf, uint16_t qcap, Thunk dispatch, Thunk start)
        : prio_(prio), q_(qbuf, qcap), dispatch_(dispatch), start_(start) {}
    ActiveObject(const ActiveObject&) = delete;             // 큐 버퍼 포인터가 자기 멤버를 가리킨다 → 복사 금지
    ActiveObject& operator=(const ActiveObject&) = delete;
    uint8_t prio() const { return prio_; }
    const EventQueue& queue() const { return q_; }
    uint32_t n_drop() const { return drop_; }
    Status dispatch(const Event& e) { return dispatch_(*this, e); }   // 썽크 → Active<D,N>::dispatch (간접 호출 1번)
private:
    friend class Framework;
    uint8_t prio_;
    EventQueue q_;
    Thunk dispatch_, start_;
    uint32_t drop_ = 0;
};

template <class D, uint16_t QN>
class Active : public ActiveObject {
public:
    using State = Status (D::*)(const Event&);
    constexpr Active(uint8_t prio, State initial)
        : ActiveObject(prio, qbuf_, QN, &dispatch_thunk, &start_thunk), state_(initial) {}
    bool in_state(State s) const { return state_ == s; }
protected:
    Status tran(State target) { next_ = target; return Status::Tran; }
private:
    static Status start_thunk(ActiveObject& ao, const Event&) {
        Active& a = static_cast<Active&>(ao);
        D& self = static_cast<D&>(a);
        if ((self.*a.state_)(kEntryEvt) == Status::Tran) on_fault(FAULT_TRAN_IN_ENTRY);
        return Status::Handled;
    }
    static Status dispatch_thunk(ActiveObject& ao, const Event& e) {
        const uint16_t idx = trace(Tk::Dispatch, ao.prio(), e.sig);
        const Status s = static_cast<Active&>(ao).dispatch(e);
        if (s == Status::Ignored && idx != 0xFFFF) g_trace[idx].kind = Tk::Ignored;
        return s;
    }
    /* RTC 한 걸음: 현재 상태 핸들러 → Tran 이면 exit(현재) → 상태 교체 → entry(새 상태).
     * 자기 전이(target == 현재)도 exit/entry 를 한다 (UML external self-transition). entry 안의 전이는 fault. */
    Status dispatch(const Event& e) {
        /* TODO(A2): D& self = static_cast<D&>(*this); r = (self.*state_)(e).
         *           r == Tran 이면 exit(현재) → state_ = next_ → entry(새). entry 가 Tran 을 돌려주면 on_fault(FAULT_TRAN_IN_ENTRY) */
        (void)e;
        return Status::Ignored;
    }
    State state_;
    State next_ = nullptr;
    const Event* qbuf_[QN] = {};
};

/* ================= 타임 이벤트 (정적 이벤트: AO 의 멤버, 풀에서 오지 않음) ================= */
struct TimeEvent : Event {
    constexpr TimeEvent(Signal s, ActiveObject& target) : Event{s, 0, 0}, target_(&target) {}
    TimeEvent(const TimeEvent&) = delete;
    TimeEvent& operator=(const TimeEvent&) = delete;
    ActiveObject* target_;
    TimeEvent* next_ = nullptr;               // 무장된 타임 이벤트 목록 (침투형 단일 연결)
    uint32_t ctr_ = 0;                        // 0 = 무장 안 됨 (목록에 없음) — 불변식
    uint32_t interval_ = 0;                   // 0 = 한 번, 아니면 주기
};

/* ================= 프레임워크: post / publish / gc / 스케줄러 / 틱 ================= */
class Framework {
public:
    static constexpr uint8_t kMaxPrio = 8;
    constexpr Framework() = default;
    Framework(const Framework&) = delete;
    Framework& operator=(const Framework&) = delete;

    void init(EventPool& pool) { pool_ = &pool; }
    void start(ActiveObject& ao) {
        if (ao.prio_ == 0 || ao.prio_ > kMaxPrio || aos_[ao.prio_] != nullptr) { on_fault(FAULT_BAD_PRIO); return; }
        aos_[ao.prio_] = &ao;
        (void)ao.start_(ao, kEntryEvt);       // 초기 상태의 entry (여기서 post 한 것은 큐로)
    }
    template <PoolEvent T>
    T* new_evt(Signal sig) {
        void* blk = pool_->alloc();
        if (blk == nullptr) return nullptr;
        T* e = ::new (blk) T{};
        e->sig = sig; e->pool = 1; e->ref = 0;
        return e;
    }
    /* post: 넘침 = 설계 버그 → fault. try_post: 넘침 = 예상된 일 → 버림 + n_drop. 실패하면 이벤트는 재활용됨 */
    bool post(ActiveObject& ao, const Event* e) { return post_impl(ao, e, false); }
    bool try_post(ActiveObject& ao, const Event* e) { return post_impl(ao, e, true); }
    void subscribe(const ActiveObject& ao, Signal sig) { subs_[sig] |= 1u << ao.prio_; }
    void unsubscribe(const ActiveObject& ao, Signal sig) { subs_[sig] &= ~(1u << ao.prio_); }

    /* 구독자 전원에게 같은 이벤트 포인터를 (zero-copy). 높은 우선순위부터. 구독자 0명이어도 누수 없음 */
    void publish(const Event* e) {
        trace(Tk::Pub, 0, e->sig);
        /* TODO(A3): 풀 이벤트면 임시 참조 +1 (CritSec) → subs_[e->sig] 비트를 높은 우선순위부터 post_impl(..., false)
         *           → 마지막에 gc(e) 로 임시 참조 반납. 구독자 0명이어도 누수가 없어야 한다 */
    }
    /* 참조 하나 반납. 마지막이면 풀로. 정적 이벤트(pool == 0)는 무시 */
    void gc(const Event* e) {
        /* TODO(A3): 정적 이벤트(pool == 0)는 무시. CritSec 안에서 ref > 1 이면 --ref, 아니면 ref = 0 후 pool_->release */
        (void)e;
    }
    /* 준비된 AO 중 가장 높은 우선순위에서 이벤트 1개를 꺼내 끝까지 처리(RTC). 없으면 false (= idle) */
    bool run_one() {
        /* TODO(A4): CritSec 안에서 — ready_ 가 0 이면 false. 가장 높은 비트(31 - countl_zero) 의 AO 큐에서 get,
         *           큐가 비면 ready 비트를 끈다. CritSec 밖에서 ao->dispatch(*e) → gc(e) → ++n_dispatched_ → true */
        return false;
    }

    void arm(TimeEvent& t, uint32_t ticks, uint32_t interval = 0) {
        CritSec cs;
        if (ticks == 0) { on_fault(FAULT_BAD_ARG); return; }
        if (t.ctr_ == 0) { t.next_ = armed_; armed_ = &t; }   // 목록에 없을 때만 넣는다
        t.ctr_ = ticks;
        t.interval_ = interval;
    }
    /* true = 만료 전에 멈췄다. false = 무장 안 됨 또는 이미 만료(그 타임아웃이 큐에 있을 수 있다!) */
    bool disarm(TimeEvent& t) {
        /* TODO(B2): CritSec. ctr_ == 0 이면 false. 아니면 armed_ 목록에서 t 를 떼고 next_ = nullptr, ctr_ = 0, true */
        (void)t;
        return false;
    }
    bool is_armed(const TimeEvent& t) const { return t.ctr_ != 0; }
    /* SysTick ISR 에서 1틱마다. 만료된 타임 이벤트를 대상 AO 에 post(보장). 한 번짜리는 목록에서 뺀다 */
    void tick() {
        /* TODO(B2): CritSec. 목록을 돌며 --ctr_. 0 이 되면 post_impl(*target_, t, false).
         *           interval_ != 0 이면 ctr_ = interval_ (목록에 남김), 아니면 목록에서 뗀다 (ctr_ == 0 ⇔ 목록에 없음) */
    }

    uint32_t ready() const { return ready_; }
    uint32_t n_dispatched() const { return n_dispatched_; }
    const EventPool& pool() const { return *pool_; }

private:
    bool post_impl(ActiveObject& ao, const Event* e, bool droppable) {
        /* TODO(B1): CritSec. 큐에 넣는 데 성공하면 풀 이벤트의 ref +1, ready_ 비트 켜고 true.
         *           실패: droppable 이면 ++ao.drop_, 아니면 on_fault(FAULT_QUEUE_FULL).
         *           그리고 아무도 안 들고 있는(ref == 0) 풀 이벤트면 여기서 풀로 돌려준다. false */
        (void)ao; (void)e; (void)droppable;
        return false;
    }
    EventPool* pool_ = nullptr;
    ActiveObject* aos_[kMaxPrio + 1] = {};
    uint32_t subs_[SIG_MAX] = {};
    uint32_t ready_ = 0;
    TimeEvent* armed_ = nullptr;
    uint32_t n_dispatched_ = 0;
};

/* ================= Part C — 시나리오 AO: 버튼(디바운스) → LED 블링크, 통계 ================= */
class Button : public Active<Button, 4> {
public:
    static constexpr uint32_t kDebounceMs = 20;
    constexpr Button(uint8_t prio, Framework& fw, const volatile uint8_t& pin)
        : Active(prio, &Button::stable), fw_(&fw), pin_(&pin), debounce_(SIG_DEBOUNCE_TO, *this) {}
    uint16_t presses() const { return seq_; }
private:
    /* 안정 상태: level_ = 마지막으로 확정된 레벨. 엣지가 오면 디바운스 창을 연다 */
    Status stable(const Event& e) {
        switch (e.sig) {
        case SIG_BTN_EDGE: last_edge_ = static_cast<const EdgeEvt&>(e).level; return tran(&Button::settling);
        default: return Status::Ignored;                       // 낡은 DEBOUNCE_TO 포함
        }
    }
    /* 디바운스 중: 마지막 엣지 후 20 ms 동안 엣지가 없으면 핀을 읽어 확정. 바뀌었으면 PRESSED/RELEASED publish.
     * 함정 두 개: ① 타이머가 이미 다시 무장됐는데 옛 타임아웃이 큐에 있었다 (낡은 타임아웃)
     *            ② 타임아웃이 큐에 들어간 뒤 새 엣지가 왔다 — 핀이 마지막으로 처리한 엣지 레벨과 다르다 (엣지가 큐에 있거나 버려졌다) */
    Status settling(const Event& e) {
        /* TODO(C1): ENTRY → arm(debounce_, kDebounceMs), EXIT → disarm, EDGE → last_edge_ 기록 + 자기 전이.
         *           DEBOUNCE_TO → ① is_armed 면 낡은 타임아웃(Ignored) ② 핀 ≠ last_edge_ 면 창 재시작(Handled)
         *           ③ 핀 ≠ level_ 이면 확정: 누름이면 ++seq_, PressEvt{seq_, g_now} 를 publish. 그 뒤 tran(stable) */
        (void)e; (void)fw_; (void)pin_; (void)level_;
        return Status::Ignored;
    }
    Framework* fw_;
    const volatile uint8_t* pin_;
    TimeEvent debounce_;
    uint8_t level_ = 0;                                         // 확정된 레벨
    uint8_t last_edge_ = 0;                                     // 마지막으로 처리한 엣지 이벤트의 레벨 (ISR 이 샘플)
    uint16_t seq_ = 0;
};

class Led : public Active<Led, 4> {
public:
    static constexpr uint32_t kOnMs = 100, kOffMs = 100;
    static constexpr uint8_t kPerPress = 3, kMaxLeft = 9;
    constexpr Led(uint8_t prio, Framework& fw, volatile uint8_t& out)
        : Active(prio, &Led::off), fw_(&fw), out_(&out), blink_(SIG_BLINK_TO, *this) {}
    uint16_t n_on() const { return n_on_; }
    bool idle() const { return in_state(&Led::off); }
private:
    void set(uint8_t v) { if (*out_ != v) { *out_ = v; trace(Tk::Led, 0, v); } }
    void add_blinks() { left_ = static_cast<uint8_t>(left_ + kPerPress > kMaxLeft ? kMaxLeft : left_ + kPerPress); }
    Status off(const Event& e) {
        switch (e.sig) {
        case SIG_ENTRY: set(0); return Status::Handled;
        case SIG_PRESSED: left_ = 0; add_blinks(); return tran(&Led::on);
        default: return Status::Ignored;                       // RELEASED, 낡은 BLINK_TO
        }
    }
    Status on(const Event& e) {
        switch (e.sig) {
        case SIG_ENTRY: set(1); ++n_on_; --left_; fw_->arm(blink_, kOnMs); return Status::Handled;
        case SIG_EXIT: set(0); return Status::Handled;         // 어느 경로로 나가든 LED 는 꺼진다
        case SIG_BLINK_TO: return tran(&Led::gap);
        case SIG_PRESSED: add_blinks(); return Status::Handled;
        default: return Status::Ignored;
        }
    }
    Status gap(const Event& e) {
        switch (e.sig) {
        case SIG_ENTRY: fw_->arm(blink_, kOffMs); return Status::Handled;
        case SIG_BLINK_TO: return left_ != 0 ? tran(&Led::on) : tran(&Led::off);
        case SIG_PRESSED: add_blinks(); return Status::Handled;
        default: return Status::Ignored;
        }
    }
    Framework* fw_;
    volatile uint8_t* out_;
    TimeEvent blink_;
    uint8_t left_ = 0;
    uint16_t n_on_ = 0;
};

class Stats : public Active<Stats, 4> {
public:
    constexpr explicit Stats(uint8_t prio) : Active(prio, &Stats::counting) {}
    uint16_t presses = 0, releases = 0;
    uint32_t last_hold_ms = 0, max_hold_ms = 0, press_t_ = 0;
private:
    Status counting(const Event& e) {
        switch (e.sig) {
        case SIG_PRESSED: ++presses; press_t_ = static_cast<const PressEvt&>(e).t; return Status::Handled;
        case SIG_RELEASED:
            ++releases;
            last_hold_ms = static_cast<const PressEvt&>(e).t - press_t_;
            if (last_hold_ms > max_hold_ms) max_hold_ms = last_hold_ms;
            return Status::Handled;
        default: return Status::Ignored;
        }
    }
};

/* 한 보드 = 풀 1 + 프레임워크 1 + AO 3. 전부 constexpr 생성 → 전역이면 constinit (.bss, 초기화 코드 0) */
struct Board {
    StaticPool<kBlock, 8> pool;
    Framework fw;
    volatile uint8_t pin = 0, led = 0;
    uint32_t edge_alloc_fail = 0;
    Button btn{3, fw, pin};
    Led led_ao{2, fw, led};
    Stats stats{1};
    void start() {
        pool.init();
        fw.init(pool);
        fw.subscribe(led_ao, SIG_PRESSED);
        fw.subscribe(stats, SIG_PRESSED);
        fw.subscribe(stats, SIG_RELEASED);
        fw.start(btn); fw.start(led_ao); fw.start(stats);
    }
    /* EXTI ISR (양쪽 엣지): 핀 레벨을 실은 이벤트를 버튼에 try_post. 넘치면 버려도 된다 — 디바운스가 핀을 다시 읽으니까 */
    void exti_isr() {
        EdgeEvt* e = fw.new_evt<EdgeEvt>(SIG_BTN_EDGE);
        if (e == nullptr) { ++edge_alloc_fail; return; }
        e->level = pin;
        (void)fw.try_post(btn, e);
    }
    void systick_isr() { fw.tick(); }
};

#ifndef AO_NO_HARNESS
/* ================= 테스트 하네스 ================= */
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* operator new 를 바꿔 끼워 센다 — 프레임워크가 힙을 한 번이라도 쓰면 마지막 CHECK 가 FAIL */
static unsigned g_new_calls = 0;
void* operator new(std::size_t n) { ++g_new_calls; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { ++g_new_calls; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static void reset_all() {
    g_fault = {}; g_now = 0; g_trace_n = 0;
    g_primask = 0; g_isr_runs = 0; g_pending_isr = nullptr; g_armed_isr = nullptr;
}

/* 시험용 AO: 상태 s1/s2, 모든 핸들러 호출을 문자열 로그에 남긴다 */
static char g_log[512];
static void logp(const char* s) { std::strncat(g_log, s, sizeof g_log - std::strlen(g_log) - 1); }
static const Event kSigB{SIG_B, 0, 0};
static const char* sig_name(Signal s) {
    switch (s) {
    case SIG_ENTRY: return "ENTRY"; case SIG_EXIT: return "EXIT"; case SIG_BTN_EDGE: return "EDGE";
    case SIG_DEBOUNCE_TO: return "DEB_TO"; case SIG_PRESSED: return "PRESSED"; case SIG_RELEASED: return "RELEASED";
    case SIG_BLINK_TO: return "BLINK_TO"; case SIG_A: return "A"; case SIG_B: return "B"; case SIG_C: return "C";
    case SIG_TO: return "TO"; default: return "?";
    }
}
struct Probe : Active<Probe, 4> {
    constexpr Probe(uint8_t prio, Framework& fw, const char* tag) : Active(prio, &Probe::s1), fw_(&fw), tag_(tag), te(SIG_TO, *this) {}
    Framework* fw_; const char* tag_; TimeEvent te;
    ActiveObject* forward_to = nullptr;      // SIG_C 를 받으면 여기와 자기 자신에게 post (RTC 시험)
    bool bad_entry = false;
    void note(const char* st, const Event& e) { logp(tag_); logp(":"); logp(st); logp("/"); logp(sig_name(e.sig)); logp(" "); }
    Status s1(const Event& e) {
        note("s1", e);
        switch (e.sig) {
        case SIG_ENTRY: case SIG_EXIT: return Status::Handled;
        case SIG_A: return tran(&Probe::s2);
        case SIG_C:
            if (forward_to) { fw_->post(*forward_to, &kSigB); fw_->post(*this, &kSigB); }
            logp("[C-done] ");
            return Status::Handled;
        default: return Status::Ignored;
        }
    }
    Status s2(const Event& e) {
        note("s2", e);
        switch (e.sig) {
        case SIG_ENTRY: return bad_entry ? tran(&Probe::s1) : Status::Handled;
        case SIG_EXIT: return Status::Handled;
        case SIG_B: return tran(&Probe::s2);    // 자기 전이
        case SIG_A: return tran(&Probe::s1);
        case SIG_TO: return Status::Handled;
        default: return Status::Ignored;
        }
    }
};
static const Event kSigA{SIG_A, 0, 0}, kSigC{SIG_C, 0, 0};

/* 가상 시간 시뮬레이터: 각 ms 마다 ① 핀 변화 → EXTI ISR ② SysTick ISR ③ main 루프(idle 까지 run_one).
 * 기본은 같은 ms 의 EXTI 가 SysTick 보다 먼저 ("엣지 다음에 타임아웃"), tick_first 면 반대 순서 */
struct PinStep { uint32_t t; uint8_t level; };
static void sim(Board& b, const PinStep* s, std::size_t n, uint32_t t_end, bool tick_first = false) {
    std::size_t i = 0;
    while (i < n && s[i].t <= g_now) { b.pin = s[i].level; b.exti_isr(); ++i; }
    for (int guard = 0; guard < 64 && b.fw.run_one(); ++guard) {}
    while (g_now < t_end) {
        ++g_now;
        if (tick_first) b.systick_isr();
        while (i < n && s[i].t == g_now) { b.pin = s[i].level; b.exti_isr(); ++i; }
        if (!tick_first) b.systick_isr();
        for (int guard = 0; guard < 64 && b.fw.run_one(); ++guard) {}
    }
}
static const char* ao_name(uint8_t prio) { return prio == 3 ? "btn" : prio == 2 ? "led" : prio == 1 ? "log" : "?"; }
static char g_render[4096];
/* 같은 시각의 기록을 한 줄로: "33: btn<DEB_TO pub:PRESSED led<PRESSED LED=1 log<PRESSED" */
static const char* render_trace() {
    g_render[0] = '\0';
    char* w = g_render; std::size_t left = sizeof g_render;
    uint32_t cur = 0xFFFFFFFFu;
    for (uint16_t i = 0; i < g_trace_n; ++i) {
        const TraceRec& r = g_trace[i];
        int k = 0;
        if (r.t != cur) { k = std::snprintf(w, left, "%s%u:", cur == 0xFFFFFFFFu ? "" : "\n", static_cast<unsigned>(r.t)); cur = r.t; w += k; left -= static_cast<std::size_t>(k); }
        switch (r.kind) {
        case Tk::Dispatch: k = std::snprintf(w, left, " %s<%s", ao_name(r.a), sig_name(r.b)); break;
        case Tk::Ignored:  k = std::snprintf(w, left, " %s<%s(ign)", ao_name(r.a), sig_name(r.b)); break;
        case Tk::Led:      k = std::snprintf(w, left, " LED=%u", static_cast<unsigned>(r.b)); break;
        case Tk::Pub:      k = std::snprintf(w, left, " pub:%s", sig_name(r.b)); break;
        }
        w += k; left -= static_cast<std::size_t>(k);
    }
    return g_render;
}
static bool trace_is(const char* expect) {
    const char* got = render_trace();
    if (std::strcmp(got, expect) == 0) return true;
    std::printf("---- expected\n%s\n---- got\n%s\n----\n", expect, got);
    return false;
}
static uint32_t xorshift32(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

/* 컴파일 시점 증명 (requires / traits) */
struct NonTrivialEvt : Event { ~NonTrivialEvt() {} };
struct BigEvt : Event { uint8_t payload[32]; };
template <class T> constexpr bool can_new_evt = requires(Framework& f) { f.new_evt<T>(SIG_A); };
static_assert(can_new_evt<EdgeEvt> && can_new_evt<PressEvt>);
static_assert(!can_new_evt<NonTrivialEvt>, "dtor 가 있는 이벤트는 풀에 못 넣는다 (gc 가 dtor 를 부르지 않음)");
static_assert(!can_new_evt<BigEvt>, "블록보다 큰 이벤트");
static_assert(!can_new_evt<int>);
static_assert(!std::is_copy_constructible_v<Button> && !std::is_copy_constructible_v<Framework>);
static_assert(!std::is_convertible_v<Status (Led::*)(const Event&), Button::State>, "다른 AO 의 핸들러로 전이 불가");
static_assert(!std::is_polymorphic_v<ActiveObject> && !std::is_polymorphic_v<Button>, "vtable 없음");
static_assert(std::is_trivially_destructible_v<Board>, "소멸자 0 → 전역이어도 atexit 등록 없음");

int main() {
    std::printf("== Part A: pool / dispatch / publish / scheduler\n");
    {   /* A1 풀 */
        reset_all();
        StaticPool<kBlock, 4> pool; pool.init();
        CHECK(pool.n_free() == 4 && pool.n_total() == 4 && pool.block_size() == kBlock);
        void* b[5] = {};
        for (int i = 0; i < 5; ++i) b[i] = pool.alloc();
        CHECK(b[0] != nullptr && b[1] != nullptr && b[2] != nullptr && b[3] != nullptr && b[4] == nullptr);
        CHECK(b[0] != b[1] && b[1] != b[2] && b[2] != b[3] && b[0] != b[3]);
        CHECK(b[0] != nullptr && b[1] != nullptr &&
              (static_cast<std::byte*>(b[1]) - static_cast<std::byte*>(b[0])) == static_cast<std::ptrdiff_t>(kBlock));
        CHECK(pool.n_free() == 0 && pool.n_min() == 0 && pool.n_fail() == 1);
        for (int i = 0; i < 4; ++i) if (b[i]) pool.release(b[i]);
        CHECK(pool.n_free() == 4 && pool.n_min() == 0);
        CHECK(b[3] != nullptr && pool.alloc() == b[3]);                        // LIFO free list: 마지막에 반납한 블록이 먼저
    }
    {   /* A2 RTC 디스패치 + entry/exit */
        reset_all(); g_log[0] = '\0';
        StaticPool<kBlock, 4> pool; Framework fw; Probe p{1, fw, "p"};
        pool.init(); fw.init(pool); fw.start(p);
        CHECK(std::strcmp(g_log, "p:s1/ENTRY ") == 0);
        g_log[0] = '\0'; fw.post(p, &kSigA); (void)fw.run_one();
        CHECK(std::strcmp(g_log, "p:s1/A p:s1/EXIT p:s2/ENTRY ") == 0 && p.in_state(&Probe::s2));
        g_log[0] = '\0'; fw.post(p, &kSigB); (void)fw.run_one();
        CHECK(std::strcmp(g_log, "p:s2/B p:s2/EXIT p:s2/ENTRY ") == 0);          // 자기 전이도 exit → entry
        g_log[0] = '\0'; fw.post(p, &kSigC); (void)fw.run_one();
        CHECK(std::strcmp(g_log, "p:s2/C ") == 0 && p.in_state(&Probe::s2));      // 무시: exit/entry 없음
        p.bad_entry = true; g_log[0] = '\0'; fw.post(p, &kSigA); fw.post(p, &kSigA); (void)fw.run_one(); (void)fw.run_one();
        CHECK(g_fault.count == 1 && g_fault.last == FAULT_TRAN_IN_ENTRY);         // entry 안에서 전이 = 설계 버그
    }
    {   /* A3 run-to-completion + 우선순위 협조 스케줄링 */
        reset_all(); g_log[0] = '\0';
        StaticPool<kBlock, 4> pool; Framework fw; Probe lo{1, fw, "lo"}, hi{5, fw, "hi"};
        pool.init(); fw.init(pool); fw.start(lo); fw.start(hi);
        lo.forward_to = &hi; g_log[0] = '\0';
        fw.post(lo, &kSigC);
        while (fw.run_one()) {}
        /* lo 가 C 를 처리하는 도중 hi 에 post 해도 hi 는 lo 의 RTC 가 끝난 뒤에 돈다 (협조형, 선점 없음) */
        CHECK(std::strcmp(g_log, "lo:s1/C [C-done] hi:s1/B lo:s1/B ") == 0);
        g_log[0] = '\0';
        fw.post(lo, &kSigA); fw.post(lo, &kSigC); fw.post(hi, &kSigA);
        CHECK(fw.ready() == ((1u << 1) | (1u << 5)));
        (void)fw.run_one();
        CHECK(std::strcmp(g_log, "hi:s1/A hi:s1/EXIT hi:s2/ENTRY ") == 0);        // 나중에 왔어도 높은 우선순위 먼저
        while (fw.run_one()) {}
        CHECK(fw.ready() == 0 && lo.in_state(&Probe::s2) && fw.n_dispatched() == 6);
        CHECK(!fw.run_one());
    }
    {   /* A4 publish / gc 참조 카운트 */
        reset_all(); g_log[0] = '\0';
        StaticPool<kBlock, 4> pool; Framework fw; Probe a{1, fw, "a"}, b{2, fw, "b"}, c{3, fw, "c"};
        pool.init(); fw.init(pool); fw.start(a); fw.start(b); fw.start(c);
        fw.subscribe(a, SIG_B); fw.subscribe(c, SIG_B);
        PressEvt* e = fw.new_evt<PressEvt>(SIG_B);
        CHECK(e != nullptr && pool.n_free() == 3);
        CHECK(e != nullptr && e->sig == SIG_B && e->pool == 1 && e->ref == 0);
        if (e) fw.publish(e);
        CHECK(e != nullptr && e->ref == 2 && pool.n_free() == 3);               // 구독자 2개가 같은 블록을 공유
        CHECK(e != nullptr && fw.run_one() && e->ref == 1 && pool.n_free() == 3);   // c(3) 먼저, 아직 a 가 들고 있다
        CHECK(e != nullptr && fw.run_one() && pool.n_free() == 4);              // 마지막 참조 → 풀로
        PressEvt* lonely = fw.new_evt<PressEvt>(SIG_A);                       // 구독자 0명
        if (lonely) fw.publish(lonely);
        CHECK(lonely != nullptr && pool.n_free() == 4 && fw.ready() == 0);     // publish 안에서 바로 회수
        g_log[0] = '\0';
        EdgeEvt* d = fw.new_evt<EdgeEvt>(SIG_C);
        if (d) { fw.post(b, d); while (fw.run_one()) {} }
        CHECK(d != nullptr && pool.n_free() == 4 && std::strcmp(g_log, "b:s1/C [C-done] ") == 0);
    }

    std::printf("== Part B: overflow policy / ISR / time events\n");
    {   /* B1 try_post (버림 + 카운터) vs post (보장 → fault) */
        reset_all();
        StaticPool<kBlock, 8> pool; Framework fw; Probe p{1, fw, "p"};
        pool.init(); fw.init(pool); fw.start(p);
        int ok = 0;
        for (int i = 0; i < 6; ++i) { EdgeEvt* e = fw.new_evt<EdgeEvt>(SIG_B); if (e && fw.try_post(p, e)) ++ok; }
        CHECK(ok == 4 && p.n_drop() == 2 && g_fault.count == 0);
        CHECK(pool.n_free() == 4);                                               // 버린 2개는 즉시 풀로
        EdgeEvt* e = fw.new_evt<EdgeEvt>(SIG_B);
        CHECK(e != nullptr && !fw.post(p, e) && g_fault.count == 1 && g_fault.last == FAULT_QUEUE_FULL);
        CHECK(pool.n_free() == 4 && p.queue().max_used() == 4);
        g_log[0] = '\0'; while (fw.run_one()) {}
        CHECK(pool.n_free() == 8 && pool.n_min() == 3);                         // 누수 0, 최저 수위 기록
    }
    {   /* B2 publish 도중 한 구독자 큐가 꽉 참 → 그 구독자만 fault, 다른 구독자는 받음, 누수 0 */
        reset_all();
        StaticPool<kBlock, 8> pool; Framework fw; Probe a{1, fw, "a"}, b{2, fw, "b"};
        pool.init(); fw.init(pool); fw.start(a); fw.start(b);
        fw.subscribe(a, SIG_B); fw.subscribe(b, SIG_B);
        for (int i = 0; i < 4; ++i) fw.post(b, &kSigC);                        // b 의 큐를 채운다
        PressEvt* e = fw.new_evt<PressEvt>(SIG_B);
        if (e) fw.publish(e);
        CHECK(e != nullptr && g_fault.count == 1 && a.queue().size() == 1 && b.queue().size() == 4);
        CHECK(e != nullptr && e->ref == 1);
        while (fw.run_one()) {}
        CHECK(pool.n_free() == 8);
    }
    {   /* B3 main 이 큐에서 꺼내는 도중 ISR 이 같은 AO 에 post → 유실 없음 */
        reset_all();
        static StaticPool<kBlock, 8> pool; static Framework fw; static Probe p{1, fw, "p"};
        pool.init(); fw.init(pool); fw.start(p);
        fw.post(p, &kSigC);
        arm_preempt([] { EdgeEvt* e = fw.new_evt<EdgeEvt>(SIG_C); if (e) (void)fw.try_post(p, e); });
        g_log[0] = '\0';
        CHECK(fw.run_one() && g_isr_runs == 1);
        CHECK(p.queue().size() == 1 && fw.ready() == (1u << 1));                 // ISR 의 이벤트가 큐에 남아 있고 ready 도 켜짐
        CHECK(fw.run_one() && !fw.run_one() && pool.n_free() == 8);
        CHECK(std::strcmp(g_log, "p:s1/C [C-done] p:s1/C [C-done] ") == 0);
        CHECK(g_primask == 0);
    }
    {   /* B4 타임 이벤트: 한 번 / 주기 / disarm / 만료 후 disarm */
        reset_all(); g_log[0] = '\0';
        StaticPool<kBlock, 4> pool; Framework fw; Probe p{1, fw, "p"};
        pool.init(); fw.init(pool); fw.start(p);
        fw.arm(p.te, 3);
        fw.tick(); fw.tick();
        CHECK(p.queue().size() == 0 && fw.is_armed(p.te));
        fw.tick();
        CHECK(p.queue().size() == 1 && !fw.is_armed(p.te));                      // 3틱째 정확히 1개, 한 번짜리는 해제
        fw.tick(); fw.tick();
        CHECK(p.queue().size() == 1);
        while (fw.run_one()) {}
        fw.arm(p.te, 2, 5);
        int fired = 0;
        for (int t = 1; t <= 17; ++t) { fw.tick(); while (fw.run_one()) ++fired; }
        CHECK(fired == 4 && fw.is_armed(p.te));                                    // 2, 7, 12, 17
        CHECK(fw.disarm(p.te) && !fw.is_armed(p.te) && !fw.disarm(p.te));
        fw.arm(p.te, 1); fw.tick();
        CHECK(!fw.disarm(p.te) && p.queue().size() == 1);                         // 만료 후 disarm = false, 타임아웃은 이미 큐에
        while (fw.run_one()) {}
        Probe q{2, fw, "q"}; fw.start(q);
        fw.arm(p.te, 5); fw.arm(q.te, 2); fw.arm(p.te, 1);                        // 재무장: 목록에 두 번 들어가면 안 된다
        fw.tick();
        CHECK(p.queue().size() == 1 && q.queue().size() == 0);
        fw.tick();
        CHECK(q.queue().size() == 1 && p.queue().size() == 1);
        for (int t = 0; t < 10; ++t) fw.tick();
        CHECK(p.queue().size() == 1 && q.queue().size() == 1 && g_fault.count == 0);
    }

    std::printf("== Part C: button -> LED blink (virtual time)\n");
    {   /* C1 튀는 누름 + 떼기 + 깜빡이는 도중 두 번째 누름: 정확한 이벤트/액션 트레이스 */
        reset_all();
        static Board b; b.start();
        static const PinStep s[] = {{10, 1}, {11, 0}, {13, 1},            // 누름 (튐 3번)
                                    {200, 0}, {201, 1}, {202, 0},         // 뗌 (튐)
                                    {300, 1}, {400, 0}};                  // 두 번째 누름/뗌 (깨끗)
        sim(b, s, sizeof s / sizeof s[0], 1300);
        CHECK(trace_is(
            "10: btn<EDGE\n"
            "11: btn<EDGE\n"
            "13: btn<EDGE\n"
            "33: btn<DEB_TO pub:PRESSED led<PRESSED LED=1 log<PRESSED\n"
            "133: led<BLINK_TO LED=0\n"
            "200: btn<EDGE\n"
            "201: btn<EDGE\n"
            "202: btn<EDGE\n"
            "222: btn<DEB_TO pub:RELEASED log<RELEASED\n"
            "233: led<BLINK_TO LED=1\n"
            "300: btn<EDGE\n"
            "320: btn<DEB_TO pub:PRESSED led<PRESSED log<PRESSED\n"
            "333: led<BLINK_TO LED=0\n"
            "400: btn<EDGE\n"
            "420: btn<DEB_TO pub:RELEASED log<RELEASED\n"
            "433: led<BLINK_TO LED=1\n"
            "533: led<BLINK_TO LED=0\n"
            "633: led<BLINK_TO LED=1\n"
            "733: led<BLINK_TO LED=0\n"
            "833: led<BLINK_TO LED=1\n"
            "933: led<BLINK_TO LED=0\n"
            "1033: led<BLINK_TO LED=1\n"
            "1133: led<BLINK_TO LED=0\n"
            "1233: led<BLINK_TO"));
        CHECK(b.stats.presses == 2 && b.stats.releases == 2 && b.stats.last_hold_ms == 100 && b.stats.max_hold_ms == 189);
        CHECK(b.led_ao.n_on() == 6 && b.led == 0 && b.led_ao.idle());
        CHECK(b.pool.n_free() == 8 && g_fault.count == 0 && b.fw.ready() == 0);
    }
    {   /* C2 디바운스 타이머 만료와 같은 ms 에 엣지 → 낡은 타임아웃. 거르지 않으면 유령 누름 */
        reset_all();
        static Board b; b.start();
        static const PinStep s[] = {{5, 1}, {11, 0}, {31, 1}, {32, 0}};   // 31 ms: 만료 + 엣지가 동시에
        sim(b, s, sizeof s / sizeof s[0], 200);
        CHECK(trace_is(
            "5: btn<EDGE\n"
            "11: btn<EDGE\n"
            "31: btn<EDGE btn<DEB_TO(ign)\n"
            "32: btn<EDGE\n"
            "52: btn<DEB_TO"));
        CHECK(b.stats.presses == 0 && b.led_ao.n_on() == 0 && b.pool.n_free() == 8);
    }
    {   /* C2b 같은 글리치, 이번엔 SysTick 이 먼저: 타임아웃이 큐에 먼저 → 핀 ≠ 마지막 엣지 → 창 재시작 */
        reset_all();
        static Board b; b.start();
        static const PinStep s[] = {{5, 1}, {11, 0}, {31, 1}, {32, 0}};
        sim(b, s, sizeof s / sizeof s[0], 200, true);
        CHECK(trace_is(
            "5: btn<EDGE\n"
            "11: btn<EDGE\n"
            "31: btn<DEB_TO btn<EDGE\n"
            "32: btn<EDGE\n"
            "52: btn<DEB_TO"));
        CHECK(b.stats.presses == 0 && b.led_ao.n_on() == 0 && b.pool.n_free() == 8 && g_fault.count == 0);
    }
    {   /* C3 같은 ms 에 엣지 13개 (EMI 버스트): 큐 4칸 → 9개 버림. 받은 마지막 엣지 레벨(0) ≠ 핀(1) → 창 한 번 더 */
        reset_all();
        static Board b; b.start();
        PinStep s[13];
        for (int i = 0; i < 13; ++i) s[i] = {50, static_cast<uint8_t>(i & 1 ? 0 : 1)};
        sim(b, s, 13, 120);
        CHECK(b.btn.n_drop() == 9 && b.edge_alloc_fail == 0 && b.btn.queue().max_used() == 4);
        CHECK(b.stats.presses == 1 && b.stats.press_t_ == 90 && b.led_ao.n_on() == 1);   // 50 + 20 + 20
        CHECK(b.pool.n_free() == 8 && g_fault.count == 0);
    }
    for (int order = 0; order < 2; ++order) {   /* C4 xorshift 무작위 튐 40회 (EXTI 먼저 / SysTick 먼저): 누름 40, LED 120번 */
        reset_all();
        static Board boards[2];
        Board& b = boards[order];
        b.start();
        static PinStep s[1024];
        std::size_t n = 0;
        uint32_t seed = 0xA07u, t = 10;
        uint8_t lvl = 0;
        int coincide = 0, glitches = 0;
        for (int k = 0; k < 40; ++k) {
            for (int phase = 0; phase < 2; ++phase) {                         // 0: 누름, 1: 뗌
                const int edges = 1 + 2 * static_cast<int>(xorshift32(seed) % 5);   // 홀수 → 끝 레벨이 바뀜
                for (int e = 0; e < edges; ++e) {
                    lvl ^= 1u;
                    s[n++] = {t, lvl};
                    if (e + 1 < edges && xorshift32(seed) % 8 == 0) { t += Button::kDebounceMs; ++coincide; }  // 타이머 만료와 같은 ms
                    else t += 1 + xorshift32(seed) % 4;                        // 튐 간격 1..4 ms
                }
                t += phase == 0 ? 40 + xorshift32(seed) % 100 : 300;
            }
            if (xorshift32(seed) % 2 == 0) {                                  // 유령 누름 함정: C2 와 같은 4-엣지 글리치
                static constexpr uint32_t kGlitch[] = {0, 6, 26, 27};
                for (uint32_t dt : kGlitch) { lvl ^= 1u; s[n++] = {t + dt, lvl}; }
                ++glitches;
            }
            t += 400 + xorshift32(seed) % 50;
        }
        sim(b, s, n, t + 50, order == 1);
        CHECK(b.stats.presses == 40 && b.stats.releases == 40 && b.btn.presses() == 40);
        CHECK(b.led_ao.n_on() == 120 && b.led == 0);
        CHECK(b.pool.n_free() == 8 && g_fault.count == 0 && b.btn.n_drop() == 0);
        CHECK(b.btn.queue().max_used() <= 2 && b.led_ao.queue().max_used() <= 2 && b.pool.n_min() >= 5);
        std::printf("   C4(%s): %zu edges (%d at timer expiry, %d glitches), pool min free %u/8, queue max btn %u led %u log %u\n", order ? "tick first" : "exti first", n, coincide, glitches,
                    static_cast<unsigned>(b.pool.n_min()), static_cast<unsigned>(b.btn.queue().max_used()),
                    static_cast<unsigned>(b.led_ao.queue().max_used()), static_cast<unsigned>(b.stats.queue().max_used()));
    }
    {   /* C5 RAM 계산 (호스트 64비트 숫자. Cortex-M 숫자는 demo_target.cpp + nm) */
        std::printf("   RAM(host): Event %zu, PressEvt %zu, TimeEvent %zu, State(mem-fn-ptr) %zu, EventQueue %zu\n",
                    sizeof(Event), sizeof(PressEvt), sizeof(TimeEvent), sizeof(Button::State), sizeof(EventQueue));
        std::printf("   RAM(host): Button %zu, Led %zu, Stats %zu, Framework %zu, pool %zu, Board %zu\n",
                    sizeof(Button), sizeof(Led), sizeof(Stats), sizeof(Framework), sizeof(StaticPool<kBlock, 8>), sizeof(Board));
        CHECK(sizeof(Event) == 4 && sizeof(PressEvt) <= kBlock && sizeof(EdgeEvt) <= kBlock);
        CHECK(sizeof(Button::State) == 2 * sizeof(void*));                      // 멤버 함수 포인터 = 2워드 (ptr, adj)
        CHECK(sizeof(StaticPool<kBlock, 8>) >= 8 * kBlock && sizeof(Board) < 1024);
    }
    CHECK(g_new_calls == 0);                                                     // 힙 0회
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
