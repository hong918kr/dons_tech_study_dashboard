// CPP-M13 constexpr 테이블 생성: CRC·sin LUT를 .rodata로
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
//
// 목표: 부팅 때 RAM에 계산하던 표(table)를 컴파일러가 만들게 해서 플래시(.rodata)에 넣는다.
//   Part A: CRC-32 테이블 + 컴파일타임에도 런타임에도 도는 crc32()
//   Part B: 자체 constexpr sin → Q15 1/4주기 테이블 → 위상(phase) 보간 조회
//   Part C: 플래시 이미지 봉인/검증, NCO(수치 제어 발진기)로 DAC 버퍼 채우기
// 섹션 배치 확인(.rodata/.init_array/.bss)은 target_check.sh 로 한다 (이 파일은 호스트 테스트).
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

// ---------------- Part A: CRC-32 (reflected, IEEE 802.3 / zlib) ----------------
inline constexpr std::uint32_t CRC32_POLY = 0xEDB8'8320u;   // 0x04C11DB7 을 비트 반전한 값
using Crc32Table = std::array<std::uint32_t, 256>;

// A1. 엔트리 i = 바이트 i 를 8번 시프트한 나머지. LSB가 1이면 (c >> 1) ^ POLY, 아니면 c >> 1.
constexpr Crc32Table make_crc32_table() {
    Crc32Table t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit)
            c = (c & 1u) ? (c >> 1) ^ CRC32_POLY : (c >> 1);
        t[i] = c;
    }
    return t;
}

// inline constexpr: 여러 TU에서 include 해도 객체 1개, 상수 초기화 보장, const 라서 읽기 전용
inline constexpr Crc32Table crc32_table = make_crc32_table();

// A2. 바이트 단위 갱신. 스트리밍을 위해 init/final 을 호출자에게 둔다.
//     crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8)
constexpr std::uint32_t crc32_update(std::uint32_t crc, std::uint8_t byte) {
    return crc32_table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
}

// A3. 한 번에 계산: init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
constexpr std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFF'FFFFu;
    for (std::uint8_t b : data) crc = crc32_update(crc, b);
    return ~crc;
}

// A4. 문자열 버전. 상수 평가에서는 reinterpret_cast<const uint8_t*> 가 금지되어 있어
//     span<const uint8_t> 으로 못 넘긴다 → char 를 하나씩 static_cast 한다.
constexpr std::uint32_t crc32(std::string_view s) {
    std::uint32_t crc = 0xFFFF'FFFFu;
    for (char ch : s) crc = crc32_update(crc, static_cast<std::uint8_t>(ch));
    return ~crc;
}

