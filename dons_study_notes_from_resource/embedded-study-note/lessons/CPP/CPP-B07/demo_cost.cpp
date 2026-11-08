/* CPP-B07 demo — C 수동 짝 맞추기 vs RAII 가드: 기계어 비교 (어셈블리만 본다, 링크 안 함)
 *   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_cost.cpp     # 임베디드 설정
 *   c++ -std=c++20 -O2 -S -o - demo_cost.cpp                                # 예외 켬 → 비교용
 */
#include <cstdint>

extern "C" {                                 // HAL 은 다른 번역 단위(C)에 있다고 가정 → 인라인 불가
void    hal_bus_lock(void* bus);
void    hal_bus_unlock(void* bus);
void    hal_cs_write(unsigned pin, bool high);
uint8_t hal_spi_xfer(void* bus, uint8_t tx);
}

/* ---- C 방식: 모든 return 경로에서 직접 짝을 맞춘다 (goto cleanup) ---- */
extern "C" int write_c(void* bus, uint8_t addr, uint8_t data) {
    int rc = 0;
    hal_bus_lock(bus);
    hal_cs_write(4, false);
    hal_spi_xfer(bus, 0x05);
    if (hal_spi_xfer(bus, 0x00) & 1u) { rc = -1; goto out_cs; }
    hal_spi_xfer(bus, 0x02);
    hal_spi_xfer(bus, addr);
    hal_spi_xfer(bus, data);
out_cs:
    hal_cs_write(4, true);
    hal_bus_unlock(bus);
    return rc;
}

/* ---- C++ 방식: 가드 두 개 ---- */
class BusLock {
public:
    explicit BusLock(void* b) : b_(b) { hal_bus_lock(b_); }
    ~BusLock() { hal_bus_unlock(b_); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
private:
    void* b_;
};
class ScopedChipSelect {
public:
    explicit ScopedChipSelect(unsigned pin) : pin_(pin) { hal_cs_write(pin_, false); }
    ~ScopedChipSelect() { hal_cs_write(pin_, true); }
    ScopedChipSelect(const ScopedChipSelect&) = delete;
    ScopedChipSelect& operator=(const ScopedChipSelect&) = delete;
private:
    unsigned pin_;
};

extern "C" int write_raii(void* bus, uint8_t addr, uint8_t data) {
    BusLock lock(bus);
    ScopedChipSelect cs(4);
    hal_spi_xfer(bus, 0x05);
    if (hal_spi_xfer(bus, 0x00) & 1u) return -1;
    hal_spi_xfer(bus, 0x02);
    hal_spi_xfer(bus, addr);
    hal_spi_xfer(bus, data);
    return 0;
}
