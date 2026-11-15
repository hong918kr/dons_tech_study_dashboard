/* CPP-A07 practice.cpp — 9. 추가 연습 답 (solution.cpp 의 프레임워크를 복사해 확장)
 *   연습 1: 이벤트 연기 defer/recall (참조 카운트로 RTC 끝 gc 를 넘긴다)
 *   연습 2: 크기별 이벤트 풀 2개 (작은 이벤트가 큰 블록을 낭비하지 않게)
 *   연습 3: tickless idle — next_expiry / skip 로 틱 수를 줄여도 같은 만료 시각
 *   연습 4: idle 진입 경쟁 — "run_one() 이 false" 와 WFI 사이의 ISR (PRIMASK 로 닫기)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

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

inline constexpr std::size_t kBlock = 32;   // 가장 큰 풀의 블록 크기 (연습 2: 8 / 32 두 풀)
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
        CritSec cs;
        FreeBlk* b = head_;
        if (b == nullptr) { ++fail_; return nullptr; }
        head_ = b->next;
        if (--free_ < min_) min_ = free_;
        return b;
    }
    void release(void* blk) {
        CritSec cs;
        head_ = ::new (blk) FreeBlk{head_};
        ++free_;
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
        D& self = static_cast<D&>(*this);
        const Status r = (self.*state_)(e);
        if (r == Status::Tran) {
            (void)(self.*state_)(kExitEvt);
            state_ = next_;
            if ((self.*state_)(kEntryEvt) == Status::Tran) on_fault(FAULT_TRAN_IN_ENTRY);
        }
        return r;
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

    /* 연습 2: 블록 크기 오름차순으로 풀 1~2개 */
    void init(EventPool& small, EventPool* large = nullptr) { pools_[0] = &small; pools_[1] = large; }
    void start(ActiveObject& ao) {
        if (ao.prio_ == 0 || ao.prio_ > kMaxPrio || aos_[ao.prio_] != nullptr) { on_fault(FAULT_BAD_PRIO); return; }
        aos_[ao.prio_] = &ao;
        (void)ao.start_(ao, kEntryEvt);       // 초기 상태의 entry (여기서 post 한 것은 큐로)
    }
    template <PoolEvent T>
    T* new_evt(Signal sig) {
        for (uint8_t i = 0; i < 2; ++i) {                     // 들어가는 가장 작은 풀 하나 (넘어가지 않는다 — 결정적)
            EventPool* p = pools_[i];
            if (p == nullptr || sizeof(T) > p->block_size()) continue;
            void* blk = p->alloc();
            if (blk == nullptr) return nullptr;
            T* e = ::new (blk) T{};
            e->sig = sig; e->pool = static_cast<uint8_t>(i + 1); e->ref = 0;
            return e;
        }
        return nullptr;
    }
    /* post: 넘침 = 설계 버그 → fault. try_post: 넘침 = 예상된 일 → 버림 + n_drop. 실패하면 이벤트는 재활용됨 */
    bool post(ActiveObject& ao, const Event* e) { return post_impl(ao, e, false); }
    bool try_post(ActiveObject& ao, const Event* e) { return post_impl(ao, e, true); }
    void subscribe(const ActiveObject& ao, Signal sig) { subs_[sig] |= 1u << ao.prio_; }
    void unsubscribe(const ActiveObject& ao, Signal sig) { subs_[sig] &= ~(1u << ao.prio_); }

    /* 구독자 전원에게 같은 이벤트 포인터를 (zero-copy). 높은 우선순위부터. 구독자 0명이어도 누수 없음 */
    void publish(const Event* e) {
        trace(Tk::Pub, 0, e->sig);
        if (e->pool != 0) { CritSec cs; ++e->ref; }          // 임시 소유: 배포 도중 재활용 방지
        uint32_t set = subs_[e->sig];
        while (set != 0) {
            const unsigned p = 31u - static_cast<unsigned>(std::countl_zero(set));
            set &= ~(1u << p);
            (void)post_impl(*aos_[p], e, false);               // 구독자 큐 넘침 = fault, 나머지에겐 계속
        }
        gc(e);                                                 // 임시 소유 반납 (아무도 안 받았으면 여기서 풀로)
    }
    /* 참조 하나 반납. 마지막이면 풀로. 정적 이벤트(pool == 0)는 무시 */
    void gc(const Event* e) {
        if (e->pool == 0) return;
        CritSec cs;
        if (e->ref > 1) { --e->ref; return; }
        e->ref = 0;
        pools_[e->pool - 1]->release(const_cast<Event*>(e));
    }
    /* 준비된 AO 중 가장 높은 우선순위에서 이벤트 1개를 꺼내 끝까지 처리(RTC). 없으면 false (= idle) */
    bool run_one() {
        ActiveObject* ao = nullptr;
        const Event* e = nullptr;
        {
            CritSec cs;                                        // ISR 의 post 와 큐·ready 비트를 공유
            if (ready_ == 0) return false;
            const unsigned p = 31u - static_cast<unsigned>(std::countl_zero(ready_));
            ao = aos_[p];
            e = ao->q_.get();
            if (ao->q_.empty()) ready_ &= ~(1u << p);
        }
        (void)ao->dispatch(*e);                                // 인터럽트 켠 채로 처리 — ISR 은 큐에 넣기만
        gc(e);
        ++n_dispatched_;
        return true;
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
        CritSec cs;
        if (t.ctr_ == 0) return false;
        TimeEvent** link = &armed_;
        while (*link != &t) link = &(*link)->next_;
        *link = t.next_;
        t.next_ = nullptr;
        t.ctr_ = 0;
        return true;
    }
    bool is_armed(const TimeEvent& t) const { return t.ctr_ != 0; }
    /* SysTick ISR 에서 1틱마다. 만료된 타임 이벤트를 대상 AO 에 post(보장). 한 번짜리는 목록에서 뺀다 */
    void tick() {
        CritSec cs;
        TimeEvent** link = &armed_;
        while (TimeEvent* t = *link) {
            if (--t->ctr_ != 0) { link = &t->next_; continue; }
            (void)post_impl(*t->target_, t, false);
            if (t->interval_ != 0) { t->ctr_ = t->interval_; link = &t->next_; }
            else { *link = t->next_; t->next_ = nullptr; }
        }
    }

    uint32_t ready() const { return ready_; }
    uint32_t n_dispatched() const { return n_dispatched_; }
    /* 연습 1: 이벤트 연기. 연기 큐가 참조 1개를 갖는다 → RTC 끝의 gc 가 회수하지 않는다 */
    bool defer(EventQueue& dq, const Event* e) {
        CritSec cs;
        if (!dq.put(e)) return false;                          // 넘침: 참조를 안 올렸으니 RTC 끝 gc 가 회수
        if (e->pool != 0) ++e->ref;
        return true;
    }
    bool recall(ActiveObject& ao, EventQueue& dq) {
        const Event* e = nullptr;
        { CritSec cs; e = dq.get(); }
        if (e == nullptr) return false;
        (void)post(ao, e);                                     // 큐가 참조 +1
        gc(e);                                                 // 연기 큐의 참조 반납
        return true;
    }
    /* 연습 3: tickless — 다음 만료까지 남은 틱 (0 = 무장된 것 없음), n 틱 건너뛰기 (만료 직전까지만) */
    uint32_t next_expiry() const {
        CritSec cs;
        uint32_t m = 0;
        for (const TimeEvent* t = armed_; t != nullptr; t = t->next_) if (m == 0 || t->ctr_ < m) m = t->ctr_;
        return m;
    }
    void skip(uint32_t n) {                                    // 전제: n < next_expiry() → 아무것도 만료되지 않는다
        CritSec cs;
        for (TimeEvent* t = armed_; t != nullptr; t = t->next_) t->ctr_ -= n;
    }

