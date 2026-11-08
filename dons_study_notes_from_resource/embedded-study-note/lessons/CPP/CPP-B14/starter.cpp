/* CPP-B14 힙 금지 강제: operator new 삭제와 링크 에러
 * 호스트(macOS)에서 전역 operator new/delete 를 "세는 버전"으로 바꿔 끼우고,
 * 펌웨어식 코드(std::array, 고정 버퍼, 정적 버퍼 placement-new)는 할당 0회,
 * std::vector / 긴 std::string / 큰 캡처 std::function 은 할당함을 측정한다.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <vector>

/* ---------------- 할당 통계 ---------------- */
struct HeapStats {
    unsigned news;          // operator new 계열 전체 호출 수 (모든 오버로드 합)
    unsigned deletes;       // operator delete 계열 (nullptr 제외)
    unsigned aligned_news;  // align_val_t 오버로드로 들어온 수
    unsigned nothrow_news;  // nothrow_t 오버로드로 들어온 수
    unsigned violations;    // NoHeapGuard 안에서 일어난 할당 시도 수
};
static HeapStats g_heap;
static bool g_forbid_heap = false;   // Part C: true 면 할당 = 위반

/* ---------------- Part A: 세는 할당기 + 전역 operator new/delete 교체 ---------------- */
static void* tracked_alloc(std::size_t n, std::size_t align, bool nothrow) {
    /* TODO(A): news / aligned_news(align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) / nothrow_news 를 센다 */
    /* TODO(C): g_forbid_heap 이면 violations 도 센다 */
    if (n == 0) n = 1;                          // 크기 0 요청도 고유한 non-null 포인터
    void* p = nullptr;
    if (align <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) p = std::malloc(n);
    else if (posix_memalign(&p, align, n) != 0) p = nullptr;
    if (p == nullptr && !nothrow) std::abort();  // -fno-exceptions: bad_alloc 대신 정지
    return p;
}
static void tracked_free(void* p) {
    if (p == nullptr) return;                   // delete nullptr 는 아무 일도 안 함
    g_heap.deletes++;
    std::free(p);
}

using std::size_t;
using std::align_val_t;
using std::nothrow_t;
#define AL(a) static_cast<size_t>(a)
// 단일 객체 new (4) + 배열 new (4)
void* operator new(size_t n) { return tracked_alloc(n, 0, false); }
void* operator new[](size_t n) { return tracked_alloc(n, 0, false); }
/* TODO(A): 나머지 new 6개 — align_val_t / nothrow_t / 둘 다 (단일·배열 각각) */
// 단일 객체 delete (6) + 배열 delete (6)
void operator delete(void* p) noexcept { tracked_free(p); }
void operator delete[](void* p) noexcept { tracked_free(p); }
/* TODO(A): 나머지 delete 10개 — sized / align_val_t / sized+align / nothrow / align+nothrow (단일·배열 각각) */

/* ---------------- Part B: 구간 측정 ---------------- */
// 최적화기가 "안 쓰이는 할당"을 통째로 지우지 못하게 객체 주소를 바깥에 흘린다.
template <class T> static void escape(T& t) { asm volatile("" : : "r"(&t) : "memory"); }

// f() 를 실행하는 동안 일어난 operator new 호출 수를 돌려준다.
template <class F> static unsigned count_allocs(F&& f) {
    f();
    return 0; /* TODO(B): 실행 전후 g_heap.news 차이 */
}
static unsigned live_blocks() { return g_heap.news - g_heap.deletes; }

/* ---------------- Part C: 힙 금지 구역 + 펌웨어식 코드 ---------------- */
class NoHeapGuard {
public:
    NoHeapGuard() : prev_(g_forbid_heap) { /* TODO(C): 금지 켜기 */ }
    ~NoHeapGuard() { (void)prev_; /* TODO(C): 이전 상태로 복원 (중첩 고려) */ }
    NoHeapGuard(const NoHeapGuard&) = delete;
    NoHeapGuard& operator=(const NoHeapGuard&) = delete;
private:
    bool prev_;
};

struct UartRegs { volatile std::uint32_t DR; volatile std::uint32_t SR; };
static UartRegs fake_uart1;                    // 실제 칩이라면 (UartRegs*)0x40011000

using RxCallback = void (*)(void* ctx, std::uint8_t byte);   // std::function 대신: 함수 포인터 + 컨텍스트

class UartDriver {
public:
    explicit UartDriver(UartRegs* r) : regs_(r) {}
    void send(std::uint8_t b) { regs_->DR = b; tx_count_++; if (cb_) cb_(ctx_, b); }
    void on_tx(RxCallback cb, void* ctx) { cb_ = cb; ctx_ = ctx; }
    unsigned tx_count() const { return tx_count_; }
    /* TODO(C): 이 타입을 new 로 만들 수 없게 클래스 전용 operator new / new[] 를 막는다 */
private:
    UartRegs* regs_;
    unsigned tx_count_ = 0;
    RxCallback cb_ = nullptr;
    void* ctx_ = nullptr;
};

alignas(UartDriver) static std::byte g_uart_storage[sizeof(UartDriver)];   // .bss 의 정적 버퍼
static UartDriver* g_uart = nullptr;

static UartDriver& uart_instance() {
    static UartDriver fallback(&fake_uart1); (void)g_uart;   // TODO(C): g_uart_storage 위에 처음 한 번만 생성
    return fallback;
}

struct SensorLog {                                   // 고정 크기 링버퍼: 힙 없음
    std::array<std::uint16_t, 32> buf{};
    unsigned head = 0, count = 0;
    void push(std::uint16_t v) { buf[head] = v; head = (head + 1) % buf.size(); if (count < buf.size()) count++; }
};
static SensorLog g_log;
static unsigned g_checksum;
static void sum_cb(void* ctx, std::uint8_t b) { *static_cast<unsigned*>(ctx) += b; }

