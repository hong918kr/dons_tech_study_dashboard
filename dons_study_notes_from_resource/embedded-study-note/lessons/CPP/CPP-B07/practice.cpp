/* CPP-B07 추가 연습 — 한 번에 검증
 * 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp
 */
#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <type_traits>
#include <utility>

/* ---------------- mock (본문과 같은 모양 + SPI CR1 레지스터) ---------------- */
enum class Op : uint8_t { Lock, Unlock, CsLow, CsHigh, Xfer, Mode };
struct Ev { Op op; uint8_t arg; };
struct EventLog {
    std::array<Ev, 64> ev{};
    unsigned n = 0, errors = 0;
    void push(Op op, uint8_t arg) { if (n < ev.size()) ev[n++] = {op, arg}; else errors++; }
};
static EventLog g_log;

struct GpioPort { volatile uint32_t ODR; volatile uint32_t BSRR; };
static GpioPort gpioa;
constexpr unsigned EE_CS = 4, IMU_CS = 6;
constexpr uint32_t CS_MASK = (1u << EE_CS) | (1u << IMU_CS);

inline void gpio_write(GpioPort& p, unsigned pin, bool high) {
    p.ODR = high ? (p.ODR | (1u << pin)) : (p.ODR & ~(1u << pin));
    g_log.push(high ? Op::CsHigh : Op::CsLow, static_cast<uint8_t>(pin));
}

struct SpiRegs { volatile uint32_t CR1; };   // STM32 SPI_CR1: bit0 CPHA, bit1 CPOL
constexpr uint32_t CR1_CPHA = 1u << 0, CR1_CPOL = 1u << 1;
static SpiRegs spi1_regs;
inline void spi_set_cr1(SpiRegs& r, uint32_t v) {
    if ((~gpioa.ODR & CS_MASK) != 0) g_log.errors++;   // CS low 중에 모드 변경 = 클럭 글리치
    r.CR1 = v;
    g_log.push(Op::Mode, static_cast<uint8_t>(v & 3u));
}

struct SpiBus { uint8_t id; bool locked; bool rtos_running; };
inline void bus_lock(SpiBus& b)   { if (b.locked) g_log.errors++;  b.locked = true;  g_log.push(Op::Lock, b.id); }
inline void bus_unlock(SpiBus& b) { if (!b.locked) g_log.errors++; b.locked = false; g_log.push(Op::Unlock, b.id); }
inline uint8_t spi_xfer(SpiBus& b, uint8_t tx) {
    uint32_t low = ~gpioa.ODR & CS_MASK;
    if (b.rtos_running && !b.locked) g_log.errors++;  // 스케줄러 시작 전엔 락이 필요 없다 (연습 5)
    if (low == 0 || (low & (low - 1u))) g_log.errors++;
    g_log.push(Op::Xfer, tx);
    return 0x5A;
}

