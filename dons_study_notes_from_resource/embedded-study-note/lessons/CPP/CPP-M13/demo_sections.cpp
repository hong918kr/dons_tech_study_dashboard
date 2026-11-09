// CPP-M13 demo_sections.cpp — 같은 표를 5가지 방식으로 정의하고 섹션 배치를 본다 (타깃용, target_check.sh 가 빌드)
//   VARIANT=1  constexpr 변수                → .rodata 기대
//   VARIANT=2  constinit (const 없음)        → 상수 초기화 O, 쓰기 가능 → .data 기대
//   VARIANT=3  constinit const               → .rodata 기대
//   VARIANT=4  생성 함수가 constexpr 아님     → 동적 초기화(.init_array + 코드) 기대. 단, 최적화기가 접을 수 있다
//   VARIANT=5  C 방식: .bss + 부팅 때 init()  → .bss + 코드
//   VARIANT=6  VARIANT 4 에서 double 을 뺀 CRC 표만 → 최적화기가 접는지 확인
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef VARIANT
#define VARIANT 1
#endif

using CrcTable = std::array<std::uint32_t, 256>;
using SinTable = std::array<std::int16_t, 257>;

#if VARIANT == 4 || VARIANT == 5 || VARIANT == 6
#define GEN            // 일반 함수: 컴파일타임 평가 불가
#else
#define GEN constexpr
#endif

GEN CrcTable make_crc() {
    CrcTable t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB8'8320u : (c >> 1);
        t[i] = c;
    }
    return t;
}

#if VARIANT != 6
GEN double sin_taylor(double x) {
    const double x2 = x * x;
    double term = x, sum = x;
    for (int n = 1; n <= 13; ++n) {
        term *= -x2 / static_cast<double>((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

GEN std::int16_t to_q15(double v) {
    const double r = v * 32768.0 + (v >= 0.0 ? 0.5 : -0.5);
    if (r >= 32767.0) return 32767;
    return static_cast<std::int16_t>(r);
}

GEN SinTable make_sin() {
    SinTable t{};
    for (std::size_t i = 0; i <= 256; ++i)
        t[i] = to_q15(sin_taylor(1.5707963267948966 * static_cast<double>(i) / 256.0));
    return t;
}
#endif  // VARIANT != 6

#if VARIANT == 1
constexpr CrcTable crc_table = make_crc();
constexpr SinTable sin_table = make_sin();
#elif VARIANT == 2
constinit CrcTable crc_table = make_crc();
constinit SinTable sin_table = make_sin();
#elif VARIANT == 3
constinit const CrcTable crc_table = make_crc();
constinit const SinTable sin_table = make_sin();
#elif VARIANT == 4
const CrcTable crc_table = make_crc();
const SinTable sin_table = make_sin();
#elif VARIANT == 6
const CrcTable crc_table = make_crc();
#elif VARIANT == 5
CrcTable crc_table;
SinTable sin_table;
extern "C" void tables_init() { crc_table = make_crc(); sin_table = make_sin(); }
#endif

extern "C" std::uint32_t crc32_run(const std::uint8_t* p, std::size_t n) {
    std::uint32_t c = 0xFFFF'FFFFu;
    for (std::size_t i = 0; i < n; ++i) c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}
#if VARIANT != 6
extern "C" int sin_at(std::size_t i) { return sin_table[i]; }
#else
extern "C" int sin_at(std::size_t i) { return static_cast<int>(i); }
#endif

// 링크용 진입점 (recipe c). 실제 스타트업은 없다 — 섹션·크기만 본다.
extern "C" [[noreturn]] void _start() {
#if VARIANT == 5
    tables_init();
#endif
    static const std::uint8_t msg[] = {'1', '2', '3'};
    volatile std::uint32_t sink = crc32_run(msg, 3);
    volatile int s = sin_at(sink & 0xFFu);
    (void)s;
    for (;;) {}
}
