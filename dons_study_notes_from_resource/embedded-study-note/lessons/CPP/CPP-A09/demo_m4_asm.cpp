// demo_m4_asm.cpp — what the "MISRA fixes" cost on Cortex-M (no std headers → recipe (a); run: bash tidy.sh asm)
// __builtin_bit_cast is what std::bit_cast is built on in libc++/libstdc++; used here only because <bit> is absent.
using u8 = unsigned char;
using u16 = unsigned short;
using i16 = short;
using u32 = unsigned int;

// (1) decode one 16-bit little-endian field: legacy pointer cast vs byte assembly
u16 decode_old(const u8* f) { return *reinterpret_cast<const u16*>(f + 1); }   // UB if misaligned / aliasing
u16 decode_new(const u8* f) { return static_cast<u16>(f[1] | (f[2] << 8)); }

// (2) union pun vs bit_cast
union FloatBits { float f; u32 u; };
u32 bits_union(float x) { FloatBits b; b.f = x; return b.u; }
u32 bits_cast(float x) { return __builtin_bit_cast(u32, x); }

// (3) legacy filter line with double literals (kept bit-exact in fixed_mc100) vs float-only (연습 1)
float filt_double(float d, float e) { return static_cast<float>(0.9 * d + 0.1 * e); }
float filt_float(float d, float e) { return 0.9F * d + 0.1F * e; }

// (4) destructor policy for a polymorphic interface
struct IfaceVirtualDtor { virtual i16 read() = 0; virtual ~IfaceVirtualDtor() = default; };
struct IfaceProtected { virtual i16 read() = 0; protected: ~IfaceProtected() = default; };
struct HallA final : IfaceVirtualDtor { i16 read() override { return 1; } };
struct HallB final : IfaceProtected { i16 read() override { return 2; } };
HallA g_a;      // instances force the vtables to be emitted in this object
HallB g_b;
