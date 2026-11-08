/* CPP-B06 RAII 기초: 인터럽트 비활성화 CriticalSection 가드
 *
 * 맥(호스트)에서 Cortex-M의 PRIMASK와 CMSIS 함수 3개를 흉내 내고,
 * 생성자=진입 / 소멸자=복원 으로 짝지은 CriticalSection 가드를 만든다.
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
 */
#include <cstdint>
#include <cstdio>
#include <type_traits>

/* ================= mock Cortex-M 코어 (CMSIS 이름 그대로) =================
 * 실제 칩에서는 CMSIS(cmsis_gcc.h)가 제공한다:
 *   __disable_irq()  → cpsid i          (PRIMASK = 1, "memory" clobber 포함)
 *   __enable_irq()   → cpsie i          (PRIMASK = 0)
 *   __get_PRIMASK()  → mrs r0, primask
 * '__' 로 시작하는 이름은 원래 구현 예약([lex.name])이지만, 테스트 전용 mock 이라
 * 드라이버 코드가 그대로 이식되도록 CMSIS와 같은 이름을 쓴다.
 */
static uint32_t g_primask = 0;                 // bit0 = 1 이면 IRQ 마스크됨
static int g_disable_calls = 0, g_enable_calls = 0, g_isr_runs = 0;
static void (*g_pending_isr)() = nullptr;      // NVIC pending 비트 1개 흉내

static void dispatch_pending() {               // 마스크가 풀려 있고 pending 이 있으면 즉시 실행
    if (g_pending_isr != nullptr && (g_primask & 1u) == 0) {
        void (*isr)() = g_pending_isr;
        g_pending_isr = nullptr;
        ++g_isr_runs;
        isr();
    }
}
inline void __disable_irq() { g_primask = 1; ++g_disable_calls; }
inline void __enable_irq()  { g_primask = 0; ++g_enable_calls; dispatch_pending(); }
inline uint32_t __get_PRIMASK() { return g_primask; }

static bool irq_masked() { return (g_primask & 1u) != 0; }
/* 인터럽트 요청: 마스크 중이면 pending 으로 남았다가 __enable_irq() 순간 실행된다 (실제 NVIC 동작) */
static void raise_irq(void (*isr)()) { g_pending_isr = isr; dispatch_pending(); }
/* main 코드의 "여기서 인터럽트가 끼어들 수 있다" 지점. arm_preempt() 로 장전한 ISR 을 한 번 발생시킨다 */
static void (*g_armed_isr)() = nullptr;
static void arm_preempt(void (*isr)()) { g_armed_isr = isr; }
static void preempt_point() {
    if (g_armed_isr != nullptr) { void (*isr)() = g_armed_isr; g_armed_isr = nullptr; raise_irq(isr); }
}

/* ================= Part A: CriticalSection ================= */
class CriticalSection {
public:
    [[nodiscard]] CriticalSection() noexcept {
        saved_ = __get_PRIMASK();      // ① 진입 전 상태 저장 (먼저 읽고)
        __disable_irq();               // ② 그다음 끈다
    }
    ~CriticalSection() {
        if ((saved_ & 1u) == 0) __enable_irq();   // 원래 켜져 있던 경우에만 다시 켠다
    }
    CriticalSection(const CriticalSection&) = delete;            // 복사 금지 (자세히는 CPP-B07)
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    uint32_t saved_ = 0;                                         // 진입 직전의 PRIMASK
};

/* 반례(이미 구현됨, 비교용): 소멸자에서 무조건 enable 하는 흔한 잘못된 가드 */
class NaiveIrqGuard {
public:
    [[nodiscard]] NaiveIrqGuard() noexcept { __disable_irq(); }
    ~NaiveIrqGuard() { __enable_irq(); }
    NaiveIrqGuard(const NaiveIrqGuard&) = delete;
    NaiveIrqGuard& operator=(const NaiveIrqGuard&) = delete;
};

static_assert(!std::is_copy_constructible_v<CriticalSection>);
static_assert(!std::is_move_constructible_v<CriticalSection>);   // 복사 선언 → 암시적 move 없음
static_assert(std::is_nothrow_destructible_v<CriticalSection>);
static_assert(sizeof(CriticalSection) == sizeof(uint32_t));      // 비용: 스택 워드 1개

/* ================= Part B: 여러 출구 경로 ================= */
constexpr int kSlots = 4;
static uint8_t g_slots[kSlots];                // DMA 디스크립터 풀 (0 = 비어 있음). ISR 도 집어 간다

