/* CPP-A09 MISRA C++/AUTOSAR 규칙 적용 리뷰 — 리팩터 대상 + old/new 동등성 하네스
 *
 * namespace legacy : legacy_mc100.cpp 의 함수·클래스를 "글자 그대로" 복사 (고치지 않는다 — 동작의 기준)
 *                    단, -fno-exceptions -fno-rtti 로 컴파일되지 않는 set_speed/calibrate_all 과 REG 매크로 코드는 뺐다
 * namespace mc     : 같은 일을 MISRA C++:2023 스타일로 (fixed_mc100.cpp 와 같은 코드). TODO 를 채운다.
 * 하네스: (1) 동등성 — 같은 입력(전수 또는 xorshift32 고정 시드)에서 legacy == mc
 *         (2) 의도된 차이 D1..D9 — legacy 의 UB/버그 영역은 동등성 도메인에서 빼고 새 계약만 검사 (레슨 §5 표)
 *         (3) 설계 성질 — 소멸자 정책·복사 가능성·explicit 을 타입 트레이트/requires 로, 힙 0 을 operator new 계수로
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti <starter|solution>.cpp -o a && ./a
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static std::uint32_t g_rng = 0x2545F491u;
static std::uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ---- 힙 계수기: 전역 operator new 교체 (CPP-B14). 새 코드는 0 번이어야 한다 ---- */
static std::size_t g_allocs = 0;
void* operator new(std::size_t n) { ++g_allocs; void* p = std::malloc(n != 0 ? n : 1); if (p == nullptr) std::abort(); return p; }
void* operator new[](std::size_t n) { ++g_allocs; void* p = std::malloc(n != 0 ? n : 1); if (p == nullptr) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

/* ---- 소멸자를 절대 부르지 않는 저장소: legacy 객체의 delete/delete[] 불일치·이중 해제(UB)를 피해 동작만 관찰 ---- */
template <class T>
class NoDestroy {
public:
    template <class... A> explicit NoDestroy(A&&... a) { ::new (static_cast<void*>(raw_)) T(std::forward<A>(a)...); }
    T& get() noexcept { return *std::launder(reinterpret_cast<T*>(raw_)); }
private:
    alignas(T) unsigned char raw_[sizeof(T)];
};

/* ====================== LEGACY (legacy_mc100.cpp 에서 복사 — 고치지 않는다) ====================== */
namespace legacy {
int g_fault_count;
char g_log[64];
int g_log_len;

void mc_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_log_len += vsnprintf(g_log + g_log_len, sizeof(g_log) - g_log_len, fmt, ap);
    va_end(ap);
}

uint8_t duty_from_pct(int pct)
{
    uint8_t d = pct * 255 / 100;
    return d;
}

struct Telemetry { uint8_t type; uint16_t id; int16_t value; uint16_t crc; };

Telemetry decode(const uint8_t* f)
{
    Telemetry t;
    t.type = f[0];
    t.id = *(const uint16_t*)(f + 1);
    t.value = *reinterpret_cast<const int16_t*>(f + 3);
    t.crc = *(uint16_t*)(f + 5);
    return t;
}

union FloatBits { float f; uint32_t u; };

uint32_t float_bits(float x)
{
    FloatBits b;
    b.f = x;
    return b.u;
}

int16_t rpm_to_q3(float rpm)          // telemetry speed in Q12.3
{
    int16_t q = rpm * 8.0f;
    return q;
}

class SampleBuf {
public:
    SampleBuf(int n) : cap(n), count(0), head(0) { data = new int16_t[n]; }
    ~SampleBuf() { delete data; }
    void push(int16_t v)
    {
        data[head] = v;
        head = (head + 1) % cap;
        if (count < cap) count++;
    }
    int16_t mean() const
    {
        int s = 0;
        for (int i = 0; i < count; i++) s += data[i];
        return count ? s / count : 0;
    }
    int16_t* data;
    int cap, count, head;
};

class Pid {
public:
    Pid(float p, float i, float d) : kp(p), ki(i), kd(d) {}
    void reset() { integ = 0; d_filt = 0; prev = 0; }
    float step(float err)
    {
        integ += err;
        if (integ > 1000) integ = 1000;
        if (integ < -1000) integ = -1000;
        d_filt = 0.9 * d_filt + 0.1 * (err - prev);
        prev = err;
        return kp * err + ki * integ + kd * d_filt;
    }
private:
    float kp, ki, kd;
    float integ, d_filt, prev;
};

class Sensor {
public:
    virtual int16_t read() = 0;
    ~Sensor() {}
};

const uint16_t k_rpm_lut[16] = { 0, 300, 600, 900, 1200, 1800, 2400, 3000,
                                 3600, 4800, 6000, 7200, 8400, 9600, 10800, 12000 };

int lut_find(const uint16_t* t, int lo, int hi, uint16_t rpm)   // first index with t[i] >= rpm
{
    if (lo >= hi) return lo;
    int mid = (lo + hi) / 2;
    if (t[mid] < rpm) return lut_find(t, mid + 1, hi, rpm);
    return lut_find(t, lo, mid, rpm);
}

int bus_probe();                       // board file: 0 = device answered

int motor_init(int retries)
{
    int tries = 0;
again:
    if (bus_probe() != 0) {
        tries++;
        if (tries < retries) goto again;
        g_fault_count++;
        mc_log("init fail %d\n", tries);
        return -1;
    }
    mc_log("init ok %d\n", tries);
    return tries;
}
}  // namespace legacy

