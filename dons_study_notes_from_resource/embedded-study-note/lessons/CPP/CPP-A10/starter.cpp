// CPP-A10 종합: 정적 할당 C++ 센서 파이프라인
//   mock I2C 버스 → IMU 드라이버 → 고정소수점 필터 → 텔레메트리 프레임. 힙 0 · 예외 0.
//   Part A: xfer(버스 잠금 RAII + 에러 문맥) · probe · configure · read_sample · mul_q15 · LowPass · pack_frame
//   Part B: retry_transient · COBS encode/decode · decode_frame (+ 장애 주입·고정소수점 경계 테스트)
//   Part C: Pipeline::step — 가상 시간 시나리오, golden frame, 스트림 round-trip, 힙 0
//   build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
//   타깃 빌드: fw_main.cpp 가 A10_LIB_ONLY 를 정의하고 이 파일을 include 한다 (budget.sh). 아래 #ifndef 블록은 호스트 전용.
//   소형판 출처 (각 레슨이 주인): Result(CPP-M11) · static_vector(CPP-M10) · DevReg 권한(CPP-M01)
//                              · BusLock RAII(CPP-B06) · constexpr CRC 표(CPP-M13)
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace a10 {

[[noreturn]] inline void trap() { __builtin_trap(); }   // 펌웨어: UDF → HardFault (fault 로그 후 리셋)

/* ================= core: 에러 · Result · static_vector ================= */
enum class BusErr : std::uint8_t { None, AddrNack, DataNack, Timeout, ArbLost };
enum class Code : std::uint8_t { None, Bus, Busy, BadId, NotReady, BadConfig, NoSpace };
struct Error {                       // 계층 매핑(BusErr→DrvErr→AppErr)은 CPP-M11. 여기선 코드 + 원인 + 문맥 한 덩어리
    Code code = Code::None;
    BusErr bus = BusErr::None;       // code == Bus 일 때 원인
    std::uint8_t reg = 0;            // 어느 레지스터에서 났나
};
template <class E> struct Fail { E e; };
template <class E> constexpr Fail<E> fail(E e) { return Fail<E>{e}; }

// CPP-M11 소형판: T 는 trivially copyable + default constructible 만 (union/수명 관리는 M11 몫)
template <class T, class E>
class [[nodiscard]] Result {
    static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>, "compact Result: trivial T only");
public:
    constexpr Result(T v) : v_(v), e_{}, ok_(true) {}
    constexpr Result(Fail<E> f) : v_{}, e_(f.e), ok_(false) {}
    constexpr explicit operator bool() const { return ok_; }
    constexpr bool ok() const { return ok_; }
    constexpr T value() const { if (!ok_) trap(); return v_; }
    constexpr E error() const { return e_; }
private:
    T v_;
    E e_;
    bool ok_;
};
template <class E>
class [[nodiscard]] Result<void, E> {
public:
    constexpr Result() : e_{}, ok_(true) {}
    constexpr Result(Fail<E> f) : e_(f.e), ok_(false) {}
    constexpr explicit operator bool() const { return ok_; }
    constexpr bool ok() const { return ok_; }
    constexpr E error() const { return e_; }
private:
    E e_;
    bool ok_;
};
// 표준 C++ 만 쓰는 전파 (CPP-M11 의 TRY 는 statement expression 확장)
#define A10_TRY(...) \
    do { if (auto&& a10_r_ = (__VA_ARGS__); !a10_r_) return ::a10::fail(a10_r_.error()); } while (0)

// CPP-M10 소형판: trivial T 만 → 평범한 배열로 충분 (placement new·수명 관리는 M10 몫)
template <class T, std::size_t N>
class static_vector {
    static_assert(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                  "compact static_vector: trivial T only (full version: CPP-M10)");
public:
    [[nodiscard]] constexpr bool push_back(const T& v) { if (n_ == N) return false; buf_[n_++] = v; return true; }
    constexpr void clear() { n_ = 0; }
    constexpr std::size_t size() const { return n_; }
    static constexpr std::size_t capacity() { return N; }
    constexpr bool empty() const { return n_ == 0; }
    constexpr T& operator[](std::size_t i) { return buf_[i]; }
    constexpr const T& operator[](std::size_t i) const { return buf_[i]; }
    constexpr T* begin() { return buf_.data(); }
    constexpr T* end() { return buf_.data() + n_; }
    constexpr const T* begin() const { return buf_.data(); }
    constexpr const T* end() const { return buf_.data() + n_; }
    constexpr std::span<const T> view() const { return {buf_.data(), n_}; }
private:
    std::array<T, N> buf_{};
    std::size_t n_ = 0;
};

