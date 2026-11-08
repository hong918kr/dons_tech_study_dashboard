/* CPP-B14 demo — 힙 사용을 "링크 에러"로 잡는 두 가지 방법
 *  방법 1 (-DPOISON + -Wl,-dead_strip): operator new 가 정의 없는 함수를 부르게 정의.
 *         아무도 new 를 안 쓰면 dead-strip 으로 사라져 링크 OK, 누가 쓰면 Undefined symbols.
 *  방법 2 (-nostdlib++): libc++ 를 링크하지 않음 → operator new 는 <new> 에 "선언만" 있고 정의가 없다.
 *  -DSHOW_ERROR      : std::vector / std::string / std::function 을 사용 → 링크 에러
 *  -DNO_DELETE_STUBS : delete 스텁을 빼면 virtual 소멸자 때문에 heap 을 안 써도 링크 에러
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef SHOW_ERROR
#include <functional>
#include <string>
#include <vector>
#endif

#ifdef POISON
extern "C" [[noreturn]] void HEAP_IS_FORBIDDEN_see_CPP_B14(void);   // 선언만, 정의는 어디에도 없다
void* operator new(std::size_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
void* operator new[](std::size_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
void* operator new(std::size_t, std::align_val_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
void* operator new[](std::size_t, std::align_val_t) { HEAP_IS_FORBIDDEN_see_CPP_B14(); }
#endif

#ifndef NO_DELETE_STUBS
// delete 는 "정의"해 둔다: virtual 소멸자가 있으면 vtable 의 deleting destructor 가 operator delete 를 참조한다
void operator delete(void* p) noexcept { if (p) std::abort(); }
void operator delete[](void* p) noexcept { if (p) std::abort(); }
void operator delete(void* p, std::size_t) noexcept { if (p) std::abort(); }
void operator delete(void* p, std::align_val_t) noexcept { if (p) std::abort(); }
#endif

struct Led {                                   // 펌웨어에서 흔한 인터페이스 + 정적 인스턴스
    virtual ~Led() = default;
    virtual void set(bool on) { state = on; }
    bool state = false;
};
static Led g_status_led;                       // 힙 아님 (.bss)
Led* g_led = &g_status_led;

template <class T> static void escape(T& t) { asm volatile("" : : "r"(&t) : "memory"); }

int main() {
    std::array<std::uint16_t, 16> adc{};       // 고정 버퍼
    adc[3] = 512;
    g_led->set(adc[3] > 100);
#ifdef SHOW_ERROR
    std::vector<int> v;  v.push_back(1);  escape(v);
    std::string s(40, 'x');  escape(s);
    std::array<int, 16> big{};
    std::function<int()> f = [big] { return big[0]; };  escape(f);
#endif
    std::printf("led=%d\n", g_led->state);
    return 0;
}