/* 주어진 모의 보드 파일 (legacy 코드 아님): 앞의 fails 번은 응답 없음 */
static int g_old_fails = 0, g_old_probes = 0;
int legacy::bus_probe() { ++g_old_probes; return g_old_fails-- > 0 ? -1 : 0; }

/* ====================== NEW (MISRA C++:2023 스타일) — fixed_mc100.cpp 와 같은 코드 ====================== */
namespace mc {

enum class RegOff : std::uint8_t { HallDr = 0x10U / 4U, NtcDr = 0x14U / 4U };
inline constexpr std::size_t kRegWords = 32U;

class RegBlock {                                   // 주입: 실제 MMIO 블록 또는 호스트 모의 배열 (주어진 코드)
public:
    explicit RegBlock(std::span<volatile std::uint32_t, kRegWords> words) noexcept : words_{words} {}
    [[nodiscard]] std::uint32_t read(RegOff off) const noexcept { return words_[static_cast<std::size_t>(off)]; }
private:
    std::span<volatile std::uint32_t, kRegWords> words_;
};

/* ---------------- Part A: 변환 · 캐스트 · union ---------------- */
inline constexpr std::int32_t kPctMax = 100;
inline constexpr std::int32_t kDutyMax = 255;

// pct 0..100 → 0..255 (legacy 와 같은 내림). D1: <0 → 0, >100 → 255 (legacy 는 8비트로 잘림)
[[nodiscard]] constexpr std::uint8_t duty_from_pct(std::int32_t pct) noexcept
{
    // TODO(A1): 범위 밖은 포화, 범위 안은 (pct * 255) / 100. 곱하기 전에 범위를 검사한다
    (void)pct;
    return 0U;
}

struct Telemetry {
    std::uint8_t type{0U};
    std::uint16_t id{0U};
    std::int16_t value{0};
    std::uint16_t crc{0U};
};
inline constexpr std::size_t kFrameLen = 7U;
inline constexpr std::size_t kIdOff = 1U;           // wire: [type][id:2][value:2][crc:2], little endian
inline constexpr std::size_t kValueOff = 3U;
inline constexpr std::size_t kCrcOff = 5U;

[[nodiscard]] constexpr std::uint16_t le16(std::span<const std::uint8_t, 2> b) noexcept
{
    // TODO(A2): b[0] 이 하위 바이트. unsigned 로 넓힌 뒤 시프트 (int 승격된 부호 있는 비트 연산 금지)
    (void)b;
    return 0U;
}

// reinterpret_cast / C 캐스트 없이. 어떤 정렬의 버퍼에서도 정의된 동작 (D2)
[[nodiscard]] constexpr Telemetry decode(std::span<const std::uint8_t, kFrameLen> f) noexcept
{
    // TODO(A2): f.subspan<오프셋, 2>() + le16. value 는 uint16 → int16 (std::bit_cast 또는 C++20 static_cast)
    (void)f;
    return Telemetry{};
}

[[nodiscard]] constexpr std::uint32_t float_bits(float x) noexcept    // union 없이
{
    // TODO(A3): union 없이 float 의 비트 패턴
    (void)x;
    return 0U;
}

inline constexpr float kQ3Scale = 8.0F;
inline constexpr float kQ3Hi = 32768.0F;                // 잘라도 int16 에 안 들어가는 첫 값
inline constexpr float kQ3Lo = -32769.0F;

// rpm → Q12.3. 범위 안은 legacy 와 같은 0 쪽 버림. D3: 범위 밖 포화, NaN → 0 (legacy 는 UB)
[[nodiscard]] inline std::int16_t rpm_to_q3(float rpm) noexcept
{
    // TODO(A4): q = rpm * 8.0F 를 legacy 와 같게 계산, 변환 전에 NaN / q >= kQ3Hi / q <= kQ3Lo 를 거른다
    (void)rpm;
    return 0;
}

/* ---------------- Part B: 클래스 · 수명 · 소유권 ---------------- */
inline constexpr std::size_t kMaxWindow = 256U;

template <std::size_t N>
class SampleWindow {                               // rule of 0: std::array 멤버, 힙 없음, 복사는 깊은 복사
    static_assert(N > 0U && N <= kMaxWindow, "window size");
public:
    void push(std::int16_t v) noexcept             // TODO(B1): legacy SampleBuf::push 와 같은 링 동작
    {
        (void)v;
    }
    [[nodiscard]] std::int16_t mean() const noexcept   // TODO(B1): buf_[0..count_) 의 int32 합 / count_ (0 이면 0)
    {
        return 0;
    }
    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    void clear() noexcept                          // TODO(B1, D5): 보정 후 첫 샘플의 평균이 그 샘플이 되게
    {
    }
private:
    std::array<std::int16_t, N> buf_{};
    std::size_t head_{0U};
    std::size_t count_{0U};
};

struct PidGains {
    float kp{0.0F};
    float ki{0.0F};
    float kd{0.0F};
};

class Pid {
public:
    explicit Pid(PidGains g) noexcept : g_{g} {}
    void reset() noexcept
    {
        integ_ = 0.0F;
        d_filt_ = 0.0F;
        prev_ = 0.0F;
    }
    // legacy step 과 비트 단위로 같아야 한다 (double 상수 유지 — 바꾸면 계약 변경, 연습 1)
    [[nodiscard]] float step(float err) noexcept   // TODO(B2): legacy 와 비트 단위로 같게. 암시적 double↔float 없이
    {
        (void)err;
        return 0.0F;
    }
private:
    static constexpr float kIntegLimit = 1000.0F;
    static constexpr double kAlpha = 0.9;
    static constexpr double kBeta = 0.1;
    PidGains g_;
    float integ_{0.0F};                            // NSDMI: 새 객체는 0 상태 (D4)
    float d_filt_{0.0F};
    float prev_{0.0F};
};

// TODO(B3): 인터페이스 소멸자·복사 정책을 정한다 — 하네스의 트레이트 CHECK 가 요구사항이다
class Sensor {
public:
    virtual ~Sensor() = default;
    [[nodiscard]] virtual std::int16_t read() noexcept = 0;
    virtual void calibrate() noexcept {}           // dynamic_cast<HallSensor*> 대신 (RTTI off)
protected:
    Sensor() = default;
};

class HallSensor : public Sensor {                 // TODO(B3): 더 파생될 일이 없다
public:
    explicit HallSensor(RegBlock regs) noexcept : regs_{regs} {}
    [[nodiscard]] std::int16_t read() noexcept override   // TODO(B3): 데이터 레지스터 → 이력 → 평균
    {
        return 0;
    }
    void calibrate() noexcept override {}          // TODO(B3)
private:
    static constexpr std::size_t kHistory = 8U;
    RegBlock regs_;
    SampleWindow<kHistory> hist_{};
};

class NtcSensor final : public Sensor {
public:
    explicit NtcSensor(RegBlock regs) noexcept : regs_{regs} {}
    [[nodiscard]] std::int16_t read() noexcept override { return static_cast<std::int16_t>(regs_.read(RegOff::NtcDr)); }
private:
    RegBlock regs_;
};

inline void calibrate_all(std::span<Sensor* const> sensors) noexcept
{
    (void)sensors;                                 // TODO(B3): 다운캐스트 없이
}

/* ---------------- Part C: 제어 흐름 (재귀 · goto · 가변 인자 · 전역) ---------------- */
inline constexpr std::array<std::uint16_t, 16> kRpmLut{0U,    300U,  600U,  900U,  1200U, 1800U, 2400U,  3000U,
                                                       3600U, 4800U, 6000U, 7200U, 8400U, 9600U, 10800U, 12000U};

// 첫 t[i] >= rpm 의 i (lower bound). 재귀 없이, 스택 O(1)
[[nodiscard]] constexpr std::size_t lut_find(std::span<const std::uint16_t> t, std::uint16_t rpm) noexcept
{
    // TODO(C1): legacy lut_find 와 같은 순서로 반으로 나누되 루프로
    (void)t;
    (void)rpm;
    return 0U;
}

template <std::size_t N>
class Log {                                        // 가변 인자 없는 고정 버퍼 로거
public:
    void put(std::string_view s) noexcept
    {
        for (const char c : s) {
            push(c);
        }
    }
    void put(std::int32_t v) noexcept                     // TODO(C2): "%d" 와 같은 글자. INT32_MIN 도
    {
        (void)v;
    }
    [[nodiscard]] std::string_view view() const noexcept { return {buf_.data(), len_}; }
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }
private:
    static constexpr std::size_t kMaxDigits = 10U;
    static constexpr std::uint32_t kRadix = 10U;
    void push(char c) noexcept                             // TODO(C2, D8): 가득 차면 버리고 표시 (넘치지 않는다)
    {
        (void)c;
    }
    std::array<char, N> buf_{};
    std::size_t len_{0U};
    bool truncated_{false};
};
using McLog = Log<64>;