/* ================= bus: 원격 레지스터 권한 · 버스 잠금 · 전송 ================= */
namespace bus {
struct RO { static constexpr bool can_read = true, can_write = false; };
struct RW { static constexpr bool can_read = true, can_write = true; };
// CPP-M01 의 Reg<Addr,T,Access> 변형: 주소는 NTTP, 권한은 타입. 다만 버스는 "객체" 이고 실패할 수 있다 → Result
template <std::uint8_t A, class Acc> struct DevReg { static constexpr std::uint8_t addr = A; using access = Acc; };

template <class B>
concept I2cBus = requires(B& b, std::uint8_t a, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) {
    { b.write_read(a, tx, rx) } -> std::same_as<Result<void, BusErr>>;   // write 후 repeated START 로 read
    { b.try_lock() } -> std::same_as<bool>;
    b.unlock();
};

// CPP-B06 의 CriticalSection 과 같은 모양: 생성자에서 잡고, 어떤 return 경로든 소멸자에서 놓는다
template <class B>
class [[nodiscard]] BusLock {
public:
    explicit BusLock(B& b) : b_(b), owned_(b.try_lock()) {}
    ~BusLock() { if (owned_) b_.unlock(); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
    bool owned() const { return owned_; }
private:
    B& b_;
    bool owned_;
};

inline constexpr std::size_t kMaxPayload = 8;

constexpr bool is_transient(const Error& e) {
    return e.code == Code::Bus && (e.bus == BusErr::DataNack || e.bus == BusErr::Timeout || e.bus == BusErr::ArbLost);
}

// A1. 한 트랜잭션: 버스를 잠그고 [reg, payload...] 를 쓴 뒤 rx 만큼 읽는다. 실패하면 Error{Bus, 원인, reg}.
//     잠금 실패 → Error{Busy}, payload 가 kMaxPayload 초과 → Error{BadConfig}. 어떤 경로든 잠금은 풀려야 한다.
template <I2cBus B>
Result<void, Error> xfer(B& b, std::uint8_t dev, std::uint8_t reg, std::span<const std::uint8_t> payload,
                         std::span<std::uint8_t> rx) {
    (void)b; (void)dev; (void)payload; (void)rx;   // TODO(A1)
    return fail(Error{Code::Bus, BusErr::Timeout, reg});
}

// B1. f() 를 부르고, 일시적 오류(is_transient)면 최대 extra 번 더 부른다. 영구 오류(AddrNack, BadId…)는 바로 돌려준다.
template <class F>
auto retry_transient(F&& f, int extra) -> decltype(f()) {
    (void)extra;   // TODO(B1): 일시적 오류면 다시
    return f();
}

// 권한이 타입에 있으므로 RO 레지스터에 write_reg 는 후보에서 빠진다 (requires 실패 → 컴파일 에러)
template <class R, I2cBus B> requires R::access::can_read
Result<std::uint8_t, Error> read_reg(B& b, std::uint8_t dev) {
    std::array<std::uint8_t, 1> v{};
    A10_TRY(xfer(b, dev, R::addr, {}, v));
    return v[0];
}
template <class R, I2cBus B> requires R::access::can_write
Result<void, Error> write_reg(B& b, std::uint8_t dev, std::uint8_t v) {
    const std::array<std::uint8_t, 1> p{v};
    return xfer(b, dev, R::addr, p, {});
}
template <class R, I2cBus B> requires R::access::can_read
Result<void, Error> read_burst(B& b, std::uint8_t dev, std::span<std::uint8_t> out) {
    return xfer(b, dev, R::addr, {}, out);
}
}  // namespace bus

/* ================= imu: 레지스터 맵 (대표값 — LSM6DSO 류를 본뜸, 데이터시트 미확인) ================= */
namespace imu {
inline constexpr std::uint8_t kAddr = 0x6A;          // 7비트 주소 (SA0=0)
inline constexpr std::uint8_t kWhoAmIValue = 0x6C;
using WHO_AM_I = bus::DevReg<0x0F, bus::RO>;
using CTRL1_XL = bus::DevReg<0x10, bus::RW>;         // [7:4] ODR, [3:2] FS
using CTRL2_G  = bus::DevReg<0x11, bus::RW>;         // [7:4] ODR, [3:2] FS
using CTRL3_C  = bus::DevReg<0x12, bus::RW>;         // BDU bit6, IF_INC bit2
using STATUS   = bus::DevReg<0x1E, bus::RO>;         // GDA bit1, XLDA bit0
using OUTX_L_G = bus::DevReg<0x22, bus::RO>;         // 0x22..0x27 자이로 xyz, 0x28..0x2D 가속도 xyz (int16 LE)
inline constexpr std::uint8_t CTRL3_BDU = 1u << 6, CTRL3_IF_INC = 1u << 2;
inline constexpr std::uint8_t STATUS_XLDA = 1u << 0, STATUS_GDA = 1u << 1;

enum class Odr : std::uint8_t { Off = 0, Hz12_5 = 1, Hz26 = 2, Hz52 = 3, Hz104 = 4, Hz208 = 5 };
enum class AccelFs : std::uint8_t { G2 = 0, G16 = 1, G4 = 2, G8 = 3 };      // 코드 순서가 단조롭지 않다 (대표값)
enum class GyroFs : std::uint8_t { Dps250 = 0, Dps500 = 1, Dps1000 = 2, Dps2000 = 3 };
struct Config { Odr odr = Odr::Hz104; AccelFs fs_xl = AccelFs::G2; GyroFs fs_g = GyroFs::Dps250; };

constexpr std::uint8_t ctrl1(const Config& c) { return static_cast<std::uint8_t>(std::uint8_t(c.odr) << 4 | std::uint8_t(c.fs_xl) << 2); }
constexpr std::uint8_t ctrl2(const Config& c) { return static_cast<std::uint8_t>(std::uint8_t(c.odr) << 4 | std::uint8_t(c.fs_g) << 2); }
constexpr std::uint32_t accel_ug_per_lsb(AccelFs fs) {   // 감도 대표값 (µg/LSB)
    switch (fs) { case AccelFs::G2: return 61; case AccelFs::G4: return 122; case AccelFs::G8: return 244; case AccelFs::G16: return 488; }
    return 0;
}

struct Raw { std::array<std::int16_t, 3> g, a; };     // 레지스터 순서 그대로: 자이로 먼저

constexpr std::int16_t le_i16(const std::uint8_t* p) {   // C++20: uint16→int16 변환은 모듈러로 정의됨
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | p[1] << 8));
}

// A2. WHO_AM_I 를 읽어 kWhoAmIValue 가 아니면 Error{BadId, None, WHO_AM_I::addr}.
template <bus::I2cBus B>
Result<void, Error> probe(B& b) {
    (void)b;   // TODO(A2)
    return fail(Error{Code::BadId, BusErr::None, 0});
}

// A3. CTRL3_C(BDU|IF_INC) 를 "먼저" 쓰고 CTRL1_XL, CTRL2_G 를 쓴 뒤, 0x10..0x12 를 한 번의 버스트로 읽어 검증.
//     다르면 Error{BadConfig, None, 처음 틀린 레지스터 주소}.
template <bus::I2cBus B>
Result<void, Error> configure(B& b, const Config& c) {
    (void)b; (void)c;   // TODO(A3)
    return fail(Error{Code::BadConfig, BusErr::None, 0});
}

// A4. STATUS 의 XLDA·GDA 가 둘 다 1 이 아니면 Error{NotReady, None, STATUS::addr}.
//     둘 다 1 이면 OUTX_L_G 부터 12바이트 버스트 1번 → Raw (LE int16).
template <bus::I2cBus B>
Result<Raw, Error> read_sample(B& b) {
    (void)b;   // TODO(A4)
    return fail(Error{Code::NotReady, BusErr::None, 0});
}
}  // namespace imu

