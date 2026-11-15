/* CPP-A03 fw_app.cpp — 작은 펌웨어 TU 하나 (UART 로거 + 보정값 + 프레임 버퍼).
 *   기본       : 런타임 의존성을 걷어 낸 판 → check_syms.sh 통과 (exit 0)
 *   -DLEGACY   : 흔한 C++ 습관 그대로 (가상 소멸자, 소멸자 있는 전역, lazy static, new) → check_syms.sh 실패 (exit 1)
 * 보드 함수(hal_*)는 다른 TU(fw_start.cpp)에 있다 → 미정의 심볼 중 "프로젝트 몫". */
#include <cstdint>
#include <new>
#include <type_traits>

extern "C" void hal_uart_write(const uint8_t* p, uint32_t n);
extern "C" int32_t hal_otp_offset();
extern "C" uint64_t hal_cycles64();

struct Frame { uint32_t seq; uint32_t w[15]; };                  /* 64 B */

class ByteSink {                                                  /* 드라이버 인터페이스 (CPP-M06) */
public:
    virtual void put(const uint8_t* p, uint32_t n) = 0;
    virtual void flush() {}
#ifdef LEGACY
    virtual ~ByteSink() = default;                                /* → D0(deleting dtor) → operator delete */
#else
protected:
    ~ByteSink() = default;                                        /* 기반 포인터로 delete 불가, 자명 소멸 */
#endif
};
class Uart final : public ByteSink {
public:
    void put(const uint8_t* p, uint32_t n) override { hal_uart_write(p, n); }
};

struct Logger {
    ByteSink* out;
    uint32_t lines = 0;
    constexpr explicit Logger(ByteSink* o) : out(o) {}
#ifdef LEGACY
    ~Logger() { out->flush(); }                                   /* 비자명 소멸자 → 소멸자 등록 */
#endif
    void line(const char* s, uint32_t n) { out->put(reinterpret_cast<const uint8_t*>(s), n); ++lines; }
};

struct Cal {                                                      /* 보정값: 런타임에 OTP 에서 읽는다 */
    int32_t offset = 0;
    constexpr Cal() = default;
    explicit Cal(int32_t o) : offset(o) {}
};

Uart g_uart;                                                      /* constexpr 생성자 → 상수 초기화 (CPP-B09) */
#ifdef LEGACY
Logger g_log{&g_uart};
const Cal& cal() { static Cal c{hal_otp_offset()}; return c; }   /* 첫 호출 때 생성 → guard */
Frame* new_frame() { return new Frame{}; }                       /* 힙 */
#else
constinit Logger g_log{&g_uart};                                  /* 소멸자 자명 → 등록 없음 */
static_assert(std::is_trivially_destructible_v<Logger>);
constinit Cal g_cal{};                                            /* 부팅 때 cal_init() 이 채운다 (IRQ 켜기 전) */
void cal_init() { g_cal = Cal{hal_otp_offset()}; }
const Cal& cal() { return g_cal; }
static Frame g_frames[4];                                         /* 고정 풀 (CPP-B14) */
static uint32_t g_next = 0;
Frame* new_frame() { Frame* f = &g_frames[g_next++ & 3u]; *f = Frame{}; return f; }
#endif

uint64_t uptime_ms(uint64_t cycles) { return cycles / 168000u; } /* 64비트 나눗셈 → __aeabi_uldivmod (양쪽 판 공통) */
void copy_frame(Frame* d, const Frame* s) { *d = *s; }            /* 64 B 복사 → memcpy 계열 (양쪽 판 공통) */
int32_t corrected(int32_t raw) { return raw - cal().offset; }

extern "C" void app_main() {
#ifndef LEGACY
    cal_init();
#endif
    Frame* f = new_frame();
    f->seq = static_cast<uint32_t>(corrected(100));
    f->w[0] = static_cast<uint32_t>(uptime_ms(hal_cycles64()));
    copy_frame(new_frame(), f);
    g_log.line("boot\n", 5);
}
