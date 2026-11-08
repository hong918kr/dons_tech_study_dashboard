// CPP-B11 std::span으로 버퍼+길이 인터페이스 정리
//   Part A: (ptr, len) UART API → std::span<const std::byte>, as_bytes / as_writable_bytes, sizeof(span)
//   Part B: 프레임 [SOF][LEN][CMD][payload..][CRC8] 인코드/파싱 — first / last / subspan + 명시적 길이 검사
//   Part C: 가짜 circular RX DMA 링 → 프레임 조립 → 콜백으로 전달
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>

// ---------------- 공통: 바이트 배열 만들기 헬퍼 ----------------
template <typename... Ts>
constexpr std::array<std::byte, sizeof...(Ts)> bytes(Ts... v) { return {static_cast<std::byte>(v)...}; }

static bool same(std::span<const std::byte> a, std::span<const std::byte> b) { return std::ranges::equal(a, b); }

// ---------------- 컴파일타임 사실 (starter/solution 공통) ----------------
using BSpan = std::span<const std::byte>;
static_assert(std::is_convertible_v<std::array<std::byte, 4>&, BSpan>);                          // std::array → span: 암시적
static_assert(std::is_convertible_v<std::byte (&)[4], BSpan>);                                   // C 배열 → span: 암시적
static_assert(!std::is_convertible_v<std::array<uint8_t, 4>&, BSpan>);                           // uint8_t → byte: 안 됨 (as_bytes)
static_assert(!std::is_convertible_v<const std::array<std::byte, 4>&, std::span<std::byte>>);    // const 버퍼에 쓰기 금지
static_assert(!std::is_convertible_v<std::array<std::byte, 4>&, std::span<const std::byte, 8>>); // 정적 extent 불일치
static_assert(!std::is_convertible_v<BSpan, std::span<const std::byte, 4>>);                     // dynamic → static: explicit 만
static_assert(std::is_constructible_v<std::span<const std::byte, 4>, BSpan>);                    //   (크기 같아야 함: 전제조건)
static_assert(std::is_trivially_copyable_v<BSpan>);                                              // 값으로 넘겨도 레지스터 2개
static_assert(decltype(std::as_bytes(std::declval<std::span<const uint32_t, 4>>()))::extent == 16);
static_assert(std::endian::native == std::endian::little);                                       // arm64 / Cortex-M 모두 LE

// ================= Part A: UART API를 span으로 =================
struct UartRegs {
    volatile uint32_t SR;
    volatile uint32_t DR;
};
static UartRegs fake_uart;                       // 실제 칩이라면 reinterpret_cast<UartRegs*>(0x40011000)
#define UART1 (&fake_uart)

static std::array<std::byte, 64> g_wire;         // "선 위로 나간" 바이트 기록 (테스트용)
static size_t g_wire_len;
static void wire_reset() { g_wire_len = 0; }
static void uart_putc(std::byte b) {
    UART1->DR = std::to_integer<uint32_t>(b);
    if (g_wire_len < g_wire.size()) g_wire[g_wire_len++] = b;
}

static std::array<std::byte, 8> g_rx_fifo;       // 가짜 RX FIFO
static size_t g_rx_head, g_rx_len;
static void rx_load(std::span<const std::byte> s) {
    g_rx_head = 0;
    g_rx_len = std::min(s.size(), g_rx_fifo.size());
    std::ranges::copy(s.first(g_rx_len), g_rx_fifo.begin());
}
static bool uart_getc(std::byte& out) {          // SR.RXNE 폴링 흉내: 비었으면 false
    if (g_rx_head == g_rx_len) return false;
    out = g_rx_fifo[g_rx_head++];
    return true;
}

