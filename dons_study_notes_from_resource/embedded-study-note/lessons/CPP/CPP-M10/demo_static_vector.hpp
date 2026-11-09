// CPP-M10 데모 공용 헤더: solution.cpp 의 static_vector 와 같은 코드 (데모가 solution.cpp 의 POSIX 하네스를 끌고 오지 않게 분리)
#pragma once
#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// ---------------- 용량 초과 정책 ----------------
struct ReturnOnFull { static void on_overflow() noexcept {} };                      // 호출자가 nullptr/false 를 확인
struct TrapOnFull { [[noreturn]] static void on_overflow() noexcept { std::abort(); } };  // 펌웨어면 fault handler/BKPT

// ---------------- Part A/B: static_vector ----------------
template <typename T, std::size_t N, typename Overflow = ReturnOnFull>
class static_vector {
    static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>,
                  "static_vector<T>: T must be a non-cv object type");

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;                 // 연속 저장이므로 포인터가 곧 contiguous iterator
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    static constexpr bool kTrivialDtor = std::is_trivially_destructible_v<T>;
    static constexpr bool kTrivialCopyCtor = std::is_trivially_copy_constructible_v<T>;
    static constexpr bool kTrivialMoveCtor = std::is_trivially_move_constructible_v<T>;
    static constexpr bool kTrivialCopyAsg = kTrivialDtor && kTrivialCopyCtor && std::is_trivially_copy_assignable_v<T>;
    static constexpr bool kTrivialMoveAsg = kTrivialDtor && kTrivialMoveCtor && std::is_trivially_move_assignable_v<T>;

    // N == 0 이어도 크기 0 배열은 ill-formed 이므로 최소 1바이트
    alignas(T) std::byte storage_[N != 0 ? N * sizeof(T) : 1];
    size_type size_ = 0;

    // i 번째 칸의 "주소". 객체가 없어도 되는 용도(construct_at 대상)에만 쓴다.
    T*       slot_(size_type i) noexcept       { return reinterpret_cast<T*>(storage_ + i * sizeof(T)); }
    const T* slot_(size_type i) const noexcept { return reinterpret_cast<const T*>(storage_ + i * sizeof(T)); }

    // Part B: [0, size_) 를 src 의 n 개로 바꾼다. 살아 있는 칸은 대입, 빈 칸은 생성, 남는 칸은 소멸.
    //   It 가 move_iterator 면 그대로 "이동" 버전이 된다.
    template <typename It>
    void assign_range_(It src, size_type n) {
        size_type i = 0;
        for (; i < size_ && i < n; ++i, ++src) data()[i] = *src;       // 살아 있는 칸: 대입
        for (; i < n; ++i, ++src) std::construct_at(slot_(i), *src);   // 빈 칸: 생성
        if (n < size_) std::destroy(data() + n, data() + size_);       // 남는 칸: 소멸
        size_ = n;
    }

