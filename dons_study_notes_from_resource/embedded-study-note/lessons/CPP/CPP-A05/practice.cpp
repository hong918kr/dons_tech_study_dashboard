/* CPP-A05 추가 연습 — 한 번에 검증
 *   연습 1 best_vector (trivial → thin, 아니면 fat)   연습 2 erase_if: 한 번 훑는 바이트 코어 + 함수 포인터 술어
 *   연습 3 원소 타입 분류 (memcpy+바이트 뷰 / memcpy / ops / 거부)   연습 4 CAN 송신 대기열 우선순위 삽입 (N 무관)
 * fat / thin 은 solution.cpp 와 같은 코드 (vec_ref 에 parts() 하나 추가).
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
 */
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

namespace fat {
template <std::copyable T, std::size_t N>
class static_vector {
    static_assert(N <= 0xFFFF, "capacity must fit uint16_t");
public:
    static_vector() noexcept = default;
    static_vector(const static_vector&) = delete;
    static_vector& operator=(const static_vector&) = delete;
    ~static_vector() requires std::is_trivially_destructible_v<T> = default;
    ~static_vector() { clear(); }

    std::size_t size() const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return N; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == N; }

    bool push_back(const T& v) noexcept {
        if (size_ >= N) return false;
        std::construct_at(slot(size_), v);
        ++size_;
        return true;
    }
    bool pop_back() noexcept {
        if (size_ == 0) return false;
        std::destroy_at(live(--size_));
        return true;
    }
    bool insert(std::size_t i, const T& v) noexcept {
        if (size_ >= N || i > size_) return false;
        if (i == size_) return push_back(v);
        T tmp(v);                                          // v 가 자기 원소여도 안전하게 먼저 복사
        std::construct_at(slot(size_), std::move(*live(size_ - 1)));
        for (std::size_t k = size_ - 1; k > i; --k) *live(k) = std::move(*live(k - 1));
        *live(i) = std::move(tmp);
        ++size_;
        return true;
    }
    bool erase(std::size_t i) noexcept {
        if (i >= size_) return false;
        for (std::size_t k = i; k + 1 < size_; ++k) *live(k) = std::move(*live(k + 1));
        std::destroy_at(live(--size_));
        return true;
    }
    void clear() noexcept { while (size_ != 0) std::destroy_at(live(--size_)); }
    T* at(std::size_t i) noexcept { return i < size_ ? live(i) : nullptr; }
    T& operator[](std::size_t i) noexcept { return *live(i); }

private:
    T* slot(std::size_t i) noexcept { return reinterpret_cast<T*>(buf_ + i * sizeof(T)); }
    T* live(std::size_t i) noexcept { return std::launder(slot(i)); }   // 살아 있는 칸만 (CPP-M07)
    std::uint16_t size_ = 0;
    alignas(T) std::byte buf_[N == 0 ? 1 : N * sizeof(T)];
};
}  // namespace fat

