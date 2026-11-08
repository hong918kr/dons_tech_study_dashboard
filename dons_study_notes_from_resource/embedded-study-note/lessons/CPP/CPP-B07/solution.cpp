/* CPP-B07 RAII 스코프 가드: SPI 칩셀렉트와 락
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp
 * mock: GPIOA(CS 핀) + SPI1 버스(뮤텍스 흉내) + 이벤트 로그. 실제 MMIO 없음.
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <type_traits>

/* ================= mock 하드웨어 + 이벤트 로그 ================= */
enum class Op : uint8_t { Lock, Unlock, CsLow, CsHigh, Xfer };
struct Ev { Op op; uint8_t arg; };          // arg: Lock/Unlock=버스 id, Cs*=핀 번호, Xfer=보낸 바이트

struct EventLog {
    std::array<Ev, 64> ev{};
    unsigned n = 0;
    unsigned errors = 0;                     // 하드웨어 규칙 위반 (이중 락, CS 없이 전송, CS 두 개 동시 low ...)
    void push(Op op, uint8_t arg) { if (n < ev.size()) ev[n++] = {op, arg}; else errors++; }
};
static EventLog g_log;

struct GpioPort {
    volatile uint32_t ODR;
    volatile uint32_t BSRR;                  // [15:0]=set, [31:16]=reset (C-B01 참고)
};
static GpioPort gpioa;                       // 실제 칩이라면 *reinterpret_cast<GpioPort*>(0x48000000)

constexpr unsigned EE_CS  = 4;               // PA4: 25AA010A EEPROM (8비트 주소)
constexpr unsigned IMU_CS = 6;               // PA6: ICM-42688 스타일 IMU
constexpr uint32_t CS_MASK = (1u << EE_CS) | (1u << IMU_CS);

inline void gpio_write(GpioPort& p, unsigned pin, bool high) {
    p.BSRR = high ? (1u << pin) : (1u << (pin + 16));
    uint32_t b = p.BSRR;                     // sim: 하드웨어가 BSRR 를 ODR 에 반영
    p.ODR = (p.ODR & ~(b >> 16)) | (b & 0xFFFFu);
    p.BSRR = 0;
    g_log.push(high ? Op::CsHigh : Op::CsLow, static_cast<uint8_t>(pin));
}

struct SpiBus {
    uint8_t id;
    bool locked;
    std::array<uint8_t, 16> rx;              // 테스트가 미리 넣어 둔 MISO 응답 (전송 1번 = 1바이트)
    unsigned rx_pos, rx_len;
};

inline void bus_lock(SpiBus& b) {            // 실제: xSemaphoreTake(spi1_mutex, portMAX_DELAY)
    if (b.locked) g_log.errors++;            // 비재귀 뮤텍스를 두 번 잡음 → 실제라면 데드락
    b.locked = true;
    g_log.push(Op::Lock, b.id);
}
inline void bus_unlock(SpiBus& b) {          // 실제: xSemaphoreGive(spi1_mutex)
    if (!b.locked) g_log.errors++;           // 안 잡은 락을 풀음 → 남의 락을 풀어버림
    b.locked = false;
    g_log.push(Op::Unlock, b.id);
}
inline uint8_t spi_xfer(SpiBus& b, uint8_t tx) {
    uint32_t low = ~gpioa.ODR & CS_MASK;     // 지금 low 인 CS 들
    if (!b.locked) g_log.errors++;           // 락 없이 전송
    if (low == 0 || (low & (low - 1u))) g_log.errors++;  // CS 0개 또는 2개 이상 → 버스 충돌
    g_log.push(Op::Xfer, tx);
    return b.rx_pos < b.rx_len ? b.rx[b.rx_pos++] : 0xFF;
}

