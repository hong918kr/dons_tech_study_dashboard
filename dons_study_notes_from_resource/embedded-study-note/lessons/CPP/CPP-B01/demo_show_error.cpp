// CPP-B01 demo — C 에선 통과하지만 C++ 에선 막히는 것들 (+ 컴파일 타임에 잡히는 UB)
//   정상 빌드 : c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_show_error.cpp -o /tmp/cppb01_de && /tmp/cppb01_de
//   에러 보기 : 위 명령에 -DSHOW_ERROR 추가
#include <cstdint>
#include <cstdio>

enum gpio_mode_t { MODE_INPUT, MODE_OUTPUT, MODE_AF, MODE_ANALOG };   // 값 범위 = 0..3 (2비트)
enum gpio_mode8_t : std::uint8_t { M8_INPUT, M8_OUTPUT, M8_AF, M8_ANALOG }; // 값 범위 = 0..255

// (a) 범위 밖 정수 → enum: 고정 기저 타입이 없으면 UB → 상수식에서는 컴파일 에러로 드러난다
constexpr gpio_mode8_t ok8 = static_cast<gpio_mode8_t>(7);   // OK: uint8_t 범위 안
#ifdef SHOW_ERROR
constexpr gpio_mode_t bad = static_cast<gpio_mode_t>(7);     // [dcl.enum]/8 범위 밖 → UB
#endif

// (b) C 의 "tentative definition" — 같은 전역을 두 번 써도 C 는 하나로 합친다
std::uint32_t tick_count;
#ifdef SHOW_ERROR
std::uint32_t tick_count;
#endif

static int send_frame(const std::uint8_t *p, int n) {
    if (n <= 0) goto fail;                 // (c) C 에서 흔한 에러 처리 goto
#ifdef SHOW_ERROR
    std::uint8_t crc = 0;                  // C: OK   C++: goto 가 이 초기화를 건너뜀 → 에러
    for (int i = 0; i < n; i++) crc ^= p[i];
    return crc;
#else
    {                                      // 고친 버전: 블록으로 감싸 goto 경로 밖으로
        std::uint8_t crc = 0;
        for (int i = 0; i < n; i++) crc ^= p[i];
        return crc;
    }
#endif
fail:
    return -1;
}

// (d) 배열 지정자 대신 switch: enumerator 를 빠뜨리면 -Wswitch(-Wall) 가 알려 준다
static const char *mode_name(gpio_mode_t m) {
    switch (m) {
    case MODE_INPUT:  return "in";
    case MODE_OUTPUT: return "out";
    case MODE_AF:     return "af";
#ifndef SHOW_ERROR
    case MODE_ANALOG: return "ana";
#endif
    }
    return "?";
}

int main() {
    const std::uint8_t f[3] = {0x01, 0x02, 0x04};
    std::printf("ok8=%d tick=%u crc=%d name=%s\n", int(ok8), unsigned(tick_count),
                send_frame(f, 3), mode_name(MODE_ANALOG));
    return 0;
}
