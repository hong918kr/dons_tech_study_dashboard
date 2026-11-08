// CPP-B09 정적 초기화: 전역 생성자와 .init_array
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o b09 && ./b09
#include <cstdint>
#include <cstdio>
#include <type_traits>

// ---- '런타임에' 생성자가 실행된 횟수 (부트 로그 대용) ----
// 이 카운터들은 0 으로 '상수 초기화'되므로 어떤 동적 초기화보다 먼저 준비돼 있다.
static int g_clock_ctor_runs;
static int g_cal_ctor_runs;
static int g_lut_ctor_runs;

// T{Args...} 를 컴파일 타임에 만들 수 있나? (= constinit 전역으로 둘 수 있나)
template <class T, auto... Args>
concept ConstantConstructible =
    requires { typename std::integral_constant<bool, (T{Args...}, true)>; };

/* ======================= Part A: 상수 초기화로 SIOF 없애기 ======================= */
struct ClockConfig {
    uint32_t sysclk_hz;
    uint32_t apb1_shift;                       // APB1 prescaler = 2^shift

    // TODO(A1): constexpr 생성자로 바꿔라. 카운터는 '런타임 실행'일 때만 올린다
    //           (힌트: std::is_constant_evaluated()).
    constexpr ClockConfig(uint32_t hz, uint32_t shift) : sysclk_hz(hz), apb1_shift(shift) {
        if (!std::is_constant_evaluated()) ++g_clock_ctor_runs;
    }
    constexpr uint32_t apb1_hz() const { return sysclk_hz >> apb1_shift; }
};

extern ClockConfig g_clock;                    // 정의는 아래 — "다른 TU 에 있다"고 생각하자

struct Uart {
    uint32_t brr;                              // STM32 USART BRR (oversampling 16) = f_APB1 / baud
    explicit Uart(uint32_t baud) : brr(g_clock.apb1_hz() / baud) {}
};
Uart g_uart{115200};                           // 동적 초기화: 같은 TU 에선 '정의 순서'대로 → g_clock 보다 먼저!

// TODO(A2): constinit 를 붙여 '상수 초기화'를 컴파일러에게 강제하라.
constinit ClockConfig g_clock{16'000'000, 1};  // HSI 16 MHz, APB1 = /2 → 8 MHz

