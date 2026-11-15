// CPP-A10 demo_errors.cpp — "컴파일이 안 돼야 하는 것" 들의 실제 에러 문구.
//   c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only demo_errors.cpp            # 통과
//   c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=N demo_errors.cpp   # N = 1..5
#define A10_LIB_ONLY
#include "solution.cpp"

struct NullBus {                       // I2cBus 를 만족하는 가장 작은 버스
    bool try_lock() { return true; }
    void unlock() {}
    a10::Result<void, a10::BusErr> write_read(std::uint8_t, std::span<const std::uint8_t>, std::span<std::uint8_t>) { return {}; }
};
struct NotABus { int write_read(int) { return 0; } };

int main() {
    NullBus b;
    (void)a10::bus::write_reg<a10::imu::CTRL1_XL>(b, a10::imu::kAddr, 0x40);   // RW: OK
#if SHOW_ERROR == 1
    (void)a10::bus::write_reg<a10::imu::WHO_AM_I>(b, a10::imu::kAddr, 0x00);   // RO 레지스터에 쓰기
#elif SHOW_ERROR == 2
    a10::imu::probe(b);                                                          // Result 무시 ([[nodiscard]])
#elif SHOW_ERROR == 3
    constexpr std::uint16_t a = a10::dsp::alpha_q15(60.0, 104.0);               // fc ≥ fs/2 → consteval 실패
    (void)a;
#elif SHOW_ERROR == 4
    struct Owner { Owner() {} ~Owner() {} int x = 0; };
    a10::static_vector<Owner, 4> v;                                              // 소형 static_vector 는 trivial T 만
    (void)v;
#elif SHOW_ERROR == 5
    NotABus nb;
    a10::app::Pipeline<NotABus> p(nb, a10::imu::Config{}, a10::app::kAlpha);    // 버스 concept 불만족
#endif
    return 0;
}
