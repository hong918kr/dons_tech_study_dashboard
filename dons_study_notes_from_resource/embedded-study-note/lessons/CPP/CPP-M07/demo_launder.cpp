// CPP-M07 demo_launder — 저장소를 재사용한 뒤의 낡은 포인터
//   c++ -std=c++20 -O2 -fno-rtti -fno-exceptions demo_launder.cpp -o /tmp/ld && /tmp/ld
//   c++ -std=c++20 -O2 -fno-rtti -fno-exceptions -fstrict-vtable-pointers demo_launder.cpp -o /tmp/ld2 && /tmp/ld2
// stale->id() 는 UB 이다. "어떻게 망가지는지" 관찰하려는 코드이며 제품 코드에 쓰면 안 된다.
#include <cstddef>
#include <cstdio>
#include <new>

struct Base { virtual int id() const = 0; virtual ~Base() = default; };
struct A : Base { int id() const override { return 1; } };
struct B : Base { int id() const override { return 2; } };

alignas(A) static std::byte raw[sizeof(A)];

__attribute__((noinline)) static Base* make_a() { return ::new (static_cast<void*>(raw)) A; }

__attribute__((noinline)) static int observe(Base* stale) {
    int before = stale->id();
    Base* fresh = ::new (static_cast<void*>(raw)) B;       // A 의 수명 종료, B 시작 (같은 주소)
    int via_stale   = stale->id();                         // UB: stale 은 B 를 가리키지 않는다
    int via_fresh   = fresh->id();                         // 정상: new 가 돌려준 포인터
    int via_launder = std::launder(reinterpret_cast<Base*>(raw))->id();
    std::printf("before=%d stale=%d fresh=%d launder=%d same_addr=%d\n",
                before, via_stale, via_fresh, via_launder, static_cast<void*>(stale) == static_cast<void*>(fresh));
    return via_stale;
}

int main() { return observe(make_a()) == 0 ? 1 : 0; }
