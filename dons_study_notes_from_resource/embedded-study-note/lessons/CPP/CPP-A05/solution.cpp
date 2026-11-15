/* CPP-A05 템플릿 bloat 다이어트: thin template 기법
 *
 *   fat::static_vector<T,N>  — 주어진 "전" 코드 (CPP-M10 의 축약판). 모든 멤버 함수가 (T, N) 마다 새로 생긴다.
 *   thin::push_raw/...       — 바이트 단위 비템플릿 코어: (size, 버퍼, 원소 크기, 용량)을 런타임 인자로 받는다.
 *   thin::vec_ref<T>         — N 을 지운 얇은 타입 뷰.  thin::static_vector<T,N> — {size, 저장소} + 한 줄 forwarder.
 *   Part A: memcpy 코어 (trivially copyable T 전용)          Part B: 자기 원소 별칭 insert, 비-trivial T 용 ops 표,
 *   fat ↔ thin 동치 테스트 (xorshift 연산열)                  Part C: CAN 게이트웨이 (N 이 다른 큐를 함수 하나로)
 *   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
 */
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <type_traits>

static int g_pass = 0, g_total = 0;
#define CHECK(expr) do { ++g_total; if (expr) { ++g_pass; std::printf("PASS %s\n", #expr); } \
                         else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

/* ================= 주어진 코드: fat::static_vector (CPP-M10 축약판, 소유자 CPP-M10) =================
 * 계약 (thin 도 똑같이 지켜야 한다 — 리팩터링 전후 동작 동일):
 *   push_back: 꽉 차면 false, 아무것도 안 바뀜         pop_back: 비었으면 false
 *   insert(i, v): 꽉 찼거나 i > size 면 false          erase(i): i >= size 면 false
 *   at(i): 범위 밖이면 nullptr                          insert 의 v 가 자기 원소를 가리켜도 정확해야 한다
 * 복사/이동은 이 레슨 범위 밖이라 막아 둔다 (CPP-M10 이 담당). */
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

/* ================= thin: 비템플릿 코어 + 얇은 래퍼 ================= */
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
    // TODO(A1): 꽉 차면 false. 아니면 끝 칸에 esz 바이트 복사, size 증가
    if (size >= g.cap) return false;
    std::memcpy(addr(buf, size, g.esz), src, g.esz);
    ++size;
    return true;
}
bool pop_raw(std::uint16_t& size) noexcept {
    // TODO(A2): 비었으면 false
    if (size == 0) return false;
    --size;
    return true;
}
bool insert_raw(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src) noexcept {
    // TODO(A3/B1): 꽉 찼거나 i > size 면 false. 뒤쪽을 한 칸 밀고(memmove) i 에 복사.
    //              src 가 밀리는 구간 [i, size) 안이면 밀린 뒤의 주소(src + esz)에서 읽어야 한다.
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
    // TODO(A4): i >= size 면 false. 뒤쪽을 한 칸 당긴다
    if (i >= size) return false;
    std::byte* p = addr(buf, i, esz);
    std::memmove(p, p + esz, (size - i - 1) * esz);
    --size;
    return true;
}
void* at_raw(std::uint16_t size, std::byte* buf, std::size_t esz, std::size_t i) noexcept {
    // TODO(A5): 범위 밖이면 nullptr
    return i < size ? addr(buf, i, esz) : nullptr;
}

