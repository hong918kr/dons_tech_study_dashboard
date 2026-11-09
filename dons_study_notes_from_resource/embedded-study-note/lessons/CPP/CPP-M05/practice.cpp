// CPP-M05 추가 연습 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>
#include <variant>

using u8  = std::uint8_t;
using u32 = std::uint32_t;

static int g_passed = 0, g_total = 0;
#define CHECK(expr)                                                     \
    do {                                                                \
        ++g_total;                                                      \
        if (expr) { ++g_passed; std::printf("PASS  %s\n", #expr); }     \
        else      { std::printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } \
    } while (0)

// 본문과 같은 뼈대 (자기완결)
template <class Derived>
class UartBase {
public:
    bool put(u8 b) { return self().put_impl(b); }
    std::size_t write(std::span<const u8> d) {
        std::size_t n = 0;
        for (u8 b : d) { if (!put(b)) break; ++n; }
        return n;
    }
private:
    friend Derived;
    UartBase() = default;
    Derived& self() { return static_cast<Derived&>(*this); }
};

class MockA : public UartBase<MockA> {
public:
    u8 tx[32]{}; std::size_t n = 0;
private:
    friend UartBase<MockA>;
    bool put_impl(u8 b) { if (n == sizeof tx) return false; tx[n++] = b; return true; }
};
class MockB : public UartBase<MockB> {      // 항상 대문자로 바꿔 보내는 다른 구현
public:
    u8 tx[32]{}; std::size_t n = 0;
private:
    friend UartBase<MockB>;
    bool put_impl(u8 b) {
        if (n == sizeof tx) return false;
        tx[n++] = (b >= 'a' && b <= 'z') ? static_cast<u8>(b - 32) : b;
        return true;
    }
};
struct WrongDerived : UartBase<MockA> {};

// ---- 연습 1: concept 로 "제대로 된 CRTP 파생인가" 검사 ----
template <class T>
concept CrtpUart = std::derived_from<T, UartBase<T>>;

template <CrtpUart U>
std::size_t greet(U& u) { const u8 d[2] = {'o', 'k'}; return u.write(d); }

static_assert(CrtpUart<MockA>);
static_assert(!CrtpUart<WrongDerived>);     // UartBase<WrongDerived> 가 아니라 UartBase<MockA> 를 상속
static_assert(!CrtpUart<int>);

// ---- 연습 2: CRTP 믹스인 두 개 — static_cast 가 포인터를 보정한다 ----
template <class D>
class Stats {
public:
    unsigned bytes = 0;
    void add(unsigned k) { bytes += k; }
};
template <class D>
class Tagged {
public:
    u32 tag = 0;
    D& self() { return static_cast<D&>(*this); }       // reinterpret_cast 면 여기서 틀린 주소
};
class Mixed : public Stats<Mixed>, public Tagged<Mixed> {
public:
    u32 value = 0x1234;
};

// ---- 연습 3: std::variant + std::visit 로 이질 배열 ----
using Port = std::variant<MockA, MockB>;
inline bool port_put(Port& p, u8 b) {
    return std::visit([b](auto& u) { return u.put(b); }, p);
}

// ---- 연습 4: virtual 어댑터 — 경계에서만 virtual ----
class IUart {
public:
    virtual bool put(u8 b) = 0;
protected:
    ~IUart() = default;     // 인터페이스로 delete 하지 않는다 (heap 없음)
};
template <class D>
class UartAdapter final : public IUart {
public:
    explicit UartAdapter(D& d) : d_(d) {}
    bool put(u8 b) override { return d_.put(b); }
private:
    D& d_;
};

// ---- 연습 5: 공통 코드 끌어올리기 — 템플릿이 아닌 프레임 코어 ----
using PutFn = bool (*)(void*, u8);
inline bool frame_core(void* ctx, PutFn put, std::span<const u8> payload) {
    if (payload.size() > 255) return false;
    u8 x = static_cast<u8>(payload.size());
    if (!put(ctx, 0x02) || !put(ctx, x)) return false;
    for (u8 b : payload) {
        if (!put(ctx, b)) return false;
        x = static_cast<u8>(x ^ b);
    }
    return put(ctx, x);
}
template <class D>
bool send_frame_thin(UartBase<D>& u, std::span<const u8> payload) {
    return frame_core(&u, [](void* p, u8 b) { return static_cast<UartBase<D>*>(p)->put(b); }, payload);
}

int main() {
    {   // 연습 1
        MockA a;
        CHECK(greet(a) == 2);
        CHECK(a.n == 2 && a.tx[0] == 'o');
    }
    {   // 연습 2: 두 번째 비어 있지 않은 기반은 오프셋이 0 이 아니다
        Mixed m;
        Tagged<Mixed>* t = &m;
        CHECK(static_cast<void*>(t) != static_cast<void*>(&m));
        CHECK(&t->self() == &m);
        CHECK(t->self().value == 0x1234u);
        CHECK(static_cast<Mixed*>(static_cast<void*>(t)) != &m);   // void* 경유 = 보정 없음 → 주소가 다르다 (역참조하면 UB)
        m.add(3);
        CHECK(m.bytes == 3u);
        CHECK(sizeof(Mixed) == 3 * sizeof(u32));
    }
    {   // 연습 3
        Port ports[2] = {Port{std::in_place_type<MockA>}, Port{std::in_place_type<MockB>}};
        for (Port& p : ports) CHECK(port_put(p, 'q'));
        CHECK(std::get<MockA>(ports[0]).tx[0] == 'q');
        CHECK(std::get<MockB>(ports[1]).tx[0] == 'Q');
        CHECK(sizeof(Port) > sizeof(MockA));        // 판별자(discriminator)가 추가된다
    }
    {   // 연습 4: 서로 다른 어댑터를 IUart* 배열에
        MockA a; MockB b;
        UartAdapter<MockA> wa(a);
        UartAdapter<MockB> wb(b);
        IUart* table[2] = {&wa, &wb};
        for (IUart* p : table) CHECK(p->put('x'));
        CHECK(a.tx[0] == 'x' && b.tx[0] == 'X');
    }
    {   // 연습 5
        MockA a;
        const u8 p[2] = {0x10, 0x20};
        const u8 want[5] = {0x02, 0x02, 0x10, 0x20, 0x32};
        CHECK(send_frame_thin(a, p));
        CHECK(a.n == 5 && std::memcmp(a.tx, want, 5) == 0);
        MockB b;
        u8 big[256] = {};
        CHECK(!send_frame_thin(b, big));
        CHECK(b.n == 0);
    }
    std::printf("%d/%d passed\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
