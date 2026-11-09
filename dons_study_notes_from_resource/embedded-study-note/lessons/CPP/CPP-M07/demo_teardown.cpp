// CPP-M07 demo_teardown — 전역 드라이버를 만드는 세 가지 방법의 ELF 비교 (build_teardown.sh 가 VM 에서 빌드)
//   CASE=1  평범한 전역 객체 Driver g_drv(115200)
//   CASE=2  constinit StaticInstance<Driver> (소멸자 없음), _start 에서 emplace
//   CASE=3  constinit StaticInstance<Driver> + 소멸자 (프로그램 끝에 자동 파괴)
//   CASE=4  CASE=2 + alignas(64) DMA 디스크립터 (.bss 정렬 확인)
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

static volatile std::uint32_t UART_CR, UART_BRR;

struct Driver {
    std::uint32_t baud;
    explicit Driver(std::uint32_t b) : baud(b) { UART_BRR = b; UART_CR = 1; }
    ~Driver() { UART_CR = 0; }
};
struct alignas(64) Desc64 {
    std::uint32_t w[4];
    explicit Desc64(std::uint32_t x) : w{x, 0, 0, 0} {}
};

template <typename T>
class StaticInstance {
    alignas(T) std::byte storage_[sizeof(T)]{};
    T* ptr_ = nullptr;
public:
    constexpr StaticInstance() noexcept = default;
    StaticInstance(const StaticInstance&) = delete;
#if CASE == 3
    constexpr ~StaticInstance() { if (ptr_) std::destroy_at(ptr_); }
#endif
    template <typename... A> T* emplace(A&&... a) {
        if (ptr_) return nullptr;
        return ptr_ = ::new (static_cast<void*>(storage_)) T(std::forward<A>(a)...);
    }
};

#if CASE == 1
Driver g_drv(115200u);
#elif CASE == 2 || CASE == 3
constinit StaticInstance<Driver> g_drv;
#elif CASE == 4
constinit StaticInstance<Desc64> g_desc;
#endif

extern "C" [[noreturn]] void _start() {
#if CASE == 2 || CASE == 3
    (void)g_drv.emplace(115200u);
#elif CASE == 4
    (void)g_desc.emplace(1u);
#endif
    for (;;) {}
}