static void firmware_tick(std::uint16_t sample) {    // 0-alloc 버전
    g_log.push(sample);
    std::array<std::uint8_t, 3> frame{0xA5, static_cast<std::uint8_t>(sample >> 8), static_cast<std::uint8_t>(sample)};
    for (std::uint8_t b : frame) uart_instance().send(b);
}

static void desktop_tick(std::uint16_t sample) {     // "PC 습관" 버전: 할당이 숨어 있다
    static std::vector<std::uint16_t>* log = nullptr;
    if (log == nullptr) log = new std::vector<std::uint16_t>();
    log->push_back(sample);
    std::string msg = "sample=" + std::to_string(sample) + " (raw adc reading)";
    std::array<std::uint32_t, 16> calib{};
    std::function<void(std::uint8_t)> on_byte = [calib](std::uint8_t b) { fake_uart1.DR = b + calib[0]; };
    for (char c : msg) on_byte(static_cast<std::uint8_t>(c));
}

template <class T> concept heap_newable = requires { new T; };

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

struct alignas(64) DmaDescriptor { std::uint32_t words[16]; };   // 64바이트 정렬 요구 (캐시 라인)

int main() {
    /* Part A: 교체한 operator new 가 모든 경로에서 불린다 */
    unsigned base_live = live_blocks();
    HeapStats s0 = g_heap;
    int* one = new int(7);           escape(one);  delete one;
    int* arr = new int[10];          escape(arr);  delete[] arr;
    int* nt = new (std::nothrow) int(1); escape(nt); delete nt;
    DmaDescriptor* d = new DmaDescriptor; escape(d); delete d;
    CHECK(g_heap.news - s0.news == 4);
    CHECK(g_heap.deletes - s0.deletes == 4);
    CHECK(g_heap.nothrow_news - s0.nothrow_news == 1);
    CHECK(g_heap.aligned_news - s0.aligned_news == 1);
    CHECK(live_blocks() == base_live);

    /* Part B: 표준 타입별 할당 횟수 측정 */
    unsigned a_arr  = count_allocs([] { std::array<int, 64> a{}; escape(a); });
    unsigned a_sso  = count_allocs([] { std::string s("short-msg"); escape(s); });
    unsigned a_long = count_allocs([] { std::string s("this string is clearly longer than SSO"); escape(s); });
    unsigned a_vec  = count_allocs([] { std::vector<int> v; for (int i = 0; i < 5; i++) v.push_back(i); escape(v); });
    unsigned a_res  = count_allocs([] { std::vector<int> v; v.reserve(8); for (int i = 0; i < 5; i++) v.push_back(i); escape(v); });
    unsigned a_fsm  = count_allocs([] { int k = 3; std::function<int()> f = [k] { return k; }; escape(f); });
    unsigned a_fbig = count_allocs([] { std::array<int, 16> big{}; std::function<int()> f = [big] { return big[0]; }; escape(f); });
    alignas(int) static std::byte raw[sizeof(int)];
    unsigned a_plc  = count_allocs([] { int* p = ::new (static_cast<void*>(raw)) int(42); escape(p); });
    std::printf("  array<int,64>=%u  string(short)=%u  string(long)=%u  vector x5=%u  reserve(8)+x5=%u\n",
                a_arr, a_sso, a_long, a_vec, a_res);
    std::printf("  function(4B capture)=%u  function(64B capture)=%u  placement-new=%u\n", a_fsm, a_fbig, a_plc);
    CHECK(a_arr == 0);
    CHECK(a_sso == 0);
    CHECK(a_long == 1);
    CHECK(a_vec >= 2);             // 기하급수 성장: 재할당이 여러 번 (libc++ 1→2→4→8)
    CHECK(a_res == 1);
    CHECK(a_fsm == 0);             // 작은 캡처는 std::function 내부 버퍼(SBO)에 들어감
    CHECK(a_fbig == 1);
    CHECK(a_plc == 0);             // placement new 는 교체 대상도, 할당도 아니다
    CHECK(live_blocks() == base_live);

    /* Part C: 힙 금지 구역 */
    CHECK(!g_forbid_heap);
    {
        NoHeapGuard outer;
        { NoHeapGuard inner; }
        CHECK(g_forbid_heap);      // 안쪽 가드가 풀려도 바깥은 여전히 금지
    }
    CHECK(!g_forbid_heap);

    unsigned v0 = g_heap.violations, n0 = g_heap.news;
    {
        NoHeapGuard no_heap;
        uart_instance().on_tx(sum_cb, &g_checksum);
        for (unsigned i = 0; i < 1000; i++) firmware_tick(static_cast<std::uint16_t>(i));
    }
    CHECK(g_heap.violations == v0);
    CHECK(g_heap.news == n0);
    CHECK(uart_instance().tx_count() == 3000);
    CHECK(fake_uart1.DR == (999u & 0xFFu));
    CHECK(g_log.count == 32 && g_log.buf[(g_log.head + 31) % 32] == 999);
    CHECK(static_cast<void*>(&uart_instance()) == static_cast<void*>(g_uart_storage));
    CHECK(!heap_newable<UartDriver>);   // new UartDriver 는 컴파일 자체가 안 됨

    {
        NoHeapGuard no_heap;
        desktop_tick(1234);
    }
    std::printf("  desktop_tick violations=%u\n", g_heap.violations - v0);
    CHECK(g_heap.violations - v0 >= 3);   // vector + string + function

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
