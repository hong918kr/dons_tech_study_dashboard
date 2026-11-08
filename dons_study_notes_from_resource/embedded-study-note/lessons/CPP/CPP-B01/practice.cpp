// CPP-B01 추가 연습 — 한 번에 검증
// 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

// 연습 1: 같은 이름의 struct 태그가 전역 배열을 가린다 (C 와 C++ 결과가 다름)
char frame[64];                              // UART DMA 버퍼 (다른 파일에서도 씀)
static unsigned frame_bytes() {
    struct frame { char hdr, len; };
    static_assert(sizeof(frame) == 2);        // C++: 가장 안쪽 이름 = struct frame
    return sizeof(::frame);                   // 전역 배열을 원하면 :: 로 명시 (또는 이름을 바꾼다)
}

// 연습 2: goto 가 초기화를 건너뛰지 않게 — 블록으로 감싸거나 early return
static int checksum(const std::uint8_t *p, int n) {
    if (!p || n <= 0) return -1;              // goto fail 대신 early return
    std::uint8_t crc = 0;
    for (int i = 0; i < n; i++) crc = std::uint8_t(crc ^ p[i]);
    return crc;
}

// 연습 3: 고정 기저 타입(fixed underlying type) enum — 레지스터 원시값을 담아도 UB 아님 (CPP-B03 참고)
enum gpio_mode8_t : std::uint8_t { M8_INPUT, M8_OUTPUT, M8_AF, M8_ANALOG };
static constexpr gpio_mode8_t raw_mode(std::uint8_t raw) { return static_cast<gpio_mode8_t>(raw); }
static constexpr bool is_valid(gpio_mode8_t m) { return m <= M8_ANALOG; }
static_assert(sizeof(gpio_mode8_t) == 1);
static_assert(raw_mode(0xFF) == 0xFF && !is_valid(raw_mode(0xFF)));   // 상수식 OK = UB 아님

// 연습 4: 배열 지정자 대안 — switch (빠진 enumerator 는 -Wswitch 가 잡음) + 길이 sentinel
enum uart_parity_t { PAR_NONE, PAR_EVEN, PAR_ODD, PAR_COUNT };
static constexpr const char *parity_name(uart_parity_t p) {
    switch (p) {
    case PAR_NONE:  return "N";
    case PAR_EVEN:  return "E";
    case PAR_ODD:   return "O";
    case PAR_COUNT: break;
    }
    return "?";
}
static constexpr std::uint8_t kParityBits[] = { 0b00, 0b10, 0b11 };   // PCE/PS 비트 (STM32 USART_CR1 식)
static_assert(sizeof kParityBits == PAR_COUNT, "테이블 길이 != enum 개수");
static_assert(parity_name(PAR_ODD)[0] == 'O');

// 연습 5: const 를 안 지키는 벤더 HAL(char*)에 문자열 리터럴 넘기기
static char tx_fifo[32];
static std::size_t tx_len;
extern "C" void legacy_uart_puts(char *s) {   // 바꿀 수 없는 C HAL 이라고 가정 (읽기만 함)
    while (*s && tx_len < sizeof tx_fifo - 1) tx_fifo[tx_len++] = *s++;
}
static void uart_puts(const char *s) {        // C++ 쪽 API 는 const char*
    legacy_uart_puts(const_cast<char *>(s));  // OK: 호출 대상이 쓰지 않음을 문서로 확인했을 때만
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    CHECK(frame_bytes() == 64);

    const std::uint8_t f[3] = { 0x01, 0x02, 0x04 };
    CHECK(checksum(f, 3) == 0x07);
    CHECK(checksum(f, 0) == -1 && checksum(nullptr, 3) == -1);

    CHECK(is_valid(raw_mode(2)) && raw_mode(2) == M8_AF);
    CHECK(!is_valid(raw_mode(7)));

    CHECK(std::strcmp(parity_name(PAR_EVEN), "E") == 0);
    CHECK(std::strcmp(parity_name(PAR_COUNT), "?") == 0);
    CHECK(kParityBits[PAR_ODD] == 0b11);

    uart_puts("AT\r\n");
    uart_puts("OK");
    CHECK(tx_len == 6 && std::memcmp(tx_fifo, "AT\r\nOK", 6) == 0);
    CHECK((std::is_same_v<decltype("AT"), const char (&)[3]>));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
