// CPP-M12 demo: Delegate 는 객체 수명을 모른다 — ASan 으로 dangling 호출 보기
//   c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address -fno-omit-frame-pointer demo_dangling.cpp -o d && ./d
//   반환으로 탈출하는 경우 컴파일러 경고:  c++ -std=c++20 -fsyntax-only -DSHOW_RETURN demo_dangling.cpp
#define main solution_main
#include "solution.cpp"
#undef main

#ifdef SHOW_RETURN
static TimerCb make_cb_bad() {
    Led led;
    return TimerCb::bind<&Led::on_tick>(led);     // 지역 객체 주소가 delegate 안에 숨어서 탈출
}
static Led* make_ptr_bad() {
    Led led;
    return &led;                                  // 비교용: 맨 포인터 반환은 경고가 나온다
}
#endif

int main() {
#ifdef SHOW_RETURN
    (void)make_cb_bad(); (void)make_ptr_bad();
#endif
    TimerCb cb;
    {
        Led led;
        cb = TimerCb::bind<&Led::on_tick>(led);
        cb(1);
        std::printf("in scope: toggles=%u\n", led.toggles);
    }   // led 수명 끝
    cb(2);   // use-after-scope
    std::printf("unreachable under ASan\n");
    return 0;
}