static void isr_claim_any() {                  // ISR: 빈 슬롯 하나를 0xEE 로 가져간다
    for (auto& s : g_slots) { if (s == 0) { s = 0xEE; return; } }
}

/* C 스타일 원본 (참고용, 그대로 동작): 출구를 하나로 모으려고 goto cleanup */
static int claim_slot_c(uint8_t owner) {
    if (owner == 0) return -2;                 // 인자 검사는 임계 구역 밖에서
    int rc = -1;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    for (int i = 0; i < kSlots; ++i) {
        if (g_slots[i] == 0) {
            preempt_point();                   // 검사와 쓰기 사이 — ISR 이 끼어들 수 있는 틈
            g_slots[i] = owner;
            rc = i;
            goto out;
        }
    }
out:
    if ((pm & 1u) == 0) __enable_irq();
    return rc;
}

/* TODO: claim_slot_c 와 같은 동작을 CriticalSection + 조기 return 으로 (goto/rc 변수 없이).
 *       owner==0 → -2, 꽉 차면 → -1, 성공 → 슬롯 번호. 검사와 쓰기 사이에 preempt_point() 호출. */
static int claim_slot(uint8_t owner) {
    if (owner == 0) return -2;                 // 가드 생성 전 → 진입 자체를 안 함
    CriticalSection cs;
    for (int i = 0; i < kSlots; ++i) {
        if (g_slots[i] == 0) {
            preempt_point();
            g_slots[i] = owner;
            return i;                          // ← 여기서 나가도 ~CriticalSection() 실행
        }
    }
    return -1;                                 // ← 여기서 나가도 마찬가지
}

/* ---- 아래 시나리오는 수정 불필요: CriticalSection 이 맞으면 통과한다 ---- */
static int classify(int code) {                // 여러 분기, 분기마다 return
    CriticalSection cs;
    switch (code) {
    case 0: return 10;
    case 1:
        if (irq_masked()) return 11;
        return -1;
    default: break;
    }
    return code < 0 ? -2 : 99;
}
static int loop_guard_inside(int n) {          // 루프 몸통 안의 가드 + continue / break
    int done = 0;
    for (int i = 0; i < n; ++i) {
        CriticalSection cs;
        if (i % 2 != 0) continue;              // 이번 회차의 cs 소멸 → 다음 회차 새로 진입
        if (i == 6) break;                     // 루프 탈출에도 소멸
        ++done;
    }
    return done;
}
static int sum_until_zero(const int* a, int n) {   // 루프 바깥 가드 1개 + break
    CriticalSection cs;
    int s = 0;
    for (int i = 0; i < n; ++i) {
        if (a[i] == 0) break;
        s += a[i];
    }
    return s;
}
static bool masked_during_return() {           // return 식은 소멸자보다 먼저 평가된다
    CriticalSection cs;
    return irq_masked();
}
static bool nested_depth(int depth) {          // 재귀 = 함수 경계를 넘는 중첩
    CriticalSection cs;
    if (depth == 0) return irq_masked();
    const bool inner_ok = nested_depth(depth - 1);
    return inner_ok && irq_masked();           // 안쪽이 끝나도 여기는 여전히 마스크여야
}

/* ================= Part C: ISR 과 공유하는 드라이버 상태 ================= */
static uint32_t g_events = 0;                  // UART ISR 이 비트를 세운다
static uint32_t g_tick_lo = 0, g_tick_hi = 0;  // 32비트 MCU의 64비트 틱 = 워드 2개
static void isr_uart_rx() { g_events |= 0x1u; }
static void isr_systick() { if (++g_tick_lo == 0) ++g_tick_hi; }