/* ================= dsp: Q15 / Q16.16 고정소수점 ================= */
namespace dsp {
constexpr std::int16_t sat16(std::int32_t v) {
    return static_cast<std::int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

// A5. Q15 × Q15 → Q15. 반올림은 half-up (+2^14 후 >>15, C++20 의 >> 는 음수에서 floor), 결과는 포화.
//     -1 × -1 = +1 은 Q15 로 표현 불가 → 32767.
constexpr std::int16_t mul_q15(std::int16_t a, std::int16_t b) {
    return static_cast<std::int16_t>((std::int32_t{a} * b) >> 15);   // TODO(A5): 내림 + 감싸기 (반올림·포화 없음)
}

void alpha_out_of_range();   // 정의 없음 (non-constexpr): consteval 안에서 불리면 컴파일 에러가 된다
// RC 저역통과의 이산화 α = ω/(ω+1), ω = 2π·fc/fs. std::exp 는 C++20 에서 constexpr 이 아니라 이 근사를 쓴다.
consteval std::uint16_t alpha_q15(double fc_hz, double fs_hz) {
    if (!(fc_hz > 0.0 && fs_hz > 0.0 && fc_hz < fs_hz / 2.0)) alpha_out_of_range();
    const double w = 6.283185307179586 * fc_hz / fs_hz;
    return static_cast<std::uint16_t>(w / (w + 1.0) * 32768.0 + 0.5);
}

// 1차 IIR: y += α(x − y). 상태 s 는 출력 LSB 아래로 F 비트를 더 가진다 (Q15 계수, 상태 정수부 16비트 + 소수 F비트).
template <int F>
class LowPass {
    static_assert(F >= 0 && F <= 15, "LowPass<F>: |x·2^F − s| must fit int32 (F ≤ 15)");
public:
    constexpr explicit LowPass(std::uint16_t alpha_q15) : a_(alpha_q15) {}
    constexpr std::int16_t step(std::int16_t x);
    constexpr std::int32_t state() const { return s_; }
private:
    std::int32_t s_ = 0;
    std::uint16_t a_;
    bool primed_ = false;
};

// A6. 첫 샘플은 상태를 x·2^F 로 채우고 x 를 그대로 반환 (시작 과도응답 제거).
//     이후 d = x·2^F − s (int32), s += round_half_up(d·α / 2^15) (곱은 int64), 출력 = round_half_up(s / 2^F) 포화.
template <int F>
constexpr std::int16_t LowPass<F>::step(std::int16_t x) {
    (void)x;   // TODO(A6)
    return 0;
}

// raw → mg: Q16.16 배율. k < 2^15 이면 |raw·k| < 2^30 이라 32비트 곱으로 충분 (M4: smulbb/mul 한 개)
constexpr std::int32_t scale_q16(std::uint32_t ug_per_lsb) {
    return static_cast<std::int32_t>((ug_per_lsb * 65536u + 500u) / 1000u);
}
constexpr std::int16_t to_mg(std::int16_t raw, std::int32_t k_q16) {
    return sat16((std::int32_t{raw} * k_q16 + 0x8000) >> 16);
}
static_assert(scale_q16(488) < 32768, "to_mg: k_q16 must stay below 2^15 to keep the product in int32");
}  // namespace dsp

/* ================= tlm: 프레임 · CRC-32 · COBS ================= */
namespace tlm {
// CRC-32 (IEEE 802.3 / zlib, reflected 0xEDB88320) — CPP-M13 소형판. 표는 constexpr → .rodata 1 KB
constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB8'8320u : (c >> 1);
        t[i] = c;
    }
    return t;
}
inline constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();
constexpr std::uint32_t crc32(std::span<const std::uint8_t> d) {
    std::uint32_t c = 0xFFFF'FFFFu;
    for (std::uint8_t b : d) c = kCrcTable[(c ^ b) & 0xFFu] ^ (c >> 8);
    return ~c;
}
constexpr std::uint32_t crc32_check() {   // 표준 검사값: "123456789" → 0xCBF43926
    const std::array<std::uint8_t, 9> s{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    return crc32(s);
}
static_assert(crc32_check() == 0xCBF4'3926u);

// 와이어 형식 v1 (리틀 엔디언, 패딩 없음):
//  0 ver u8 | 1 flags u8 | 2 seq u16 | 4 t_ms u32 | 8 accel_mg i16×3 | 14 gyro i16×3 | 20 err_count u16 | 22 crc32 u32
inline constexpr std::size_t kFrameSize = 26, kCrcOffset = 22;
inline constexpr std::uint8_t kVersion = 1, kFlagErrSinceLast = 0x01;
inline constexpr std::size_t kWireMax = kFrameSize + 1 + 1;    // COBS 오버헤드 1 (254바이트 이하) + 구분자 0x00
struct Frame {
    std::uint8_t ver = 0, flags = 0;
    std::uint16_t seq = 0;
    std::uint32_t t_ms = 0;
    std::array<std::int16_t, 3> accel_mg{}, gyro{};
    std::uint16_t err_count = 0;
    friend constexpr bool operator==(const Frame&, const Frame&) = default;
};
enum class DecodeErr : std::uint8_t { None, Cobs, BadLength, BadCrc, BadVersion };

constexpr void put_le16(std::uint8_t* p, std::uint16_t v) { p[0] = std::uint8_t(v); p[1] = std::uint8_t(v >> 8); }
constexpr void put_le32(std::uint8_t* p, std::uint32_t v) { put_le16(p, std::uint16_t(v)); put_le16(p + 2, std::uint16_t(v >> 16)); }
constexpr std::uint16_t get_le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
constexpr std::uint32_t get_le32(const std::uint8_t* p) { return get_le16(p) | std::uint32_t{get_le16(p + 2)} << 16; }

// A7. Frame → 26바이트 (위 표의 오프셋, LE). 마지막 4바이트 = crc32(앞 22바이트) LE.
constexpr std::array<std::uint8_t, kFrameSize> pack_frame(const Frame& f) {
    (void)f;   // TODO(A7)
    return {};
}

// B2. COBS 인코드 (구분자 0x00 은 붙이지 않는다). 공간이 모자라면 false. 0바이트 입력 → {0x01}.
//     254개 연속 비영 바이트로 끝나면 꼬리 0x01 블록을 만들지 않는다 (위키백과 예제와 같은 변형).
template <std::size_t N>
constexpr bool cobs_encode(std::span<const std::uint8_t> in, static_vector<std::uint8_t, N>& out) {
    (void)in; (void)out;   // TODO(B2)
    return false;
}

// B3. COBS 디코드 (구분자 제외한 바이트). 코드 0, 블록이 입력 끝을 넘음, 데이터 안의 0x00, out 부족 → DecodeErr::Cobs.
//     코드 < 0xFF 인 블록 뒤에는 (입력이 더 남아 있을 때만) 0x00 을 하나 붙인다.
constexpr Result<std::size_t, DecodeErr> cobs_decode(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) {
    (void)in; (void)out;   // TODO(B3)
    return fail(DecodeErr::Cobs);
}

// B4. COBS 해제 → 길이 == 26 (아니면 BadLength) → CRC (BadCrc) → 버전 (BadVersion) → 필드 복원.
constexpr Result<Frame, DecodeErr> decode_frame(std::span<const std::uint8_t> wire) {
    (void)wire;   // TODO(B4)
    return fail(DecodeErr::Cobs);
}
}  // namespace tlm

/* ================= app: 파이프라인 ================= */
namespace app {
inline constexpr int kFracBits = 8;
inline constexpr int kExtraRetries = 1;
inline constexpr std::uint16_t kAlpha = dsp::alpha_q15(5.0, 104.0);   // fc 5 Hz @ ODR 104 Hz

template <bus::I2cBus B>
class Pipeline {
    using LP = dsp::LowPass<kFracBits>;
public:
    constexpr Pipeline(B& b, imu::Config c, std::uint16_t alpha)
        : bus_(b), cfg_(c), k_mg_(dsp::scale_q16(imu::accel_ug_per_lsb(c.fs_xl))),
          lp_{LP{alpha}, LP{alpha}, LP{alpha}, LP{alpha}, LP{alpha}, LP{alpha}} {}

    Result<void, Error> init() {
        A10_TRY(imu::probe(bus_));
        return imu::configure(bus_, cfg_);
    }

    // C1. 샘플 1개 → 프레임 1개를 out 끝에 [COBS 바이트..., 0x00] 로 붙이고 붙인 바이트 수를 돌려준다.
    //   - read_sample 은 retry_transient(…, kExtraRetries) 로. NotReady → 0 (아무 상태도 안 바뀜).
    //   - 그 밖의 오류 → errs_ 포화 증가, err_since_last_ = true, 오류 반환 (프레임 없음, seq 그대로).
    //   - 필터: 가속도 lp_[0..2] → to_mg(·, k_mg_), 자이로 lp_[3..5] (raw 카운트).
    //   - flags = err_since_last_ ? kFlagErrSinceLast : 0, err_count = errs_, seq = seq_.
    //   - seq_ 는 프레임을 "만든" 순간 증가 (out 이 모자라 버려도) → 수신 측은 seq 틈으로 유실을 안다.
    //   - out 남은 공간 < 인코딩 길이 → Error{NoSpace} (반쪽 프레임을 쓰지 않는다). 성공했을 때만 err_since_last_ = false.
    template <std::size_t N>
    Result<std::size_t, Error> step(std::uint32_t t_ms, static_vector<std::uint8_t, N>& out) {
        (void)t_ms; (void)out;   // TODO(C1)
        return std::size_t{0};
    }

    std::uint16_t seq() const { return seq_; }
    std::uint16_t err_count() const { return errs_; }

private:
    B& bus_;
    imu::Config cfg_;
    std::int32_t k_mg_;
    std::array<LP, 6> lp_;
    std::uint16_t seq_ = 0;
    std::uint16_t errs_ = 0;
    bool err_since_last_ = false;
};
}  // namespace app
}  // namespace a10

