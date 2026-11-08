/* CPP-B14 demo — "= delete" 로 operator new 를 막을 수 있는 곳 / 없는 곳
 *   c++ $F demo_delete_error.cpp -o /tmp/b14_del && /tmp/b14_del          # 정상
 *   c++ $F -DSHOW_ERROR -c demo_delete_error.cpp -o /dev/null              # 컴파일 에러 3개
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>

#ifdef SHOW_ERROR
void* operator new(std::size_t) = delete;     // (1) 전역 operator new 는 = delete 불가
#endif

struct UartDriver {
    explicit UartDriver(volatile std::uint32_t* dr) : dr_(dr) {}
    void put(std::uint8_t b) { *dr_ = b; }
    static void* operator new(std::size_t) = delete;     // 클래스 전용: 이 타입만 new 금지
    static void* operator new[](std::size_t) = delete;
    volatile std::uint32_t* dr_;
};

static volatile std::uint32_t fake_dr;
alignas(UartDriver) static std::byte storage[sizeof(UartDriver)];
static UartDriver g_uart2(&fake_dr);                     // 정적 객체: OK

int main() {
    UartDriver* u = ::new (static_cast<void*>(storage)) UartDriver(&fake_dr);   // ::new = 전역 placement: OK
    u->put(0x41);
    g_uart2.put(0x42);
#ifdef SHOW_ERROR
    UartDriver* h = new UartDriver(&fake_dr);                                   // (2) 힙: 에러
    UartDriver* q = new (static_cast<void*>(storage)) UartDriver(&fake_dr);     // (3) :: 없는 placement 도 에러
    (void)h; (void)q;
#endif
    std::printf("DR=0x%02X\n", static_cast<unsigned>(fake_dr));
    return 0;
}
