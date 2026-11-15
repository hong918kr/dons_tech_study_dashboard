/* CPP-A03 demo_pure_host.cpp — 순수 가상 함수가 "실제로" 불리는 경로 (호스트, libc++abi 의 __cxa_pure_virtual)
 *   c++ -std=c++20 -O0 -fno-exceptions -fno-rtti demo_pure_host.cpp -o pure && ./pure ; echo "exit=$?"
 *   -DMY_STUB: 내 __cxa_pure_virtual 를 정의 → 정적 링커가 libc++abi 대신 이걸 묶는지 본다 */
#include <cstdio>
#include <cstdlib>
#ifdef MY_STUB
extern "C" [[noreturn]] void __cxa_pure_virtual() {
    std::fprintf(stderr, "fw-style trap: pure virtual called (lr=%p)\n", __builtin_return_address(0));
    std::_Exit(42);
}
#endif
struct Sensor {
    Sensor() { start(); }                     /* 생성 중엔 동적 타입이 Sensor → 슬롯은 __cxa_pure_virtual */
    void start() { std::printf("start: %d\n", sample()); }   /* 비가상 함수를 거쳐 간접 호출 ([class.abstract]p6: UB) */
    virtual int sample() = 0;
    virtual ~Sensor() = default;
};
struct Temp final : Sensor { int sample() override { return 25; } };
int main() { Temp t; std::printf("never: %d\n", t.sample()); }
