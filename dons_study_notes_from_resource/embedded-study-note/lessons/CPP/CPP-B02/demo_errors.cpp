// CPP-B02 demo — extern "C" 로는 "안 되는 것" 들 (컴파일 에러를 직접 보기)
//   c++ -std=c++20 -fsyntax-only demo_errors.cpp                 # 통과 (에러 블록 꺼짐)
//   c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_errors.cpp    # 에러 3개
#include <cstdint>

extern "C" void hal_write(uint32_t v);          // OK: C 이름 "hal_write" 하나

#ifdef SHOW_ERROR
// (1) C 링키지 함수는 오버로드 불가 — 심볼 이름에 파라미터 정보가 없으니 둘 다 "hal_write"
extern "C" void hal_write(uint16_t v);

// (2) 링키지 지정(linkage-specification)은 네임스페이스 범위에서만 쓸 수 있다 → 클래스 안 불가
struct Uart {
    extern "C" static void isr();
};

// (3) 템플릿은 C 링키지를 가질 수 없다 ([temp.pre])
extern "C" template <typename T> void reg_write(T v);
#endif

int main() {}
