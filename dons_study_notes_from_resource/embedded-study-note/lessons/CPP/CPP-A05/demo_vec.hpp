// CPP-A05 demo_vec.hpp — solution.cpp 의 fat::static_vector 와 thin (래퍼 + 코어 선언) 을 그대로 옮긴 것.
// 측정·에러 데모(demo_bloat.cpp, demo_errors.cpp, demo_global_placement.cpp)가 공유한다. 코어 정의는 demo_thin_core.cpp.
//   -DTHIN_CORE_INLINE : 코어를 헤더에 inline 으로 넣는 "header-only" 변형 (인라인되면 bloat 이 돌아오는지 측정용)
#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#ifdef THIN_CORE_INLINE
#define CORE_INLINE inline
#else
#define CORE_INLINE
#endif

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

// 비템플릿 코어 — 정의는 demo_thin_core.cpp (다른 번역 단위 = 인라인되지 않음, LTO 제외)
CORE_INLINE bool push_raw(std::uint16_t& size, std::byte* buf, geom g, const void* src) noexcept;
CORE_INLINE bool pop_raw(std::uint16_t& size) noexcept;
CORE_INLINE bool insert_raw(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src) noexcept;
CORE_INLINE bool erase_raw(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i) noexcept;
CORE_INLINE void* at_raw(std::uint16_t size, std::byte* buf, std::size_t esz, std::size_t i) noexcept;
CORE_INLINE bool push_ops(std::uint16_t& size, std::byte* buf, geom g, const void* src, const elem_ops& op) noexcept;
CORE_INLINE bool pop_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept;
CORE_INLINE bool insert_ops(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src, const elem_ops& op) noexcept;
CORE_INLINE bool erase_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i, const elem_ops& op) noexcept;
CORE_INLINE void clear_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept;

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

#ifdef THIN_CORE_INLINE
#include "demo_thin_core.cpp"
#endif