namespace thin {

// memcpy 코어를 써도 되는 T: 비트 복사가 곧 복사 생성이고, 소멸이 할 일 없음.
// is_trivially_copyable 만으로는 부족하다 — 복사 생성자가 delete 된 이동 전용 trivial 타입도 true 다.
template <class T>
concept MemcpyElem = std::is_trivially_copyable_v<T> && std::is_trivially_copy_constructible_v<T> &&
                     std::is_trivially_destructible_v<T>;

// 원소 크기 + 용량: 4 바이트 값 하나 → AAPCS 에서 레지스터 하나로 전달된다
struct geom { std::uint16_t esz; std::uint16_t cap; };

// 비-trivial T 용 "타입별 연산 표" — 타입마다 함수 5개 + 표 1개(.rodata)만 생긴다. N 과는 무관.
struct elem_ops {
    void (*copy_construct)(void* dst, const void* src);
    void (*move_construct)(void* dst, void* src);
    void (*copy_assign)(void* dst, const void* src);
    void (*move_assign)(void* dst, void* src);
    void (*destroy)(void* p);
};
template <class T> T* obj(void* p) { return std::launder(static_cast<T*>(p)); }
template <class T> const T* obj(const void* p) { return std::launder(static_cast<const T*>(p)); }
template <class T> void op_copy_construct(void* d, const void* s) { std::construct_at(static_cast<T*>(d), *obj<T>(s)); }
template <class T> void op_move_construct(void* d, void* s) { std::construct_at(static_cast<T*>(d), std::move(*obj<T>(s))); }
template <class T> void op_copy_assign(void* d, const void* s) { *obj<T>(d) = *obj<T>(s); }
template <class T> void op_move_assign(void* d, void* s) { *obj<T>(d) = std::move(*obj<T>(s)); }
template <class T> void op_destroy(void* p) { std::destroy_at(obj<T>(p)); }
template <class T>
inline constexpr elem_ops ops_for{&op_copy_construct<T>, &op_move_construct<T>, &op_copy_assign<T>,
                                  &op_move_assign<T>, &op_destroy<T>};

/* ---- 비템플릿 코어: T 도 N 도 모른다 → 프로그램 전체에 한 벌. 상태는 (size, buf) 를 인자로 받는다 ----
 * 객체 안에 버퍼 포인터를 저장하지 않는 이유는 5. 풀이 "전역 배치" 참고 (저장하면 전역 벡터 전체가 .data 로 간다). */
[[maybe_unused]] static std::byte* addr(std::byte* buf, std::size_t i, std::size_t esz) noexcept { return buf + i * esz; }
// src 가 살아 있는 구간 [from, size) 안의 원소인가 — 서로 다른 객체의 포인터에 내장 < 는 unspecified, std::less 는 전순서
[[maybe_unused]] static bool in_range(const void* src, std::byte* buf, std::size_t from, std::size_t size, std::size_t esz) noexcept {
    const std::byte* s = static_cast<const std::byte*>(src);
    std::less<const std::byte*> lt;
    return !lt(s, addr(buf, from, esz)) && lt(s, addr(buf, size, esz));
}

// ---- Part A: memcpy 코어 (MemcpyElem 전용) ----
bool push_raw(std::uint16_t& size, std::byte* buf, geom g, const void* src) noexcept {
    if (size >= g.cap) return false;
    std::memcpy(addr(buf, size, g.esz), src, g.esz);
    ++size;
    return true;
}
bool pop_raw(std::uint16_t& size) noexcept {
    if (size == 0) return false;
    --size;
    return true;
}
bool insert_raw(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src) noexcept {
    if (size >= g.cap || i > size) return false;
    const std::byte* s = static_cast<const std::byte*>(src);
    if (in_range(s, buf, i, size, g.esz)) s += g.esz;
    std::byte* p = addr(buf, i, g.esz);
    std::memmove(p + g.esz, p, (size - i) * g.esz);
    std::memcpy(p, s, g.esz);
    ++size;
    return true;
}
bool erase_raw(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i) noexcept {
    if (i >= size) return false;
    std::byte* p = addr(buf, i, esz);
    std::memmove(p, p + esz, (size - i - 1) * esz);
    --size;
    return true;
}
void* at_raw(std::uint16_t size, std::byte* buf, std::size_t esz, std::size_t i) noexcept {
    return i < size ? addr(buf, i, esz) : nullptr;
}

// ---- Part B: ops 표 코어 (비-trivial T). 원소 하나하나를 표의 함수 포인터로 생성/대입/소멸 ----
bool push_ops(std::uint16_t& size, std::byte* buf, geom g, const void* src, const elem_ops& op) noexcept {
    if (size >= g.cap) return false;
    op.copy_construct(addr(buf, size, g.esz), src);
    ++size;
    return true;
}
bool pop_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    if (size == 0) return false;
    op.destroy(addr(buf, --size, esz));
    return true;
}
bool insert_ops(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src, const elem_ops& op) noexcept {
    if (size >= g.cap || i > size) return false;
    if (i == size) return push_ops(size, buf, g, src, op);
    const std::byte* s = static_cast<const std::byte*>(src);
    if (in_range(s, buf, i, size, g.esz)) s += g.esz;
    op.move_construct(addr(buf, size, g.esz), addr(buf, size - 1, g.esz));
    for (std::size_t k = size - 1; k > i; --k) op.move_assign(addr(buf, k, g.esz), addr(buf, k - 1, g.esz));
    op.copy_assign(addr(buf, i, g.esz), s);
    ++size;
    return true;
}
bool erase_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i, const elem_ops& op) noexcept {
    if (i >= size) return false;
    for (std::size_t k = i; k + 1 < size; ++k) op.move_assign(addr(buf, k, esz), addr(buf, k + 1, esz));
    op.destroy(addr(buf, --size, esz));
    return true;
}
void clear_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    while (size != 0) op.destroy(addr(buf, --size, esz));
}