/* ======================= Part B: 첫 사용 시 초기화 (Meyers singleton) ======================= */
volatile uint32_t g_otp[2] = {37u, 0x0001'0400u};   // mock OTP: [0]=offset, [1]=gain(Q16)

struct SensorCal {                             // OTP 를 읽어야 하므로 constexpr 불가 → 런타임 생성자
    int32_t  offset;
    uint32_t gain_q16;
    SensorCal() : offset(static_cast<int32_t>(g_otp[0])), gain_q16(g_otp[1]) { ++g_cal_ctor_runs; }
};

struct AdcLut {                                // 부팅 때 만들기엔 무거운 테이블 (256 엔트리 계산)
    uint16_t v[256];
    AdcLut() : v{} {
        for (uint32_t i = 0; i < 256; ++i) v[i] = static_cast<uint16_t>((i * 3300u) / 255u);  // mV
        ++g_lut_ctor_runs;
    }
};

SensorCal& sensor_cal();
AdcLut&    adc_lut();

struct Filter {
    int32_t offset;
    Filter() : offset(sensor_cal().offset) {}  // 다른 전역을 '함수로' 가져온다
};
Filter g_filter;                               // 동적 초기화 — main 전에 sensor_cal() 호출

// TODO(B1/B2): 아래 두 전역을 없애고, 각 접근 함수 안의 static 지역 변수로 옮겨라.
// (전역 g_cal_global / g_lut_global 제거)
// (첫 호출 때 생성 — guard 변수가 '한 번만'을 보장)
SensorCal& sensor_cal() { static SensorCal inst; return inst; }
AdcLut&    adc_lut()    { static AdcLut inst;    return inst; }

/* ======================= Part C: 부트 시퀀스 시뮬레이션 ======================= */
using InitFn = void (*)();

// TODO(C1): newlib __libc_init_array 처럼 preinit 배열 전부 → init 배열 전부, 각각 앞에서부터 호출.
void run_init_arrays(const InitFn* pre_b, const InitFn* pre_e, const InitFn* b, const InitFn* e) {
    for (const InitFn* f = pre_b; f != pre_e; ++f) (*f)();
    for (const InitFn* f = b; f != e; ++f) (*f)();
}

// mock 하드웨어 (리셋 값 = 0)
struct RccRegs   { volatile uint32_t APB1ENR; };
struct UsartRegs { volatile uint32_t BRR; volatile uint32_t CR1; };
RccRegs   g_rcc;
UsartRegs g_usart2;
constexpr uint32_t RCC_APB1ENR_USART2EN = 1u << 17;
constexpr uint32_t USART_CR1_UE = 1u << 13, USART_CR1_TE = 1u << 3;

// 클럭 게이트가 꺼진 주변장치에 쓰면 무시된다 (실제 STM32 동작을 흉내)
void usart_write(volatile uint32_t& reg, uint32_t v) {
    if (g_rcc.APB1ENR & RCC_APB1ENR_USART2EN) reg = v;
}
void rcc_enable_usart2() { g_rcc.APB1ENR = g_rcc.APB1ENR | RCC_APB1ENR_USART2EN; }

struct EagerUart {                             // ❌ 생성자에서 하드웨어를 만진다
    explicit EagerUart(uint32_t baud) {
        usart_write(g_usart2.BRR, g_clock.apb1_hz() / baud);
        usart_write(g_usart2.CR1, USART_CR1_UE | USART_CR1_TE);
    }
};
EagerUart g_eager_uart{9600};                  // main 전에 실행 → 클럭 꺼짐 → 쓰기가 사라진다

struct UartDriver {                            // ✅ 생성자는 설정만 저장, 하드웨어는 init() 에서
    const ClockConfig* clk;
    uint32_t baud;
    bool ready;
    // TODO(C2): constexpr 생성자로 (하드웨어 접근 금지)
    constexpr UartDriver(const ClockConfig* c, uint32_t b) : clk(c), baud(b), ready(false) {}
    // TODO(C3): 클럭이 켜진 뒤 호출. BRR = APB1 클럭 / baud, CR1 = UE|TE, ready = true
    void init() {
        usart_write(g_usart2.BRR, clk->apb1_hz() / baud);
        usart_write(g_usart2.CR1, USART_CR1_UE | USART_CR1_TE);
        ready = true;
    }
};
constinit UartDriver g_uart2{&g_clock, 115200};  // 상수 초기화: .init_array 엔트리 없음

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char g_order[8];                        // Part C: 호출 순서 기록 (상수 초기화된 버퍼)
static int  g_order_n;
static void rec(char c) { if (g_order_n < 7) g_order[g_order_n++] = c; }

int main() {
    const int clock_runs_at_main = g_clock_ctor_runs;   // main 진입 시점 스냅샷
    const int lut_runs_at_main   = g_lut_ctor_runs;
    const uint32_t brr_at_main   = g_usart2.BRR;

    /* Part A */
    CHECK((ConstantConstructible<ClockConfig, 16'000'000u, 1u>));
    CHECK(clock_runs_at_main == 0);                       // g_clock 은 런타임 생성자 0 번
    CHECK(g_uart.brr == 8'000'000u / 115200u);            // = 69, 링크/정의 순서와 무관해야
    CHECK(g_clock.apb1_hz() == 8'000'000u);
    {
        ClockConfig hse{25'000'000, 2};                   // 지역 객체는 런타임 생성 → 카운트 +1
        CHECK(hse.apb1_hz() == 6'250'000u && g_clock_ctor_runs == clock_runs_at_main + 1);
    }

    /* Part B */
    CHECK(g_filter.offset == 37);                         // main 전에 써도 올바른 보정값
    CHECK(&sensor_cal() == &sensor_cal() && g_cal_ctor_runs == 1);
    CHECK(lut_runs_at_main == 0);                         // 안 쓴 테이블은 부팅 때 만들지 않는다
    CHECK(adc_lut().v[255] == 3300u && adc_lut().v[0] == 0u);
    CHECK(g_lut_ctor_runs == 1);                          // 여러 번 불러도 생성은 1 번

    /* Part C */
    static constexpr InitFn pre[] = {[] { rec('p'); }};
    static constexpr InitFn ini[] = {[] { rec('A'); }, [] { rec('B'); }, [] { rec('C'); }};
    run_init_arrays(pre, pre + 1, ini, ini + 3);
    CHECK(g_order_n == 4 && g_order[0] == 'p' && g_order[1] == 'A' && g_order[3] == 'C');
    CHECK(brr_at_main == 0);                              // EagerUart 의 쓰기는 사라졌다 (버그 재현)
    CHECK((ConstantConstructible<UartDriver, &g_clock, 115200u>));
    rcc_enable_usart2();                                  // main: 클럭 먼저
    g_uart2.init();                                       //       그 다음 주변장치
    CHECK(g_usart2.BRR == 8'000'000u / 115200u);
    CHECK(g_usart2.CR1 == (USART_CR1_UE | USART_CR1_TE));
    CHECK(g_uart2.ready);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
