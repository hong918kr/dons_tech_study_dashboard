/* CPP-B06 추가 연습 — 한 번에 검증
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <cstdint>
#include <cstdio>
#include <type_traits>

/* ---- mock 코어: PRIMASK + BASEPRI + DWT->CYCCNT (CMSIS 이름 그대로, 테스트 전용) ---- */
static uint32_t g_primask = 0;
static uint32_t g_basepri = 0;                 // 0 = 마스킹 없음. N≠0 이면 우선순위 값 ≥ N 인 IRQ 차단
static uint32_t g_cyccnt = 0;                  // 사이클 카운터 흉내
static int g_enable_calls = 0;
inline void __disable_irq() { g_primask = 1; }
inline void __enable_irq()  { g_primask = 0; ++g_enable_calls; }
inline uint32_t __get_PRIMASK() { return g_primask; }
inline uint32_t __get_BASEPRI() { return g_basepri; }
inline void __set_BASEPRI(uint32_t v) { g_basepri = v & 0xFFu; }
inline void __set_BASEPRI_MAX(uint32_t v) {    // MSR BASEPRI_MAX: 더 "세게" 막는 방향으로만 쓴다
    v &= 0xFFu;
    if (v != 0 && (g_basepri == 0 || v < g_basepri)) g_basepri = v;
}
static bool irq_masked() { return (g_primask & 1u) != 0; }
static bool irq_blocked(uint32_t prio) {       // 이 우선순위 값의 IRQ 가 지금 막혀 있나?
    return irq_masked() || (g_basepri != 0 && prio >= g_basepri);
}
static void burn(uint32_t cycles) { g_cyccnt += cycles; }

