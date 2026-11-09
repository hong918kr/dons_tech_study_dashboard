// CPP-M06 추가 연습 — 전부 PASS, exit 0
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>

using u32 = std::uint32_t;

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// ---- 연습 1: 다중 상속이면 vptr 가 기반 수만큼 (기반마다 부분 객체가 있다)
struct TxIf { virtual void tx(u32) = 0; protected: ~TxIf() = default; };
struct RxIf { virtual u32  rx() = 0;    protected: ~RxIf() = default; };
class Combo final : public TxIf, public RxIf {
public:
    void tx(u32 v) override { last_ = v; }
    u32  rx() override { return last_; }
private:
    u32 last_ = 0;
};
constexpr u32 mi_object_bytes(u32 ptr, u32 vptrs, u32 field_bytes, u32 field_align) {
    const u32 align = field_align > ptr ? field_align : ptr;
    const u32 start = (vptrs * ptr + field_align - 1u) / field_align * field_align;
    return (start + field_bytes + align - 1u) / align * align;
}

// ---- 연습 2: GCC 가 실제로 만든 가드 — vtable 슬롯(함수 포인터)을 비교
struct Dev;
using StatusFn = u32 (*)(const Dev*);
struct Dev { StatusFn status; u32 hz; };
static u32 uart_status(const Dev* d) { return d->hz ? 1u : 0u; }
static u32 spi_status(const Dev* d)  { return d->hz ? 2u : 0u; }
static inline u32 guarded_status(const Dev* d, unsigned& fast, unsigned& slow) {
    if (d->status == uart_status) { ++fast; return d->hz ? 1u : 0u; }   // 인라인된 복사본
    ++slow;
    return d->status(d);
}

// ---- 연습 3: 닫힌 집합이면 switch — vtable·vptr 없이 태그 1바이트
enum class Kind : std::uint8_t { Uart, Spi, I2c };
struct Tagged { Kind k; u32 hz; };
constexpr u32 status_switch(const Tagged& t) {
    switch (t.k) {
        case Kind::Uart: return t.hz ? 1u : 0u;
        case Kind::Spi:  return t.hz ? 2u : 0u;
        case Kind::I2c:  return t.hz ? 3u : 0u;
    }
    return 0;
}

// ---- 연습 4: vptr 비용의 분기점 — 인스턴스당 vptr vs 클래스당 정적 ops 포인터 1개
struct Budget { u32 flash; u32 ram; };
constexpr bool fits(Budget b, u32 flash_limit, u32 ram_limit) { return b.flash <= flash_limit && b.ram <= ram_limit; }
constexpr u32 vptr_ram(u32 ptr, u32 instances) { return ptr * instances; }
constexpr u32 shared_ops_ram(u32 ptr, u32 instances) { return instances ? ptr : 0u; }
constexpr u32 ram_saved(u32 ptr, u32 instances) { return vptr_ram(ptr, instances) - shared_ops_ram(ptr, instances); }

int main() {
    // 연습 1
    CHECK(sizeof(Combo) == mi_object_bytes(sizeof(void*), 2, 4, 4));
    CHECK(mi_object_bytes(4, 2, 4, 4) == 12);
    CHECK(mi_object_bytes(8, 2, 4, 4) == 24);
    CHECK(mi_object_bytes(4, 1, 1, 1) == 8);
    CHECK(mi_object_bytes(4, 2, 8, 8) == 16);
    {
        Combo c; TxIf& t = c; RxIf& r = c;
        t.tx(0xBEEF);
        CHECK(r.rx() == 0xBEEF);                                           // RxIf 쪽 호출: this 보정 thunk 경유
        CHECK(static_cast<const void*>(&t) != static_cast<const void*>(&r)); // 두 기반 부분객체 주소가 다르다
    }
    static_assert(std::is_polymorphic_v<Combo> && !std::has_virtual_destructor_v<Combo>);

    // 연습 2
    {
        Dev u{uart_status, 115200}, s{spi_status, 1000000}, off{uart_status, 0};
        unsigned fast = 0, slow = 0;
        CHECK(guarded_status(&u, fast, slow) == 1 && fast == 1 && slow == 0);
        CHECK(guarded_status(&s, fast, slow) == 2 && fast == 1 && slow == 1);
        CHECK(guarded_status(&off, fast, slow) == 0 && fast == 2 && slow == 1);
    }

    // 연습 3
    static constexpr struct { Kind k; u32 hz; u32 want; } cases[] = {
        {Kind::Uart, 1, 1}, {Kind::Spi, 1, 2}, {Kind::I2c, 1, 3},
        {Kind::Uart, 0, 0}, {Kind::Spi, 0, 0}, {Kind::I2c, 0, 0},
    };
    for (const auto& c : cases) CHECK(status_switch(Tagged{c.k, c.hz}) == c.want);
    static_assert(!std::is_polymorphic_v<Tagged>);
    static_assert(sizeof(Tagged) == 8);                                    // 태그 1 + 패딩 3 + hz 4 (vptr 없음)

    // 연습 4
    CHECK(fits(Budget{1024, 256}, 1024, 256));
    CHECK(!fits(Budget{1025, 256}, 1024, 256));
    CHECK(!fits(Budget{1024, 257}, 1024, 256));
    CHECK(ram_saved(4, 0) == 0);
    CHECK(ram_saved(4, 1) == 0);                                           // 1개면 이득 없음
    CHECK(ram_saved(4, 8) == 28);
    CHECK(vptr_ram(4, 8) == 32 && shared_ops_ram(4, 8) == 4);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
