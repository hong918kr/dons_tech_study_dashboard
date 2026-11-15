/* CPP-A08 demo_errors.cpp — "컴파일이 안 돼야 하는 것" 을 실제 에러로 확인한다 (제품 헤더 사용)
 *   c++ -std=c++20 -fsyntax-only demo_errors.cpp                    # 에러 없음
 *   c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp     # 1..4 각각 */
#include "fake_tmp.hpp"
#include "sensor.hpp"

struct OnlyRead {   // write 가 없다
    BusErr write_read(uint8_t, uint8_t, std::span<uint8_t>) { return BusErr::Ok; }
};
class HalfFake : public II2c {   // write 를 override 하는 것을 잊었다
public:
    BusErr write_read(uint8_t, uint8_t, std::span<uint8_t>) override { return BusErr::Ok; }
};

int main() {
    FakeTmp dev;
    FakeBusV vbus{dev};
    TempSensor<II2c> ok{vbus};
    (void)ok;
#if SHOW_ERROR == 1
    OnlyRead r;
    TempSensor<OnlyRead> s{r};             // concept 불만족: 어느 요구가 빠졌는지 나온다
#elif SHOW_ERROR == 2
    II2c* p = &vbus;
    delete p;                              // protected 소멸자: 기반 포인터 delete 금지
#elif SHOW_ERROR == 3
    HalfFake h;                            // 순수 가상 하나 미구현 → 추상 클래스
#elif SHOW_ERROR == 4
    struct Spy : FakeBusV { using FakeBusV::FakeBusV; };   // final 을 상속할 수 없다
#endif
}
