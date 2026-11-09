// CPP-M13 demo_odr.hpp — 헤더의 constexpr 표: inline 을 붙여야 객체가 1개
#pragma once
using u32 = unsigned int;
struct Table { u32 v[256]; };

constexpr Table make_table() {
    Table t{};
    for (u32 i = 0; i < 256; ++i) {
        u32 c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        t.v[i] = c;
    }
    return t;
}

#ifdef USE_INLINE
inline constexpr Table table = make_table();   // C++17 inline 변수: 프로그램 전체에 객체 1개
#else
constexpr Table table = make_table();          // 내부 링크(const 가 함의): TU 마다 사본
#endif

u32 lookup_a(u32 i);
u32 lookup_b(u32 i);
