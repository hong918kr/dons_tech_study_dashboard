// CPP-M07 추가 연습 — 한 번에 검증
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

// ---- 공통: 본문의 StaticInstance 를 줄인 판 ----
template <typename T>
class StaticInstance {
    alignas(T) std::byte storage_[sizeof(T)]{};
    T* ptr_ = nullptr;
public:
    constexpr StaticInstance() noexcept = default;
    StaticInstance(const StaticInstance&) = delete;
    StaticInstance& operator=(const StaticInstance&) = delete;
    template <typename... A> requires std::constructible_from<T, A...>
    [[nodiscard]] T* emplace(A&&... a) {
        if (ptr_) return nullptr;
        return ptr_ = ::new (static_cast<void*>(storage_)) T(std::forward<A>(a)...);
    }
    bool destroy() {
        if (!ptr_) return false;
        T* p = ptr_; ptr_ = nullptr; std::destroy_at(p); return true;
    }
    T* get() noexcept { return ptr_; }

    // 연습 2: 인자를 먼저 값으로 복사해 둔 뒤 소멸 → 재생성
    template <typename... A>
    T* replace(A&&... a) {
        std::tuple<std::decay_t<A>...> snapshot(std::forward<A>(a)...);
        destroy();
        return std::apply([this](auto&&... v) { return this->emplace(std::move(v)...); }, std::move(snapshot));
    }
};

static int g_ctors, g_dtors;
struct Counted {
    int value;
    explicit Counted(int v = 0) : value(v) { ++g_ctors; }
    Counted(const Counted& o) : value(o.value) { ++g_ctors; }
    ~Counted() { ++g_dtors; value = -1; }
};
static void reset_counts() { g_ctors = g_dtors = 0; }

// 연습 1: 스코프가 끝나면 destroy 하는 가드 (RAII 는 CPP-B06/B07 참고)
template <typename T>
class ScopedInstance {
    StaticInstance<T>& s_;
    T* p_;
public:
    template <typename... A>
    explicit ScopedInstance(StaticInstance<T>& s, A&&... a) : s_(s), p_(s.emplace(std::forward<A>(a)...)) {}
    ScopedInstance(const ScopedInstance&) = delete;
    ScopedInstance& operator=(const ScopedInstance&) = delete;
    ~ScopedInstance() { if (p_) s_.destroy(); }
    T* get() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
};

// 연습 3: 비트맵으로 관리하는 N칸 슬롯. 할당 알고리즘이 아니라 수명 관리가 주제다.
template <typename T, unsigned N>
class Slots {
    static_assert(N >= 1 && N <= 32);
    alignas(T) std::byte storage_[N * sizeof(T)]{};
    std::uint32_t used_ = 0;

    int index_of(const T* p) const {
        auto a = reinterpret_cast<std::uintptr_t>(p);
        auto b = reinterpret_cast<std::uintptr_t>(storage_);
        if (a < b || a >= b + N * sizeof(T)) return -1;
        if ((a - b) % sizeof(T) != 0) return -1;
        return static_cast<int>((a - b) / sizeof(T));
    }
public:
    constexpr Slots() noexcept = default;
    Slots(const Slots&) = delete;
    Slots& operator=(const Slots&) = delete;

    template <typename... A>
    [[nodiscard]] T* acquire(A&&... a) {
        for (unsigned i = 0; i < N; ++i) {
            if (used_ & (1u << i)) continue;
            T* p = ::new (static_cast<void*>(storage_ + i * sizeof(T))) T(std::forward<A>(a)...);
            used_ |= 1u << i;
            return p;
        }
        return nullptr;
    }
    bool release(T* p) {
        int i = index_of(p);
        if (i < 0 || !(used_ & (1u << i))) return false;
        used_ &= ~(1u << i);
        std::destroy_at(p);
        return true;
    }
    unsigned count() const { return static_cast<unsigned>(std::popcount(used_)); }
};

struct alignas(64) Desc64 {
    std::uint32_t w[4];
    explicit Desc64(std::uint32_t x) : w{x, 0, 0, 0} {}
};
static_assert(alignof(Slots<Desc64, 4>) == 64 && sizeof(Slots<Desc64, 4>) == 5 * 64);

// 연습 4: implicit-lifetime 헤더 해석 (복사 경로 / 제자리 경로)
struct FwHeader { std::uint32_t magic; std::uint16_t ver; std::uint16_t flags; std::uint32_t len; };
static_assert(std::is_trivially_copyable_v<FwHeader> && sizeof(FwHeader) == 12);
constexpr std::uint32_t FW_MAGIC = 0x4D37'4657u;

// 복사 경로: 정렬·길이 조건이 없다. 실패하면 false.
bool load_header(const std::byte* in, std::size_t n, FwHeader& out) {
    if (n < sizeof(FwHeader)) return false;
    std::memcpy(&out, in, sizeof(FwHeader));
    return out.magic == FW_MAGIC;
}
// 제자리 경로: 정렬된 수신 버퍼에 memcpy 로 채운 뒤 헤더로 본다.
const FwHeader* view_header(std::byte* rx, std::size_t cap, const void* image, std::size_t n) {
    if (cap < n || n < sizeof(FwHeader)) return nullptr;
    if (reinterpret_cast<std::uintptr_t>(rx) % alignof(FwHeader) != 0) return nullptr;
    std::memcpy(rx, image, n);                      // 목적지에 객체를 암시적으로 만든다
    const FwHeader* h = std::launder(reinterpret_cast<const FwHeader*>(rx));
    return h->magic == FW_MAGIC ? h : nullptr;
}