private:
    bool post_impl(ActiveObject& ao, const Event* e, bool droppable) {
        CritSec cs;
        if (ao.q_.put(e)) {
            if (e->pool != 0) ++e->ref;
            ready_ |= 1u << ao.prio_;
            return true;
        }
        if (droppable) ++ao.drop_;
        else on_fault(FAULT_QUEUE_FULL);
        if (e->pool != 0 && e->ref == 0) pools_[e->pool - 1]->release(const_cast<Event*>(e));   // 아무도 안 들고 있으면 회수
        return false;
    }
    EventPool* pools_[2] = {};
    ActiveObject* aos_[kMaxPrio + 1] = {};
    uint32_t subs_[SIG_MAX] = {};
    uint32_t ready_ = 0;
    TimeEvent* armed_ = nullptr;
    uint32_t n_dispatched_ = 0;
};


/* ================= 하네스 ================= */
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static unsigned g_new_calls = 0;
void* operator new(std::size_t n) { ++g_new_calls; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static void reset_all() { g_fault = {}; g_now = 0; g_trace_n = 0; g_primask = 0; g_isr_runs = 0; g_pending_isr = nullptr; g_armed_isr = nullptr; }

struct ReqEvt : Event { uint16_t id; };
struct BigEvt : Event { uint8_t payload[24]; };

/* ---------- 연습 1: Worker — 바쁠 때 온 요청을 연기했다가 한가해지면 하나씩 꺼낸다 ---------- */
struct Worker : Active<Worker, 4> {
    static constexpr uint32_t kJobTicks = 50;
    constexpr explicit Worker(uint8_t prio, Framework& fw) : Active(prio, &Worker::idle), fw_(&fw), te_(SIG_TO, *this) {}
    Framework* fw_;
    TimeEvent te_;
    const Event* dbuf_[2] = {};
    EventQueue deferred_{dbuf_, 2};
    uint16_t done_[8] = {};
    uint8_t n_done = 0;
    uint16_t lost = 0;
    Status idle(const Event& e) {
        switch (e.sig) {
        case SIG_ENTRY: (void)fw_->recall(*this, deferred_); return Status::Handled;   // 밀린 요청 하나 다시 꺼내기
        case SIG_A:
            if (n_done < 8) done_[n_done++] = static_cast<const ReqEvt&>(e).id;
            return tran(&Worker::busy);
        default: return Status::Ignored;
        }
    }
    Status busy(const Event& e) {
        switch (e.sig) {
        case SIG_ENTRY: fw_->arm(te_, kJobTicks); return Status::Handled;
        case SIG_A: if (!fw_->defer(deferred_, &e)) ++lost; return Status::Handled;
        case SIG_TO: return tran(&Worker::idle);
        default: return Status::Ignored;
        }
    }
};

/* ---------- 연습 3: 주기 타이머 두 개를 가진 Probe ---------- */
struct Ticker : Active<Ticker, 4> {
    constexpr explicit Ticker(uint8_t prio) : Active(prio, &Ticker::run), a(SIG_A, *this), b(SIG_B, *this) {}
    TimeEvent a, b;
    uint32_t log_t[16] = {};
    Signal log_s[16] = {};
    uint8_t n = 0;
    Status run(const Event& e) {
        if ((e.sig == SIG_A || e.sig == SIG_B) && n < 16) { log_t[n] = g_now; log_s[n] = e.sig; ++n; return Status::Handled; }
        return Status::Ignored;
    }
};

/* ---------- 연습 4: idle 루프 두 가지. wfi_model: 대기 중인 인터럽트가 있으면 바로 깨고, 없으면 "잔다" ---------- */
static int g_sleeps = 0;
static void wfi_model() { if (g_pending_isr == nullptr) ++g_sleeps; }
static Framework* g_fw4 = nullptr;
static ActiveObject* g_ao4 = nullptr;
static const ReqEvt kReqStatic{{SIG_A, 0, 0}, 99};    // 신호 SIG_A 는 항상 ReqEvt — Event 로 만들면 Worker 의 static_cast 가 객체 밖을 읽는다 (ASan 실측)
static void isr_post() { (void)g_fw4->try_post(*g_ao4, &kReqStatic); }
static void idle_naive(Framework& fw) {
    if (!fw.run_one()) {
        isr_window();                    // ← 검사와 WFI 사이에 인터럽트
        wfi_model();
    }
}
static void idle_safe(Framework& fw) {
    if (fw.run_one()) return;
    const uint32_t pm = irq_save();      // cpsid i
    if (fw.ready() == 0) {
        isr_window();                    // 여기서 온 인터럽트는 pending 으로 남는다
        wfi_model();                     // pending 인터럽트가 있으면 WFI 는 바로 깬다 (PRIMASK=1 이어도)
    }
    irq_restore(pm);                     // cpsie i → pending ISR 실행
}

int main() {
    std::printf("== 연습 1: defer / recall\n");
    {
        reset_all();
        StaticPool<kBlock, 8> pool; pool.init();
        Framework fw; fw.init(pool);
        Worker w{1, fw}; fw.start(w);
        for (uint16_t id = 1; id <= 4; ++id) {
            ReqEvt* r = fw.new_evt<ReqEvt>(SIG_A);
            if (r) { r->id = id; (void)fw.post(w, r); }
        }
        while (fw.run_one()) {}
        CHECK(w.n_done == 1 && w.deferred_.size() == 2 && w.lost == 1);
        CHECK(pool.n_free() == 6);                            // 처리 중 0 + 연기 2개 (잃어버린 1개는 이미 회수)
        for (int t = 0; t < 200; ++t) { ++g_now; fw.tick(); while (fw.run_one()) {} }
        CHECK(w.n_done == 3 && w.done_[0] == 1 && w.done_[1] == 2 && w.done_[2] == 3);
        CHECK(w.deferred_.size() == 0 && pool.n_free() == 8 && g_fault.count == 0);
    }
    std::printf("== 연습 2: 크기별 풀 2개\n");
    {
        reset_all();
        StaticPool<8, 4> small; StaticPool<32, 2> large; small.init(); large.init();
        Framework fw; fw.init(small, &large);
        ReqEvt* r = fw.new_evt<ReqEvt>(SIG_A);
        CHECK(r != nullptr && r->pool == 1 && small.n_free() == 3 && large.n_free() == 2);
        BigEvt* b = fw.new_evt<BigEvt>(SIG_B);
        CHECK(b != nullptr && b->pool == 2 && large.n_free() == 1);
        ReqEvt* more[4] = {};
        for (auto& m : more) m = fw.new_evt<ReqEvt>(SIG_A);
        CHECK(more[2] != nullptr && more[3] == nullptr && large.n_free() == 1);   // 작은 풀이 비어도 큰 풀로 넘어가지 않는다
        if (r) fw.gc(r);
        if (b) fw.gc(b);
        for (auto* m : more) if (m) fw.gc(m);
        CHECK(small.n_free() == 4 && large.n_free() == 2);    // 각자 자기 풀로
        CHECK(sizeof(ReqEvt) <= 8 && sizeof(BigEvt) > 8 && sizeof(BigEvt) <= 32);
    }
    std::printf("== 연습 3: tickless\n");
    {
        uint32_t ref_t[16]; Signal ref_s[16]; uint8_t ref_n = 0;
        int wakeups[2] = {0, 0};
        for (int mode = 0; mode < 2; ++mode) {
            reset_all();
            StaticPool<kBlock, 4> pool; pool.init();
            Framework fw; fw.init(pool);
            Ticker tk{1}; fw.start(tk);
            fw.arm(tk.a, 7, 30); fw.arm(tk.b, 12, 45);
            while (g_now < 200) {
                if (mode == 1) {                                 // tickless: 다음 만료 직전까지 한 번에
                    const uint32_t k = fw.next_expiry();
                    const uint32_t step = (k == 0 || g_now + k > 200) ? 200 - g_now : k;
                    fw.skip(step - 1); g_now += step - 1;
                }
                ++g_now; fw.tick(); ++wakeups[mode];
                while (fw.run_one()) {}
            }
            if (mode == 0) { ref_n = tk.n; std::memcpy(ref_t, tk.log_t, sizeof ref_t); std::memcpy(ref_s, tk.log_s, sizeof ref_s); }
            else {
                CHECK(tk.n == ref_n && ref_n == 12);                      // A: 7,37,..,187 (7개) + B: 12,57,..,192 (5개)
                CHECK(std::memcmp(ref_t, tk.log_t, sizeof ref_t) == 0 && std::memcmp(ref_s, tk.log_s, sizeof ref_s) == 0);
            }
        }
        CHECK(ref_t[0] == 7 && ref_s[0] == SIG_A && ref_t[1] == 12 && ref_s[1] == SIG_B && ref_t[2] == 37);
        CHECK(wakeups[0] == 200 && wakeups[1] == 13);             // 만료 12번 + 200 에서 한 번
        std::printf("   wakeups: every tick %d, tickless %d\n", wakeups[0], wakeups[1]);
    }
    std::printf("== 연습 4: idle 진입 경쟁\n");
    {
        reset_all(); g_sleeps = 0;
        static StaticPool<kBlock, 4> pool; pool.init();
        static Framework fw; fw.init(pool);
        static Worker w{1, fw}; fw.start(w);
        g_fw4 = &fw; g_ao4 = &w;
        arm_preempt(isr_post);
        idle_naive(fw);
        CHECK(g_isr_runs == 1 && fw.ready() != 0 && g_sleeps == 1);   // 일이 있는데 잠들었다 (다음 인터럽트까지 지연)
        while (fw.run_one()) {}
        reset_all(); g_sleeps = 0;
        for (int t = 0; t < 60; ++t) { ++g_now; fw.tick(); while (fw.run_one()) {} }   // w 를 idle 로 되돌림
        g_sleeps = 0;
        arm_preempt(isr_post);
        idle_safe(fw);
        CHECK(g_isr_runs == 1 && fw.ready() != 0 && g_sleeps == 0 && g_primask == 0);   // 잠들지 않고 바로 처리하러
        CHECK(fw.run_one() && w.n_done == 2);
    }
    CHECK(g_new_calls == 0);
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