// ---- 얇은 타입 래퍼 (주어짐) ----
// 래퍼는 "반드시" 인라인돼야 N 이 코드를 안 만든다. GCC -Os 는 호출처가 여럿이면 한 줄짜리 forwarder 도
// static_vector<T,N>::push_back 같은 (T,N) 별 함수로 빼 버렸다 (실측) → always_inline 으로 못박는다.
#define THIN_INLINE [[gnu::always_inline]]
// vec_ref<T>: N 을 지운 뷰 (std::span 처럼 값으로 넘긴다, 3 워드). 모든 메서드가 한 줄 인라인 → 코어 호출 하나.
// std::copyable: 복사/대입 못 하는 T 는 여기서 한 줄 에러로 (ops 표 깊은 곳의 에러 대신)
template <std::copyable T>
class vec_ref {
    static_assert(sizeof(T) <= 0xFFFF);
    static constexpr bool kRaw = MemcpyElem<T>;
public:
    THIN_INLINE vec_ref(std::uint16_t& size, std::byte* buf, std::uint16_t cap) noexcept : size_(&size), buf_(buf), cap_(cap) {}
    std::size_t size() const noexcept { return *size_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool empty() const noexcept { return *size_ == 0; }
    bool full() const noexcept { return *size_ == cap_; }

    THIN_INLINE bool push_back(const T& v) noexcept {
        if constexpr (kRaw) return push_raw(*size_, buf_, g(), &v);
        else return push_ops(*size_, buf_, g(), &v, ops_for<T>);
    }
    THIN_INLINE bool pop_back() noexcept {
        if constexpr (kRaw) return pop_raw(*size_);
        else return pop_ops(*size_, buf_, sizeof(T), ops_for<T>);
    }
    THIN_INLINE bool insert(std::size_t i, const T& v) noexcept {
        if constexpr (kRaw) return insert_raw(*size_, buf_, g(), i, &v);
        else return insert_ops(*size_, buf_, g(), i, &v, ops_for<T>);
    }
    THIN_INLINE bool erase(std::size_t i) noexcept {
        if constexpr (kRaw) return erase_raw(*size_, buf_, sizeof(T), i);
        else return erase_ops(*size_, buf_, sizeof(T), i, ops_for<T>);
    }
    THIN_INLINE void clear() noexcept {
        if constexpr (kRaw) *size_ = 0;
        else clear_ops(*size_, buf_, sizeof(T), ops_for<T>);
    }
    THIN_INLINE T* at(std::size_t i) const noexcept {
        void* p = at_raw(*size_, buf_, sizeof(T), i);
        return p != nullptr ? std::launder(static_cast<T*>(p)) : nullptr;
    }
    THIN_INLINE T& operator[](std::size_t i) const noexcept { return *std::launder(reinterpret_cast<T*>(buf_ + i * sizeof(T))); }
    // 바이트 그대로 CRC/DMA/전송: 패딩이 없어야(같은 값 = 같은 바이트) 의미가 있다
    std::span<const std::byte> bytes() const noexcept
        requires(kRaw && std::has_unique_object_representations_v<T>) {
        return {buf_, *size_ * sizeof(T)};
    }
    // (연습 2 용으로 추가) 코어 함수에 넘길 원재료
    struct parts_t { std::uint16_t* size; std::byte* buf; std::uint16_t cap; };
    parts_t parts() const noexcept { return {size_, buf_, cap_}; }
private:
    geom g() const noexcept { return {static_cast<std::uint16_t>(sizeof(T)), cap_}; }
    std::uint16_t* size_;
    std::byte* buf_;
    std::uint16_t cap_;
};

// static_vector<T,N>: 레이아웃은 fat 과 똑같이 {size, 저장소} 뿐. 메서드는 ref() 로 넘기는 한 줄짜리.
template <std::copyable T, std::size_t N>
class static_vector {
    static_assert(N <= 0xFFFF, "capacity must fit uint16_t");
public:
    static_vector() noexcept = default;
    static_vector(const static_vector&) = delete;
    static_vector& operator=(const static_vector&) = delete;
    ~static_vector() requires MemcpyElem<T> = default;
    ~static_vector() { ref().clear(); }