// A1: 보낸 바이트 수를 반환
static size_t uart_write(std::span<const std::byte> data) {
    (void)data; (void)uart_putc;
    return 0; /* TODO: 바이트마다 uart_putc */
}
// A2: 옛 C 호출자를 위한 얇은 shim — 내부는 span API 하나로
static size_t uart_write_legacy(const uint8_t* p, size_t n) {
    (void)p; (void)n;
    return 0; /* TODO: (p, n) → span → as_bytes → uart_write */
}
// A3: dst가 찰 때까지 또는 FIFO가 빌 때까지 읽고, 읽은 수를 반환
static size_t uart_read(std::span<std::byte> dst) {
    (void)dst; (void)uart_getc;
    return 0; /* TODO: uart_getc 로 dst를 채운다 */
}
// A4: 예측 — sizeof(span)은 포인터 몇 개 크기인가?
constexpr size_t kStaticSpanWords  = 0;   // TODO: std::span<const std::byte, 8>
constexpr size_t kDynamicSpanWords = 0;   // TODO: std::span<const std::byte>

struct Cfg {                                     // 패딩 없는 4바이트 설정 레코드
    uint16_t id;
    uint8_t mode;
    uint8_t gain;
};

// ================= Part B: 프레임 [SOF][LEN][CMD][payload LEN][CRC8] =================
inline constexpr std::byte kSof{0xA5};
inline constexpr size_t kHdrLen = 3, kCrcLen = 1, kMaxPayload = 16;

enum class ParseStatus : uint8_t { Ok, NeedMore, BadSof, BadLen, BadCrc };
struct Frame {
    std::byte cmd{};
    std::span<const std::byte> payload{};        // 원본 버퍼를 가리키는 뷰 (복사 아님!)
};
struct ParseResult {
    ParseStatus status;
    Frame frame;
    size_t consumed;                             // Ok: 프레임 길이, Bad*: 1 (재동기화), NeedMore: 0
};

// 주어진 것: CRC-8/SMBUS (poly 0x07, init 0x00). check("123456789") = 0xF4
constexpr uint8_t crc8(std::span<const std::byte> data) {
    uint8_t c = 0;
    for (std::byte b : data) {
        c ^= std::to_integer<uint8_t>(b);
        for (int i = 0; i < 8; ++i) c = static_cast<uint8_t>((c & 0x80u) ? (c << 1) ^ 0x07u : (c << 1));
    }
    return c;
}
static_assert(crc8(bytes('1', '2', '3', '4', '5', '6', '7', '8', '9')) == 0xF4);

// B1: out에 프레임을 쓰고 길이 반환. 자리가 모자라거나 payload가 너무 길면 0
static size_t encode_frame(std::span<std::byte> out, std::byte cmd, std::span<const std::byte> payload) {
    (void)out; (void)cmd; (void)payload;
    return 0; /* TODO: 길이 검사 → SOF, LEN, CMD, payload, CRC8(SOF..payload) */
}
// B2: buf 앞에서 프레임 하나를 파싱. 모든 subspan/first/last 앞에 길이 검사!
static ParseResult parse_frame(std::span<const std::byte> buf) {
    (void)buf;
    return {ParseStatus::NeedMore, {}, 0}; /* TODO: empty → SOF → 헤더 길이 → LEN 상한 → 전체 길이 → CRC */
}

// ================= Part C: circular RX DMA 링 → 프레임 =================
struct DmaStreamRegs {
    volatile uint32_t CR;
    volatile uint32_t NDTR;                      // STM32 DMA_SxNDTR: 남은 전송 수 (감소, 0 → 자동 reload)
};
static DmaStreamRegs fake_dma;
#define DMA_RX (&fake_dma)

inline constexpr size_t kRxRingLen = 32;
alignas(32) static std::array<std::byte, kRxRingLen> g_dma_ring;   // DMA가 쓰는 링 (CPU는 읽기만)

// 하드웨어 흉내: 선에서 들어온 바이트를 링에 쓰고 NDTR 감소
static void sim_dma_rx(std::span<const std::byte> wire) {
    for (std::byte b : wire) {
        const size_t wr = kRxRingLen - DMA_RX->NDTR;
        g_dma_ring[wr] = b;
        const uint32_t left = DMA_RX->NDTR - 1u;
        DMA_RX->NDTR = (left == 0u) ? static_cast<uint32_t>(kRxRingLen) : left;
    }
}
// 주어진 것: DMA가 다음에 쓸 위치 (0..kRxRingLen-1)
static size_t dma_write_index() {
    const size_t ndtr = DMA_RX->NDTR;                        // volatile 읽기 1번
    std::atomic_signal_fence(std::memory_order_acquire);     // 컴파일러 배리어: 링 읽기를 이 위로 당기지 마라
    return (kRxRingLen - ndtr) % kRxRingLen;
}