// ---- Part B: ops 표 코어 (비-trivial T). 원소 하나하나를 표의 함수 포인터로 생성/대입/소멸 ----
bool push_ops(std::uint16_t& size, std::byte* buf, geom g, const void* src, const elem_ops& op) noexcept {
    // TODO(B2): 끝 칸(raw)에 copy_construct
    if (size >= g.cap) return false;
    op.copy_construct(addr(buf, size, g.esz), src);
    ++size;
    return true;
}
bool pop_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    // TODO(B3): 마지막 원소 destroy
    if (size == 0) return false;
    op.destroy(addr(buf, --size, esz));
    return true;
}
bool insert_ops(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src, const elem_ops& op) noexcept {
    // TODO(B4): raw 끝 칸엔 move_construct, 살아 있는 칸끼리는 move_assign (뒤에서부터), i 에는 copy_assign.
    //           i == size 면 push. 별칭 처리는 A3 과 같은 방법 (밀린 원본은 한 칸 뒤에 있다)
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
    // TODO(B5): 앞으로 move_assign, 마지막 칸 destroy
    if (i >= size) return false;
    for (std::size_t k = i; k + 1 < size; ++k) op.move_assign(addr(buf, k, esz), addr(buf, k + 1, esz));
    op.destroy(addr(buf, --size, esz));
    return true;
}
void clear_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    // TODO(B6): 살아 있는 원소를 모두 destroy (뒤에서부터)
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
struct CanFrame {                               // 16 B, 패딩 없음 (pad 를 명시) → 바이트 = 값
    std::uint32_t id;
    std::uint8_t dlc;
    std::uint8_t pad[3];
    std::uint8_t data[8];
    bool operator==(const CanFrame&) const = default;
};
struct CanFrameLoose {                          // 16 B 중 3 B 가 컴파일러 패딩 → 바이트 비교/CRC 금지
    std::uint32_t id;
    std::uint8_t dlc;
    std::uint8_t data[8];
    bool operator==(const CanFrameLoose&) const = default;
};
struct LogRec { std::uint32_t t_ms; std::uint16_t code; std::uint16_t arg; bool operator==(const LogRec&) const = default; };
struct MoveOnlyPod {                            // trivially copyable 이지만 복사 생성 불가
    std::uint32_t v;
    MoveOnlyPod(const MoveOnlyPod&) = delete;
    MoveOnlyPod(MoveOnlyPod&&) = default;
};

// 살아 있는 주소 장부: 죽은 칸에 대입/복사하거나 이중 소멸하면 bad 증가
struct Ledger {
    const void* addr[64] = {};
    int live = 0, bad = 0, ctor = 0, dtor = 0;
    int find(const void* p) const { for (int k = 0; k < live; ++k) if (addr[k] == p) return k; return -1; }
    void add(const void* p) { if (find(p) >= 0 || live == 64) ++bad; else addr[live++] = p; ++ctor; }
    void remove(const void* p) { int k = find(p); if (k < 0) { ++bad; return; } addr[k] = addr[--live]; ++dtor; }
    void must_live(const void* p) { if (find(p) < 0) ++bad; }
};
static Ledger g_led;
struct Tracked {
    std::uint32_t v;
    explicit Tracked(std::uint32_t x = 0) : v(x) { g_led.add(this); }
    Tracked(const Tracked& o) : v(o.v) { g_led.must_live(&o); g_led.add(this); }
    Tracked(Tracked&& o) noexcept : v(o.v) { g_led.must_live(&o); o.v = 0xDEADu; g_led.add(this); }
    Tracked& operator=(const Tracked& o) { g_led.must_live(this); g_led.must_live(&o); v = o.v; return *this; }
    Tracked& operator=(Tracked&& o) noexcept { g_led.must_live(this); g_led.must_live(&o); v = o.v; o.v = 0xDEADu; return *this; }
    ~Tracked() { g_led.remove(this); }
    bool operator==(const Tracked& o) const { return v == o.v; }
};

/* ================= Part C: CAN 게이트웨이 ================= */
static std::uint32_t crc32_bytes(std::span<const std::byte> b) {     // 비트 단위 CRC-32 (LUT 판은 CPP-M13)
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::byte x : b) {
        c ^= std::to_integer<std::uint32_t>(x);
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
// id ∈ [lo, hi] 인 프레임을 순서 유지하며 지우고 지운 개수를 돌려준다. 용량(N)이 다른 큐 모두 이 함수 하나로.
static std::size_t drop_ids(thin::vec_ref<CanFrame> q, std::uint32_t lo, std::uint32_t hi) {
    // TODO(C1)
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < q.size();) {
        if (q[i].id >= lo && q[i].id <= hi) { q.erase(i); ++dropped; }
        else ++i;
    }
    return dropped;
}
// 로그가 꽉 찼으면 가장 오래된 것(0번)을 버리고 넣는다. 버린 개수를 evicted 에 누적.
static bool log_push_evict(thin::vec_ref<LogRec> log, const LogRec& r, std::uint32_t& evicted) {
    // TODO(C2)
    if (log.capacity() == 0) return false;
    if (log.full()) { log.erase(0); ++evicted; }
    return log.push_back(r);
}

// requires-식은 템플릿(개념) 안에서만 "거짓" 이 된다 — 템플릿 밖에서 쓰면 그냥 컴파일 에러 ([expr.prim.req])
template <class V> concept HasBytes = requires(V& v) { v.bytes(); };

