// CPP-M07 demo_codegen — Cortex-M4 용 코드 비교 (asm_m4.sh 로 컴파일). 실행하지 않는다.
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

struct Drv { std::uint32_t baud; std::uint32_t rx; explicit Drv(std::uint32_t b) : baud(b), rx(0) {} };

struct Ptr {                                       // 포인터 저장 판
    alignas(Drv) std::byte s[sizeof(Drv)]{};
    Drv* p = nullptr;
};
struct Launder {                                   // bool 판
    alignas(Drv) std::byte s[sizeof(Drv)]{};
    bool alive = false;
};

extern "C" {
// 1) get
Drv* get_ptr(Ptr* x)         { return x->p; }
Drv* get_launder(Launder* x) { return x->alive ? std::launder(reinterpret_cast<Drv*>(x->s)) : nullptr; }

// 2) launder 자체의 비용: 같은 읽기
std::uint32_t read_cast(Launder* x)    { return reinterpret_cast<Drv*>(x->s)->baud; }
std::uint32_t read_launder(Launder* x) { return std::launder(reinterpret_cast<Drv*>(x->s))->baud; }

// 3) 생성 비교: C 스타일 init / placement new / 포인터 저장 판 / bool 판
struct CUart { std::uint32_t baud; std::uint32_t rx; };
void c_init(CUart* u, std::uint32_t b) { u->baud = b; u->rx = 0; }
void place_only(void* buf, std::uint32_t b) { ::new (buf) Drv(b); }
Drv* emplace_ptr(Ptr* x, std::uint32_t b) {
    if (x->p) return nullptr;
    return x->p = ::new (static_cast<void*>(x->s)) Drv(b);
}
Drv* emplace_launder(Launder* x, std::uint32_t b) {
    if (x->alive) return nullptr;
    Drv* d = ::new (static_cast<void*>(x->s)) Drv(b);
    x->alive = true;
    return d;
}

// 4) ISR 가 포인터를 읽는 비용: 컴파일러 펜스 vs acquire
Drv* isr_signal_fence(std::atomic<Drv*>* a) {
    Drv* p = a->load(std::memory_order_relaxed);
    std::atomic_signal_fence(std::memory_order_acquire);
    return p;
}
Drv* isr_acquire(std::atomic<Drv*>* a) { return a->load(std::memory_order_acquire); }
}
