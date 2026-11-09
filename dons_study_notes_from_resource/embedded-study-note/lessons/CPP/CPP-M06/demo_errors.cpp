// CPP-M06 demo_errors.cpp — 컴파일 에러를 눈으로 본다
//   c++ -std=c++20 -fno-exceptions -fno-rtti -c demo_errors.cpp                 → 성공 (SHOW_ERROR 없음)
//   c++ -std=c++20 -fno-exceptions -fno-rtti -c demo_errors.cpp -DSHOW_ERROR=N   → N 번 블록이 에러
struct ByteBus {
    virtual unsigned status() const = 0;
protected:
    constexpr ByteBus() = default;
    ~ByteBus() = default;
};
struct UartDrv final : ByteBus {
    unsigned status() const override { return 1; }
};
struct Open : ByteBus {
    unsigned status() const final { return 2; }
};

unsigned ok(ByteBus& b) { return b.status(); }

#if SHOW_ERROR == 1
void bad_delete(ByteBus* p) { delete p; }                    // protected 소멸자
#elif SHOW_ERROR == 2
struct Sub : UartDrv {};                                     // final 클래스 상속
#elif SHOW_ERROR == 3
struct Sub2 : Open { unsigned status() const override { return 3; } };  // final 함수 재정의
#elif SHOW_ERROR == 4
struct Bad : ByteBus { unsigned status() override { return 4; } };      // const 빠짐: override 가 잡아 준다
#elif SHOW_ERROR == 5
ByteBus make_bus() { return ByteBus{}; }                     // 추상 클래스는 값으로 못 만든다
#endif