#ifndef A10_LIB_ONLY
/* ===================================================================================================
 * 호스트 전용: mock I2C 장치 · 장애 주입 · 하네스 · 힙 트랩
 * =================================================================================================== */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace a10;

// 힙 0: 전역 operator new 를 "트랩" 으로 바꾼다. 어떤 코드든 new 를 부르면 여기서 멈춘다 (펌웨어 판은 budget.sh 의 nm 검사)
static int g_heap_calls = 0;
void* operator new(std::size_t n) { ++g_heap_calls; std::printf("HEAP TRAP: operator new(%zu)\n", n); __builtin_trap(); }
void* operator new[](std::size_t n) { ++g_heap_calls; std::printf("HEAP TRAP: operator new[](%zu)\n", n); __builtin_trap(); }

static int g_pass = 0, g_total = 0;
#define CHECK(expr)                                                                         \
    do {                                                                                    \
        ++g_total;                                                                          \
        if (expr) { ++g_pass; std::printf("PASS  %s\n", #expr); }                           \
        else std::printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);                   \
    } while (0)

static std::uint32_t g_rng = 0x2545'F491u;
static std::uint32_t xorshift32() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

// mock IMU on I2C: 128바이트 레지스터 파일, IF_INC 이면 주소 자동 증가, OUT 읽기는 STATUS 의 data-ready 를 지운다.
// 장애 주입: n번째 트랜잭션에 BusErr 반환, n번째 트랜잭션의 rx[0] 비트 뒤집기 (I2C 는 데이터 오류를 모른다)
class MockI2c {
public:
    std::array<std::uint8_t, 128> regs{};
    std::uint32_t txns = 0, unlocked_xfers = 0, ro_writes = 0, locks = 0, unlocks = 0;
    static_vector<std::uint8_t, 64> write_log;     // 쓰인 레지스터 주소 순서
    std::array<std::uint8_t, 16> last_tx{};
    std::size_t last_tx_len = 0;

    MockI2c() { regs[imu::WHO_AM_I::addr] = imu::kWhoAmIValue; }   // CTRL3_C 리셋값 0 (IF_INC=0) — mock 의 선택
    void inject(std::uint32_t at_txn, BusErr e) { if (nf_ < faults_.size()) faults_[nf_++] = {at_txn, e}; }
    void flip_rx(std::uint32_t at_txn, std::uint8_t mask) { flip_at_ = at_txn; flip_mask_ = mask; }
    void hold_lock() { locked_ = true; }                  // "다른 드라이버" 가 버스를 쥐고 있음
    bool locked() const { return locked_; }
    void push_sample(const imu::Raw& r) {
        for (std::size_t i = 0; i < 3; ++i) {
            tlm::put_le16(&regs[0x22 + 2 * i], static_cast<std::uint16_t>(r.g[i]));
            tlm::put_le16(&regs[0x28 + 2 * i], static_cast<std::uint16_t>(r.a[i]));
        }
        regs[imu::STATUS::addr] |= imu::STATUS_XLDA | imu::STATUS_GDA;
    }

    bool try_lock() { if (locked_) return false; locked_ = true; ++locks; return true; }
    void unlock() { locked_ = false; ++unlocks; }
    Result<void, BusErr> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx) {
        const std::uint32_t n = txns++;
        if (!locked_) ++unlocked_xfers;
        last_tx_len = tx.size();
        for (std::size_t i = 0; i < tx.size() && i < last_tx.size(); ++i) last_tx[i] = tx[i];
        for (std::size_t i = 0; i < nf_; ++i) if (faults_[i].at == n) return fail(faults_[i].err);
        if (addr != imu::kAddr) return fail(BusErr::AddrNack);
        if (tx.empty()) return {};
        std::uint8_t p = tx[0] & 0x7Fu;
        const bool inc = (regs[imu::CTRL3_C::addr] & imu::CTRL3_IF_INC) != 0;
        for (std::size_t i = 1; i < tx.size(); ++i) { write(p, tx[i]); if (inc) p = (p + 1) & 0x7Fu; }
        bool data_read = false;
        for (std::uint8_t& b : rx) {
            b = regs[p];
            data_read = data_read || (p >= 0x22 && p <= 0x2D);
            if (inc) p = (p + 1) & 0x7Fu;
        }
        if (data_read) regs[imu::STATUS::addr] &= static_cast<std::uint8_t>(~(imu::STATUS_XLDA | imu::STATUS_GDA));
        if (n == flip_at_ && !rx.empty()) rx[0] ^= flip_mask_;
        return {};
    }

private:
    struct Fault { std::uint32_t at; BusErr err; };
    std::array<Fault, 8> faults_{};
    std::size_t nf_ = 0;
    std::uint32_t flip_at_ = 0xFFFF'FFFFu;
    std::uint8_t flip_mask_ = 0;
    bool locked_ = false;
    void write(std::uint8_t p, std::uint8_t v) {
        if (p == imu::WHO_AM_I::addr || p == imu::STATUS::addr || (p >= 0x20 && p <= 0x2D)) { ++ro_writes; return; }
        regs[p] = v;
        (void)write_log.push_back(p);
    }
};
static_assert(bus::I2cBus<MockI2c>);