struct RxChunks {
    std::span<const std::byte> first, second;    // 링 끝을 넘으면 두 조각
};
// C1: [rd, wr) 구간을 최대 두 개의 연속 span으로. 인덱스가 범위를 벗어나면 빈 결과
static RxChunks ring_readable(std::span<const std::byte> ring, size_t rd, size_t wr) {
    (void)ring; (void)rd; (void)wr;
    return {}; /* TODO: wr >= rd 이면 한 조각, 아니면 [rd, end) + [0, wr) */
}

using FrameHandler = void (*)(std::byte cmd, std::span<const std::byte> payload);
struct RxParser {
    size_t rd = 0;                               // 링에서 다음에 읽을 위치
    std::array<std::byte, 48> acc{};             // 프레임 조립용 선형 버퍼
    size_t acc_len = 0;
    uint32_t frames = 0, errors = 0, overflows = 0;
};
// C2: acc 뒤에 chunk를 붙인다. 자리가 없으면 overflows++, 쌓인 것 버리고 chunk의 마지막 부분만 보관
static void rx_append(RxParser& p, std::span<const std::byte> chunk) {
    (void)p; (void)chunk; /* TODO */
}
// C3: 새 DMA 데이터 수거 → 프레임마다 on_frame 호출 → 처리한 앞부분 버리기. 전달한 프레임 수 반환
static size_t rx_poll(RxParser& p, FrameHandler on_frame) {
    (void)p; (void)on_frame; (void)dma_write_index;
    /* TODO: ① wr = dma_write_index()  ② ring_readable → rx_append ×2, p.rd = wr
             ③ acc 앞에서부터 parse_frame 반복 (Ok → on_frame, Bad* → errors++, NeedMore → 멈춤)
             ④ 처리한 앞부분을 버리고 남은 바이트를 acc 앞으로 당긴다 */
    return 0;
}

// 테스트용 핸들러: span을 저장하지 않고 "복사"해 둔다
struct GotFrame {
    std::byte cmd;
    std::array<std::byte, kMaxPayload> data;
    size_t len;
};
static std::array<GotFrame, 8> g_got;
static size_t g_got_n;
static void on_frame(std::byte cmd, std::span<const std::byte> payload) {
    if (g_got_n >= g_got.size() || payload.size() > kMaxPayload) return;
    GotFrame& g = g_got[g_got_n++];
    g.cmd = cmd;
    g.len = payload.size();
    std::ranges::copy(payload, g.data.begin());
}
static bool got_is(size_t i, uint8_t cmd, std::span<const std::byte> payload) {
    return i < g_got_n && g_got[i].cmd == std::byte{cmd} && same(std::span{g_got[i].data}.first(g_got[i].len), payload);
}

