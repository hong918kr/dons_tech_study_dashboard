/* CPP-B10 std::array로 C 배열 대체하기
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter  (solution.cpp 도 같은 방식)
 * (at() 는 일부러 쓰지 않는다 — -fno-exceptions 에서는 범위 초과 시 그냥 abort())
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

/* ---------------- Part A: constexpr 룩업 테이블 + 크기를 잃지 않는 전달 ---------------- */
/* CRC-8/SMBUS: poly 0x07, init 0x00, 반사 없음, xorout 없음. check("123456789") = 0xF4 */
constexpr std::array<std::uint8_t, 256> make_crc8_table(std::uint8_t poly) {
    std::array<std::uint8_t, 256> t{};
    (void)poly;
    /* TODO: t[i] = i 를 MSB 부터 8번 시프트하며, 밀려나는 비트가 1이면 poly 를 XOR */
    return t;                                               /* C 배열은 값으로 return 불가 */
}

/* 컴파일 타임에 다 계산 → 플래시(.rodata)에 256바이트 상수로 들어간다 */
inline constexpr auto kCrc8Table = make_crc8_table(0x07);
static_assert(sizeof(kCrc8Table) == 256, "std::array 는 C 배열과 크기가 같다 (오버헤드 0)");

/* 배열을 참조로 받으면 N 이 타입에 남는다 → 포인터로 decay 되지 않음 */
template <std::size_t N>
constexpr std::uint8_t crc8(const std::array<std::uint8_t, N>& data) {
    (void)data;
    return 0; /* TODO: range-for 로 crc = kCrc8Table[crc ^ b] */
}

/* ---------------- Part B: 값 의미론 · to_array · 컴파일 타임 인덱스 ---------------- */
using Frame = std::array<std::uint8_t, 8>;   /* 7바이트 payload + 1바이트 CRC */

/* 값으로 받아(복사) 마지막 바이트에 CRC 를 넣고 값으로 돌려준다. 호출자 배열은 그대로. */
template <std::size_t N>
constexpr std::array<std::uint8_t, N> seal_frame(std::array<std::uint8_t, N> f) {
    static_assert(N >= 2, "payload 1바이트 + CRC 1바이트 이상");
    /* TODO: 앞 N-1 바이트의 CRC 를 f[N-1] 에 넣는다 */
    return f;
}

/* 타이머 프리스케일러 표 — 개수를 손으로 세지 않는다 (std::to_array, C++20) */
inline constexpr auto kPrescalers = std::to_array<std::uint16_t>({1, 2, 4, 8, 16, 64, 256, 1024});
static_assert(kPrescalers.size() == 8);

constexpr int prescaler_index(std::uint16_t div) {
    (void)div;
    return -1; /* TODO: kPrescalers 에서 div 의 위치, 없으면 -1 */
}

/* ADC 4채널 스캔 결과. 채널 번호가 컴파일 타임 상수면 범위 초과는 컴파일 에러여야 한다. */
using AdcFrame = std::array<std::uint16_t, 4>;
template <std::size_t Ch, std::size_t N>
    /* TODO: Ch 가 범위 밖이면 이 템플릿이 아예 후보에서 빠지도록 제약을 건다 */
constexpr std::uint16_t adc_channel(const std::array<std::uint16_t, N>& f) {
    (void)f; return 0; /* TODO: 컴파일 타임 인덱스로 꺼낸다 (operator[]/at() 아님) */
}
/* "이 채널 번호로 호출이 되는가?" 를 컴파일 타임에 질문하는 장치 (requires-expression) */
template <std::size_t Ch>
constexpr bool kChannelOk = requires(const AdcFrame& f) { adc_channel<Ch>(f); };

/* ---------------- Part C: DMA 버퍼 (mock STM32 DMA 채널) ---------------- */
struct DmaChannel {                     /* 실제 칩: DMA1_Channel1 @ 0x40020008 */
    volatile std::uintptr_t CMAR;       /* memory address (호스트는 64비트라 uintptr_t) */
    volatile std::uint32_t  CNDTR;      /* 전송 개수, 16비트 필드 */
    volatile std::uint32_t  CCR;        /* EN(0) TCIE(1) HTIE(2) CIRC(5) MINC(7) PSIZE[9:8] MSIZE[11:10] */
};
inline constexpr std::uint32_t CCR_EN = 1u << 0, CCR_TCIE = 1u << 1, CCR_HTIE = 1u << 2,
                               CCR_CIRC = 1u << 5, CCR_MINC = 1u << 7;
constexpr std::uint32_t size_code(std::size_t bytes) { return bytes == 1 ? 0u : bytes == 2 ? 1u : 2u; }

/* 버퍼 주소와 개수를 한 인자에서 꺼내므로 "길이 불일치" 가 생길 수 없다 */
template <class T, std::size_t N>
void dma_start_circular(DmaChannel& ch, std::array<T, N>& buf) {
    /* TODO: static_assert 3개 — CNDTR 16비트 범위, 짝수 개, 원소 크기 1/2/4 */
    (void)ch; (void)buf;
    /* TODO: 채널 끄기 → CMAR = 버퍼 주소, CNDTR = 원소 개수 →
     *       CCR = MSIZE|PSIZE(size_code) | MINC | CIRC | HTIE | TCIE | EN */
}