/* TODO: ISR-safe 하게 — 읽기와 지우기 사이에 들어온 이벤트를 잃으면 안 된다 */
static uint32_t take_events() {
    CriticalSection cs;
    const uint32_t ev = g_events;
    preempt_point();
    g_events = 0;
    return ev;
}
/* TODO: ISR-safe 하게 — lo/hi 가 서로 다른 시점의 값이면 안 된다 (torn read) */
static uint64_t ticks_now() {
    CriticalSection cs;
    const uint32_t lo = g_tick_lo;
    preempt_point();
    const uint32_t hi = g_tick_hi;
    return (uint64_t{hi} << 32) | lo;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void reset() {
    g_primask = 0; g_disable_calls = g_enable_calls = g_isr_runs = 0;
    g_pending_isr = g_armed_isr = nullptr;
    for (auto& s : g_slots) s = 0;
    g_events = 0; g_tick_lo = g_tick_hi = 0;
}
static bool balanced() { return !irq_masked() && g_disable_calls == g_enable_calls; }

int main() {
    /* ---- Part A: 기본 · 중첩 · 이미 꺼진 상태에서 진입 ---- */
    reset();
    {
        CriticalSection cs;
        CHECK(irq_masked());
    }
    CHECK(!irq_masked());
    CHECK(g_disable_calls == 1 && g_enable_calls == 1);

    reset();
    {
        CriticalSection outer;
        {
            CriticalSection inner;
            CHECK(irq_masked());
        }
        CHECK(irq_masked());                   // ★ inner 가 끝나도 outer 는 아직 보호 중
    }
    CHECK(!irq_masked());
    CHECK(g_enable_calls == 1);                // enable 은 가장 바깥에서 한 번만

    reset();
    g_primask = 1;                             // 부트 코드가 cpsid i 로 꺼 둔 구간처럼
    { CriticalSection cs; }
    CHECK(irq_masked());                       // 원래 꺼져 있었으면 계속 꺼둔다
    CHECK(g_enable_calls == 0);

    reset();
    bool naive_hole = false;
    {
        NaiveIrqGuard outer;
        { NaiveIrqGuard inner; }
        naive_hole = !irq_masked();            // 반례: outer 안인데 이미 켜져 버렸다
    }
    CHECK(naive_hole);

    /* ---- Part B: 조기 return · 분기 · 루프 · 재귀 ---- */
    reset();
    CHECK(claim_slot(0x11) == 0);
    CHECK(claim_slot(0x22) == 1);
    CHECK(claim_slot(0) == -2);
    CHECK(claim_slot(0x33) == 2 && claim_slot(0x44) == 3);
    CHECK(claim_slot(0x55) == -1);             // 꽉 참 → 루프 끝까지 돌고 return
    CHECK(balanced() && g_disable_calls == 5); // owner==0 은 진입조차 안 함

    reset();
    arm_preempt(isr_claim_any);                // 검사-쓰기 틈에 ISR 이 슬롯을 노린다
    CHECK(claim_slot(0x33) == 0);
    CHECK(g_slots[0] == 0x33 && g_slots[1] == 0xEE);   // ISR 은 복원 순간 실행 → 다음 슬롯
    CHECK(g_isr_runs == 1 && balanced());

    reset();
    arm_preempt(isr_claim_any);                // C goto 버전도 같은 결과 (비교용)
    CHECK(claim_slot_c(0x33) == 0 && g_slots[1] == 0xEE && balanced());

    reset();
    CHECK(classify(0) == 10 && classify(1) == 11);
    CHECK(classify(5) == 99 && classify(-3) == -2);
    CHECK(balanced() && g_disable_calls == 4);

    reset();
    CHECK(loop_guard_inside(10) == 3);         // i = 0,2,4 처리, i = 6 에서 break
    CHECK(g_disable_calls == 7 && balanced()); // i = 0..6 매 회차 진입·해제

    reset();
    const int arr[] = {3, 4, 0, 100};
    CHECK(sum_until_zero(arr, 4) == 7 && g_disable_calls == 1 && balanced());

    reset();
    CHECK(masked_during_return());
    CHECK(!irq_masked());

    reset();
    CHECK(nested_depth(5));
    CHECK(g_disable_calls == 6 && g_enable_calls == 1 && !irq_masked());

    /* ---- Part C: ISR 과 공유하는 상태 ---- */
    reset();
    g_events = 0x4u;
    arm_preempt(isr_uart_rx);                  // 읽기와 지우기 사이에 RX 이벤트 도착
    CHECK(take_events() == 0x4u);
    CHECK(g_events == 0x1u);                   // RX 이벤트는 사라지지 않고 다음 차례를 기다린다

    reset();
    g_tick_lo = 0xFFFFFFFFu;                   // 다음 틱에서 lo → hi 캐리
    arm_preempt(isr_systick);
    CHECK(ticks_now() == 0x00000000FFFFFFFFull);   // 찢어진 값 0x1_FFFFFFFF 이 아니어야
    CHECK(g_tick_hi == 1 && g_tick_lo == 0 && g_isr_runs == 1);

    reset();
    {
        CriticalSection outer;                 // 이미 임계 구역 안에서 호출
        arm_preempt(isr_systick);
        (void)ticks_now();
        CHECK(irq_masked() && g_isr_runs == 0);    // 안쪽 복원이 ISR 을 풀어 주면 안 된다
    }
    CHECK(g_isr_runs == 1 && !irq_masked());   // 바깥 가드가 끝나는 순간 pending ISR 실행

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