// 테스트 도우미: 원소 i 의 "키" (없으면 0xFFFFFFFF — 스텁 상태에서도 널 역참조 없이 FAIL 만 나게)
template <class T> std::uint32_t key_of(const T& x) { return static_cast<std::uint32_t>(x); }
std::uint32_t key_of(const CanFrame& f) { return f.id; }
std::uint32_t key_of(const LogRec& r) { return r.t_ms; }
std::uint32_t key_of(const Tracked& t) { return t.v; }
template <class V> std::uint32_t K(V& v, std::size_t i) { auto* p = v.at(i); return p != nullptr ? key_of(*p) : 0xFFFFFFFFu; }
// 자기 원소 j 를 가리키는 참조로 i 에 insert
template <class V> bool insert_own(V& v, std::size_t i, std::size_t j) { auto* p = v.at(j); return p != nullptr && v.insert(i, *p); }

/* ================= 동치 테스트: 같은 연산열을 fat 과 thin 에 ================= */
struct EqStats { int push_full = 0, insert_bad = 0, erase_bad = 0, alias = 0, steps = 0; };

template <class A, class B>
static bool same(A& a, B& b) {
    if (a.size() != b.size() || a.capacity() != b.capacity()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        auto* x = a.at(k); auto* y = b.at(k);
        if (x == nullptr || y == nullptr || !(*x == *y)) return false;
    }
    return a.at(a.size()) == nullptr && b.at(b.size()) == nullptr;
}

template <class T, std::size_t N, class Make>
static bool equiv_run(std::uint32_t seed, int steps, Make make, EqStats& st) {
    fat::static_vector<T, N> a;
    thin::static_vector<T, N> b;
    std::uint32_t s = seed;
    for (int n = 0; n < steps; ++n) {
        const std::uint32_t r = xorshift32(s);
        const std::size_t i = (r >> 8) % (a.size() + 2);      // size+1 은 범위 밖
        const T val = make(r >> 4);
        bool ra = false, rb = false;
        switch (r % 16) {
        case 0: case 1: case 2: case 3: case 4:
            ra = a.push_back(val); rb = b.push_back(val); if (!ra) ++st.push_full; break;
        case 5: case 6: case 7:
            ra = a.pop_back(); rb = b.pop_back(); break;
        case 8: case 9: case 10:
            ra = a.insert(i, val); rb = b.insert(i, val); if (!ra) ++st.insert_bad; break;
        case 11:                                               // 자기 원소를 가리키는 참조로 insert
            if (a.size() == 0) continue;
            { const std::size_t j = (r >> 20) % a.size(); ra = a.insert(i, *a.at(j)); rb = b.insert(i, *b.at(j)); }
            ++st.alias; break;
        case 12: case 13: case 14:
            ra = a.erase(i); rb = b.erase(i); if (!ra) ++st.erase_bad; break;
        default:
            if ((r >> 24) % 8 == 0) { a.clear(); b.clear(); ra = rb = true; }
            else { ra = a.at(i) != nullptr; rb = b.at(i) != nullptr; }
            break;
        }
        ++st.steps;
        if (ra != rb || !same(a, b)) {
            std::printf("  mismatch at step %d (op %u, i %zu)\n", n, r % 16, i);
            return false;
        }
    }
    return true;
}

