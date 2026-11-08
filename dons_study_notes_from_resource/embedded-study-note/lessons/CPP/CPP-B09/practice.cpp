// CPP-B09 추가 연습 — 한 번에 검증
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o b09p && ./b09p
#include <cstdint>
#include <cstdio>
#include <optional>
#include <type_traits>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 연습 1: constinit ≠ const ---- */
constinit uint32_t g_tick_count = 0;          // 상수 초기화 + 수정 가능 (SysTick ISR 이 ++)
constexpr uint32_t kTickHz = 1000;            // constexpr 변수는 암시적으로 const
static_assert(!std::is_const_v<decltype(g_tick_count)>);
static_assert(std::is_const_v<decltype(kTickHz)>);
void systick_isr() { ++g_tick_count; }

/* ---- 연습 2: constexpr 생성자를 가진 함수 내부 static → guard 없음, 런타임 생성 0 번 ---- */
static int g_board_ctor_runs;
struct BoardInfo {
    uint32_t rev;
    constexpr explicit BoardInfo(uint32_t r) : rev(r) {
        if (!std::is_constant_evaluated()) ++g_board_ctor_runs;
    }
};
BoardInfo& board_info() {
    static BoardInfo inst{3};                 // 상수 초기화 → 프로그램 시작 전에 이미 .data 에 있다
    return inst;
}

/* ---- 연습 3: 같은 TU 안의 동적 초기화 순서 = 정의 순서 ---- */
static char g_log[8];                         // 상수(0) 초기화 → 어떤 생성자보다 먼저 준비됨
static int  g_log_n;
struct Tag { explicit Tag(char c) { if (g_log_n < 7) g_log[g_log_n++] = c; } };
Tag g_tag_c{'C'};                             // 이름과 무관하게 '정의된 순서'대로 실행
Tag g_tag_a{'A'};
Tag g_tag_b{'B'};

/* ---- 연습 4: 2단계 초기화 — constinit std::optional + board_init() 에서 emplace ---- */
static int g_cal_ctor_runs;
volatile uint32_t g_otp_offset = 37;
struct SensorCal {
    int32_t offset;
    SensorCal() : offset(static_cast<int32_t>(g_otp_offset)) { ++g_cal_ctor_runs; }
};
constinit std::optional<SensorCal> g_cal;     // 비어 있는 optional 은 constexpr 생성 → .bss/.data
static_assert(std::is_trivially_destructible_v<std::optional<SensorCal>>);
void board_init() {                           // 클럭/전원 준비가 끝난 '뒤' main 에서 호출
    if (!g_cal) g_cal.emplace();
}

/* ---- 연습 5: 전역은 trivially destructible 하게 (atexit 등록 X) ---- */
struct StatsWithDtor { uint32_t n; ~StatsWithDtor() {} };   // 빈 소멸자라도 '사용자 제공'이면 non-trivial
struct Stats         { uint32_t n; };                        // 소멸자 선언 안 함 → trivial
static_assert(!std::is_trivially_destructible_v<StatsWithDtor>);
static_assert(std::is_trivially_destructible_v<Stats>);
constinit Stats g_stats{0};

/* ---- 연습 6: 손으로 만든 init-once + ISR 재진입 감지 ---- */
enum class InitState : uint8_t { Uninit, Busy, Done };
static int g_cal2_ctor_runs;
static void (*g_isr_hook)();                  // 생성 도중 '인터럽트가 들어온 상황'을 흉내
struct SlowCal {
    int32_t offset;
    SlowCal() : offset(42) {
        ++g_cal2_ctor_runs;
        if (g_isr_hook) g_isr_hook();         // 생성자 실행 중에 ISR 이 끼어든다
    }
};
constinit InitState g_cal2_state = InitState::Uninit;
constinit std::optional<SlowCal> g_cal2;

const SlowCal* try_get_cal2() {
    switch (g_cal2_state) {
    case InitState::Done: return &*g_cal2;
    case InitState::Busy: return nullptr;     // 초기화 도중 재진입: 기다리면 데드락, 다시 만들면 이중 초기화
    case InitState::Uninit:
        g_cal2_state = InitState::Busy;
        g_cal2.emplace();
        g_cal2_state = InitState::Done;
        return &*g_cal2;
    }
    return nullptr;
}
static bool g_isr_ran;
static const SlowCal* g_isr_saw;
static void fake_isr() { g_isr_ran = true; g_isr_saw = try_get_cal2(); }

int main() {
    const int board_runs_at_main = g_board_ctor_runs;

    // 연습 1
    systick_isr(); systick_isr();
    CHECK(g_tick_count == 2 && kTickHz == 1000);

    // 연습 2
    CHECK(board_info().rev == 3 && &board_info() == &board_info());
    CHECK(board_runs_at_main == 0 && g_board_ctor_runs == 0);   // 런타임 생성자 0 번
    BoardInfo local{5};                                          // 지역 객체는 런타임 생성
    CHECK(local.rev == 5 && g_board_ctor_runs == 1);

    // 연습 3
    CHECK(g_log_n == 3 && g_log[0] == 'C' && g_log[1] == 'A' && g_log[2] == 'B');

    // 연습 4
    CHECK(!g_cal.has_value() && g_cal_ctor_runs == 0);           // main 진입 시 아직 비어 있음
    board_init(); board_init();
    CHECK(g_cal.has_value() && g_cal->offset == 37 && g_cal_ctor_runs == 1);

    // 연습 5
    g_stats.n++;
    CHECK(g_stats.n == 1);

    // 연습 6
    g_isr_hook = fake_isr;
    const SlowCal* c = try_get_cal2();
    g_isr_hook = nullptr;
    CHECK(g_isr_ran && g_isr_saw == nullptr);                                // ISR 은 '아직 준비 안 됨'을 봤다
    CHECK(c != nullptr && c->offset == 42 && g_cal2_ctor_runs == 1);
    CHECK(try_get_cal2() == c && g_cal2_ctor_runs == 1);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
