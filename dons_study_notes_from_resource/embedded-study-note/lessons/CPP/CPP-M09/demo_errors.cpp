// CPP-M09 demo_errors — 이동 전용 핸들과 rvalue-only 큐의 컴파일 에러 확인
// ok:   c++ -std=c++20 -fsyntax-only demo_errors.cpp
// fail: c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   (2, 3 도 같은 방식)
#include <cstddef>
#include <cstdint>
#include <utility>

struct Handle {
    std::uint8_t idx = 0xFF;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& o) noexcept : idx(o.idx) { o.idx = 0xFF; }
    Handle& operator=(Handle&& o) noexcept { idx = o.idx; o.idx = 0xFF; return *this; }
};

template <class T>
struct Slot {
    T v{};
    void push(T&& x) { v = std::move(x); }
};

struct Frame { unsigned char bytes[256]{}; };

int main() {
    Handle a;
    Handle b(std::move(a));       // OK: 소유권 이전
    Slot<Frame> s;
    Frame f;
    s.push(std::move(f));         // OK: 비용을 보이게 적었다
#if SHOW_ERROR == 1
    Handle c(b);                  // 복사 금지: deleted
#elif SHOW_ERROR == 2
    s.push(f);                    // lvalue 는 T&& 에 묶이지 않는다: 몰래 복사 금지
#elif SHOW_ERROR == 3
    const Handle k;
    Handle m(std::move(k));       // const 객체는 이동할 수 없다: 복사도 deleted 이므로 에러 (Probe 는 조용히 복사됐다)
#endif
    (void)b;
}
