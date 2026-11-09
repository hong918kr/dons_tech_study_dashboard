// CPP-M06 측정용 공용 헤더 — 표준 헤더 없이(-nostartfiles 맨몸 링크) 드라이버 3종을 virtual 로 정의한다.
//   -DUSE_FINAL        드라이버 클래스에 final
//   -DVIRTUAL_DTOR     ByteBus 에 virtual 소멸자 (기본: protected 비가상)
//   -DOUTLINE_DTOR     virtual 소멸자를 .cpp 에 정의 → ByteBus vtable 이 실제로 방출됨 (__cxa_pure_virtual 슬롯)
//   -DONLY_UART        UartDrv 하나만 존재하는 프로그램
#pragma once
using u32 = unsigned int;
using u8  = unsigned char;

#ifdef USE_FINAL
#define DRV_FINAL final
#else
#define DRV_FINAL
#endif

class ByteBus {
public:
    virtual bool init(u32 hz) = 0;
    virtual u32  write(const u8* p, u32 n) = 0;
    virtual u32  status() const = 0;
    virtual void selftest() = 0;           // 아무도 호출하지 않는 가상 함수 (gc-sections 실험용)
#ifdef VIRTUAL_DTOR
#ifdef OUTLINE_DTOR
    virtual ~ByteBus();
#else
    virtual ~ByteBus() {}
#endif
#endif
protected:
    constexpr ByteBus() = default;
#ifndef VIRTUAL_DTOR
    ~ByteBus() = default;                  // protected 비가상: delete 불가, deleting dtor 없음
#endif
};

class UartDrv DRV_FINAL : public ByteBus {
public:
    constexpr explicit UartDrv(u32 base) : base_(base) {}
    bool init(u32 hz) override;            // 첫 비인라인 가상 함수 = key function → vtable 은 demo_uart.cpp 에만
    u32  write(const u8* p, u32 n) override;
    u32  status() const override { return hz_ ? 1u : 0u; }   // 인라인 가능한 짧은 가상 함수
    void selftest() override;
    void debug_dump() const;               // 아무도 호출하지 않는 *비가상* 함수 (대조군)
private:
    u32 base_;
    u32 hz_ = 0;
};

#ifndef ONLY_UART
class SpiDrv DRV_FINAL : public ByteBus {
public:
    constexpr explicit SpiDrv(u32 base) : base_(base) {}
    bool init(u32 hz) override;
    u32  write(const u8* p, u32 n) override;
    u32  status() const override { return hz_ ? 2u : 0u; }
    void selftest() override;
private:
    u32 base_;
    u32 hz_ = 0;
};

class I2cDrv DRV_FINAL : public ByteBus {
public:
    constexpr I2cDrv(u32 base, u8 addr) : base_(base), addr_(addr) {}
    bool init(u32 hz) override;
    u32  write(const u8* p, u32 n) override;
    u32  status() const override { return hz_ ? 3u : 0u; }
    void selftest() override;
private:
    u32 base_;
    u32 hz_ = 0;
    u8  addr_;
};
#endif

// demo_board.cpp 가 정의
extern UartDrv g_uart;
ByteBus& console();
UartDrv& uart_ref();
#ifndef ONLY_UART
extern ByteBus* const g_buses[3];
#endif
