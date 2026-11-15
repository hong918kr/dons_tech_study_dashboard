// CPP-A05 demo_thin_core.cpp — thin 코어 정의 (solution.cpp 의 TODO 를 채운 본문과 같다). 프로그램 전체에 한 벌.
#ifndef DEMO_THIN_CORE_INCLUDED
#define DEMO_THIN_CORE_INCLUDED
#include "demo_vec.hpp"
namespace thin {
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
CORE_INLINE bool push_raw(std::uint16_t& size, std::byte* buf, geom g, const void* src) noexcept {
    if (size >= g.cap) return false;
    std::memcpy(addr(buf, size, g.esz), src, g.esz);
    ++size;
    return true;
}
CORE_INLINE bool pop_raw(std::uint16_t& size) noexcept {
    if (size == 0) return false;
    --size;
    return true;
}
CORE_INLINE bool insert_raw(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src) noexcept {
    if (size >= g.cap || i > size) return false;
    const std::byte* s = static_cast<const std::byte*>(src);
    if (in_range(s, buf, i, size, g.esz)) s += g.esz;
    std::byte* p = addr(buf, i, g.esz);
    std::memmove(p + g.esz, p, (size - i) * g.esz);
    std::memcpy(p, s, g.esz);
    ++size;
    return true;
}
CORE_INLINE bool erase_raw(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i) noexcept {
    if (i >= size) return false;
    std::byte* p = addr(buf, i, esz);
    std::memmove(p, p + esz, (size - i - 1) * esz);
    --size;
    return true;
}
CORE_INLINE void* at_raw(std::uint16_t size, std::byte* buf, std::size_t esz, std::size_t i) noexcept {
    return i < size ? addr(buf, i, esz) : nullptr;
}

// ---- Part B: ops 표 코어 (비-trivial T). 원소 하나하나를 표의 함수 포인터로 생성/대입/소멸 ----
CORE_INLINE bool push_ops(std::uint16_t& size, std::byte* buf, geom g, const void* src, const elem_ops& op) noexcept {
    if (size >= g.cap) return false;
    op.copy_construct(addr(buf, size, g.esz), src);
    ++size;
    return true;
}
CORE_INLINE bool pop_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    if (size == 0) return false;
    op.destroy(addr(buf, --size, esz));
    return true;
}
CORE_INLINE bool insert_ops(std::uint16_t& size, std::byte* buf, geom g, std::size_t i, const void* src, const elem_ops& op) noexcept {
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
CORE_INLINE bool erase_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, std::size_t i, const elem_ops& op) noexcept {
    if (i >= size) return false;
    for (std::size_t k = i; k + 1 < size; ++k) op.move_assign(addr(buf, k, esz), addr(buf, k + 1, esz));
    op.destroy(addr(buf, --size, esz));
    return true;
}
CORE_INLINE void clear_ops(std::uint16_t& size, std::byte* buf, std::size_t esz, const elem_ops& op) noexcept {
    while (size != 0) op.destroy(addr(buf, --size, esz));
}

}  // namespace thin
#endif