    THIN_INLINE vec_ref<T> ref() noexcept { return {size_, storage_, static_cast<std::uint16_t>(N)}; }
    operator vec_ref<T>() noexcept { return ref(); }          // 배열 → span 처럼 암시적 변환

    std::size_t size() const noexcept { return size_; }
    static constexpr std::size_t capacity() noexcept { return N; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == N; }
    THIN_INLINE bool push_back(const T& v) noexcept { return ref().push_back(v); }
    THIN_INLINE bool pop_back() noexcept { return ref().pop_back(); }
    THIN_INLINE bool insert(std::size_t i, const T& v) noexcept { return ref().insert(i, v); }
    THIN_INLINE bool erase(std::size_t i) noexcept { return ref().erase(i); }
    THIN_INLINE void clear() noexcept { ref().clear(); }
    THIN_INLINE T* at(std::size_t i) noexcept { return ref().at(i); }
    THIN_INLINE T& operator[](std::size_t i) noexcept { return ref()[i]; }
    std::span<const std::byte> bytes() noexcept requires requires(vec_ref<T> r) { r.bytes(); } { return ref().bytes(); }
private:
    std::uint16_t size_ = 0;
    alignas(T) std::byte storage_[N == 0 ? 1 : N * sizeof(T)];
};
}  // namespace thin

/* ================= 테스트용 타입 ================= */
struct CanFrame {
    std::uint32_t id;
    std::uint8_t dlc;
    std::uint8_t pad[3];
    std::uint8_t data[8];
    bool operator==(const CanFrame&) const = default;
};
struct Counted {                                     // 비-trivial (복사 시 카운트)
    std::uint32_t v;
    static inline int copies = 0;
    explicit Counted(std::uint32_t x = 0) : v(x) {}
    Counted(const Counted& o) : v(o.v) { ++copies; }
    Counted& operator=(const Counted& o) { v = o.v; ++copies; return *this; }
    ~Counted() {}
};
struct VolM { volatile std::uint32_t v; };
struct ConstM { const std::uint32_t v; };
struct FloatM { float f; };
struct PadM { std::uint8_t a; std::uint32_t b; };
struct Bits2 { std::uint32_t a : 3; };

/* ================= 연습 1 — best_vector: trivial 이면 thin, 아니면 fat 그대로 ================= */
template <class T, std::size_t N>
using best_vector = std::conditional_t<thin::MemcpyElem<T>, thin::static_vector<T, N>, fat::static_vector<T, N>>;

/* ================= 연습 2 — erase_if: 한 번 훑는 바이트 코어 + 얇은 템플릿 술어 ================= */
static std::size_t g_moved_bytes = 0;                // 원소 이동량 계측 (memmove/memcpy 바이트 합)
static void counted_move(std::byte* d, const std::byte* s, std::size_t n) { std::memmove(d, s, n); g_moved_bytes += n; }

