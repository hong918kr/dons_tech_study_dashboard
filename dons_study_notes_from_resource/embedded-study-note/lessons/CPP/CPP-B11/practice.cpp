// CPP-B11 추가 연습 — 한 번에 검증
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>
#include <utility>

template <typename... Ts>
constexpr std::array<std::byte, sizeof...(Ts)> bytes(Ts... v) { return {static_cast<std::byte>(v)...}; }
static bool same(std::span<const std::byte> a, std::span<const std::byte> b) { return std::ranges::equal(a, b); }

// 연습 1: prefix 비교 — first() 앞에 길이 검사
static bool starts_with(std::span<const std::byte> s, std::span<const std::byte> prefix) {
    return s.size() >= prefix.size() && std::ranges::equal(s.first(prefix.size()), prefix);
}

// 연습 2: 정적 extent로 "정확히 N바이트"를 타입에 박는다
static uint16_t get_le16(std::span<const std::byte, 2> b) {
    return static_cast<uint16_t>(std::to_integer<uint16_t>(b[0]) | (std::to_integer<uint16_t>(b[1]) << 8));
}
static uint32_t get_be32(std::span<const std::byte, 4> b) {
    return (std::to_integer<uint32_t>(b[0]) << 24) | (std::to_integer<uint32_t>(b[1]) << 16) |
           (std::to_integer<uint32_t>(b[2]) << 8) | std::to_integer<uint32_t>(b[3]);
}
static_assert(!std::is_convertible_v<std::span<const std::byte, 3>, std::span<const std::byte, 4>>);  // 크기 다르면 컴파일 에러
static_assert(!std::is_convertible_v<std::span<const std::byte>, std::span<const std::byte, 4>>);     // dynamic → static 은 explicit

// 연습 3: 절대 UB가 안 나는 subspan — off + cnt 덧셈 오버플로까지 막는다
static bool checked_subspan(std::span<const std::byte> s, size_t off, size_t cnt, std::span<const std::byte>& out) {
    if (off > s.size() || cnt > s.size() - off) return false;   // off + cnt <= size() 로 쓰면 랩어라운드!
    out = s.subspan(off, cnt);
    return true;
}

// 연습 4: 큰 버퍼를 page 단위로 잘라 쓰기 (SPI flash page program / DMA 최대 전송 길이)
static size_t g_chunk_sizes[8];
static size_t g_chunk_n;
static void flash_page_write(size_t addr, std::span<const std::byte> page) {
    (void)addr;
    if (g_chunk_n < 8) g_chunk_sizes[g_chunk_n++] = page.size();
}
static size_t write_in_chunks(size_t addr, std::span<const std::byte> data, size_t chunk) {
    if (chunk == 0) return 0;
    size_t n = 0;
    while (!data.empty()) {
        const size_t k = std::min(chunk, data.size());
        flash_page_write(addr, data.first(k));
        data = data.subspan(k);                 // k <= size() 이므로 안전
        addr += k;
        ++n;
    }
    return n;
}

// 연습 5: 바이트 → trivially copyable 구조체 (as_writable_bytes)
struct SensorSample {
    uint16_t id;
    int16_t temp_c10;   // 0.1도 단위
};
template <typename T>
static bool load_pod(std::span<const std::byte> src, T& out) {
    static_assert(std::is_trivially_copyable_v<T>, "memcpy로 옮길 수 있는 타입만");
    const std::span<std::byte, sizeof(T)> dst = std::as_writable_bytes(std::span<T, 1>{&out, 1});
    if (src.size() < dst.size()) return false;
    std::ranges::copy(src.first(dst.size()), dst.begin());
    return true;
}
static_assert(decltype(std::as_writable_bytes(std::declval<std::span<SensorSample, 1>>()))::extent == 4);
static_assert(std::endian::native == std::endian::little);

// 연습 6: span을 "반환"하는 올바른 방법 — 호출자 버퍼 안의 실제 쓴 부분
static std::span<std::byte> format_ack(std::span<std::byte> out, uint8_t seq) {
    constexpr size_t kAckLen = 3;
    if (out.size() < kAckLen) return {};
    out[0] = std::byte{0x06};                    // ACK
    out[1] = std::byte{seq};
    out[2] = std::byte{seq} ^ std::byte{0xFF};   // 보수 체크
    return out.first(kAckLen);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main() {
    // 1
    const auto rx = bytes('O', 'K', '\r', '\n');
    CHECK(starts_with(rx, std::as_bytes(std::span{"OK"}).first<2>()));
    CHECK(!starts_with(rx, std::as_bytes(std::span{"ERROR"}).first<5>()));   // prefix가 더 길어도 UB 없음
    CHECK(starts_with(rx, {}));

    // 2
    const auto pkt = bytes(0x7E, 0x34, 0x12, 0xDE, 0xAD, 0xBE, 0xEF);
    const std::span<const std::byte> p = pkt;
    CHECK(p.size() >= 7);                                         // 아래 subspan<> 들의 전제조건 확인
    CHECK(get_le16(p.subspan<1, 2>()) == 0x1234);
    CHECK(get_be32(p.subspan<3, 4>()) == 0xDEADBEEFu);
    CHECK(get_be32(std::span<const std::byte, 4>{p.data() + 3, 4}) == 0xDEADBEEFu);

    // 3
    std::span<const std::byte> out;
    CHECK(checked_subspan(p, 3, 4, out) && out.size() == 4 && out[0] == std::byte{0xDE});
    CHECK(checked_subspan(p, 7, 0, out) && out.empty());          // 끝 위치 + 0개는 합법
    CHECK(!checked_subspan(p, 8, 0, out));
    CHECK(!checked_subspan(p, 2, SIZE_MAX, out));                 // off + cnt 가 랩어라운드하는 공격 입력

    // 4
    const std::array<std::byte, 70> image{};
    CHECK(write_in_chunks(0x1000, image, 32) == 3);
    CHECK(g_chunk_n == 3 && g_chunk_sizes[0] == 32 && g_chunk_sizes[1] == 32 && g_chunk_sizes[2] == 6);
    CHECK(write_in_chunks(0, image, 0) == 0);

    // 5
    SensorSample s{};
    CHECK(load_pod(bytes(0x01, 0x00, 0xF6, 0xFF), s) && s.id == 1 && s.temp_c10 == -10);
    CHECK(!load_pod(bytes(0x01, 0x00), s));

    // 6
    std::array<std::byte, 8> txbuf{};
    const std::span<std::byte> ack = format_ack(txbuf, 0x05);
    CHECK(ack.size() == 3 && ack.data() == txbuf.data());
    CHECK(same(ack, bytes(0x06, 0x05, 0xFA)));
    std::array<std::byte, 2> tiny{};
    CHECK(format_ack(tiny, 1).empty());

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