// goto 없이. probe 는 주입(템플릿 심, CPP-A08), 고장 카운터·로그는 인자로 (전역 없음)
template <typename Probe>
[[nodiscard]] std::int32_t motor_init(Probe probe, std::int32_t retries, McLog& log, std::int32_t& faults) noexcept
{
    // TODO(C3): legacy motor_init 과 반환값·probe 호출 수·고장 카운트·로그 글자가 같게 (goto 없이)
    (void)probe;
    (void)retries;
    (void)log;
    (void)faults;
    return 0;
}

}  // namespace mc

/* ====================== HARNESS ====================== */
template <class T> concept can_delete = requires(T* p) { delete p; };

static bool same_bits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

int main()
{
    unsigned bad = 0;

    /* ---------- Part A ---------- */
    for (int p = 0; p <= 100; p++) {
        if (legacy::duty_from_pct(p) != mc::duty_from_pct(p)) bad++;
    }
    CHECK(bad == 0 && mc::duty_from_pct(50) == 127 && mc::duty_from_pct(100) == 255);   // 0..100 전수 동등
    CHECK(legacy::duty_from_pct(101) == 1 && legacy::duty_from_pct(-1) == 254 && legacy::duty_from_pct(200) == 254);  // D1 고정
    CHECK(mc::duty_from_pct(101) == 255 && mc::duty_from_pct(-1) == 0);
    CHECK(mc::duty_from_pct(std::numeric_limits<std::int32_t>::max()) == 255 &&
          mc::duty_from_pct(std::numeric_limits<std::int32_t>::min()) == 0);           // 곱하기 전에 범위 검사

    alignas(4) std::uint8_t raw[16] = {};
    bad = 0;
    for (int t = 0; t < 20000; t++) {
        std::uint8_t* f = raw + 1;             // legacy 는 f+1, f+3, f+5 가 2바이트 정렬일 때만 호출 (D2)
        for (int i = 0; i < 7; i++) f[i] = static_cast<std::uint8_t>(rnd());
        const legacy::Telemetry o = legacy::decode(f);
        const mc::Telemetry n = mc::decode(std::span<const std::uint8_t, 7>{f, 7});
        if (o.type != n.type || o.id != n.id || o.value != n.value || o.crc != n.crc) bad++;
        std::uint8_t* g = raw + 8 + static_cast<unsigned>(t % 2);   // 다른 정렬에서도 같은 결과 (new 만)
        for (int i = 0; i < 7; i++) g[i] = f[i];
        const mc::Telemetry m = mc::decode(std::span<const std::uint8_t, 7>{g, 7});
        if (m.id != n.id || m.value != n.value || m.crc != n.crc) bad++;
    }
    CHECK(bad == 0);                                                                    // 2만 프레임
    {
        static constexpr std::array<std::uint8_t, 7> fr{0x01, 0x34, 0x12, 0x9C, 0xFF, 0xAD, 0xDE};
        constexpr mc::Telemetry t = mc::decode(fr);                                     // constexpr 로도 돈다
        CHECK(t.type == 1 && t.id == 0x1234 && t.value == -100 && t.crc == 0xDEAD);    // selftest 프레임
    }

    bad = 0;
    for (int t = 0; t < 100000; t++) {
        const std::uint32_t u = rnd();
        const float x = std::bit_cast<float>(u);
        if (legacy::float_bits(x) != mc::float_bits(x) || mc::float_bits(x) != u) bad++;
    }
    CHECK(bad == 0 && mc::float_bits(1.0F) == 0x3F800000u && mc::float_bits(-0.0F) == 0x80000000u);

    bad = 0;
    for (int t = 0; t < 50000; t++) {
        const float x = (static_cast<float>(static_cast<std::int32_t>(rnd() % 655360u)) - 327680.0F) / 80.0F;  // -4096..4095.99
        if (legacy::rpm_to_q3(x) != mc::rpm_to_q3(x)) bad++;
    }
    CHECK(bad == 0);
    CHECK(legacy::rpm_to_q3(4095.875F) == 32767 && mc::rpm_to_q3(4095.875F) == 32767 &&
          legacy::rpm_to_q3(-4096.0F) == -32768 && mc::rpm_to_q3(-4096.0F) == -32768);  // 경계: 동등
    CHECK(mc::rpm_to_q3(4096.0F) == 32767 && mc::rpm_to_q3(5000.0F) == 32767 && mc::rpm_to_q3(-5000.0F) == -32768);  // D3
    CHECK(mc::rpm_to_q3(std::nanf("")) == 0 && mc::rpm_to_q3(std::numeric_limits<float>::infinity()) == 32767);

    /* ---------- Part B ---------- */
    static_assert(std::is_convertible_v<int, legacy::SampleBuf>);                      // legacy: SampleBuf b = 8; 가 컴파일됨
    CHECK((!std::is_convertible_v<int, mc::SampleWindow<8>> && std::is_trivially_copyable_v<mc::SampleWindow<8>>));
    bad = 0;
    const std::size_t a0 = g_allocs;
    for (int t = 0; t < 2000; t++) {
        NoDestroy<legacy::SampleBuf> ob(8);
        mc::SampleWindow<8> nw;
        const unsigned len = rnd() % 40u;
        for (unsigned i = 0; i < len; i++) {
            const auto v = static_cast<std::int16_t>(rnd());
            ob.get().push(v);
            nw.push(v);
            if (ob.get().mean() != nw.mean()) bad++;
        }
    }
    CHECK(bad == 0);                                                                    // 2000 수열 × 매 push 평균
    CHECK(g_allocs - a0 == 2000);                                                       // legacy 1회/객체, mc 0회

    {   // D4 고정: legacy 복사는 버퍼를 공유한다 (얕은 복사 → 소멸 시 이중 해제)
        NoDestroy<legacy::SampleBuf> ob(8);
        for (int i = 0; i < 8; i++) ob.get().push(10);
        NoDestroy<legacy::SampleBuf> oc(ob.get());
        oc.get().push(90);
        CHECK(ob.get().data == oc.get().data && ob.get().mean() == 20);                 // 원본이 바뀌었다
        mc::SampleWindow<8> nw;
        for (int i = 0; i < 8; i++) nw.push(10);
        mc::SampleWindow<8> nc = nw;
        nc.push(90);
        CHECK(nw.mean() == 10 && nc.mean() == 20);                                      // 독립
    }
    {   // D5: 보정(clear) 뒤 첫 샘플
        NoDestroy<legacy::SampleBuf> ob(8);
        ob.get().push(10); ob.get().push(20); ob.get().push(30);
        ob.get().count = 0;                                                             // legacy calibrate_all 이 하던 일
        ob.get().push(100);
        CHECK(ob.get().mean() == 10);                                                   // 낡은 data[0]
        mc::SampleWindow<8> nw;
        nw.push(10); nw.push(20); nw.push(30);
        nw.clear();
        nw.push(100);
        CHECK(nw.mean() == 100 && nw.count() == 1);
    }

    CHECK((!std::is_convertible_v<mc::PidGains, mc::Pid>));                            // explicit
    bad = 0;
    for (int t = 0; t < 500; t++) {
        const float kp = static_cast<float>(rnd() % 4001u) / 1000.0F - 2.0F;
        const float ki = static_cast<float>(rnd() % 4001u) / 1000.0F - 2.0F;
        const float kd = static_cast<float>(rnd() % 4001u) / 1000.0F - 2.0F;
        legacy::Pid op(kp, ki, kd);
        op.reset();                                     // legacy 는 reset() 없이 쓰면 미초기화 읽기 (UB, D4)
        mc::Pid np(mc::PidGains{kp, ki, kd});
        for (int s = 0; s < 40; s++) {
            const float e = static_cast<float>(static_cast<std::int32_t>(rnd() % 60001u) - 30000) / 100.0F;  // ±300: 적분 포화 포함
            if (!same_bits(op.step(e), np.step(e))) bad++;
        }
    }
    CHECK(bad == 0);                                                                    // 2만 스텝 비트 단위 동등
    {
        mc::Pid p1(mc::PidGains{1.0F, 0.0F, 0.0F}), p2(mc::PidGains{0.0F, 1.0F, 0.0F}), p3(mc::PidGains{0.0F, 0.0F, 1.0F});
        CHECK(p1.step(2.0F) == 2.0F && p2.step(5.0F) == 5.0F && p3.step(10.0F) == 1.0F);   // 새 객체 = 0 상태
        CHECK(p2.step(2000.0F) == 1000.0F && p2.step(-5000.0F) == -1000.0F);                // 적분 포화
    }

    CHECK(can_delete<legacy::Sensor> && !can_delete<mc::Sensor>);                       // 기반 포인터 delete 금지
    CHECK(std::is_polymorphic_v<mc::Sensor> && !std::has_virtual_destructor_v<mc::Sensor>);
    CHECK(std::is_final_v<mc::HallSensor> && !std::is_copy_constructible_v<mc::HallSensor> &&
          std::is_destructible_v<mc::HallSensor>);
    {
        std::array<volatile std::uint32_t, mc::kRegWords> regs{};
        const mc::RegBlock rb{std::span<volatile std::uint32_t, mc::kRegWords>{regs}};
        const std::size_t a1 = g_allocs;
        mc::HallSensor hall{rb};
        mc::NtcSensor ntc{rb};
        regs[0x10 / 4] = 100;
        const std::int16_t h1 = hall.read();
        regs[0x10 / 4] = 200;
        const std::int16_t h2 = hall.read();
        CHECK(h1 == 100 && h2 == 150);
        regs[0x14 / 4] = 0xFFFFFF9Cu;
        CHECK(ntc.read() == -100);                                                      // C++20: 모듈러 변환
        const std::array<mc::Sensor*, 2> all{&hall, &ntc};
        mc::calibrate_all(all);
        regs[0x10 / 4] = 40;
        CHECK(hall.read() == 40 && ntc.read() == -100);                                 // Hall 만 보정됨
        CHECK(g_allocs == a1);                                                          // 센서 생성·사용에 힙 0
    }

    /* ---------- Part C ---------- */
    bad = 0;
    for (std::uint32_t r = 0; r <= 0xFFFFu; r++) {
        const int o = legacy::lut_find(legacy::k_rpm_lut, 0, 16, static_cast<std::uint16_t>(r));
        if (static_cast<std::size_t>(o) != mc::lut_find(mc::kRpmLut, static_cast<std::uint16_t>(r))) bad++;
    }
    CHECK(bad == 0 && mc::lut_find(mc::kRpmLut, 1000) == 4 && mc::lut_find(mc::kRpmLut, 12001) == 16);
    bad = 0;
    for (int t = 0; t < 3000; t++) {
        std::array<std::uint16_t, 32> tb{};
        const unsigned n = rnd() % 33u;
        std::uint16_t v = 0;
        for (unsigned i = 0; i < n; i++) { v = static_cast<std::uint16_t>(v + rnd() % 2000u * (rnd() % 2u)); tb[i] = v; }  // 중복 포함 오름차순
        for (int q = 0; q < 64; q++) {
            std::uint16_t r = static_cast<std::uint16_t>(rnd());
            if (q == 0) r = 0;
            if (q == 1) r = 0xFFFF;
            if (q >= 2 && q < 10 && n > 0) r = static_cast<std::uint16_t>(tb[rnd() % n] + (q % 3) - 1);
            const int o = legacy::lut_find(tb.data(), 0, static_cast<int>(n), r);
            if (static_cast<std::size_t>(o) != mc::lut_find(std::span<const std::uint16_t>{tb.data(), n}, r)) bad++;
        }
    }
    CHECK(bad == 0);                                                                    // 무작위 표 3000 × 64 질의

    bad = 0;
    char ref[32];
    for (int t = 0; t < 20000; t++) {
        std::int32_t v = static_cast<std::int32_t>(rnd());
        if (t % 4 == 1) v %= 1000;
        if (t == 0) v = std::numeric_limits<std::int32_t>::min();
        if (t == 1) v = std::numeric_limits<std::int32_t>::max();
        if (t == 2) v = 0;
        if (t == 3) v = -1;
        mc::Log<64> l;
        l.put(v);
        const int k = std::snprintf(ref, sizeof ref, "%d", static_cast<int>(v));
        if (l.view() != std::string_view(ref, static_cast<std::size_t>(k))) bad++;
    }
    CHECK(bad == 0);                                                                    // "%d" 와 2만 개 동등
    {
        mc::Log<8> l;
        l.put("abcdefghij");
        l.put(123);
        CHECK(l.view() == "abcdefgh" && l.truncated());                                 // D8: 넘치지 않는다
    }

    bad = 0;
    for (int fails = 0; fails <= 12; fails++) {
        for (int retries = -2; retries <= 10; retries++) {
            legacy::g_log_len = 0; legacy::g_log[0] = '\0'; legacy::g_fault_count = 0;
            g_old_fails = fails; g_old_probes = 0;
            const int ro = legacy::motor_init(retries);
            int left = fails, probes = 0;
            mc::McLog log;
            std::int32_t faults = 0;
            const std::int32_t rn = mc::motor_init([&] { ++probes; return left-- > 0 ? -1 : 0; }, retries, log, faults);
            if (ro != rn || g_old_probes != probes || legacy::g_fault_count != faults ||
                std::string_view(legacy::g_log, static_cast<std::size_t>(legacy::g_log_len)) != log.view()) bad++;
        }
    }
    CHECK(bad == 0);                                                                    // 13 × 13 (반환·probe 수·고장·로그 글자)
    {
        int left = 2;
        mc::McLog log;
        std::int32_t faults = 0;
        const std::size_t a2 = g_allocs;
        CHECK(mc::motor_init([&] { return left-- > 0 ? -1 : 0; }, 3, log, faults) == 2 && log.view() == "init ok 2\n");
        left = 5;
        CHECK(mc::motor_init([&] { return left-- > 0 ? -1 : 0; }, 3, log, faults) == -1 && faults == 1 &&
              log.view() == "init ok 2\ninit fail 3\n");
        CHECK(g_allocs == a2);                                                          // Part C 경로에 힙 0
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
