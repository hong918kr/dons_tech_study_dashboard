// CPP-A10 demo_crc_size.cpp — 연습 1 의 CRC-32 세 구현을 Cortex-M4 로 컴파일해 크기만 비교 (bash asm_targets.sh crc)
// 헤더 없이 (recipe a) — 표는 constexpr 로 .rodata 에 들어간다.
using u8 = unsigned char;
using u32 = unsigned int;
constexpr u32 kPoly = 0xEDB88320u;
struct T256 { u32 v[256]; };
struct T16 { u32 v[16]; };
constexpr T256 make256() { T256 t{}; for (u32 i = 0; i < 256; ++i) { u32 c = i; for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ kPoly : c >> 1; t.v[i] = c; } return t; }
constexpr T16 make16() { T16 t{}; for (u32 i = 0; i < 16; ++i) { u32 c = i; for (int k = 0; k < 4; ++k) c = (c & 1u) ? (c >> 1) ^ kPoly : c >> 1; t.v[i] = c; } return t; }
constexpr T256 kT256 = make256();
constexpr T16 kT16 = make16();
extern "C" u32 crc_byte(const u8* d, u32 n) { u32 c = ~0u; for (u32 i = 0; i < n; ++i) c = kT256.v[(c ^ d[i]) & 0xFFu] ^ (c >> 8); return ~c; }
extern "C" u32 crc_nibble(const u8* d, u32 n) {
    u32 c = ~0u;
    for (u32 i = 0; i < n; ++i) { c ^= d[i]; c = kT16.v[c & 0xFu] ^ (c >> 4); c = kT16.v[c & 0xFu] ^ (c >> 4); }
    return ~c;
}
extern "C" u32 crc_bitwise(const u8* d, u32 n) {
    u32 c = ~0u;
    for (u32 i = 0; i < n; ++i) { c ^= d[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (kPoly & (0u - (c & 1u))); }
    return ~c;
}