/* ================= Part A: 두 개의 스코프 가드 ================= */
class [[nodiscard]] BusLock {
public:
    explicit BusLock(SpiBus& bus) : bus_(bus) { bus_lock(bus_); }
    ~BusLock() { bus_unlock(bus_); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
    SpiBus& bus() const { return bus_; }     // 락을 가진 사람만 버스에 접근 (Part C)
private:
    SpiBus& bus_;
};

class [[nodiscard]] ScopedChipSelect {
public:
    ScopedChipSelect(GpioPort& port, unsigned pin) : port_(port), pin_(pin) {
        gpio_write(port_, pin_, false);      // active-low: assert = low
    }
    ~ScopedChipSelect() {
        gpio_write(port_, pin_, true);       // deassert = high
    }
    ScopedChipSelect(const ScopedChipSelect&) = delete;
    ScopedChipSelect& operator=(const ScopedChipSelect&) = delete;
private:
    GpioPort& port_;
    unsigned pin_;
};

static_assert(!std::is_copy_constructible_v<BusLock>);
static_assert(!std::is_copy_assignable_v<BusLock>);
static_assert(!std::is_move_constructible_v<BusLock>);           // 복사 삭제 → 이동도 암시 선언 안 됨
static_assert(!std::is_copy_constructible_v<ScopedChipSelect>);
static_assert(!std::is_move_constructible_v<ScopedChipSelect>);

/* 25xx EEPROM 명령 */
constexpr uint8_t CMD_READ = 0x03, CMD_WRITE = 0x02, CMD_WREN = 0x06, CMD_RDSR = 0x05;
constexpr uint8_t SR_WIP = 0x01;             // Write-In-Progress

/* RDSR: lock → CS low → 0x05 → 상태 1바이트 → CS high → unlock */
inline uint8_t ee_read_status(SpiBus& bus) {
    BusLock lock(bus);                       // 먼저 만든 것이 나중에 소멸
    ScopedChipSelect cs(gpioa, EE_CS);
    spi_xfer(bus, CMD_RDSR);
    return spi_xfer(bus, 0x00);              // 반환값 계산 → 그 다음 cs, lock 소멸
}

/* ================= Part B: 조기 return 과 CS 프레임 여러 개 ================= */
enum class Status : uint8_t { Ok, Busy, Blank };

inline Status ee_write_byte(SpiBus& bus, uint8_t addr, uint8_t data) {
    BusLock lock(bus);
    {
        ScopedChipSelect cs(gpioa, EE_CS);
        spi_xfer(bus, CMD_RDSR);
        if (spi_xfer(bus, 0x00) & SR_WIP) return Status::Busy;   // cs → lock 순서로 해제됨
    }
    {
        ScopedChipSelect cs(gpioa, EE_CS);   // WREN 은 CS 상승 에지에서 래치 → 자기 프레임 필요
        spi_xfer(bus, CMD_WREN);
    }
    ScopedChipSelect cs(gpioa, EE_CS);
    spi_xfer(bus, CMD_WRITE);
    spi_xfer(bus, addr);
    spi_xfer(bus, data);
    return Status::Ok;
}

/* ================= Part C: 같은 버스의 두 장치 + 락 토큰 ================= */
constexpr uint8_t EE_ADDR_ODR = 0x10;        // EEPROM 에 저장된 IMU 설정값 위치
constexpr uint8_t IMU_PWR_MGMT0 = 0x4E, IMU_ACCEL_CONFIG0 = 0x50;

/* const BusLock& = "이미 락을 잡았다"는 컴파일타임 증명. 안에서 다시 잠그지 않는다. */
inline uint8_t ee_read(const BusLock& held, uint8_t addr) {
    ScopedChipSelect cs(gpioa, EE_CS);
    spi_xfer(held.bus(), CMD_READ);
    spi_xfer(held.bus(), addr);
    return spi_xfer(held.bus(), 0x00);
}
inline void imu_write_reg(const BusLock& held, uint8_t reg, uint8_t val) {
    ScopedChipSelect cs(gpioa, IMU_CS);
    spi_xfer(held.bus(), reg & 0x7Fu);      // bit7 = 0 → write
    spi_xfer(held.bus(), val);
}
inline Status apply_imu_config(SpiBus& bus) {
    BusLock lock(bus);                       // 락은 한 번, CS 프레임은 여러 번
    uint8_t odr = ee_read(lock, EE_ADDR_ODR);
    if (odr == 0xFF) return Status::Blank;   // 지워진 EEPROM → IMU 는 건드리지 않음
    imu_write_reg(lock, IMU_PWR_MGMT0, 0x0F);
    imu_write_reg(lock, IMU_ACCEL_CONFIG0, odr);
    return Status::Ok;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

constexpr Ev L{Op::Lock, 1}, U{Op::Unlock, 1};
constexpr Ev lo(unsigned pin) { return {Op::CsLow, static_cast<uint8_t>(pin)}; }
constexpr Ev hi(unsigned pin) { return {Op::CsHigh, static_cast<uint8_t>(pin)}; }
constexpr Ev x(uint8_t tx) { return {Op::Xfer, tx}; }

static void reset(SpiBus& b, std::initializer_list<uint8_t> rx) {
    g_log = EventLog{};
    gpioa.ODR = CS_MASK;                     // 모든 CS idle = high
    b = SpiBus{1, false, {}, 0, 0};
    for (uint8_t v : rx) b.rx[b.rx_len++] = v;
}
static bool log_is(std::initializer_list<Ev> want) {
    bool ok = g_log.n == want.size();
    unsigned i = 0;
    for (const Ev& w : want) { ok = ok && g_log.ev[i].op == w.op && g_log.ev[i].arg == w.arg; i++; }
    if (!ok) {
        static const char* name[] = {"Lock", "Unlock", "CsLow", "CsHigh", "Xfer"};
        std::printf("  got:");
        for (unsigned k = 0; k < g_log.n; k++)
            std::printf(" %s(%#x)", name[static_cast<unsigned>(g_log.ev[k].op)], g_log.ev[k].arg);
        std::printf("\n");
    }
    return ok;
}
static bool idle(const SpiBus& b) { return !b.locked && (gpioa.ODR & CS_MASK) == CS_MASK && g_log.errors == 0; }

int main() {
    SpiBus spi1{};

    /* Part A — 복사 금지 + 기본 순서 */
    CHECK(!std::is_copy_constructible_v<BusLock>);
    CHECK(!std::is_copy_assignable_v<BusLock>);
    CHECK(!std::is_move_constructible_v<BusLock>);
    CHECK(!std::is_copy_constructible_v<ScopedChipSelect>);
    CHECK(!std::is_copy_assignable_v<ScopedChipSelect>);
    reset(spi1, {0xFF, 0x02});
    CHECK(ee_read_status(spi1) == 0x02);
    CHECK(log_is({L, lo(EE_CS), x(CMD_RDSR), x(0x00), hi(EE_CS), U}));
    CHECK(idle(spi1));
    reset(spi1, {});
    {
        BusLock lock(spi1);
        CHECK(spi1.locked);
    }
    CHECK(log_is({L, U}) && idle(spi1));

    /* Part B — 조기 return 경로와 정상 경로 */
    reset(spi1, {0xFF, SR_WIP});
    CHECK(ee_write_byte(spi1, 0x20, 0xAB) == Status::Busy);
    CHECK(log_is({L, lo(EE_CS), x(CMD_RDSR), x(0x00), hi(EE_CS), U}));
    CHECK(idle(spi1));
    reset(spi1, {0xFF, 0x00});
    CHECK(ee_write_byte(spi1, 0x20, 0xAB) == Status::Ok);
    CHECK(log_is({L, lo(EE_CS), x(CMD_RDSR), x(0x00), hi(EE_CS),
                     lo(EE_CS), x(CMD_WREN), hi(EE_CS),
                     lo(EE_CS), x(CMD_WRITE), x(0x20), x(0xAB), hi(EE_CS), U}));
    CHECK(idle(spi1));

    /* Part C — 같은 버스의 두 장치, 락은 한 번 */
    reset(spi1, {0xFF, 0xFF, 0x06});
    CHECK(apply_imu_config(spi1) == Status::Ok);
    CHECK(log_is({L, lo(EE_CS), x(CMD_READ), x(EE_ADDR_ODR), x(0x00), hi(EE_CS),
                     lo(IMU_CS), x(IMU_PWR_MGMT0), x(0x0F), hi(IMU_CS),
                     lo(IMU_CS), x(IMU_ACCEL_CONFIG0), x(0x06), hi(IMU_CS), U}));
    CHECK(idle(spi1));
    reset(spi1, {0xFF, 0xFF, 0xFF});
    CHECK(apply_imu_config(spi1) == Status::Blank);
    CHECK(log_is({L, lo(EE_CS), x(CMD_READ), x(EE_ADDR_ODR), x(0x00), hi(EE_CS), U}));
    CHECK(idle(spi1));
    CHECK((!std::is_invocable_v<decltype(&ee_read), SpiBus&, uint8_t>));   // 락 없이 호출 불가
    CHECK((std::is_invocable_v<decltype(&ee_read), const BusLock&, uint8_t>));

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