/* 본문의 두 가드 (그대로) */
class [[nodiscard]] BusLock {
public:
    explicit BusLock(SpiBus& bus) : bus_(bus) { bus_lock(bus_); }
    ~BusLock() { bus_unlock(bus_); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
    SpiBus& bus() const { return bus_; }
private:
    SpiBus& bus_;
};
class [[nodiscard]] ScopedChipSelect {
public:
    ScopedChipSelect(GpioPort& port, unsigned pin) : port_(port), pin_(pin) { gpio_write(port_, pin_, false); }
    ~ScopedChipSelect() { gpio_write(port_, pin_, true); }
    ScopedChipSelect(const ScopedChipSelect&) = delete;
    ScopedChipSelect& operator=(const ScopedChipSelect&) = delete;
private:
    GpioPort& port_;
    unsigned pin_;
};

/* ---------------- 연습 1: 멤버 선언 순서 = 생성 순서, 소멸은 그 역순 ---------------- */
class SpiTransaction {
public:
    SpiTransaction(SpiBus& bus, GpioPort& port, unsigned pin) : lock_(bus), cs_(port, pin) {}
    uint8_t xfer(uint8_t tx) { return spi_xfer(lock_.bus(), tx); }
private:
    BusLock lock_;              // 먼저 선언 → 먼저 생성(lock) → 나중 소멸(unlock)
    ScopedChipSelect cs_;
};
class BadTransaction {          // 선언 순서만 뒤집음 (초기화 목록 순서는 상관없다!)
public:
    BadTransaction(SpiBus& bus, GpioPort& port, unsigned pin) : cs_(port, pin), lock_(bus) {}
    uint8_t xfer(uint8_t tx) { return spi_xfer(lock_.bus(), tx); }
private:
    ScopedChipSelect cs_;       // 락 없이 CS 를 먼저 내림 + 락을 푼 뒤에 CS 를 올림
    BusLock lock_;
};

/* ---------------- 연습 2: 이동 가능한 락 (std::unique_lock 모양) ---------------- */
class [[nodiscard]] UniqueBusLock {
public:
    explicit UniqueBusLock(SpiBus& b) : bus_(&b) { bus_lock(b); }
    ~UniqueBusLock() { if (bus_) bus_unlock(*bus_); }            // moved-from 이면 아무것도 안 함
    UniqueBusLock(UniqueBusLock&& o) noexcept : bus_(std::exchange(o.bus_, nullptr)) {}
    UniqueBusLock& operator=(UniqueBusLock&& o) noexcept {
        if (this != &o) {
            if (bus_) bus_unlock(*bus_);                              // 내가 가진 락부터 놓는다
            bus_ = std::exchange(o.bus_, nullptr);
        }
        return *this;
    }
    UniqueBusLock(const UniqueBusLock&) = delete;
    UniqueBusLock& operator=(const UniqueBusLock&) = delete;
    bool owns_lock() const { return bus_ != nullptr; }
private:
    SpiBus* bus_;               // nullptr = 빈 상태 (참조로는 표현 불가 → 포인터)
};
static UniqueBusLock begin_session(SpiBus& b) {
    UniqueBusLock l(b);         // 이름 있는 지역 변수 반환 → 이동 생성자가 필요 (NRVO 는 선택 사항)
    return l;
}
static_assert(std::is_move_constructible_v<UniqueBusLock>);
static_assert(!std::is_copy_constructible_v<UniqueBusLock>);

/* ---------------- 연습 3: 이동 불가 가드도 팩토리로 반환 (C++17 보장된 복사 생략) ---------------- */
static BusLock acquire(SpiBus& b) { return BusLock{b}; }   // prvalue → 호출자 변수 자리에 바로 생성
static_assert(!std::is_move_constructible_v<BusLock>);

/* ---------------- 연습 4: 범용 scope_exit 람다 가드 ---------------- */
template <class F>
class [[nodiscard]] ScopeExit {
public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ~ScopeExit() { if (active_) f_(); }
    void dismiss() { active_ = false; }      // "커밋" — 정리 작업 취소
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
private:
    F f_;
    bool active_ = true;
};

enum class Status : uint8_t { Ok, Busy };
/* mode 0 버스에 mode 3 장치가 하나 섞여 있다: 잠깐 바꾸고 어떤 경로로 나가든 되돌린다 */
static Status mode3_read(SpiBus& bus, bool device_busy) {
    BusLock lock(bus);
    const uint32_t saved = spi1_regs.CR1;
    spi_set_cr1(spi1_regs, saved | CR1_CPOL | CR1_CPHA);
    ScopeExit restore{[&] { spi_set_cr1(spi1_regs, saved); }};   // CS 보다 먼저 선언 → CS high 뒤에 실행
    ScopedChipSelect cs(gpioa, IMU_CS);
    spi_xfer(bus, 0x80 | 0x75);
    if (device_busy) return Status::Busy;
    spi_xfer(bus, 0x00);
    return Status::Ok;
}
/* dismiss: 성공하면 새 모드를 유지(커밋), 실패하면 되돌림(롤백) */
static bool switch_to_mode3(SpiBus& bus, bool id_ok) {
    BusLock lock(bus);
    const uint32_t saved = spi1_regs.CR1;
    spi_set_cr1(spi1_regs, saved | CR1_CPOL | CR1_CPHA);
    ScopeExit rollback{[&] { spi_set_cr1(spi1_regs, saved); }};
    if (!id_ok) return false;
    rollback.dismiss();
    return true;
}

/* ---------------- 연습 5: 조건부 락 — std::optional + emplace ---------------- */
static uint8_t read_who_am_i(SpiBus& bus) {
    std::optional<BusLock> lock;             // 이동 불가 타입도 OK: emplace 는 제자리 생성
    if (bus.rtos_running) lock.emplace(bus);  // 스케줄러 시작 전(부트 초기화)엔 락 생략
    ScopedChipSelect cs(gpioa, IMU_CS);
    spi_xfer(bus, 0x80 | 0x75);
    return spi_xfer(bus, 0x00);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

constexpr Ev L{Op::Lock, 1}, U{Op::Unlock, 1};
constexpr Ev lo(unsigned p) { return {Op::CsLow, static_cast<uint8_t>(p)}; }
constexpr Ev hi(unsigned p) { return {Op::CsHigh, static_cast<uint8_t>(p)}; }
constexpr Ev x(uint8_t tx) { return {Op::Xfer, tx}; }
constexpr Ev mode(uint8_t m) { return {Op::Mode, m}; }

static void reset(SpiBus& b, bool rtos = true) {
    g_log = EventLog{};
    gpioa.ODR = CS_MASK;
    spi1_regs.CR1 = 0;
    b = SpiBus{1, false, rtos};
}
static bool log_is(std::initializer_list<Ev> want) {
    bool ok = g_log.n == want.size();
    unsigned i = 0;
    for (const Ev& w : want) { ok = ok && g_log.ev[i].op == w.op && g_log.ev[i].arg == w.arg; i++; }
    if (!ok) {
        static const char* name[] = {"Lock", "Unlock", "CsLow", "CsHigh", "Xfer", "Mode"};
        std::printf("  got:");
        for (unsigned k = 0; k < g_log.n; k++)
            std::printf(" %s(%#x)", name[static_cast<unsigned>(g_log.ev[k].op)], g_log.ev[k].arg);
        std::printf("\n");
    }
    return ok;
}

int main() {
    SpiBus bus{};

    /* 연습 1 */
    reset(bus);
    { SpiTransaction t(bus, gpioa, EE_CS); t.xfer(0x05); }
    CHECK(log_is({L, lo(EE_CS), x(0x05), hi(EE_CS), U}));
    reset(bus);
    { BadTransaction t(bus, gpioa, EE_CS); t.xfer(0x05); }
    CHECK(log_is({lo(EE_CS), L, x(0x05), U, hi(EE_CS)}));     // 버그가 재현됨: 락 밖에서 CS 가 움직인다

    /* 연습 2 */
    reset(bus);
    {
        UniqueBusLock a = begin_session(bus);
        UniqueBusLock b = std::move(a);
        CHECK(!a.owns_lock() && b.owns_lock());
    }
    CHECK(log_is({L, U}) && g_log.errors == 0);               // moved-from 은 unlock 하지 않는다
    reset(bus);
    SpiBus bus2{2, false, true};
    {
        UniqueBusLock p(bus);
        UniqueBusLock q(bus2);
        p = std::move(q);                                      // bus 는 즉시 풀리고, p 가 bus2 를 소유
        CHECK(!bus.locked && bus2.locked && !q.owns_lock());
    }
    CHECK(log_is({L, {Op::Lock, 2}, U, {Op::Unlock, 2}}) && g_log.errors == 0);

    /* 연습 3 */
    reset(bus);
    {
        BusLock l = acquire(bus);
        CHECK(bus.locked && &l.bus() == &bus);
    }
    CHECK(log_is({L, U}));

    /* 연습 4 */
    reset(bus);
    CHECK(mode3_read(bus, false) == Status::Ok);
    CHECK(log_is({L, mode(3), lo(IMU_CS), x(0xF5), x(0x00), hi(IMU_CS), mode(0), U}));
    CHECK(spi1_regs.CR1 == 0 && g_log.errors == 0);
    reset(bus);
    CHECK(mode3_read(bus, true) == Status::Busy);
    CHECK(log_is({L, mode(3), lo(IMU_CS), x(0xF5), hi(IMU_CS), mode(0), U}));
    CHECK(spi1_regs.CR1 == 0 && g_log.errors == 0);
    reset(bus);
    CHECK(!switch_to_mode3(bus, false));
    CHECK(log_is({L, mode(3), mode(0), U}) && spi1_regs.CR1 == 0);
    reset(bus);
    CHECK(switch_to_mode3(bus, true));
    CHECK(log_is({L, mode(3), U}) && spi1_regs.CR1 == (CR1_CPOL | CR1_CPHA));

    /* 연습 5 */
    reset(bus, /*rtos=*/false);
    CHECK(read_who_am_i(bus) == 0x5A);
    CHECK(log_is({lo(IMU_CS), x(0xF5), x(0x00), hi(IMU_CS)}) && g_log.errors == 0);
    reset(bus, /*rtos=*/true);
    CHECK(read_who_am_i(bus) == 0x5A);
    CHECK(log_is({L, lo(IMU_CS), x(0xF5), x(0x00), hi(IMU_CS), U}) && g_log.errors == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