// 코어: 술어는 (원소 주소, 문맥) → bool 인 함수 포인터. 남길 원소만 앞으로 한 번씩 당긴다 (안정, O(n)).
std::size_t erase_if_raw(std::uint16_t& size, std::byte* buf, std::size_t esz,
                         bool (*pred)(const void* elem, void* ctx), void* ctx) noexcept {
    std::size_t w = 0;
    for (std::size_t r = 0; r < size; ++r) {
        std::byte* e = buf + r * esz;
        if (pred(e, ctx)) continue;
        if (w != r) counted_move(buf + w * esz, e, esz);
        ++w;
    }
    const std::size_t removed = size - w;
    size = static_cast<std::uint16_t>(w);
    return removed;
}
template <class T, class Pred>
    requires thin::MemcpyElem<T> && std::predicate<Pred&, const T&>
std::size_t erase_if(thin::vec_ref<T> v, Pred pred) {
    auto p = v.parts();
    // (T, Pred) 마다 생기는 건 이 캡처 없는 람다 하나 (몇 명령) — 루프는 코어에 한 벌
    return erase_if_raw(*p.size, p.buf, sizeof(T),
                        [](const void* e, void* ctx) { return (*static_cast<Pred*>(ctx))(*std::launder(static_cast<const T*>(e))); },
                        &pred);
}
// 비교용: 본문 drop_ids 식 (erase 를 반복) — 지울 때마다 뒤쪽 전체를 당긴다 (O(n²))
template <class T, class Pred>
std::size_t erase_loop(thin::vec_ref<T> v, Pred pred) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < v.size();) {
        if (pred(v[i])) { g_moved_bytes += (v.size() - i - 1) * sizeof(T); v.erase(i); ++n; }
        else ++i;
    }
    return n;
}

/* ================= 연습 3 — 원소 타입 분류: 어느 경로로 저장하나 ================= */
enum class Path { MemcpyBytes, MemcpyOnly, Ops, Rejected };
template <class T>
constexpr Path elem_path() {
    if constexpr (!std::copyable<T>) return Path::Rejected;
    else if constexpr (!thin::MemcpyElem<T>) return Path::Ops;
    else if constexpr (std::has_unique_object_representations_v<T>) return Path::MemcpyBytes;
    else return Path::MemcpyOnly;
}

/* ================= 연습 4 — CAN 송신 대기열: id 우선순위 정렬 삽입 (N 무관 함수 하나) ================= */
// id 가 작을수록 먼저. 같은 id 는 들어온 순서(FIFO). 꽉 차면 false.
bool insert_sorted(thin::vec_ref<CanFrame> q, const CanFrame& f) {
    std::size_t lo = 0, hi = q.size();
    while (lo < hi) {                                // upper_bound: f.id 보다 큰 첫 위치
        const std::size_t mid = lo + (hi - lo) / 2;
        if (q[mid].id <= f.id) lo = mid + 1; else hi = mid;
    }
    return q.insert(lo, f);
}

