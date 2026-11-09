// CPP-M08 demo_errors.cpp — 컴파일 에러 실물 보기
//   c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   (1~5)
#include <cstddef>
#include <new>

struct Msg {
    int v = 0;
    static void* operator new(std::size_t) noexcept { return nullptr; }
    static void  operator delete(void*) noexcept {}
    static void* operator new[](std::size_t) = delete;
    static void  operator delete[](void*) = delete;
};
struct Sealed final {
    int v = 0;
    static void* operator new(std::size_t) noexcept { return nullptr; }
    static void  operator delete(void*) noexcept {}
};

alignas(Msg) static unsigned char buf[sizeof(Msg)];

int main() {
#if SHOW_ERROR == 1
    Msg* a = new Msg[2];                  // 배열 new 는 = delete
    (void)a;
#elif SHOW_ERROR == 2
    Msg* b = new (buf) Msg;               // 클래스 operator new 가 전역 placement 를 가린다
    (void)b;
#elif SHOW_ERROR == 3
    Msg* c = new (std::nothrow) Msg;      // 전역 nothrow 판도 가려진다
    (void)c;
#elif SHOW_ERROR == 4
    struct Bigger : Sealed { int more[8]; };   // final → 파생 금지
#elif SHOW_ERROR == 5
    struct Wrong { static void* operator new(std::size_t) noexcept; };
    // 선언은 noexcept, 정의는 아니면? (아래 Wrong2 참고)
#endif
    Msg* ok = ::new (buf) Msg;            // :: 를 붙이면 전역 placement 를 직접 지명
    ok->~Msg();
    return 0;
}
#if SHOW_ERROR == 5
struct Wrong2 { static void* operator new(std::size_t) noexcept; };
void* Wrong2::operator new(std::size_t) { return nullptr; }   // 예외 명세 불일치
#endif