// 컴파일 실패 증명 (demo_errors.cpp 가 실제 에러 문구를 보여 준다)
template <class R> concept CanWrite = requires(MockI2c& m) { bus::write_reg<R>(m, imu::kAddr, std::uint8_t{0}); };
template <class R> concept CanRead = requires(MockI2c& m) { bus::read_reg<R>(m, imu::kAddr); };
static_assert(CanWrite<imu::CTRL1_XL> && CanRead<imu::CTRL1_XL>);
static_assert(!CanWrite<imu::WHO_AM_I> && !CanWrite<imu::STATUS> && CanRead<imu::WHO_AM_I>);
struct NotABus { int write_read(int); };
static_assert(!bus::I2cBus<NotABus>);
static_assert(!std::is_copy_constructible_v<bus::BusLock<MockI2c>>);
static_assert(std::is_trivially_copyable_v<tlm::Frame> && std::is_trivially_copyable_v<imu::Raw>);

static bool same_raw(const imu::Raw& x, const imu::Raw& y) { return x.g == y.g && x.a == y.a; }
static imu::Raw rand_raw() {
    imu::Raw r{};
    for (std::size_t i = 0; i < 3; ++i) { r.g[i] = static_cast<std::int16_t>(xorshift32()); r.a[i] = static_cast<std::int16_t>(xorshift32()); }
    return r;
}
template <std::size_t N>
static bool bytes_eq(const static_vector<std::uint8_t, N>& v, std::span<const std::uint8_t> want) {
    if (v.size() != want.size()) return false;
    for (std::size_t i = 0; i < want.size(); ++i) if (v[i] != want[i]) return false;
    return true;
}
// 스트림을 0x00 으로 잘라 프레임마다 decode (수신 측 모델)
template <std::size_t N, std::size_t M>
static bool decode_stream(const static_vector<std::uint8_t, N>& s, static_vector<tlm::Frame, M>& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != 0) continue;
        const auto f = tlm::decode_frame(std::span<const std::uint8_t>(s.begin() + start, i - start));
        if (!f || !out.push_back(f.value())) return false;
        start = i + 1;
    }
    return start == s.size();
}

