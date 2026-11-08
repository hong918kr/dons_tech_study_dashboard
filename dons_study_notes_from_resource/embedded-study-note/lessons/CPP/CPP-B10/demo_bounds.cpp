/* CPP-B10 demo: 범위 초과를 "컴파일 에러" 로 바꾸는 4가지 방법 + C 배열이 못 하는 것
 *  정상 빌드:  c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_bounds.cpp -o demo_bounds && ./demo_bounds
 *  에러 보기:  c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR -c demo_bounds.cpp
 */
#include <array>
#include <cstdint>
#include <cstdio>

constexpr std::array<std::uint8_t, 4> kLut{10, 20, 30, 40};

template <std::size_t Ch, std::size_t N>
    requires (Ch < N)                                 /* ③ 제약: 후보에서 빠짐 */
constexpr std::uint8_t pick(const std::array<std::uint8_t, N>& a) { return std::get<Ch>(a); }

template <class T, std::size_t N>
void dma_start(std::array<T, N>& buf) {               /* ④ static_assert: 친절한 메시지 */
    static_assert(N % 2 == 0, "half-transfer double buffering needs an even element count");
    (void)buf;
}

int main() {
    std::uint8_t ok = std::get<2>(kLut) + pick<3>(kLut);
    std::printf("ok = %u\n", ok);
#ifdef SHOW_ERROR
    auto e1 = std::get<4>(kLut);                      /* ① std::get<I>: 라이브러리 static_assert */
    constexpr auto e2 = kLut[4];                      /* ② 상수 평가 중 범위 초과 = 컴파일 에러 */
    auto e3 = pick<4>(kLut);                          /* ③ requires 불만족 */
    std::array<std::uint16_t, 3> odd{};
    dma_start(odd);                                   /* ④ static_assert 실패 */
    std::uint8_t c1[4] = {1, 2, 3, 4};
    std::uint8_t c2[4] = c1;                          /* ⑤ C 배열은 복사 초기화 불가 */
    (void)e1; (void)e2; (void)e3; (void)c2;
#endif
    return 0;
}