/* 캐시(Cortex-M7 D-cache) 유지보수를 라인 단위로 하려면: 시작 주소와 전체 크기가 라인 배수 */
template <std::size_t Line, class T, std::size_t N>
bool cache_line_safe(const std::array<T, N>& buf) {
    (void)buf; return false; /* TODO: 시작 주소 % Line == 0 && 전체 바이트 수 % Line == 0 */
}

/* 반쪽 합. Half 는 템플릿 인자 → 런타임 인덱스 계산 실수가 컴파일 에러로 바뀐다 */
template <std::size_t Half, class T, std::size_t N>
std::uint32_t sum_half(const std::array<T, N>& buf) {
    /* TODO: Half 가 0/1 이 아니면 컴파일 에러 */
    (void)buf;
    return 0; /* TODO: [Half*N/2, (Half+1)*N/2) 구간의 합 */
}

/* DMA ISR: HT(half-transfer) 면 앞 반쪽, TC(transfer-complete) 면 뒤 반쪽을 처리 */
template <class T, std::size_t N>
std::uint32_t dma_isr(bool half_transfer, const std::array<T, N>& buf) {
    return half_transfer ? sum_half<0>(buf) : sum_half<1>(buf);
}

/* TODO: 두 DMA 버퍼 모두 32바이트(캐시 라인) 경계에 놓이게 */
static std::array<std::uint16_t, 64> g_adc_dma{};               /* 128B */
static std::array<std::uint8_t, 32>  g_uart_rx{};

/* 하드웨어 흉내: DMA 는 CMAR 주소에 CNDTR 개를 쓴다 */
static void sim_dma_fill_u16(DmaChannel& ch, std::uint16_t first) {
    if (ch.CMAR == 0) return;                               /* 아직 설정 안 됨 */
    auto* p = reinterpret_cast<std::uint16_t*>(ch.CMAR);
    for (std::uint32_t i = 0; i < ch.CNDTR; ++i) p[i] = static_cast<std::uint16_t>(first + i);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    /* Part A */
    constexpr bool kTableBuiltAtCompileTime = kCrc8Table[1] == 0x07;   /* constexpr 가 아니면 컴파일 에러 */
    CHECK(kTableBuiltAtCompileTime);
    CHECK(kCrc8Table[0x80] == 0x89 && kCrc8Table[0xFF] == 0xF3);
    constexpr std::array<std::uint8_t, 9> kCheck{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    constexpr std::uint8_t kCheckCrc = crc8(kCheck);
    CHECK(kCheckCrc == 0xF4);
    CHECK(crc8(std::array<std::uint8_t, 0>{}) == 0x00);

    /* Part B */
    const Frame raw{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00};
    const Frame sealed = seal_frame(raw);
    CHECK(raw[7] == 0x00);                     /* 값으로 넘겼으니 원본은 그대로 */
    CHECK(sealed[7] == 0xD8);
    CHECK(crc8(sealed) == 0x00);               /* CRC 를 붙인 전체의 CRC 는 0 */
    Frame copy{};
    copy = sealed;                             /* C 배열은 대입 불가, std::array 는 OK */
    CHECK(copy == sealed && copy != raw);
    constexpr int kIdx64 = prescaler_index(64);
    CHECK(kIdx64 == 5);
    CHECK(prescaler_index(3) == -1);
    const AdcFrame adc{100, 200, 300, 400};
    CHECK(adc_channel<0>(adc) == 100 && adc_channel<3>(adc) == 400);
    CHECK(kChannelOk<3>);
    CHECK(!kChannelOk<4>);                     /* 범위 밖 채널은 "호출 불가" 여야 한다 */

    /* Part C */
    DmaChannel adc_ch{};
    dma_start_circular(adc_ch, g_adc_dma);
    CHECK(adc_ch.CMAR == reinterpret_cast<std::uintptr_t>(g_adc_dma.data()));
    CHECK(adc_ch.CNDTR == 64);
    CHECK(adc_ch.CCR == 0x5A7u);               /* MSIZE=PSIZE=16bit | MINC | CIRC | HTIE | TCIE | EN */
    sim_dma_fill_u16(adc_ch, 0);               /* "하드웨어" 가 0..63 을 채움 */
    CHECK(dma_isr(true, g_adc_dma) == 496);    /* HT: 0..31 의 합 */
    CHECK(dma_isr(false, g_adc_dma) == 1520);  /* TC: 32..63 의 합 */
    CHECK(cache_line_safe<32>(g_adc_dma));
    DmaChannel uart_ch{};
    dma_start_circular(uart_ch, g_uart_rx);
    CHECK(uart_ch.CNDTR == 32 && uart_ch.CCR == 0x0A7u);   /* 8비트: 크기 코드 0 */

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