public:
    // user-provided 라서 `static_vector<...> v{};` 가 저장소 전체를 0 으로 채우지 않는다
    static_vector() noexcept {}

    static_vector(std::initializer_list<T> il) requires std::is_copy_constructible_v<T> {
        for (const T& x : il) if (!push_back(x)) break;
    }

    // ---- Part B: 소멸자 / 복사 / 이동 (조건부 trivial) ----
    ~static_vector() requires kTrivialDtor = default;
    ~static_vector() { clear(); }

    static_vector(const static_vector&) requires kTrivialCopyCtor = default;
    static_vector(const static_vector& o) requires(std::is_copy_constructible_v<T> && !kTrivialCopyCtor) {
        assign_range_(o.begin(), o.size());
    }

    // 이동도 원소별: 인라인 저장소라 포인터를 훔칠 수 없다. 원본의 size 는 그대로(원소는 moved-from).
    static_vector(static_vector&&) requires kTrivialMoveCtor = default;
    static_vector(static_vector&& o) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(std::is_move_constructible_v<T> && !kTrivialMoveCtor) {
        assign_range_(std::make_move_iterator(o.begin()), o.size());
    }

    static_vector& operator=(const static_vector&) requires kTrivialCopyAsg = default;
    static_vector& operator=(const static_vector& o)
        requires(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T> && !kTrivialCopyAsg) {
        if (this != &o) assign_range_(o.begin(), o.size());
        return *this;
    }

    static_vector& operator=(static_vector&&) requires kTrivialMoveAsg = default;
    static_vector& operator=(static_vector&& o)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T> && !kTrivialMoveAsg) {
        if (this != &o) assign_range_(std::make_move_iterator(o.begin()), o.size());
        return *this;
    }

    // ---- 크기 ----
    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == N; }
    static constexpr size_type capacity() noexcept { return N; }
    static constexpr size_type max_size() noexcept { return N; }

    // ---- 원소 접근 ----
    // std::launder 는 "그 주소에 살아 있는 T 가 있을 것"이 전제조건이라, 비었을 때는 쓰면 안 된다 (CPP-M07 참고)
    T*       data() noexcept       { return size_ ? std::launder(slot_(0)) : slot_(0); }
    const T* data() const noexcept { return size_ ? std::launder(slot_(0)) : slot_(0); }

    T&       operator[](size_type i) noexcept       { assert(i < size_); return *std::launder(slot_(i)); }
    const T& operator[](size_type i) const noexcept { assert(i < size_); return *std::launder(slot_(i)); }
    T&       front() noexcept       { return (*this)[0]; }
    const T& front() const noexcept { return (*this)[0]; }
    T&       back() noexcept       { return (*this)[size_ - 1]; }
    const T& back() const noexcept { return (*this)[size_ - 1]; }
    // at() 의 예외 없는 대체: 범위 밖이면 nullptr
    T*       try_at(size_type i) noexcept       { return i < size_ ? data() + i : nullptr; }
    const T* try_at(size_type i) const noexcept { return i < size_ ? data() + i : nullptr; }

    // ---- 반복자 ----
    iterator begin() noexcept { return data(); }
    iterator end() noexcept { return data() + size_; }
    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + size_; }
    const_iterator cbegin() const noexcept { return data(); }
    const_iterator cend() const noexcept { return data() + size_; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    // ---- Part A: 끝에 추가 / 제거 ----
    // 성공하면 새 원소 포인터, 꽉 찼으면 Overflow::on_overflow() 후 nullptr (인자는 건드리지 않음)
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    [[nodiscard]] T* emplace_back(Args&&... args) {
        if (full()) { Overflow::on_overflow(); return nullptr; }
        T* p = std::construct_at(slot_(size_), std::forward<Args>(args)...);
        ++size_;
        return p;
    }
    [[nodiscard]] bool push_back(const T& x) requires std::is_copy_constructible_v<T> { return emplace_back(x) != nullptr; }
    [[nodiscard]] bool push_back(T&& x) requires std::is_move_constructible_v<T> { return emplace_back(std::move(x)) != nullptr; }

    void pop_back() {
        assert(size_ > 0);
        std::destroy_at(data() + size_ - 1);
        --size_;
    }

    void clear() noexcept {
        std::destroy(begin(), end());
        size_ = 0;
    }

    // ---- Part B: 중간 삽입 / 삭제 ----
    // pos 앞에 삽입. 꽉 찼으면 on_overflow() 후 nullptr. 인자가 자기 원소를 가리켜도 안전해야 한다.
    template <typename... Args>
        requires(std::is_constructible_v<T, Args...> && std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
    [[nodiscard]] iterator emplace(const_iterator pos, Args&&... args) {
        assert(pos >= cbegin() && pos <= cend());
        const size_type idx = static_cast<size_type>(pos - cbegin());
        if (full()) { Overflow::on_overflow(); return nullptr; }
        if (idx == size_) return emplace_back(std::forward<Args>(args)...);

        T tmp(std::forward<Args>(args)...);                         // 먼저 만든다: args 가 내 원소를 가리킬 수 있다
        T* const d = data();
        std::construct_at(slot_(size_), std::move(d[size_ - 1]));   // 끝 칸은 raw → 생성
        std::move_backward(d + idx, d + size_ - 1, d + size_);      // 나머지는 살아 있음 → 대입
        d[idx] = std::move(tmp);
        ++size_;
        return d + idx;
    }
    [[nodiscard]] iterator insert(const_iterator pos, const T& x)
        requires(std::is_copy_constructible_v<T> && std::is_move_assignable_v<T>) { return emplace(pos, x); }
    [[nodiscard]] iterator insert(const_iterator pos, T&& x)
        requires(std::is_move_constructible_v<T> && std::is_move_assignable_v<T>) { return emplace(pos, std::move(x)); }

    // pos 원소 제거. 반환: 제거된 다음 원소 위치
    iterator erase(const_iterator pos) requires std::is_move_assignable_v<T> {
        assert(pos >= cbegin() && pos < cend());
        T* const p = begin() + (pos - cbegin());
        std::move(p + 1, end(), p);
        std::destroy_at(end() - 1);
        --size_;
        return p;
    }

    iterator erase(const_iterator first, const_iterator last) requires std::is_move_assignable_v<T> {
        assert(first >= cbegin() && first <= last && last <= cend());
        T* const f = begin() + (first - cbegin());
        if (first == last) return f;
        T* const l = begin() + (last - cbegin());
        T* const new_end = std::move(l, end(), f);
        std::destroy(new_end, end());
        size_ -= static_cast<size_type>(l - f);
        return f;
    }

    friend bool operator==(const static_vector& a, const static_vector& b)
        requires requires(const T& x) { { x == x } -> std::convertible_to<bool>; } {
        return a.size_ == b.size_ && std::equal(a.begin(), a.end(), b.begin());
    }
};