int main() {
    using thin::MemcpyElem;
    // ---------- 컴파일 시점 증명 ----------
    static_assert(MemcpyElem<std::uint8_t> && MemcpyElem<CanFrame> && MemcpyElem<CanFrameLoose>);
    static_assert(!MemcpyElem<Tracked> && !MemcpyElem<MoveOnlyPod>);
    static_assert(std::is_trivially_copyable_v<MoveOnlyPod>);         // trivially copyable 만 보면 함정
    // RAM 은 그대로: thin 객체는 fat 과 같은 {size, 저장소}. N 을 지운 뷰 타입은 N 과 무관하게 하나
    static_assert(sizeof(thin::static_vector<CanFrame, 16>) == sizeof(fat::static_vector<CanFrame, 16>));
    static_assert(sizeof(thin::static_vector<std::uint8_t, 5>) == sizeof(fat::static_vector<std::uint8_t, 5>));
    static_assert(std::is_same_v<decltype(std::declval<thin::static_vector<std::uint32_t, 4>&>().ref()),
                                 decltype(std::declval<thin::static_vector<std::uint32_t, 64>&>().ref())>);
    static_assert(std::is_convertible_v<thin::static_vector<CanFrame, 16>&, thin::vec_ref<CanFrame>>);
    static_assert(!std::is_convertible_v<thin::static_vector<CanFrameLoose, 16>&, thin::vec_ref<CanFrame>>);
    // 코어 함수는 T·N 과 무관한 보통 함수 하나
    static_assert(std::is_same_v<decltype(&thin::erase_raw), bool (*)(std::uint16_t&, std::byte*, std::size_t, std::size_t) noexcept>);
    // 바이트 뷰는 패딩 없는 trivially copyable T 에만
    static_assert(HasBytes<thin::static_vector<CanFrame, 4>> && HasBytes<thin::vec_ref<CanFrame>>);
    static_assert(!HasBytes<thin::static_vector<CanFrameLoose, 4>> && !HasBytes<thin::vec_ref<Tracked>>);
    // 복사 금지 (fat 과 같은 계약 — 비-trivial T 를 바이트 복사하면 안 된다)
    static_assert(!std::is_copy_constructible_v<thin::static_vector<std::uint32_t, 4>>);
    // trivial T 이면 소멸자도 trivial (fat 과 같은 성질 유지)
    static_assert(std::is_trivially_destructible_v<thin::static_vector<CanFrame, 8>>);
    static_assert(!std::is_trivially_destructible_v<thin::static_vector<Tracked, 8>>);

    std::puts("--- Part A: memcpy 코어");
    {
        thin::static_vector<std::uint32_t, 4> v;
        CHECK(v.size() == 0 && v.capacity() == 4 && v.empty());
        CHECK(v.push_back(10) && v.push_back(20) && v.push_back(30));
        CHECK(v.size() == 3 && K(v, 0) == 10 && K(v, 2) == 30);
        CHECK(v.at(3) == nullptr && v.at(100) == nullptr);
        CHECK(v.insert(1, 15) && v.size() == 4 && v.full());
        CHECK(K(v, 1) == 15 && K(v, 3) == 30);
        CHECK(!v.push_back(99) && !v.insert(0, 99) && v.size() == 4);          // 꽉 참: 그대로
        CHECK(v.erase(0) && v.size() == 3 && K(v, 0) == 15);
        CHECK(!v.erase(3) && !v.insert(5, 1) && v.size() == 3);                // 범위 밖
        CHECK(v.insert(3, 40) && K(v, 3) == 40);                               // i == size → 끝에 붙이기
        CHECK(v.pop_back() && v.size() == 3 && K(v, 2) == 30);
        v.clear();
        CHECK(v.empty() && !v.pop_back() && v.at(0) == nullptr);
    }
    {
        thin::static_vector<std::uint32_t, 0> z;                               // N == 0 도 컴파일되고 항상 실패
        std::uint32_t x = 5;
        CHECK(!z.push_back(x) && !z.insert(0, x) && !z.erase(0) && z.at(0) == nullptr && z.capacity() == 0);
    }
    {
        thin::static_vector<CanFrame, 3> q;                                    // 16 B 원소
        CanFrame f{0x100, 8, {}, {1, 2, 3, 4, 5, 6, 7, 8}};
        CHECK(q.push_back(f));
        f.id = 0x200;
        CHECK(q.insert(0, f) && K(q, 0) == 0x200 && K(q, 1) == 0x100);
        CHECK(q.at(1) && q.at(1)->data[7] == 8 && reinterpret_cast<std::uintptr_t>(q.at(1)) % alignof(CanFrame) == 0);
    }

    std::puts("--- Part B: 별칭 insert, ops 표");
    {
        thin::static_vector<std::uint16_t, 8> v;
        for (std::uint16_t k = 1; k <= 4; ++k) v.push_back(static_cast<std::uint16_t>(k * 11));   // 11 22 33 44
        CHECK(insert_own(v, 0, 2) && v.size() == 5);                           // 33 11 22 33 44
        CHECK(K(v, 0) == 33 && K(v, 1) == 11 && K(v, 3) == 33);
        CHECK(insert_own(v, 2, 4) && K(v, 2) == 44 && K(v, 5) == 44);          // 33 11 44 22 33 44
        CHECK(insert_own(v, 4, 1) && K(v, 4) == 11 && v.size() == 7);          // 앞쪽 원소 참조(밀리지 않음)
        CHECK(insert_own(v, 7, 0) && K(v, 7) == 33 && v.full());               // 끝에 자기 원소
    }
    {
        g_led = Ledger{};
        {
            thin::static_vector<Tracked, 4> t;
            Tracked a(1), b(2), c(3);
            CHECK(t.push_back(a) && t.push_back(b) && t.push_back(c));
            CHECK(g_led.live == 6 && K(t, 1) == 2);
            CHECK(insert_own(t, 1, 2) && t.size() == 4);                       // 1 3 2 3
            CHECK(K(t, 1) == 3 && K(t, 2) == 2 && K(t, 3) == 3);
            CHECK(!t.push_back(a) && g_led.live == 7);                         // 실패한 호출은 아무것도 생성하지 않음
            CHECK(t.erase(0) && t.size() == 3 && K(t, 0) == 3 && g_led.live == 6);
            CHECK(t.pop_back() && t.size() == 2 && g_led.live == 5);
            CHECK(g_led.bad == 0);
        }
        CHECK(g_led.live == 0 && g_led.bad == 0 && g_led.ctor == g_led.dtor);   // 소멸자가 남은 원소를 정리
    }
    {
        EqStats st;
        bool ok = true;
        ok = ok && equiv_run<std::uint8_t, 4>(0x1234ABCDu, 20000, [](std::uint32_t r) { return static_cast<std::uint8_t>(r); }, st);
        ok = ok && equiv_run<std::uint32_t, 16>(0x0BADF00Du, 20000, [](std::uint32_t r) { return r; }, st);
        ok = ok && equiv_run<CanFrame, 7>(0xC0FFEE11u, 20000,
                                          [](std::uint32_t r) { return CanFrame{r & 0x7FF, 8, {}, {std::uint8_t(r), 0, 0, 0, 0, 0, 0, std::uint8_t(r >> 8)}}; }, st);
        ok = ok && equiv_run<std::uint32_t, 0>(0xFEEDu, 2000, [](std::uint32_t r) { return r; }, st);
        CHECK(ok);
        std::printf("  trivial: steps=%d push_full=%d insert_bad=%d erase_bad=%d alias=%d\n",
                    st.steps, st.push_full, st.insert_bad, st.erase_bad, st.alias);
        CHECK(st.push_full > 1000 && st.insert_bad > 1000 && st.erase_bad > 500 && st.alias > 1000);
        g_led = Ledger{};
        EqStats st2;
        CHECK((equiv_run<Tracked, 6>(0x5EEDu, 20000, [](std::uint32_t r) { return Tracked(r & 0xFFFF); }, st2)));
        std::printf("  Tracked: steps=%d push_full=%d alias=%d ledger bad=%d live=%d\n",
                    st2.steps, st2.push_full, st2.alias, g_led.bad, g_led.live);
        CHECK(g_led.bad == 0 && g_led.live == 0 && g_led.ctor == g_led.dtor);
    }

    std::puts("--- Part C: CAN 게이트웨이");
    {
        const char* nine = "123456789";
        CHECK(crc32_bytes(std::as_bytes(std::span(nine, 9))) == 0xCBF43926u);  // CRC-32 check value
        thin::static_vector<CanFrame, 16> can1;
        thin::static_vector<CanFrame, 4> can2;
        for (std::uint32_t id : {0x080u, 0x7E0u, 0x101u, 0x7E8u, 0x102u, 0x7DFu}) can1.push_back(CanFrame{id, 2, {}, {std::uint8_t(id), 0, 0, 0, 0, 0, 0, 0}});
        for (std::uint32_t id : {0x7E0u, 0x7E1u, 0x300u}) can2.push_back(CanFrame{id, 1, {}, {}});
        CHECK(drop_ids(can1, 0x7DF, 0x7EF) == 3 && can1.size() == 3);         // OBD 진단 id 걸러내기
        CHECK(K(can1, 0) == 0x080 && K(can1, 1) == 0x101 && K(can1, 2) == 0x102);
        CHECK(drop_ids(can2, 0x7DF, 0x7EF) == 2 && can2.size() == 1 && K(can2, 0) == 0x300);
        CHECK(drop_ids(can2, 0, 0xFFFFFFFFu) == 1 && can2.empty() && drop_ids(can2, 0, 1) == 0);
        const std::array<CanFrame, 3> expect{CanFrame{0x080, 2, {}, {0x80}}, CanFrame{0x101, 2, {}, {0x01}}, CanFrame{0x102, 2, {}, {0x02}}};
        CHECK(can1.bytes().size() == 48 && crc32_bytes(can1.bytes()) == crc32_bytes(std::as_bytes(std::span(expect))));

        thin::static_vector<LogRec, 4> log;
        std::uint32_t ev = 0;
        for (std::uint32_t t = 1; t <= 6; ++t) CHECK(log_push_evict(log, LogRec{t * 10, std::uint16_t(t), 0}, ev));
        CHECK(log.size() == 4 && ev == 2 && K(log, 0) == 30 && K(log, 3) == 60);
        thin::static_vector<LogRec, 0> nolog;
        CHECK(!log_push_evict(nolog, LogRec{1, 1, 1}, ev) && ev == 2);
    }

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