// golden: golden_check.py (Python zlib.crc32 + struct.pack('<…') + 독립 COBS) 로 만든 바이트
static const tlm::Frame kGoldenFrame{1, 0, 0x1234, 0x89AB'CDEFu, {1000, -2, 16384}, {-32768, 32767, 0}, 0x0102};
static constexpr std::array<std::uint8_t, 28> kGoldenWire{
    0x02, 0x01, 0x0B, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89, 0xE8, 0x03, 0xFE, 0xFF, 0x02, 0x40, 0x04,
    0x80, 0xFF, 0x7F, 0x01, 0x07, 0x02, 0x01, 0x5B, 0xFE, 0x2E, 0xB5, 0x00};

int main() {
    std::printf("--- Part A: 드라이버 · 고정소수점 · 프레임 ---\n");
    {
        MockI2c m;
        const auto id = bus::read_reg<imu::WHO_AM_I>(m, imu::kAddr);
        CHECK(id.ok() && id.value() == 0x6C);
        CHECK(m.last_tx_len == 1 && m.last_tx[0] == 0x0F && m.txns == 1);
        CHECK(!m.locked() && m.locks == 1 && m.unlocks == 1 && m.unlocked_xfers == 0);
        CHECK(bus::write_reg<imu::CTRL1_XL>(m, imu::kAddr, 0x40).ok() && m.regs[0x10] == 0x40);
        CHECK(m.last_tx_len == 2 && m.last_tx[0] == 0x10 && m.last_tx[1] == 0x40);
        const std::array<std::uint8_t, 9> big{};
        const auto r = bus::xfer(m, imu::kAddr, 0x10, big, {});
        CHECK(!r && r.error().code == Code::BadConfig && m.txns == 2 && !m.locked());
    }
    {
        MockI2c m;
        CHECK(imu::probe(m).ok());
        m.regs[0x0F] = 0x69;                                   // 다른 칩 (예: 이전 세대)
        const auto p = imu::probe(m);
        CHECK(!p && p.error().code == Code::BadId && p.error().reg == 0x0F);
        const auto q = imu::probe(m);
        CHECK(!q && !m.locked() && m.unlocked_xfers == 0);
    }
    {
        MockI2c m;
        const imu::Config c{imu::Odr::Hz104, imu::AccelFs::G4, imu::GyroFs::Dps500};
        CHECK(imu::ctrl1(c) == 0x48 && imu::ctrl2(c) == 0x44);
        CHECK(imu::configure(m, c).ok());
        CHECK(m.regs[0x10] == 0x48 && m.regs[0x11] == 0x44 && m.regs[0x12] == 0x44);
        CHECK(m.write_log.size() == 3 && m.write_log[0] == 0x12 && m.write_log[1] == 0x10 && m.write_log[2] == 0x11);
        CHECK(m.txns == 4 && m.ro_writes == 0 && !m.locked());  // 쓰기 3 + 검증 버스트 1
    }
    {
        MockI2c m;
        CHECK(imu::configure(m, imu::Config{}).ok());
        const auto nr = imu::read_sample(m);
        CHECK(!nr && nr.error().code == Code::NotReady && nr.error().reg == 0x1E);
        const imu::Raw in{{-32768, 32767, 0}, {1000, -2, 16384}};
        m.push_sample(in);
        const std::uint32_t t0 = m.txns;
        const auto s = imu::read_sample(m);
        CHECK(s.ok() && same_raw(s.value(), in));
        CHECK(m.txns - t0 == 2 && m.last_tx[0] == 0x22 && (m.regs[0x1E] & 0x03) == 0);
        const auto again = imu::read_sample(m);
        CHECK(!again && again.error().code == Code::NotReady);       // 읽고 나면 data-ready 꺼짐
    }
    {
        using dsp::mul_q15;
        CHECK(mul_q15(16384, 16384) == 8192);                // 0.5 × 0.5
        CHECK(mul_q15(-32768, -32768) == 32767);             // (-1)(-1) = +1 → 포화
        CHECK(mul_q15(-32768, 32767) == -32767);
        CHECK(mul_q15(1, 16384) == 1 && mul_q15(-1, 16384) == 0);   // ±0.5 LSB: half-up 은 비대칭
        CHECK(mul_q15(3, 16384) == 2 && mul_q15(-3, 16384) == -1);  // 1.5 → 2, -1.5 → -1
        CHECK(mul_q15(32767, 32767) == 32766 && mul_q15(0, -32768) == 0);
        CHECK(dsp::alpha_q15(5.0, 104.0) == 7602);
    }
    {
        dsp::LowPass<8> lp(4096);                                // α = 0.125
        CHECK(lp.step(-500) == -500 && lp.state() == -500 * 256); // 첫 샘플 = 그대로
        std::int16_t y = 0;
        for (int i = 0; i < 200; ++i) y = lp.step(100);
        CHECK(y == 100);                                         // 소수 비트 덕에 정확히 도달
        dsp::LowPass<8> lq(4096);
        (void)lq.step(0);
        CHECK(lq.step(100) == 13 && lq.step(100) == 23);         // 12.5 → 13, 23.4 → 23
    }
    {
        tlm::Frame f = kGoldenFrame;
        const auto b = tlm::pack_frame(f);
        CHECK(b[0] == 0x01 && b[2] == 0x34 && b[3] == 0x12 && b[4] == 0xEF && b[7] == 0x89);
        CHECK(b[8] == 0xE8 && b[9] == 0x03 && b[10] == 0xFE && b[11] == 0xFF && b[14] == 0x00 && b[15] == 0x80);
        CHECK(tlm::get_le32(&b[22]) == 0xB52E'FE5Bu);            // Python zlib.crc32 와 같은 값
        CHECK(tlm::crc32(std::span<const std::uint8_t>(b.data(), 22)) == 0xB52E'FE5Bu);
    }

    std::printf("--- Part B: 장애 주입 · 재시도 · 고정소수점 경계 · COBS · 디코더 ---\n");
    {
        MockI2c m;
        CHECK(imu::configure(m, imu::Config{}).ok());
        const imu::Raw in = rand_raw();
        m.push_sample(in);
        m.inject(m.txns + 1, BusErr::DataNack);                  // 버스트 읽기에서 NACK
        const std::uint32_t t0 = m.txns;
        const auto s = bus::retry_transient([&] { return imu::read_sample(m); }, 1);
        CHECK(s.ok() && same_raw(s.value(), in) && m.txns - t0 == 4);   // STATUS+버스트 ×2
        m.push_sample(in);
        m.inject(m.txns, BusErr::Timeout);
        m.inject(m.txns + 1, BusErr::Timeout);
        const std::uint32_t t1 = m.txns;
        const auto e = bus::retry_transient([&] { return imu::read_sample(m); }, 1);
        CHECK(!e && e.error().code == Code::Bus && e.error().bus == BusErr::Timeout && e.error().reg == 0x1E);
        CHECK(m.txns - t1 == 2 && !m.locked() && m.unlocked_xfers == 0);
        m.inject(m.txns, BusErr::AddrNack);                      // 장치 없음 → 재시도하지 않는다
        const std::uint32_t t2 = m.txns;
        const auto a = bus::retry_transient([&] { return imu::read_sample(m); }, 3);
        CHECK(!a && a.error().bus == BusErr::AddrNack && m.txns - t2 == 1);
        const auto bad = bus::retry_transient([&] { return imu::probe(m); }, 3);   // 성공 경로
        CHECK(bad.ok() && m.locks == m.unlocks);
    }
    {
        MockI2c m;
        m.hold_lock();
        const auto r = imu::probe(m);
        CHECK(!r && r.error().code == Code::Busy && m.txns == 0 && m.locked());   // 남의 잠금은 안 푼다
    }
    {
        MockI2c m;
        m.flip_rx(3, 0x10);                                      // 검증 버스트의 CTRL1_XL 바이트가 깨짐
        const auto r = imu::configure(m, imu::Config{});
        CHECK(!r && r.error().code == Code::BadConfig && r.error().reg == 0x10);
        MockI2c n;
        n.flip_rx(0, 0x01);                                      // WHO_AM_I 응답 비트 오류
        const auto p = imu::probe(n);
        CHECK(!p && p.error().code == Code::BadId);
    }
    {
        MockI2c m;                                               // IF_INC 를 안 켜면: 버스트가 같은 레지스터만 12번
        const imu::Raw in{{0x0102, 0x0304, 0x0506}, {0x0708, 0x090A, 0x0B0C}};
        m.push_sample(in);
        const auto s = imu::read_sample(m);
        CHECK(s.ok() && s.value().g[0] == 0x0202 && s.value().a[2] == 0x0202);   // 쓰레기지만 "성공"
    }
    {
        int16_t mx = 0, mn = 0;
        dsp::LowPass<8> lp(32767);
        bool ok = true;
        for (int i = 0; i < 1000; ++i) {
            const std::int16_t y = lp.step((i & 1) ? std::int16_t{32767} : std::int16_t{-32768});
            mx = y > mx ? y : mx; mn = y < mn ? y : mn;
            ok = ok && lp.state() <= 32767 * 256 && lp.state() >= -32768 * 256;
        }
        CHECK(ok && mx == 32765 && mn == -32768);                // 극단 교대 입력: 범위 안, UB 없음 (α<1 이라 32767 엔 못 닿음)
        dsp::LowPass<0> d0(4096), u0(4096);
        (void)d0.step(0); (void)u0.step(100);
        std::int16_t yu = 0, yd = 0;
        for (int i = 0; i < 300; ++i) { yu = d0.step(100); yd = u0.step(0); }
        CHECK(yu == 97 && yd == 4);                              // F=0: 데드밴드 (올라갈 때 3, 내려갈 때 4 LSB 모자람)
    }
    {
        const double a = 4096.0 / 32768.0;                       // 기준 모델도 양자화된 α 를 쓴다 → 산술 오차만 비교
        dsp::LowPass<8> lp(4096);
        double ref = 0.0, max_err = 0.0;
        g_rng = 0x1234'5678u;
        for (int i = 0; i < 20000; ++i) {
            const std::int16_t x = (i % 2000 < 1000) ? static_cast<std::int16_t>(xorshift32())
                                                     : static_cast<std::int16_t>(static_cast<std::int32_t>(xorshift32() % 2001u) - 1000);
            const std::int16_t y = lp.step(x);
            ref = (i == 0) ? x : ref + a * (x - ref);
            const double e = std::fabs(y - ref);
            max_err = e > max_err ? e : max_err;
        }
        const double bound = 0.5 + std::ldexp(1.0, -(8 + 1)) / a; // 0.5 + 2^-(F+1)/α = 0.515625
        std::printf("      LowPass<8> α=0.125: max |y - y_double| = %.6f (bound %.6f)\n", max_err, bound);
        CHECK(max_err <= bound);
        const std::int32_t k = dsp::scale_q16(61);
        double mg_err = 0.0;
        for (std::int32_t raw = -32768; raw <= 32767; ++raw) {
            const double e = std::fabs(dsp::to_mg(static_cast<std::int16_t>(raw), k) - raw * 0.061);
            mg_err = e > mg_err ? e : mg_err;
        }
        std::printf("      to_mg ±2g (k=%d): max |mg - raw*0.061| = %.6f (bound 0.75)\n", static_cast<int>(k), mg_err);
        CHECK(k == 3998 && mg_err <= 0.75);
    }
    {
        static_vector<std::uint8_t, 600> o;
        const std::array<std::uint8_t, 1> z{0};
        CHECK(tlm::cobs_encode(z, o) && bytes_eq(o, std::array<std::uint8_t, 2>{1, 1}));
        o.clear();
        const std::array<std::uint8_t, 4> v1{0x11, 0x22, 0x00, 0x33};
        CHECK(tlm::cobs_encode(v1, o) && bytes_eq(o, std::array<std::uint8_t, 5>{3, 0x11, 0x22, 2, 0x33}));
        o.clear();
        CHECK(tlm::cobs_encode(std::span<const std::uint8_t>{}, o) && bytes_eq(o, std::array<std::uint8_t, 1>{1}));
        std::array<std::uint8_t, 255> run{};
        for (std::size_t i = 0; i < 255; ++i) run[i] = static_cast<std::uint8_t>(i + 1);   // 01..FF
        o.clear();
        CHECK(tlm::cobs_encode(std::span<const std::uint8_t>(run.data(), 254), o) && o.size() == 255 && o[0] == 0xFF);
        o.clear();
        CHECK(tlm::cobs_encode(run, o) && o.size() == 257 && o[0] == 0xFF && o[255] == 0x02 && o[256] == 0xFF);
        static_vector<std::uint8_t, 4> tiny;
        CHECK(!tlm::cobs_encode(v1, tiny));                      // 공간 부족 → false
    }
    {
        bool ok = true;
        g_rng = 0xC0B5'0001u;
        for (int t = 0; t < 400 && ok; ++t) {
            std::array<std::uint8_t, 600> in{};
            const std::size_t len = xorshift32() % 600u;
            const std::uint32_t zero_rate = xorshift32() % 4u;    // 0: 0x00 없음 … 3: 많음
            for (std::size_t i = 0; i < len; ++i) {
                const std::uint32_t r = xorshift32();
                in[i] = (zero_rate != 0 && r % (64u >> (zero_rate * 2u)) == 0) ? 0 : static_cast<std::uint8_t>(r >> 8 | 1u);
            }
            static_vector<std::uint8_t, 700> enc;
            ok = tlm::cobs_encode(std::span<const std::uint8_t>(in.data(), len), enc);
            for (std::uint8_t b : enc) ok = ok && b != 0;
            ok = ok && enc.size() <= len + len / 254 + 1;
            std::array<std::uint8_t, 600> dec{};
            const auto n = tlm::cobs_decode(enc.view(), dec);
            ok = ok && n.ok() && n.value() == len;
            for (std::size_t i = 0; ok && i < len; ++i) ok = dec[i] == in[i];
        }
        CHECK(ok);                                               // 400개 무작위 버퍼 round-trip
        std::array<std::uint8_t, 8> d{};
        CHECK(tlm::cobs_decode(std::array<std::uint8_t, 3>{3, 0x11, 0x00}, d).error() == tlm::DecodeErr::Cobs);
        CHECK(tlm::cobs_decode(std::array<std::uint8_t, 2>{5, 0x11}, d).error() == tlm::DecodeErr::Cobs);
        CHECK(tlm::cobs_decode(std::array<std::uint8_t, 2>{0, 0x11}, d).error() == tlm::DecodeErr::Cobs);
        std::array<std::uint8_t, 2> small{};
        CHECK(tlm::cobs_decode(std::array<std::uint8_t, 4>{4, 1, 2, 3}, small).error() == tlm::DecodeErr::Cobs);
    }
    {
        const auto ok = tlm::decode_frame(std::span<const std::uint8_t>(kGoldenWire.data(), 27));
        CHECK(ok.ok() && ok.value() == kGoldenFrame);
        int bad_crc = 0;
        for (std::size_t bit = 0; bit < tlm::kFrameSize * 8; ++bit) {   // 프레임의 모든 1비트 오류
            auto raw = tlm::pack_frame(kGoldenFrame);
            raw[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
            static_vector<std::uint8_t, 32> w;
            if (!tlm::cobs_encode(raw, w)) break;
            const auto r = tlm::decode_frame(w.view());
            bad_crc += (!r && r.error() == tlm::DecodeErr::BadCrc) ? 1 : 0;
        }
        CHECK(bad_crc == 208);
        static_vector<std::uint8_t, 32> w;
        const auto raw = tlm::pack_frame(kGoldenFrame);
        CHECK(tlm::cobs_encode(std::span<const std::uint8_t>(raw.data(), 25), w) &&
              tlm::decode_frame(w.view()).error() == tlm::DecodeErr::BadLength);
        tlm::Frame v2 = kGoldenFrame;
        v2.ver = 2;
        w.clear();
        CHECK(tlm::cobs_encode(tlm::pack_frame(v2), w) && tlm::decode_frame(w.view()).error() == tlm::DecodeErr::BadVersion);
        CHECK(tlm::decode_frame(std::span<const std::uint8_t>(kGoldenWire.data(), 20)).error() == tlm::DecodeErr::BadLength);
        CHECK(tlm::decode_frame(std::span<const std::uint8_t>(kGoldenWire.data(), 18)).error() == tlm::DecodeErr::Cobs);
    }

    std::printf("--- Part C: 파이프라인 (가상 시간) ---\n");
    {
        static_vector<std::uint8_t, 28> w;
        CHECK(tlm::cobs_encode(tlm::pack_frame(kGoldenFrame), w) && w.push_back(0) && bytes_eq(w, kGoldenWire));
    }
    {
        MockI2c m;
        app::Pipeline<MockI2c> p(m, imu::Config{}, app::kAlpha);
        CHECK(p.init().ok() && m.regs[0x12] == 0x44 && m.regs[0x10] == 0x40);
        static_vector<std::uint8_t, 2048> stream;
        static_vector<tlm::Frame, 64> expect;
        std::array<dsp::LowPass<app::kFracBits>, 6> ref{dsp::LowPass<8>{app::kAlpha}, dsp::LowPass<8>{app::kAlpha},
            dsp::LowPass<8>{app::kAlpha}, dsp::LowPass<8>{app::kAlpha}, dsp::LowPass<8>{app::kAlpha}, dsp::LowPass<8>{app::kAlpha}};
        const std::int32_t k = dsp::scale_q16(61);
        std::uint16_t eseq = 0, eerr = 0;
        bool eflag = false, step_ok = true;
        int errors = 0, idle = 0;
        g_rng = 0xA10A'10A1u;
        for (std::uint32_t i = 0; i < 50; ++i) {
            const std::uint32_t t_ms = i * 10u;
            imu::Raw in = rand_raw();
            if (i != 30) m.push_sample(in);                      // 30: 새 데이터 없음
            if (i == 10) m.inject(m.txns, BusErr::Timeout);      // 1번 실패 → 재시도로 회복
            if (i == 20) { m.inject(m.txns, BusErr::Timeout); m.inject(m.txns + 1, BusErr::Timeout); }   // 재시도 소진
            const auto r = p.step(t_ms, stream);
            if (i == 30) { idle += (r.ok() && r.value() == 0) ? 1 : 0; continue; }
            if (i == 20) { errors += (!r && r.error().bus == BusErr::Timeout) ? 1 : 0; ++eerr; eflag = true; continue; }
            step_ok = step_ok && r.ok() && r.value() == 28;
            tlm::Frame f{};
            f.ver = 1; f.flags = eflag ? 1 : 0; f.seq = eseq++; f.t_ms = t_ms; f.err_count = eerr;
            for (std::size_t c = 0; c < 3; ++c) { f.accel_mg[c] = dsp::to_mg(ref[c].step(in.a[c]), k); f.gyro[c] = ref[3 + c].step(in.g[c]); }
            eflag = false;
            (void)expect.push_back(f);
        }
        CHECK(step_ok && errors == 1 && idle == 1);
        CHECK(p.seq() == 48 && p.err_count() == 1 && stream.size() == 48u * 28u);
        static_vector<tlm::Frame, 64> got;
        CHECK(decode_stream(stream, got) && got.size() == 48);
        bool same = got.size() == expect.size();
        for (std::size_t i = 0; same && i < got.size(); ++i) same = got[i] == expect[i];
        CHECK(same);                                             // 필터·변환·플래그·seq·시간이 프레임마다 일치
        CHECK(got[20].flags == 1 && got[20].err_count == 1 && got[20].t_ms == 210 && got[19].flags == 0 && got[21].flags == 0);
        CHECK(got[19].err_count == 0 && got[47].err_count == 1 && got[47].seq == 47 && got[47].t_ms == 490);
        CHECK(!m.locked() && m.unlocked_xfers == 0 && m.locks == m.unlocks);
    }
    {
        MockI2c m;
        app::Pipeline<MockI2c> p(m, imu::Config{}, app::kAlpha);
        CHECK(p.init().ok());
        static_vector<std::uint8_t, 30> out;                     // 프레임 1개(28)만 들어감
        m.push_sample(rand_raw());
        CHECK(p.step(0, out).ok() && out.size() == 28);
        m.push_sample(rand_raw());
        const auto full = p.step(10, out);
        CHECK(!full && full.error().code == Code::NoSpace && out.size() == 28);   // 반쪽 프레임 없음
        static_vector<tlm::Frame, 4> got;
        CHECK(decode_stream(out, got) && got.size() == 1 && got[0].seq == 0);
        out.clear();
        m.push_sample(rand_raw());
        CHECK(p.step(20, out).ok());
        got.clear();
        CHECK(decode_stream(out, got) && got[0].seq == 2);       // seq 1 이 빠짐 → 수신 측이 유실을 안다
    }
    {
        MockI2c m;
        m.regs[0x0F] = 0x00;                                     // 부팅 시 센서 무응답 흉내
        app::Pipeline<MockI2c> p(m, imu::Config{}, app::kAlpha);
        const auto r = p.init();
        CHECK(!r && r.error().code == Code::BadId && m.txns == 1);   // probe 실패면 configure 까지 가지 않는다
    }
    std::printf("      sizeof: Pipeline<MockI2c>=%zu  LowPass<8>=%zu  Frame=%zu  Result<Raw,Error>=%zu\n",
                sizeof(app::Pipeline<MockI2c>), sizeof(dsp::LowPass<8>), sizeof(tlm::Frame), sizeof(Result<imu::Raw, Error>));
    CHECK(sizeof(app::Pipeline<MockI2c>) <= 96);
    CHECK(g_heap_calls == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif  // A10_LIB_ONLY