// A5. 컴파일타임 검증 — 표가 틀리면 빌드가 깨져야 한다.
static_assert(crc32_table[0]   == 0x0000'0000u);
static_assert(crc32_table[1]   == 0x7707'3096u);
static_assert(crc32_table[128] == 0xEDB8'8320u);
static_assert(crc32_table[255] == 0x2D02'EF8Du);
static_assert(crc32("123456789") == 0xCBF4'3926u, "CRC-32 check value");
static_assert(crc32("") == 0u);

// ---------------- Part B: Q15 sin 테이블 ----------------
inline constexpr double PI = 3.14159265358979323846;

// B1. std::sin 은 C++20 에서 constexpr 가 아니다(C++26 P1383 에서야 constexpr). 직접 Taylor 급수로.
//     범위: 0 <= x <= pi/2 만 쓴다. sin x = x - x^3/3! + x^5/5! - ...
//     항 점화식: term(n) = term(n-1) * (-x*x) / ((2n)(2n+1)), n = 1..13
constexpr double sin_taylor(double x) {
    const double x2 = x * x;
    double term = x;
    double sum  = x;
    for (int n = 1; n <= 13; ++n) {
        term *= -x2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

// B2. [-1.0, 1.0] → Q15. 스케일 32768, 가장 가까운 정수로 반올림(0에서 먼 쪽), 범위 포화.
//     주의: +1.0 * 32768 = 32768 은 int16_t 에 안 들어간다 → 32767 로 포화.
//     cast 전에 반드시 범위를 확인 ([conv.fpint]: 범위 밖 변환은 UB → 상수 평가면 컴파일 에러).
constexpr std::int16_t to_q15(double v) {
    const double scaled  = v * 32768.0;
    const double rounded = scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5;
    if (rounded >=  32767.0) return  32767;
    if (rounded <= -32768.0) return -32768;
    return static_cast<std::int16_t>(rounded);
}

// B3. 1/4 주기 테이블: 구간 256개, 엔트리 257개(양 끝 포함). i → sin(pi/2 * i/256).
inline constexpr std::size_t SIN_SEGMENTS = 256;
using SinQuarter = std::array<std::int16_t, SIN_SEGMENTS + 1>;

constexpr SinQuarter make_sin_quarter() {
    SinQuarter t{};
    for (std::size_t i = 0; i <= SIN_SEGMENTS; ++i) {
        const double x = (PI / 2.0) * static_cast<double>(i) / static_cast<double>(SIN_SEGMENTS);
        t[i] = to_q15(sin_taylor(x));
    }
    return t;
}

inline constexpr SinQuarter sin_quarter = make_sin_quarter();

// B4. 16비트 위상(65536 = 2*pi) → Q15. [15:14] 사분면, [13:6] 표 인덱스, [5:0] 보간 비율.
//     사분면 0: a=t[idx],     b=t[idx+1]
//     사분면 1: a=t[256-idx], b=t[255-idx]   (거울상)
//     사분면 2,3: 0,1 의 부호 반전
//     보간: y = a + ((b - a) * frac + 32) >> 6  (음수 >> 는 C++20 부터 산술 시프트로 정의됨)
constexpr std::int16_t sin_q15(std::uint16_t phase) {
    const unsigned quadrant = phase >> 14;
    const unsigned idx      = (phase >> 6) & 0xFFu;
    const int      frac     = phase & 0x3F;
    const bool     mirror   = (quadrant & 1u) != 0;
    const int a = sin_quarter[mirror ? 256 - idx : idx];
    const int b = sin_quarter[mirror ? 255 - idx : idx + 1];
    const int y = a + (((b - a) * frac + 32) >> 6);
    return static_cast<std::int16_t>((quadrant & 2u) ? -y : y);
}

// B5. 컴파일타임 검증
static_assert(sin_quarter[0]   == 0);
static_assert(sin_quarter[128] == 23170);            // 0.70710678 * 32768 = 23170.47
static_assert(sin_quarter[256] == 32767);            // +1.0 은 포화
static_assert(sin_q15(0x0000) == 0);
static_assert(sin_q15(0x4000) == 32767);
static_assert(sin_q15(0x8000) == 0);
static_assert(sin_q15(0xC000) == -32767);

// 모든 인접 엔트리가 단조 증가하는가 (컴파일타임 전수 검사용)
consteval bool sin_quarter_monotonic() {
    for (std::size_t i = 1; i < sin_quarter.size(); ++i)
        if (sin_quarter[i] < sin_quarter[i - 1]) return false;
    return true;
}

// ---------------- Part C: 플래시 이미지 봉인과 NCO ----------------
inline constexpr std::uint32_t IMAGE_MAGIC  = 0x4D31'3343u;   // "C31M" (LE 바이트로 43 33 31 4D)
inline constexpr std::size_t   HEADER_SIZE  = 12;             // magic, length, crc: 모두 LE 32비트

constexpr std::uint32_t read_le32(std::span<const std::uint8_t> p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
constexpr void write_le32(std::span<std::uint8_t> p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);       p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24);
}

enum class ImageStatus { Ok, TooShort, BadMagic, BadLength, BadCrc };

// C1. 이미지 형식: [magic][payload 길이][payload 의 CRC-32][payload...]
//     검사 순서: 헤더 크기 → magic → 길이 (flash.size() - HEADER_SIZE 초과면 BadLength) → CRC
//     길이 뒤의 남는 바이트(패딩)는 허용하고 CRC 범위에서 제외한다.
constexpr ImageStatus verify_image(std::span<const std::uint8_t> flash) {
    if (flash.size() < HEADER_SIZE) return ImageStatus::TooShort;
    if (read_le32(flash.subspan(0, 4)) != IMAGE_MAGIC) return ImageStatus::BadMagic;
    const std::uint32_t len = read_le32(flash.subspan(4, 4));
    if (len > flash.size() - HEADER_SIZE) return ImageStatus::BadLength;
    if (read_le32(flash.subspan(8, 4)) != crc32(flash.subspan(HEADER_SIZE, len))) return ImageStatus::BadCrc;
    return ImageStatus::Ok;
}

// C2. payload 를 받아 헤더가 붙은 이미지를 만든다. constexpr 이므로 봉인된 설정 블록을 .rodata 로 만들 수 있다.
template <std::size_t N>
constexpr std::array<std::uint8_t, HEADER_SIZE + N> seal_image(const std::array<std::uint8_t, N>& payload) {
    std::array<std::uint8_t, HEADER_SIZE + N> img{};
    const std::span<std::uint8_t> out{img};
    write_le32(out.subspan(0, 4), IMAGE_MAGIC);
    write_le32(out.subspan(4, 4), static_cast<std::uint32_t>(N));
    write_le32(out.subspan(8, 4), crc32(std::span<const std::uint8_t>{payload}));
    for (std::size_t i = 0; i < N; ++i) img[HEADER_SIZE + i] = payload[i];
    return img;
}

// 컴파일타임에 봉인한 보정값 블록 (플래시에 그대로 들어간다)
inline constexpr std::array<std::uint8_t, 8> CAL_PAYLOAD{0x10, 0x27, 0x00, 0x00, 0x34, 0x12, 0xAB, 0xCD};
inline constexpr auto sealed_cal = seal_image(CAL_PAYLOAD);

// C3. NCO 위상 증분: step = round(freq * 2^32 / fs). consteval → 런타임 나눗셈 0회.
//     freq 는 0 이상 fs/2 미만이어야 한다 — 어기면 빌드 에러가 나도록.
consteval std::uint32_t nco_step(std::uint32_t freq_hz, std::uint32_t fs_hz) {
    if (fs_hz == 0 || freq_hz >= fs_hz / 2) std::abort();   // 상수 평가 중 호출 → 컴파일 에러
    return static_cast<std::uint32_t>(((static_cast<std::uint64_t>(freq_hz) << 32) + fs_hz / 2) / fs_hz);
}

struct Nco {
    std::uint32_t phase = 0;
    std::uint32_t step  = 0;
    // C4. 현재 위상의 상위 16비트로 sin 을 읽고 위상을 전진한다 (uint32 wrap = 위상 2*pi 순환).
    std::int16_t next() {
        const std::int16_t s = sin_q15(static_cast<std::uint16_t>(phase >> 16));
        phase += step;
        return s;
    }
};

// C5. DMA half/full 콜백이 부르는 형태: buf 를 채운다. 호출 사이에 위상이 끊기면 안 된다.
inline void fill_dac(std::span<std::int16_t> buf, Nco& nco) {
    for (std::int16_t& s : buf) s = nco.next();
}

// ---------------- 테스트 하네스 ----------------
static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
// 표현식을 상수 평가한 결과를 런타임에 보고한다 (constexpr 변수의 초기화식은 반드시 상수식)
#define CT_CHECK(expr) do { constexpr bool ct_ok_ = (expr); CHECK(ct_ok_); } while (0)

// 기준 구현: 표 없이 비트 단위로 (런타임). 표 기반 결과와 대조한다.
static std::uint32_t crc32_bitwise(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xFFFF'FFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ CRC32_POLY : (c >> 1);
    }
    return ~c;
}

// 컴파일 실패 증명: "이 값이 상수식이 되는가"를 requires 로 묻는다 (상수 평가 실패 = 치환 실패 = false)
constexpr std::int16_t naive_q15(double v) { return static_cast<std::int16_t>(v * 32768.0); }  // 일부러 나쁜 구현
template <std::int16_t V> struct Q15Probe {};
template <std::uint32_t V> struct StepProbe {};
template <double V> concept NaiveQ15Const = requires { typename Q15Probe<naive_q15(V)>; };
template <double V> concept SafeQ15Const  = requires { typename Q15Probe<to_q15(V)>; };
template <std::uint32_t F, std::uint32_t FS> concept StepConst = requires { typename StepProbe<nco_step(F, FS)>; };

static std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

static double host_sin_q15(std::uint32_t phase) {
    return 32768.0 * std::sin(2.0 * PI * static_cast<double>(phase) / 65536.0);
}

int main() {
    // ---- Part A: 표 값 (상수 평가) ----
    CT_CHECK(crc32_table[0] == 0u);
    CT_CHECK(crc32_table[1] == 0x7707'3096u);
    CT_CHECK(crc32_table[2] == 0xEE0E'612Cu);
    CT_CHECK(crc32_table[4] == 0x076D'C419u);
    CT_CHECK(crc32_table[8] == 0x0EDB'8832u);
    CT_CHECK(crc32_table[128] == 0xEDB8'8320u);
    CT_CHECK(crc32_table[255] == 0x2D02'EF8Du);
    // 표 전체가 비트 단위 정의와 일치
    {
        unsigned bad = 0;
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB8'8320u : (c >> 1);
            if (crc32_table[i] != c) ++bad;
        }
        CHECK(bad == 0);
    }
    // 알려진 벡터 (컴파일타임, CRC-32/ISO-HDLC)
    CT_CHECK(crc32("") == 0u);
    CT_CHECK(crc32("a") == 0xE8B7'BE43u);
    CT_CHECK(crc32("abc") == 0x3524'41C2u);
    CT_CHECK(crc32("123456789") == 0xCBF4'3926u);
    CT_CHECK(crc32("The quick brown fox jumps over the lazy dog") == 0x414F'A339u);

    // string_view 와 span 경로가 같은 값
    {
        const std::array<std::uint8_t, 9> d{'1','2','3','4','5','6','7','8','9'};
        CHECK(crc32(std::span<const std::uint8_t>{d}) == 0xCBF4'3926u);
        CT_CHECK(crc32(std::span<const std::uint8_t>{CAL_PAYLOAD}) != 0u);
    }
    // 고정 시드 무작위 버퍼: 표 기반 == 비트 단위, 길이 경계 0/1/255/256/257/1024
    {
        std::uint8_t buf[1024];
        std::uint32_t seed = 0xC0DE'0013u;
        for (auto& b : buf) b = static_cast<std::uint8_t>(xorshift32(seed));
        const std::size_t lens[] = {0, 1, 2, 255, 256, 257, 511, 1024};
        unsigned bad = 0;
        for (std::size_t n : lens)
            if (crc32(std::span<const std::uint8_t>{buf, n}) != crc32_bitwise(buf, n)) ++bad;
        CHECK(bad == 0);
        // 스트리밍: 임의의 분할점에서 잘라 넣어도 결과가 같다
        unsigned bad2 = 0;
        for (int t = 0; t < 200; ++t) {
            const std::size_t cut = xorshift32(seed) % 1025u;
            std::uint32_t c = 0xFFFF'FFFFu;
            for (std::size_t i = 0; i < cut; ++i) c = crc32_update(c, buf[i]);
            for (std::size_t i = cut; i < 1024; ++i) c = crc32_update(c, buf[i]);
            if (~c != crc32_bitwise(buf, 1024)) ++bad2;
        }
        CHECK(bad2 == 0);
    }

    // ---- Part B: sin_taylor 를 호스트 std::sin 과 대조 (런타임) ----
    {
        double worst = 0.0;
        for (int i = 0; i <= 4096; ++i) {
            const double x = (PI / 2.0) * i / 4096.0;
            const double e = std::fabs(sin_taylor(x) - std::sin(x));
            if (e > worst) worst = e;
        }
        CHECK(worst < 1e-15);                 // 항 13개 + double 반올림 오차
    }
    CT_CHECK(sin_taylor(0.0) == 0.0);
    CT_CHECK(sin_taylor(PI / 2.0) > 0.9999999999 && sin_taylor(PI / 2.0) < 1.0000000001);

    // to_q15: 반올림·포화 경계
    CT_CHECK(to_q15(0.0) == 0);
    CT_CHECK(to_q15(1.0) == 32767);           // 포화
    CT_CHECK(to_q15(-1.0) == -32768);         // 음수 쪽은 표현 가능
    CT_CHECK(to_q15(2.0) == 32767);
    CT_CHECK(to_q15(-2.0) == -32768);
    CT_CHECK(to_q15(0.5) == 16384);
    CT_CHECK(to_q15(-0.5) == -16384);
    CT_CHECK(to_q15(1.0 / 65536.0) == 1);     // 0.5 → 0에서 먼 쪽으로 1
    CT_CHECK(to_q15(-1.0 / 65536.0) == -1);
    CT_CHECK(to_q15(1.0 / 131072.0) == 0);    // 0.25 → 0
    CT_CHECK(to_q15(0.99998) == 32767);       // 32767.34 → 32767

    // 범위 밖 변환은 상수식이 될 수 없다 (naive 는 1.0 에서 실패, to_q15 는 포화라서 통과)
    CT_CHECK(NaiveQ15Const<0.5>);
    CT_CHECK(!NaiveQ15Const<1.0>);
    CT_CHECK(SafeQ15Const<1.0>);
    CT_CHECK(SafeQ15Const<-1.0>);

    // 표 값과 성질
    CT_CHECK(sin_quarter.size() == 257u);
    CT_CHECK(sin_quarter[0] == 0);
    CT_CHECK(sin_quarter[128] == 23170);
    CT_CHECK(sin_quarter[256] == 32767);
    CT_CHECK(sin_quarter_monotonic());
    {
        // 표의 각 엔트리: 진짜 값과의 차이는 반올림 0.5 LSB 이내 (마지막 포화 엔트리 제외)
        double worst = 0.0;
        for (std::size_t i = 0; i < 256; ++i) {
            const double exact = 32768.0 * std::sin((PI / 2.0) * static_cast<double>(i) / 256.0);
            const double e = std::fabs(static_cast<double>(sin_quarter[i]) - exact);
            if (e > worst) worst = e;
        }
        CHECK(worst <= 0.5 + 1e-9);
    }

    // sin_q15: 사분면 경계 (상수 평가)
    CT_CHECK(sin_q15(0x0000) == 0);
    CT_CHECK(sin_q15(0x2000) == 23170);
    CT_CHECK(sin_q15(0x4000) == 32767);
    CT_CHECK(sin_q15(0x6000) == 23170);
    CT_CHECK(sin_q15(0x8000) == 0);
    CT_CHECK(sin_q15(0xA000) == -23170);
    CT_CHECK(sin_q15(0xC000) == -32767);
    CT_CHECK(sin_q15(0xE000) == -23170);

    // 전체 65536 위상 전수 검사: 오차 한계, 대칭성, 범위
    {
        double worst = 0.0;
        unsigned bad_range = 0, bad_mirror = 0, bad_odd = 0;
        for (std::uint32_t p = 0; p < 65536; ++p) {
            const int s = sin_q15(static_cast<std::uint16_t>(p));
            const double e = std::fabs(static_cast<double>(s) - host_sin_q15(p));
            if (e > worst) worst = e;
            if (s > 32767 || s < -32767) ++bad_range;
            // 보간 후에도 sin(pi - x) == sin(x), sin(-x) == -sin(x) 가 전체 위상에서 정확히 성립해야 한다
            const int mirrored = sin_q15(static_cast<std::uint16_t>(0x8000u - p));
            const int negated  = sin_q15(static_cast<std::uint16_t>(0u - p));
            if (mirrored != s) ++bad_mirror;
            if (negated != -s) ++bad_odd;
        }
        CHECK(worst <= 1.1);                  // 표 반올림 0.5 + 보간 곡률 + 출력 반올림 0.5 (실측 1.0007)
        CHECK(bad_range == 0);
        CHECK(bad_mirror == 0);
        CHECK(bad_odd == 0);
    }

    // ---- Part C: 이미지 ----
    {
        // 컴파일타임에 봉인한 블록의 값
        CT_CHECK(sealed_cal.size() == HEADER_SIZE + 8);
        CT_CHECK(read_le32(std::span<const std::uint8_t>{sealed_cal}.subspan(0, 4)) == IMAGE_MAGIC);
        CT_CHECK(read_le32(std::span<const std::uint8_t>{sealed_cal}.subspan(4, 4)) == 8u);
        CT_CHECK(verify_image(sealed_cal) == ImageStatus::Ok);
        CHECK(verify_image(sealed_cal) == ImageStatus::Ok);    // 런타임도 같은 코드

        std::array<std::uint8_t, HEADER_SIZE + 52> img{};
        std::array<std::uint8_t, 52> pay{};
        std::uint32_t seed = 0x1234'5678u;
        for (auto& b : pay) b = static_cast<std::uint8_t>(xorshift32(seed));
        img = seal_image(pay);
        CHECK(verify_image(img) == ImageStatus::Ok);

        // 페이로드 모든 비트를 하나씩 뒤집으면 전부 검출 (CRC 는 1비트 오류를 놓치지 않는다)
        unsigned missed = 0;
        for (std::size_t i = HEADER_SIZE; i < img.size(); ++i)
            for (int b = 0; b < 8; ++b) {
                auto bad = img;
                bad[i] = static_cast<std::uint8_t>(bad[i] ^ (1u << b));
                if (verify_image(bad) != ImageStatus::BadCrc) ++missed;
            }
        CHECK(missed == 0);

        // 경계: 짧은 입력, magic 파손, 길이 과대, 저장된 CRC 파손
        CHECK(verify_image(std::span<const std::uint8_t>{img}.first(11)) == ImageStatus::TooShort);
        CHECK(verify_image(std::span<const std::uint8_t>{img}.first(0)) == ImageStatus::TooShort);
        auto m = img; m[0] ^= 0x01;
        CHECK(verify_image(m) == ImageStatus::BadMagic);
        auto l = img; write_le32(std::span<std::uint8_t>{l}.subspan(4, 4), 53u);
        CHECK(verify_image(l) == ImageStatus::BadLength);
        auto h = img; write_le32(std::span<std::uint8_t>{h}.subspan(4, 4), 0xFFFF'FFFFu);
        CHECK(verify_image(h) == ImageStatus::BadLength);       // 덧셈 오버플로로 통과하면 안 된다
        auto c = img; c[8] ^= 0x80;
        CHECK(verify_image(c) == ImageStatus::BadCrc);

        // 길이 0 이미지
        const auto empty = seal_image(std::array<std::uint8_t, 0>{});
        CHECK(empty.size() == HEADER_SIZE);
        CHECK(verify_image(empty) == ImageStatus::Ok);

        // 페이로드 뒤 패딩은 CRC 에 포함되지 않는다
        std::array<std::uint8_t, HEADER_SIZE + 52 + 4> padded{};
        std::memcpy(padded.data(), img.data(), img.size());
        padded[HEADER_SIZE + 52] = 0xFF;
        CHECK(verify_image(padded) == ImageStatus::Ok);
    }

    // ---- Part C: NCO ----
    {
        constexpr std::uint32_t STEP_1K = nco_step(1000, 48000);
        CT_CHECK(STEP_1K == 89'478'485u);                    // 2^32 / 48 = 89478485.33 → 반올림
        CT_CHECK(nco_step(0, 48000) == 0u);
        CT_CHECK(nco_step(12000, 48000) == 0x4000'0000u);    // fs/4 → 정확히 1/4 회전

        CT_CHECK((StepConst<1000u, 48000u>));
        CT_CHECK(!(StepConst<24000u, 48000u>));              // fs/2 이상은 consteval 에서 거부
        CT_CHECK(!(StepConst<30000u, 48000u>));
        CT_CHECK(!(StepConst<1000u, 0u>));

        Nco n{0, STEP_1K};
        std::array<std::int16_t, 48> samples{};
        fill_dac(samples, n);
        CHECK(samples[0] == 0);
        CHECK(samples[12] >= 32766 && samples[12] <= 32767);  // 1/4 주기 ≈ 피크 (위상 오차 몇 LSB 이내)
        CHECK(samples[24] >= -4 && samples[24] <= 4);         // phase = 0x7FFFFFF8: 위상 하위 16비트를 버려 생긴 오차 약 3 LSB
        CHECK(samples[36] <= -32766);

        // 위상 연속성: 버퍼를 나눠 채워도 한 번에 채운 것과 같다 (DMA half/full)
        Nco one{0, STEP_1K}, two{0, STEP_1K};
        std::array<std::int16_t, 96> whole{};
        std::array<std::int16_t, 96> split{};
        fill_dac(whole, one);
        fill_dac(std::span<std::int16_t>{split}.first(48), two);
        fill_dac(std::span<std::int16_t>{split}.last(48), two);
        CHECK(whole == split);
        CHECK(one.phase == two.phase);

        // 위상 wrap: 2^32 근처에서 끊기지 않는다
        Nco w{0xFFFF'FFF0u, 0x20u};
        const std::int16_t before = w.next();
        CHECK(w.phase == 0x10u);
        CHECK(before >= -4 && before <= 0);                   // 0xFFFF → sin(-1 위상 LSB) ≈ -3 LSB

        // 한 주기 합 ≈ 0 (DC 성분이 거의 없다)
        long long sum = 0;
        for (auto s : samples) sum += s;
        CHECK(sum >= -48 && sum <= 48);

        // 직교(I/Q): I^2 + Q^2 ≈ 1.0, 모든 위상에서 (fixed seed 무작위 시작 위상)
        std::uint32_t seed = 0xABCD'0001u;
        Nco qi{xorshift32(seed), nco_step(1234, 48000)};
        Nco qq{qi.phase + 0x4000'0000u, qi.step};
        double worst = 0.0;
        for (int k = 0; k < 5000; ++k) {
            const double i = qi.next() / 32768.0, q = qq.next() / 32768.0;
            const double e = std::fabs(i * i + q * q - 1.0);
            if (e > worst) worst = e;
        }
        CHECK(worst < 1e-4);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