// ---------------- 테스트 하네스 ----------------
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // ---- Part A ----
    wire_reset();
    const std::array<std::byte, 4> ping = bytes(0x01, 0x02, 0x03, 0x04);
    CHECK(uart_write(ping) == 4);                                          // std::array → span (암시적)
    const std::byte raw[2] = {std::byte{0xAA}, std::byte{0x55}};
    CHECK(uart_write(raw) == 2);                                           // C 배열 → span (길이 자동)
    const std::array<uint8_t, 3> u8 = {0x10, 0x20, 0x30};
    CHECK(uart_write(std::as_bytes(std::span{u8})) == 3);                  // uint8_t 버퍼는 as_bytes
    CHECK(uart_write(std::as_bytes(std::span{"AT\r\n"}).first<4>()) == 4); // 문자열 리터럴: NUL 빼고
    CHECK(g_wire_len == 13);
    CHECK(same(std::span{g_wire}.first(g_wire_len),
               bytes(1, 2, 3, 4, 0xAA, 0x55, 0x10, 0x20, 0x30, 'A', 'T', '\r', '\n')));
    CHECK(UART1->DR == '\n');

    wire_reset();
    const uint8_t legacy[5] = {9, 8, 7, 6, 5};
    CHECK(uart_write_legacy(legacy, sizeof legacy) == 5);
    CHECK(g_wire_len == 5 && g_wire[4] == std::byte{5});
    CHECK(uart_write_legacy(nullptr, 0) == 0);

    rx_load(bytes(0x34, 0x12, 0x02, 0x07, 0xEE));
    Cfg cfg{};
    CHECK(uart_read(std::as_writable_bytes(std::span{&cfg, 1})) == sizeof(Cfg));
    CHECK(cfg.id == 0x1234 && cfg.mode == 2 && cfg.gain == 7);
    std::array<std::byte, 4> tail{};
    CHECK(uart_read(tail) == 1 && tail[0] == std::byte{0xEE});            // FIFO에 남은 1바이트만

    CHECK(sizeof(std::span<const std::byte, 8>) == kStaticSpanWords * sizeof(void*));
    CHECK(sizeof(std::span<const std::byte>) == kDynamicSpanWords * sizeof(void*));

    // ---- Part B ----
    std::array<std::byte, 32> fb{};
    const auto pl = bytes(1, 2, 3);
    size_t n = encode_frame(fb, std::byte{0x10}, pl);
    CHECK(n == 7);
    CHECK(same(std::span{fb}.first(7), bytes(0xA5, 0x03, 0x10, 1, 2, 3, 0xD3)));
    ParseResult r = parse_frame(std::span{fb}.first(n));
    CHECK(r.status == ParseStatus::Ok && r.consumed == 7);
    CHECK(r.frame.cmd == std::byte{0x10} && same(r.frame.payload, pl));
    CHECK(r.frame.payload.data() == fb.data() + 3);                        // 복사 없이 원본을 가리킴

    n = encode_frame(fb, std::byte{0x01}, {});                             // payload 0바이트
    CHECK(n == 4 && fb[3] == std::byte{0x8F});
    r = parse_frame(std::span{fb}.first(n));
    CHECK(r.status == ParseStatus::Ok && r.frame.payload.empty());

    CHECK(parse_frame({}).status == ParseStatus::NeedMore);
    n = encode_frame(fb, std::byte{0x10}, pl);
    CHECK(parse_frame(std::span{fb}.first(2)).status == ParseStatus::NeedMore);      // 헤더 미완성
    CHECK(parse_frame(std::span{fb}.first(n > 0 ? n - 1 : 0)).status == ParseStatus::NeedMore);  // CRC 아직
    const auto lie = bytes(0xA5, 0xC8, 0x10, 0, 0);                        // LEN=200 이라고 주장
    r = parse_frame(lie);
    CHECK(r.status == ParseStatus::BadLen && r.consumed == 1);
    const auto lie2 = bytes(0xA5, 0x0F, 0x10, 1, 2);                       // LEN=15 인데 5바이트뿐
    CHECK(parse_frame(lie2).status == ParseStatus::NeedMore);
    const auto junk = bytes(0x00, 0xA5);
    r = parse_frame(junk);
    CHECK(r.status == ParseStatus::BadSof && r.consumed == 1);
    fb[4] ^= std::byte{0x01};                                              // payload 1비트 오류
    r = parse_frame(std::span{fb}.first(n));
    CHECK(r.status == ParseStatus::BadCrc && r.consumed == 1);

    const size_t n1 = encode_frame(fb, std::byte{0x10}, pl);               // 두 프레임 연속
    const size_t n2 = encode_frame(std::span{fb}.subspan(n1), std::byte{0x20}, bytes(0x55));
    const std::span<const std::byte> two = std::span{fb}.first(n1 + n2);
    r = parse_frame(two);
    CHECK(r.status == ParseStatus::Ok && r.consumed == n1);
    r = parse_frame(two.subspan(r.consumed));
    CHECK(r.status == ParseStatus::Ok && r.frame.cmd == std::byte{0x20} && r.frame.payload.size() == 1);

    std::array<std::byte, 5> small{};
    const std::array<std::byte, 17> big{};
    CHECK(encode_frame(small, std::byte{0x10}, pl) == 0);                  // 출력 자리 부족 (7 필요)
    CHECK(encode_frame(fb, std::byte{0x10}, big) == 0);                    // payload > kMaxPayload

    // ---- Part C ----
    const std::array<std::byte, 8> ring8{};
    RxChunks c = ring_readable(ring8, 2, 6);
    CHECK(c.first.data() == ring8.data() + 2 && c.first.size() == 4 && c.second.empty());
    c = ring_readable(ring8, 6, 3);
    CHECK(c.first.data() == ring8.data() + 6 && c.first.size() == 2);
    CHECK(c.second.data() == ring8.data() && c.second.size() == 3);
    c = ring_readable(ring8, 6, 0);
    CHECK(c.first.size() == 2 && c.second.empty());
    c = ring_readable(ring8, 5, 5);
    CHECK(c.first.empty() && c.second.empty());
    c = ring_readable(ring8, 9, 1);                                        // 잘못된 인덱스 → UB 대신 빈 결과
    CHECK(c.first.empty() && c.second.empty());

    RxParser ov{};
    const std::array<std::byte, 60> flood{};
    rx_append(ov, flood);
    CHECK(ov.overflows == 1 && ov.acc_len == 48);
    rx_append(ov, pl);
    CHECK(ov.overflows == 2 && ov.acc_len == 3);

    DMA_RX->NDTR = kRxRingLen;
    RxParser rx{};
    g_got_n = 0;
    std::array<std::byte, 24> tx{};
    n = encode_frame(tx, std::byte{0x10}, pl);                             // 링 [0,7)
    sim_dma_rx(std::span{tx}.first(n));
    CHECK(rx_poll(rx, on_frame) == 1);
    CHECK(got_is(0, 0x10, pl));

    n = encode_frame(tx, std::byte{0x20}, bytes(0xAA, 0xBB));              // 6바이트, 두 번에 나눠 도착
    sim_dma_rx(std::span{tx}.first(4));
    CHECK(rx_poll(rx, on_frame) == 0 && rx.acc_len == 4);
    if (n > 4) sim_dma_rx(std::span{tx}.subspan(4, n - 4));                // 링 [7,13) (n-4 언더플로 방지)
    CHECK(rx_poll(rx, on_frame) == 1 && rx.acc_len == 0);
    CHECK(got_is(1, 0x20, bytes(0xAA, 0xBB)));

    sim_dma_rx(bytes(0x00, 0xFF));                                         // 쓰레기 2바이트 → [13,15)
    n = encode_frame(tx, std::byte{0x30}, bytes(0x11));
    tx[3] ^= std::byte{0x01};                                              // CRC 깨진 프레임 → [15,20)
    sim_dma_rx(std::span{tx}.first(n));
    n = encode_frame(tx, std::byte{0x40}, bytes(0x11, 0x22, 0x33, 0x44, 0x55, 0x66));
    sim_dma_rx(std::span{tx}.first(n));                                    // 좋은 프레임 → [20,30)
    CHECK(rx_poll(rx, on_frame) == 1);
    CHECK(got_is(2, 0x40, bytes(0x11, 0x22, 0x33, 0x44, 0x55, 0x66)));
    CHECK(rx.errors == 7);                                                 // BadSof×2, BadCrc×1, BadSof×4

    n = encode_frame(tx, std::byte{0x50}, bytes(0xDE, 0xAD, 0xBE, 0xEF));  // 8바이트: [30,32) + [0,6) 랩어라운드
    sim_dma_rx(std::span{tx}.first(n));
    CHECK(rx_poll(rx, on_frame) == 1 && rx.rd == 6);
    CHECK(got_is(3, 0x50, bytes(0xDE, 0xAD, 0xBE, 0xEF)));

    n = encode_frame(tx, std::byte{0x60}, {});                             // 한 번 poll에 두 프레임
    sim_dma_rx(std::span{tx}.first(n));
    n = encode_frame(tx, std::byte{0x61}, bytes(0x01));
    sim_dma_rx(std::span{tx}.first(n));
    CHECK(rx_poll(rx, on_frame) == 2);
    CHECK(got_is(4, 0x60, {}) && got_is(5, 0x61, bytes(0x01)));
    CHECK(rx.frames == 6 && rx.overflows == 0 && rx.acc_len == 0);
    CHECK(rx_poll(rx, on_frame) == 0);                                     // 새 데이터 없음

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
