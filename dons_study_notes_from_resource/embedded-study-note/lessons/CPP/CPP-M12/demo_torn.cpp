// CPP-M12 demo: Delegate 2워드 쓰기가 찢어지면(torn write) 어떻게 되나  (의도적 UB — 관찰 전용)
//   32비트 MCU 에서 8바이트 구조체 대입은 STRD 또는 STR 2회. 그 사이 ISR 이 들어온 상황을 memcpy 두 번으로 재현한다.
//   c++ -std=c++20 -O1 -fno-exceptions -fno-rtti demo_torn.cpp -o t && ./t
#define main solution_main
#include "solution.cpp"
#undef main
#include <cstring>

struct Pump   { unsigned rpm  = 1000; void on(std::uint32_t) { std::printf("  Pump::on   this=%p rpm=%u\n", static_cast<void*>(this), rpm); } };
struct Heater { unsigned watt = 50;   void on(std::uint32_t) { std::printf("  Heater::on this=%p watt=%u\n", static_cast<void*>(this), watt); } };

int main() {
    Pump p; Heater h;
    TimerCb slot = TimerCb::bind<&Pump::on>(p);
    const TimerCb next = TimerCb::bind<&Heater::on>(h);

    std::printf("pump   @%p, heater @%p\n", static_cast<void*>(&p), static_cast<void*>(&h));
    std::printf("before update:\n"); slot(0);

    // word 0(문맥)만 새 값, word 1(스텁)은 옛 값인 찰나
    void* word[2];
    std::memcpy(word, &next, sizeof word);
    std::memcpy(reinterpret_cast<char*>(&slot), &word[0], sizeof(void*));
    std::printf("ISR fires between the two stores (ctx=Heater, stub=Pump):\n"); slot(0);

    std::memcpy(reinterpret_cast<char*>(&slot) + sizeof(void*), &word[1], sizeof(void*));
    std::printf("after update:\n"); slot(0);
    return 0;
}