int main() {
    std::puts("--- 연습 1: best_vector");
    static_assert(std::is_same_v<best_vector<std::uint32_t, 8>, thin::static_vector<std::uint32_t, 8>>);
    static_assert(std::is_same_v<best_vector<CanFrame, 8>, thin::static_vector<CanFrame, 8>>);
    static_assert(std::is_same_v<best_vector<Counted, 8>, fat::static_vector<Counted, 8>>);
    {
        best_vector<Counted, 4> c;
        Counted a(7);
        Counted::copies = 0;
        CHECK(c.push_back(a) && c.push_back(a) && c.insert(0, Counted(9)));
        CHECK(c.size() == 3 && c.at(0) && c.at(0)->v == 9 && Counted::copies >= 3);
        best_vector<std::uint16_t, 4> u;
        CHECK(u.push_back(1) && u.insert(0, 2) && u.at(0) && *u.at(0) == 2 && !u.erase(5));
    }

    std::puts("--- 연습 2: erase_if (O(n)) vs erase 반복 (O(n^2))");
    {
        thin::static_vector<CanFrame, 64> a, b;
        std::uint32_t s = 0xACE1u;
        for (int k = 0; k < 64; ++k) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            const CanFrame f{s & 0x7FFu, 8, {}, {std::uint8_t(k)}};
            a.push_back(f); b.push_back(f);
        }
        auto odd = [](const CanFrame& f) { return (f.id & 1u) != 0; };
        g_moved_bytes = 0;
        const std::size_t na = erase_if(a.ref(), odd);
        const std::size_t moved_fast = g_moved_bytes;
        g_moved_bytes = 0;
        const std::size_t nb = erase_loop(b.ref(), odd);
        const std::size_t moved_slow = g_moved_bytes;
        bool same = na == nb && a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = *a.at(i) == *b.at(i);
        CHECK(same && na > 20 && a.size() + na == 64);
        std::printf("  removed %zu / 64, moved bytes: erase_if %zu, erase loop %zu\n", na, moved_fast, moved_slow);
        CHECK(moved_fast <= 64 * sizeof(CanFrame) && moved_slow > 4 * moved_fast);
        thin::static_vector<std::uint8_t, 4> e;
        CHECK(erase_if(e.ref(), [](std::uint8_t) { return true; }) == 0 && e.empty());
        e.push_back(1); e.push_back(2); e.push_back(3);
        CHECK(erase_if(e.ref(), [](std::uint8_t x) { return x != 2; }) == 2 && e.size() == 1 && *e.at(0) == 2);
    }

    std::puts("--- 연습 3: 원소 타입 분류 (Apple clang 21 에서 관찰한 값)");
    static_assert(elem_path<CanFrame>() == Path::MemcpyBytes);
    static_assert(elem_path<std::uint32_t>() == Path::MemcpyBytes);
    static_assert(elem_path<PadM>() == Path::MemcpyOnly);        // 패딩 3 B
    static_assert(elem_path<FloatM>() == Path::MemcpyOnly);      // float: +0/-0, NaN 들 → 같은 값 ≠ 같은 바이트
    static_assert(elem_path<Bits2>() == Path::MemcpyOnly);       // 안 쓰는 비트필드 29 비트
    static_assert(elem_path<VolM>() == Path::MemcpyBytes);       // volatile 멤버도 trivially copyable (실측)
    static_assert(elem_path<ConstM>() == Path::Rejected);        // const 멤버 → 대입 불가 → std::copyable 아님
    static_assert(elem_path<Counted>() == Path::Ops);
    CHECK(elem_path<FloatM>() == Path::MemcpyOnly && elem_path<Counted>() == Path::Ops);

    std::puts("--- 연습 4: 우선순위 정렬 삽입");
    {
        thin::static_vector<CanFrame, 8> q8;
        thin::static_vector<CanFrame, 3> q3;
        const std::uint32_t ids[] = {0x300, 0x100, 0x200, 0x100, 0x050, 0x7FF};
        int k = 0;
        for (std::uint32_t id : ids) {
            const CanFrame f{id, 1, {}, {std::uint8_t(k++)}};
            insert_sorted(q8, f);
            insert_sorted(q3, f);
        }
        CHECK(q8.size() == 6 && q8.at(0)->id == 0x050 && q8.at(5)->id == 0x7FF);
        CHECK(q8.at(1)->id == 0x100 && q8.at(1)->data[0] == 1 && q8.at(2)->id == 0x100 && q8.at(2)->data[0] == 3);   // 같은 id 는 FIFO
        bool sorted = true;
        for (std::size_t i = 1; i < q8.size(); ++i) sorted = sorted && q8.at(i - 1)->id <= q8.at(i)->id;
        CHECK(sorted);
        CHECK(q3.size() == 3 && q3.full() && !insert_sorted(q3, CanFrame{0x001, 0, {}, {}}));
        CHECK(q3.at(0)->id == 0x100 && q3.at(1)->id == 0x200 && q3.at(2)->id == 0x300);   // 앞 3개만 들어갔다
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
