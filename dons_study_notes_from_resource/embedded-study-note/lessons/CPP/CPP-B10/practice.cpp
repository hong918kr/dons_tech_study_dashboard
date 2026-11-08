/* CPP-B10 추가 연습 — 한 번에 검증
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

/* 연습 1: 비트 뒤집기 룩업 테이블 (C-B01 연습 4 의 reverse8 을 표로) — 컴파일 타임 생성 */
constexpr std::array<std::uint8_t, 256> make_rev8_table() {
    std::array<std::uint8_t, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        std::uint8_t r = 0;
        for (int b = 0; b < 8; ++b)
            if (i & (1u << b)) r = static_cast<std::uint8_t>(r | (0x80u >> b));
        t[i] = r;
    }
    return t;
}
inline constexpr auto kRev8 = make_rev8_table();
static_assert(kRev8[0x01] == 0x80 && kRev8[0xB4] == 0x2D && kRev8[0xFF] == 0xFF);

/* 연습 2: 5-샘플 메디안 필터 — 값으로 받은 복사본이 곧 정렬용 작업 공간 (C++20: std::sort 가 constexpr) */
constexpr std::uint16_t median5(std::array<std::uint16_t, 5> s) {
    std::sort(s.begin(), s.end());
    return s[2];
}
static_assert(median5({100, 4095, 102, 98, 101}) == 101);   /* 스파이크 4095 제거 */

/* 연습 3: 2차원 std::array 는 C 의 uint8_t[3][4] 와 같은 배치 + 구조적 바인딩 */
using Mat3x4 = std::array<std::array<std::uint8_t, 4>, 3>;
static_assert(sizeof(Mat3x4) == 12);
struct Rgb { std::uint8_t r, g, b; };
constexpr Rgb split_rgb(const std::array<std::uint8_t, 3>& px) {
    auto [r, g, b] = px;            /* std::array 는 tuple-like → 구조적 바인딩 가능 */
    return {r, g, b};
}

/* 연습 4: 핑퐁 버퍼 — std::swap(a, b) 는 "내용 N 바이트 복사", 포인터 교환이 아니다 */
template <std::size_t N>
struct PingPong {
    std::array<std::array<std::uint8_t, N>, 2> buf{};
    std::uint8_t dma_side = 0;                                   /* DMA 가 지금 쓰는 쪽 */
    std::array<std::uint8_t, N>& dma()  { return buf[dma_side]; }
    std::array<std::uint8_t, N>& cpu()  { return buf[dma_side ^ 1u]; }
    void flip() { dma_side ^= 1u; }                              /* O(1): 인덱스만 바꾼다 */
};

/* 연습 5: 펌웨어 버전 비교 — std::array 의 ==, < 는 사전식(lexicographic) 비교 */
using Version = std::array<std::uint8_t, 3>;                     /* major.minor.patch */
constexpr bool needs_update(const Version& running, const Version& available) {
    return running < available;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* 1 */
    CHECK(kRev8[0x01] == 0x80);
    CHECK(kRev8[kRev8[0x5C]] == 0x5C);                       /* 두 번 뒤집으면 원래대로 */

    /* 2 */
    const std::array<std::uint16_t, 5> win{7, 3, 9, 1, 5};
    CHECK(median5(win) == 5);
    CHECK(win[0] == 7 && win[3] == 1);                       /* 원본은 정렬되지 않음 */

    /* 3 */
    Mat3x4 m{};
    auto a00 = reinterpret_cast<std::uintptr_t>(&m[0][0]);
    auto a10 = reinterpret_cast<std::uintptr_t>(&m[1][0]);
    CHECK(a10 - a00 == 4);                                   /* 행 사이 패딩 없음 */
    const Rgb c = split_rgb({0x12, 0x34, 0x56});
    CHECK(c.r == 0x12 && c.g == 0x34 && c.b == 0x56);

    /* 4 */
    std::array<std::uint8_t, 4> x{1, 1, 1, 1}, y{2, 2, 2, 2};
    const std::uint8_t* px = x.data();
    std::swap(x, y);
    CHECK(x[0] == 2 && y[0] == 1 && x.data() == px);         /* 내용이 바뀌었지 주소는 그대로 */
    PingPong<16> pp;
    std::uint8_t* first_dma = pp.dma().data();
    pp.dma().fill(0xAA);
    pp.flip();
    CHECK(pp.cpu().data() == first_dma && pp.cpu()[15] == 0xAA);
    CHECK(pp.dma().data() != first_dma && pp.dma()[0] == 0x00);

    /* 5 */
    CHECK(needs_update({1, 4, 2}, {1, 10, 0}));              /* 4 < 10 — 문자열 비교였다면 틀림 */
    CHECK(!needs_update({2, 0, 0}, {1, 99, 99}));
    CHECK((Version{1, 2, 3} == Version{1, 2, 3}));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
