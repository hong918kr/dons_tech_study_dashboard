// CPP-M02 demo_bitfield_layout.cpp — 같은 비트필드 소스, 타깃별로 다른 메모리 배치 (실행 없이 LLVM IR 로 확인)
//   clang++ --target=<triple> -std=c++20 -O2 -emit-llvm -S demo_bitfield_layout.cpp -o - | grep -E 'layout_|size_' 
// -emit-llvm 은 해당 CPU 백엔드 없이도 동작한다. -O2 가 memcpy 를 상수로 접어 ret i32 <값> 이 나온다.
struct Bits { unsigned a : 4; unsigned b : 12; unsigned c : 16; };

extern "C" unsigned layout_abc() {
    Bits s{};
    s.a = 0x1; s.b = 0x234; s.c = 0x5678;
    unsigned u = 0;
    __builtin_memcpy(&u, &s, sizeof u);
    return u;
}

struct Mixed { unsigned char a : 4; unsigned b : 4; };     // 선언 타입이 섞인 비트필드
extern "C" const int size_mixed = sizeof(Mixed);

struct Straddle { unsigned a : 30; unsigned b : 4; };      // 저장 단위를 넘는 필드
extern "C" const int size_straddle = sizeof(Straddle);
