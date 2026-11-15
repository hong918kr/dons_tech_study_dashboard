// CPP-A10 추가 연습 — 한 번에 검증
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
//   연습 1: CRC-32 니블 표 (64 B) vs 바이트 표 (1 KB) vs 비트 루프 — 같은 값
//   연습 2: 수신 측 FrameSplitter — UART 바이트 스트림에서 0x00 으로 프레임을 자르고 쓰레기·과길이에서 재동기
//   연습 3: LossTracker — u16 seq 감싸기(wrap)를 넘어서 유실·중복 세기
//   연습 4: MovingAverage<N> — 2의 거듭제곱 창, 누적합 정수 이동평균 (무작위 입력으로 무차별 대입과 비교)
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

static int g_pass = 0, g_total = 0;
#define CHECK(expr)                                                                         \
    do {                                                                                    \
        ++g_total;                                                                          \
        if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); }                           \
        else std::printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                   \
    } while (0)

static std::uint32_t g_rng = 0x2545'F491u;
static std::uint32_t xorshift32() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ---------------- 연습 1: CRC-32 세 가지 ---------------- */
constexpr std::uint32_t kPoly = 0xEDB8'8320u;
constexpr std::array<std::uint32_t, 256> make_t256() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ kPoly : c >> 1; t[i] = c; }
    return t;
}
constexpr std::array<std::uint32_t, 16> make_t16() {      // 4비트씩: 표 16개 × 4 B = 64 B
    std::array<std::uint32_t, 16> t{};
    for (std::uint32_t i = 0; i < 16; ++i) { std::uint32_t c = i; for (int k = 0; k < 4; ++k) c = (c & 1u) ? (c >> 1) ^ kPoly : c >> 1; t[i] = c; }
    return t;
}
inline constexpr auto kT256 = make_t256();
inline constexpr auto kT16 = make_t16();
constexpr std::uint32_t crc_byte(std::span<const std::uint8_t> d) {
    std::uint32_t c = ~0u; for (std::uint8_t b : d) c = kT256[(c ^ b) & 0xFFu] ^ (c >> 8); return ~c;
}
constexpr std::uint32_t crc_nibble(std::span<const std::uint8_t> d) {
    std::uint32_t c = ~0u;
    for (std::uint8_t b : d) {
        c ^= b;
        c = kT16[c & 0xFu] ^ (c >> 4);                       // 아래 니블
        c = kT16[c & 0xFu] ^ (c >> 4);                       // 위 니블
    }
    return ~c;
}
constexpr std::uint32_t crc_bitwise(std::span<const std::uint8_t> d) {
    std::uint32_t c = ~0u;
    for (std::uint8_t b : d) { c ^= b; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (kPoly & (0u - (c & 1u))); }
    return ~c;
}
constexpr std::array<std::uint8_t, 9> kCheck{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(crc_byte(kCheck) == 0xCBF4'3926u && crc_nibble(kCheck) == 0xCBF4'3926u && crc_bitwise(kCheck) == 0xCBF4'3926u);

/* ---------------- 연습 2: FrameSplitter (본문 decode_frame 의 소형 사본 포함) ---------------- */
inline constexpr std::size_t kFrameSize = 26, kMaxEncoded = 27;   // 구분자 제외 COBS 최대 길이
struct Frame { std::uint16_t seq = 0; std::uint32_t t_ms = 0; };    // 연습에 필요한 필드만 비교

static std::size_t cobs_encode(std::span<const std::uint8_t> in, std::uint8_t* out) {   // 테스트용 송신 측
    std::size_t o = 1, code_at = 0; std::uint8_t code = 1;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != 0) { out[o++] = in[i]; ++code; }
        if (in[i] == 0 || code == 0xFF) { out[code_at] = code; code = 1; code_at = o++; }
    }
    out[code_at] = code;
    return o;
}
static bool cobs_decode(std::span<const std::uint8_t> in, std::uint8_t* out, std::size_t cap, std::size_t& n) {
    std::size_t i = 0; n = 0;
    while (i < in.size()) {
        const std::uint8_t code = in[i++];
        if (code == 0) return false;
        for (std::uint8_t k = 1; k < code; ++k) { if (i >= in.size() || in[i] == 0 || n >= cap) return false; out[n++] = in[i++]; }
        if (code != 0xFF && i < in.size()) { if (n >= cap) return false; out[n++] = 0; }
    }
    return true;
}
static std::array<std::uint8_t, kFrameSize> make_frame(std::uint16_t seq, std::uint32_t t_ms) {
    std::array<std::uint8_t, kFrameSize> b{};
    b[0] = 1; b[2] = std::uint8_t(seq); b[3] = std::uint8_t(seq >> 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = std::uint8_t(t_ms >> (8 * i));
    for (std::size_t i = 8; i < 22; ++i) b[i] = std::uint8_t(xorshift32());          // 센서 값 자리: 0x00 도 섞인다
    const std::uint32_t c = crc_byte(std::span<const std::uint8_t>(b.data(), 22));
    for (int i = 0; i < 4; ++i) b[22 + i] = std::uint8_t(c >> (8 * i));
    return b;
}

class FrameSplitter {
public:
    enum class Ev : std::uint8_t { None, Frame, Bad };
    // 바이트 하나 (UART RX ISR 이나 DMA 링 버퍼에서). 0x00 이면 모은 바이트를 판정한다.
    Ev feed(std::uint8_t b) {
        if (b != 0) {
            if (n_ < buf_.size()) buf_[n_++] = b; else overrun_ = true;    // 과길이: 다음 0x00 까지 버린다
            return Ev::None;
        }
        const bool over = overrun_;
        const std::size_t len = n_;
        n_ = 0; overrun_ = false;
        if (len == 0) return Ev::None;                                       // 연속 구분자 = 빈 프레임, 무시
        if (over) { ++bad; return Ev::Bad; }
        std::array<std::uint8_t, kFrameSize + 1> raw{};
        std::size_t n = 0;
        if (!cobs_decode(std::span<const std::uint8_t>(buf_.data(), len), raw.data(), raw.size(), n) || n != kFrameSize ||
            crc_byte(std::span<const std::uint8_t>(raw.data(), 22)) !=
                (raw[22] | raw[23] << 8 | std::uint32_t{raw[24]} << 16 | std::uint32_t{raw[25]} << 24)) {
            ++bad;
            return Ev::Bad;
        }
        last.seq = static_cast<std::uint16_t>(raw[2] | raw[3] << 8);
        last.t_ms = raw[4] | raw[5] << 8 | std::uint32_t{raw[6]} << 16 | std::uint32_t{raw[7]} << 24;
        ++good;
        return Ev::Frame;
    }
    Frame last{};
    std::uint32_t good = 0, bad = 0;
private:
    std::array<std::uint8_t, kMaxEncoded> buf_{};
    std::size_t n_ = 0;
    bool overrun_ = false;
};

/* ---------------- 연습 3: LossTracker ---------------- */
class LossTracker {
public:
    void on_seq(std::uint16_t s) {
        if (!started_) { started_ = true; next_ = static_cast<std::uint16_t>(s + 1u); return; }
        const std::uint16_t gap = static_cast<std::uint16_t>(s - next_);    // 모듈러 2^16 거리
        if (gap < 0x8000u) { lost += gap; next_ = static_cast<std::uint16_t>(s + 1u); }   // 앞으로 감: gap 개 유실
        else ++stale;                                                        // 뒤로 감: 중복/재전송/순서 뒤바뀜
    }
    std::uint32_t lost = 0, stale = 0;
private:
    std::uint16_t next_ = 0;
    bool started_ = false;
};

/* ---------------- 연습 4: MovingAverage<N> ---------------- */
template <std::size_t N>
class MovingAverage {
    static_assert(N >= 1 && (N & (N - 1)) == 0, "N must be a power of two (shift instead of divide)");
    static_assert(N <= 65536, "32768·N must fit int32");
    static constexpr int kShift = [] { int s = 0; while ((std::size_t{1} << s) < N) ++s; return s; }();
public:
    std::int16_t step(std::int16_t x) {
        sum_ += x - win_[i_];                     // 빠지는 값 빼고 들어오는 값 더하기: O(1)
        win_[i_] = x;
        i_ = (i_ + 1) & (N - 1);
        if constexpr (kShift == 0) return static_cast<std::int16_t>(sum_);
        else return static_cast<std::int16_t>((sum_ + (std::int32_t{1} << (kShift - 1))) >> kShift);   // half-up
    }
private:
    std::array<std::int16_t, N> win_{};
    std::int32_t sum_ = 0;
    std::size_t i_ = 0;
};

int main() {
    std::printf("--- 연습 1: CRC 세 가지 ---\n");
    {
        bool same = true;
        g_rng = 0xC4C3'2001u;
        for (int t = 0; t < 300; ++t) {
            std::array<std::uint8_t, 64> d{};
            const std::size_t len = xorshift32() % 65u;
            for (std::size_t i = 0; i < len; ++i) d[i] = static_cast<std::uint8_t>(xorshift32());
            const std::span<const std::uint8_t> s(d.data(), len);
            same = same && crc_byte(s) == crc_nibble(s) && crc_byte(s) == crc_bitwise(s);
        }
        CHECK(same);
        CHECK(sizeof(kT16) == 64 && sizeof(kT256) == 1024);
        CHECK(crc_nibble(std::span<const std::uint8_t>{}) == 0u);          // 빈 입력: ~(~0) = 0
    }
    std::printf("--- 연습 2: FrameSplitter ---\n");
    {
        std::array<std::uint8_t, 4096> stream{};
        std::size_t n = 0;
        auto put = [&](std::uint8_t b) { if (n < stream.size()) stream[n++] = b; };
        g_rng = 0x5EED'0002u;
        int sent = 0;
        for (std::uint16_t k = 0; k < 40; ++k) {
            const auto f = make_frame(k, 1000u + k * 10u);
            std::array<std::uint8_t, 32> enc{};
            const std::size_t m = cobs_encode(f, enc.data());
            if (k == 7) { for (std::size_t i = 0; i < 10; ++i) put(enc[i]); put(0); }          // 송신 중단 → 잘린 프레임
            else if (k == 13) { for (int i = 0; i < 50; ++i) put(0x55); put(0); }               // 쓰레기 50바이트 (과길이)
            else if (k == 21) { enc[5] ^= 0x04; for (std::size_t i = 0; i < m; ++i) put(enc[i]); put(0); }   // 비트 오류
            else { for (std::size_t i = 0; i < m; ++i) put(enc[i]); put(0); ++sent; }
            if (k % 9 == 0) put(0);                                                             // 여분 구분자
        }
        FrameSplitter fs;
        std::uint16_t prev = 0xFFFF;
        bool order = true;
        for (std::size_t i = 0; i < n; ++i)
            if (fs.feed(stream[i]) == FrameSplitter::Ev::Frame) {
                order = order && static_cast<std::uint16_t>(fs.last.seq) != prev && fs.last.t_ms == 1000u + fs.last.seq * 10u;
                prev = fs.last.seq;
            }
        CHECK(static_cast<int>(fs.good) == sent && sent == 37);
        CHECK(fs.bad == 3 && order);
        FrameSplitter g;                                      // 처음부터 0x00 없이 시작한 중간 바이트(쓰레기) → 첫 0x00 에서 Bad 1개 후 정상
        for (int i = 0; i < 5; ++i) (void)g.feed(0x33);
        CHECK(g.feed(0) == FrameSplitter::Ev::Bad && g.bad == 1);
    }
    std::printf("--- 연습 3: LossTracker ---\n");
    {
        LossTracker t;
        for (std::uint32_t s = 65530; s < 65536 + 5; ++s) t.on_seq(static_cast<std::uint16_t>(s));   // 감싸기
        CHECK(t.lost == 0 && t.stale == 0);
        t.on_seq(7);                                          // 5, 6 유실
        CHECK(t.lost == 2);
        t.on_seq(7); t.on_seq(3);                             // 중복, 옛 번호
        CHECK(t.stale == 2 && t.lost == 2);
        t.on_seq(8);
        CHECK(t.lost == 2 && t.stale == 2);
        LossTracker w;
        w.on_seq(65534); w.on_seq(1);                         // 65535, 0 유실
        CHECK(w.lost == 2);
    }
    std::printf("--- 연습 4: MovingAverage ---\n");
    {
        MovingAverage<8> ma;
        std::array<std::int16_t, 8> hist{};
        bool ok = true;
        g_rng = 0x0A0A'0004u;
        for (int i = 0; i < 5000; ++i) {
            const auto x = static_cast<std::int16_t>(xorshift32());
            hist[static_cast<std::size_t>(i) % 8] = x;
            std::int32_t s = 0;
            for (std::int16_t h : hist) s += h;
            ok = ok && ma.step(x) == static_cast<std::int16_t>((s + 4) >> 3);
        }
        CHECK(ok);                                            // 누적합이 무차별 합과 매번 같다 (오차 누적 없음)
        MovingAverage<4> st;
        std::int16_t y = 0;
        for (int i = 0; i < 3; ++i) y = st.step(100);
        CHECK(y == 75 && st.step(100) == 100 && st.step(100) == 100);   // 4샘플 뒤 정확히 도달
        MovingAverage<1> one;
        CHECK(one.step(-32768) == -32768 && one.step(32767) == 32767);
    }
    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