// 연습 5: construct_at/destroy_at 는 상수 평가에서 쓸 수 있고, 컴파일러가 수명을 검사한다
struct CtCounted {
    int v;
    constexpr explicit CtCounted(int x) : v(x) {}
    constexpr ~CtCounted() { v = -1; }
};
constexpr int ct_lifecycle() {
    std::allocator<CtCounted> a;
    CtCounted* p = a.allocate(1);
    std::construct_at(p, 41);
    int first = p->v;
    std::destroy_at(p);
    std::construct_at(p, first + 1);                // 같은 저장소를 재사용
    int second = p->v;
    std::destroy_at(p);
    a.deallocate(p, 1);
    return second;
}
static_assert(ct_lifecycle() == 42);

template <bool Twice>
constexpr int ct_destroy() {
    std::allocator<CtCounted> a;
    CtCounted* p = a.allocate(1);
    std::construct_at(p, 1);
    std::destroy_at(p);
    if (Twice) std::destroy_at(p);                  // 이미 끝난 수명의 객체를 다시 소멸
    a.deallocate(p, 1);
    return 1;
}
template <bool Twice>
concept destroy_is_constant = requires { typename std::bool_constant<(ct_destroy<Twice>(), true)>; };
static_assert(destroy_is_constant<false>);
static_assert(!destroy_is_constant<true>);

// ---------------- 테스트 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static StaticInstance<Counted> g_inst;

static int early_return(bool bail) {
    ScopedInstance<Counted> guard(g_inst, 5);
    if (!guard) return -1;
    if (bail) return guard.get()->value;
    return guard.get()->value + 1;
}

int main() {
    // 연습 1
    reset_counts();
    CHECK(early_return(true) == 5 && g_ctors == 1 && g_dtors == 1 && g_inst.get() == nullptr);
    CHECK(early_return(false) == 6 && g_ctors == 2 && g_dtors == 2 && g_inst.get() == nullptr);
    {
        ScopedInstance<Counted> outer(g_inst, 1);
        ScopedInstance<Counted> inner(g_inst, 2);          // 이미 차 있으므로 실패, 소멸도 하지 않아야 한다
        CHECK(outer && !inner && g_inst.get()->value == 1);
    }
    CHECK(g_ctors == 3 && g_dtors == 3 && g_inst.get() == nullptr);
    static_assert(!std::is_copy_constructible_v<ScopedInstance<Counted>>);

    // 연습 2
    reset_counts();
    static StaticInstance<Counted> r;
    CHECK(r.emplace(10) != nullptr);
    Counted* p = r.replace(r.get()->value);                 // lvalue 로 자기 내부를 가리키는 인자
    CHECK(p && p->value == 10 && r.get() == p);
    CHECK(g_ctors == 2 && g_dtors == 1);
    p = r.replace(r.get()->value + 5);
    CHECK(p && p->value == 15 && g_ctors == 3 && g_dtors == 2);
    CHECK(r.destroy() && g_dtors == 3);

    // 연습 3
    reset_counts();
    static Slots<Counted, 3> s;
    Counted* a = s.acquire(1), *b = s.acquire(2), *c = s.acquire(3);
    CHECK(a && b && c && s.count() == 3);
    CHECK(a != b && b != c);
    CHECK(s.acquire(4) == nullptr && g_ctors == 3);         // 가득 참: 생성자가 불리면 안 된다
    CHECK(s.release(b) && s.count() == 2 && g_dtors == 1);
    CHECK(!s.release(b) && g_dtors == 1);                   // 이중 해제 거부
    Counted foreign(9);
    CHECK(!s.release(&foreign));                            // 슬롯 밖 포인터 거부
    Counted* d = s.acquire(4);
    CHECK(d == b && d->value == 4);                         // 빈 칸 재사용
    CHECK(s.release(a) && s.release(c) && s.release(d));
    CHECK(g_dtors == 4 && s.count() == 0);

    static Slots<Desc64, 4> ds;
    Desc64* d0 = ds.acquire(1u), *d1 = ds.acquire(2u);
    CHECK(d0 && d1 && reinterpret_cast<std::uintptr_t>(d0) % 64 == 0 && reinterpret_cast<std::uintptr_t>(d1) % 64 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(d1) - reinterpret_cast<std::uintptr_t>(d0) == 64);
    CHECK(ds.release(d0) && ds.release(d1));

    // 연습 4
    const FwHeader img{FW_MAGIC, 3, 0x10, 4096};
    alignas(FwHeader) static std::byte rx[32];
    const FwHeader* h = view_header(rx, sizeof rx, &img, sizeof img);
    CHECK(h && h->ver == 3 && h->flags == 0x10 && h->len == 4096);
    CHECK(view_header(rx + 1, sizeof rx - 1, &img, sizeof img) == nullptr);     // 정렬 위반 거부
    CHECK(view_header(rx, 4, &img, sizeof img) == nullptr);                     // 용량 부족 거부
    FwHeader bad = img; bad.magic = 0;
    CHECK(view_header(rx, sizeof rx, &bad, sizeof bad) == nullptr);             // magic 거부

    alignas(4) std::byte unaligned[20]{};
    std::memcpy(unaligned + 1, &img, sizeof img);
    FwHeader out{};
    CHECK(load_header(unaligned + 1, sizeof img, out) && out.len == 4096);      // 복사 경로는 비정렬도 OK
    CHECK(!load_header(unaligned + 1, 4, out));

    // 연습 5 는 전부 static_assert (컴파일되면 통과). 런타임에서 값만 한 번 더 본다.
    CHECK(ct_lifecycle() == 42);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
