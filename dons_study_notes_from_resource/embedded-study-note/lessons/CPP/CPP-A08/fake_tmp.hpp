/* CPP-A08 fake_tmp.hpp — 호스트 테스트 전용: solution.cpp 의 FakeTmp / FakeBus / FakeBusV 구간을 그대로 옮겼다. */
#pragma once
#include <initializer_list>
#include "sensor.hpp"

/* ===================== 가짜 장치 FakeTmp: 스크립트 응답 + 오류 주입 + 호출 로그 =====================
 * 레지스터 파일을 가진 "센서 모형". 각 트랜잭션은 주입 큐에서 Fault 하나를 꺼내 적용한다 (비었으면 None). */
enum class Fault : uint8_t { None, Nack, Timeout, BadCrc };
enum class Op : uint8_t { WriteRead, Write };
struct Call { Op op; uint8_t addr; uint8_t reg; uint8_t len; uint16_t value; BusErr result; };

class FakeTmp {
public:
    static constexpr size_t kLog = 48, kQ = 16;
    uint16_t regs[16] = {};
    uint8_t addr = ts::kAddr;
    bool ignore_config_writes = false;   // "설정이 안 먹는 칩" (전원 불량·쓰기 보호 흉내)

    FakeTmp() { reset(); }
    void reset() {
        for (uint16_t& r : regs) r = 0;
        regs[ts::kRegDeviceId] = ts::kDeviceId;
        regs[ts::kRegTemp] = 0x0C80;     // 25.000 °C
        addr = ts::kAddr;
        ignore_config_writes = false;
        nlog_ = dropped_ = 0;
        fq_head_ = fq_n_ = tq_head_ = tq_n_ = 0;
    }
    void inject(Fault f) { if (fq_n_ < kQ) fq_[(fq_head_ + fq_n_++) % kQ] = f; }
    void inject(Fault f, int times) { for (int i = 0; i < times; ++i) inject(f); }
    void script_temp(std::initializer_list<uint16_t> raws) {   // TEMP 읽기마다 하나씩. 다 쓰면 regs[TEMP] 유지
        for (uint16_t r : raws) if (tq_n_ < kQ) tq_[(tq_head_ + tq_n_++) % kQ] = r;
    }
    size_t pending_faults() const { return fq_n_; }
    size_t ncalls() const { return nlog_; }
    const Call& call(size_t i) const { return log_[i]; }
    size_t count(Op op, uint8_t reg) const {
        size_t n = 0;
        for (size_t i = 0; i < nlog_; ++i) if (log_[i].op == op && log_[i].reg == reg) ++n;
        return n;
    }

    /* [A4] write_read: 주소 불일치 → NACK. Fault 적용. 성공이면 rx = MSB, LSB, CRC (BadCrc 면 CRC 한 비트 뒤집음).
     *      TEMP 는 스크립트가 있으면 그 값을 꺼낸다 (전송이 성공했을 때만 소비). 모든 호출을 로그에 남긴다. */
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) {
        const Fault f = next_fault();
        uint16_t v = 0;
        BusErr res = BusErr::Ok;
        if (a != addr || f == Fault::Nack) res = BusErr::Nack;
        else if (f == Fault::Timeout) res = BusErr::Timeout;
        else {
            v = regs[reg & 0x0Fu];
            if (reg == ts::kRegTemp && tq_n_ > 0) { v = tq_[tq_head_]; tq_head_ = (tq_head_ + 1) % kQ; --tq_n_; }
            uint8_t frame[3] = {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v & 0xFFu), 0};
            frame[2] = crc8(frame, 2);
            if (f == Fault::BadCrc) frame[2] ^= 0x01u;
            for (size_t i = 0; i < rx.size() && i < 3; ++i) rx[i] = frame[i];
        }
        log(Call{Op::WriteRead, a, reg, static_cast<uint8_t>(rx.size()), v, res});
        return res;
    }
    /* [A4] write: 주소 불일치·Nack → NACK, Timeout. 4바이트 + CRC 맞음 → 쓰기 가능한 레지스터(CONFIG)만 갱신.
     *      CRC 틀림 → NACK (장치가 거부). 모든 호출을 로그에 남긴다 (value = 보낸 값). */
    BusErr write(uint8_t a, std::span<const uint8_t> tx) {
        const Fault f = next_fault();
        const uint8_t reg = tx.empty() ? 0xFFu : tx[0];
        const uint16_t v = tx.size() >= 3 ? static_cast<uint16_t>(tx[1] << 8 | tx[2]) : 0;
        BusErr res = BusErr::Ok;
        if (a != addr || f == Fault::Nack) res = BusErr::Nack;
        else if (f == Fault::Timeout) res = BusErr::Timeout;
        else if (tx.size() != 4 || crc8(tx.data() + 1, 2) != tx[3]) res = BusErr::Nack;
        else if (reg == ts::kRegConfig && !ignore_config_writes) regs[ts::kRegConfig] = v;
        log(Call{Op::Write, a, reg, static_cast<uint8_t>(tx.size()), v, res});
        return res;
    }

private:
    Fault next_fault() {
        if (fq_n_ == 0) return Fault::None;
        const Fault f = fq_[fq_head_];
        fq_head_ = (fq_head_ + 1) % kQ;
        --fq_n_;
        return f;
    }
    void log(const Call& c) { if (nlog_ < kLog) log_[nlog_++] = c; else ++dropped_; }
    Call log_[kLog] = {};
    size_t nlog_ = 0, dropped_ = 0;
    Fault fq_[kQ] = {};
    size_t fq_head_ = 0, fq_n_ = 0;
    uint16_t tq_[kQ] = {};
    size_t tq_head_ = 0, tq_n_ = 0;
};

/* ===================== 세 Rig: 같은 FakeTmp 를 세 이음매로 꽂는다 ===================== */
class FakeBus {   // (1) 템플릿 주입용: 가상 함수 없음, FakeTmp 로 그대로 전달
public:
    explicit FakeBus(FakeTmp& d) : d_(d) {}
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) { return d_.write_read(a, reg, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return d_.write(a, tx); }
private:
    FakeTmp& d_;
};

/* [B2] (2) 인터페이스 주입용 가짜: II2c 를 구현해 FakeTmp 로 전달 */
class FakeBusV final : public II2c {
public:
    explicit FakeBusV(FakeTmp& d) : d_(d) {}
    BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) override { return d_.write_read(a, reg, rx); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) override { return d_.write(a, tx); }
private:
    FakeTmp& d_;
};