class CriticalSection {                        // 본문 정답과 같은 가드
public:
    [[nodiscard]] CriticalSection() noexcept : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~CriticalSection() { if ((saved_ & 1u) == 0) __enable_irq(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
private:
    uint32_t saved_;
};

/* ---- 연습 1: 람다를 임계 구역 안에서 실행하고 결과를 "값으로" 돌려주는 critical() ---- */
template <class F>
auto critical(F&& f) {                         // auto: 참조를 벗겨 값으로 복사 (복사는 가드 안에서)
    CriticalSection cs;
    return f();                                // void 를 돌려주는 f 도 OK ([stmt.return])
}
static uint32_t g_counter = 0;
static uint32_t& counter_ref() { return g_counter; }

/* ---- 연습 2: FreeRTOS 식 중첩 카운터 — 순진한 버전 vs 고친 버전 ---- */
static int g_nest = 0;
static void enter_critical_naive() { __disable_irq(); ++g_nest; }
static void exit_critical_naive()  { if (--g_nest == 0) __enable_irq(); }   // 0 이 되면 무조건 enable

static uint32_t g_outer_saved = 0;
static void enter_critical() {
    const uint32_t pm = __get_PRIMASK();
    __disable_irq();                           // 카운터도 공유 데이터 → 끈 다음에 만진다
    if (g_nest++ == 0) g_outer_saved = pm;     // 가장 바깥 진입 때만 원래 상태 기억
}
static void exit_critical() {
    if (--g_nest == 0 && (g_outer_saved & 1u) == 0) __enable_irq();
}

/* ---- 연습 3: BASEPRI 가드 — 우선순위가 낮은 IRQ 만 막고, 급한 IRQ(모터 제어 등)는 살린다 ---- */
class BasePriGuard {
public:
    [[nodiscard]] explicit BasePriGuard(uint8_t level) noexcept : saved_(__get_BASEPRI()) {
        __set_BASEPRI_MAX(level);              // 이미 더 세게 막혀 있으면 그대로 둔다
    }
    ~BasePriGuard() { __set_BASEPRI(saved_); } // 복원은 무조건 쓰기 (BASEPRI_MAX 로는 낮출 수 없다)
    BasePriGuard(const BasePriGuard&) = delete;
    BasePriGuard& operator=(const BasePriGuard&) = delete;
private:
    uint32_t saved_;
};

/* ---- 연습 4: 임계 구역 길이 측정 가드 (최대 유지 시간 기록) ---- */
static uint32_t g_cs_max_cycles = 0;
class TimedCriticalSection {
public:
    [[nodiscard]] TimedCriticalSection() noexcept : start_(g_cyccnt) {}
    ~TimedCriticalSection() {                  // 본문이 먼저 실행 → 그다음 멤버 cs_ 소멸(복원)
        const uint32_t held = g_cyccnt - start_;   // unsigned 뺄셈: 카운터가 한 바퀴 돌아도 맞다
        if (held > g_cs_max_cycles) g_cs_max_cycles = held;
        last_inside_masked = irq_masked();
    }
    static inline bool last_inside_masked = false;
private:
    CriticalSection cs_;                       // 선언 순서 = 초기화 순서: IRQ 먼저 끄고
    uint32_t start_;                           // 그다음 시작 시각 기록
};

/* ---- 연습 5: 이름 없는 임시 가드 버그 ([[nodiscard]] 가 없으면 경고 0개) ---- */
struct PlainGuard {
    PlainGuard() noexcept : saved_(__get_PRIMASK()) { __disable_irq(); }
    ~PlainGuard() { if ((saved_ & 1u) == 0) __enable_irq(); }
    PlainGuard(const PlainGuard&) = delete;
    PlainGuard& operator=(const PlainGuard&) = delete;
    uint32_t saved_;
};
static bool buggy_inside_masked() { PlainGuard();  return irq_masked(); }  // 줄 끝에서 이미 소멸
static bool fixed_inside_masked() { PlainGuard g;  return irq_masked(); }

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void reset() {
    g_primask = 0; g_basepri = 0; g_cyccnt = 0; g_enable_calls = 0;
    g_counter = 0; g_nest = 0; g_outer_saved = 0; g_cs_max_cycles = 0;
}

int main() {
    /* 연습 1 */
    reset();
    CHECK(critical([] { return irq_masked(); }));
    critical([] { ++g_counter; });             // void 람다
    CHECK(g_counter == 1 && !irq_masked());
    static_assert(std::is_same_v<decltype(critical(counter_ref)), uint32_t>);   // 참조가 아니라 값
    g_counter = 42;
    CHECK(critical(counter_ref) == 42u);

    /* 연습 2 */
    reset();
    enter_critical_naive(); enter_critical_naive();
    exit_critical_naive();
    CHECK(irq_masked());                       // 카운터 방식은 중첩 자체는 잘 처리한다
    exit_critical_naive();
    CHECK(!irq_masked());
    reset();
    g_primask = 1;                             // 부트 코드가 cpsid i 로 꺼 둔 구간
    enter_critical_naive(); exit_critical_naive();
    CHECK(!irq_masked());                      // 순진한 버전: 켜면 안 되는데 켜 버렸다 (버그 재현)
    reset();
    g_primask = 1;
    enter_critical(); enter_critical(); exit_critical(); exit_critical();
    CHECK(irq_masked() && g_enable_calls == 0);    // 고친 버전: 원래 상태 유지
    reset();
    enter_critical(); enter_critical(); exit_critical();
    CHECK(irq_masked());
    exit_critical();
    CHECK(!irq_masked() && g_enable_calls == 1);

    /* 연습 3 (우선순위 값이 작을수록 급함. STM32 는 상위 4비트만 유효 → 0x10 단위) */
    reset();
    {
        BasePriGuard outer(0x40);
        CHECK(g_basepri == 0x40 && irq_blocked(0x80) && !irq_blocked(0x20));
        {
            BasePriGuard inner(0x80);          // 더 약한 마스크 요청 → 무시되어야
            CHECK(g_basepri == 0x40);
            BasePriGuard inner2(0x20);         // 더 강한 마스크 요청 → 올라간다
            CHECK(g_basepri == 0x20 && irq_blocked(0x30));
        }
        CHECK(g_basepri == 0x40);              // 안쪽 가드들이 끝나면 바깥 레벨로 복원
    }
    CHECK(g_basepri == 0 && !irq_blocked(0xF0));

    /* 연습 4 */
    reset();
    { TimedCriticalSection t; burn(10); }
    { TimedCriticalSection t; burn(250); }
    { TimedCriticalSection t; burn(30); }
    CHECK(g_cs_max_cycles == 250);
    CHECK(TimedCriticalSection::last_inside_masked && !irq_masked());
    reset();
    g_cyccnt = 0xFFFFFFF0u;                    // 카운터 오버플로 직전
    { TimedCriticalSection t; burn(0x20); }
    CHECK(g_cs_max_cycles == 0x20u);

    /* 연습 5 */
    reset();
    CHECK(!buggy_inside_masked());             // 보호받지 못했다 — 런타임에야 드러나는 버그
    CHECK(fixed_inside_masked() && !irq_masked());

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
